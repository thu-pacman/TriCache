// Copyright 2022 Guanyu Feng, Tsinghua University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fcntl.h>
#include <omp.h>
#include <random>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

const size_t PAGE_SIZE = 4096;

int main(int argc, char **argv)
{

    if (argc <= 4)
    {
        printf("usage: %s num_requests phy_size_in_GB partition_hit_rate is_allpage [file_path] ...\n", argv[0]);
        return 0;
    }

    const size_t num_requests = std::stoul(argv[1]);

    // const size_t phy_size = std::stoul(argv[2]) * (1lu << 30);
    // const size_t num_ppages = phy_size / PAGE_SIZE;

    printf("%lu %lu\n", std::stoul(argv[2]), std::stoul(argv[2]) * (1lu << 30));
    const size_t virt_size = std::stoul(argv[2]) * (1lu << 30);
    const size_t num_vpages = virt_size / PAGE_SIZE;

    const double partition_hit_rate = std::stod(argv[3]);

    // const size_t num_vpages = (double) num_ppages / partition_hit_rate;
    // const size_t virt_size = num_vpages * PAGE_SIZE;

    const size_t num_ppages = (double)num_vpages * partition_hit_rate;
    const size_t phy_size = num_ppages * PAGE_SIZE;

    using item_type = uint64_t;
    const size_t items_per_page = PAGE_SIZE / sizeof(item_type);

    printf("NUM_THREADS: %d\n", omp_get_max_threads());

    bool is_allpage = std::stoul(argv[4]);

    int fd = -1;
    char *pool = nullptr;
    if (argc > 5)
    {
        fd = open(argv[5], O_CREAT | O_RDWR, 0640);
        if (fd == -1)
            throw std::runtime_error(std::string("open path ") + argv[4] + " error.");
        pool = (char *)mmap(nullptr, virt_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (pool == MAP_FAILED)
            throw std::runtime_error("mmap error.");
        int ret = madvise(pool, virt_size, MADV_RANDOM);
        if (ret != 0)
            throw std::runtime_error("madvise error.");
    }
    else
    {
        pool = (char *)mmap(nullptr, virt_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, fd, 0);
    }

    double sum_throughput = 0;

    #pragma omp parallel
    {
        auto my_cpu = sched_getcpu();

        item_type check_sum = 0;
        std::mt19937_64 rand(omp_get_thread_num());

        #pragma omp for schedule(dynamic, 64)
        for (size_t page_id = 0; page_id < num_ppages; page_id++)
        {
            auto pointer = pool + page_id * PAGE_SIZE;
            if (fd == -1)
                ((item_type *)pointer)[0] = page_id; // avoid dummy IO ops
            else
                check_sum += ((item_type *)pointer)[0];
        }

        auto private_base = (num_ppages / omp_get_max_threads()) * omp_get_thread_num();
        ;

        for (size_t private_hit_rate = 0; private_hit_rate <= 100; private_hit_rate += 5)
        {
            const size_t private_range =
                (private_hit_rate == 0) ? num_ppages : (num_ppages / omp_get_max_threads() * 100 / private_hit_rate);

            #pragma omp master
            sum_throughput = 0;

            #pragma omp barrier
            auto start = std::chrono::high_resolution_clock::now();

            if (!is_allpage)
            {
                for (size_t i = 0; i < num_requests; i++)
                {
                    auto page_id = rand() % num_vpages;
                    if (page_id < num_ppages)
                        page_id = (page_id % private_range + private_base) % num_ppages;
                    auto pointer = pool + page_id * PAGE_SIZE;
                    check_sum += ((item_type *)pointer)[rand() % items_per_page];
                    // ((item_type*)pointer)[rand() % items_per_page] += i;
                }
            }
            else
            {
                for (size_t i = 0; i < num_requests; i++)
                {
                    auto page_id = rand() % num_vpages;
                    if (page_id < num_ppages)
                        page_id = (page_id % private_range + private_base) % num_ppages;
                    auto pointer = pool + page_id * PAGE_SIZE;
                    for (size_t j = 0; j < items_per_page; j++)
                    {
                        check_sum += ((item_type *)pointer)[j];
                        // ((item_type*)pointer)[j] += i;
                    }
                }
            }

            #pragma omp barrier
            auto end = std::chrono::high_resolution_clock::now();

            #pragma omp critical
            {
                double throughput = 1e9 * num_requests / (end - start).count();
                printf("\t[%d] : %lf ops/s : %lu\n", my_cpu, throughput, check_sum);
                sum_throughput += throughput;
            }

            #pragma omp barrier
            #pragma omp master
            printf("SharedHitRate = %lf , PrivateHitRate = %lf , Request : %lf ops/s\n", partition_hit_rate,
                   0.01 * private_hit_rate, sum_throughput);
        }
    }

    munmap(pool, virt_size);
    if (fd != -1)
        close(fd);

    return 0;
}
