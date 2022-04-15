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

#include "shared_cache.hpp"
#include "type.hpp"
#include <atomic>
#include <omp.h>
#include <stdexcept>
#include <string>
#include <thread>

const size_t virt_size = 128lu * (1 << 30);
const size_t num_vpages = virt_size / scache::CACHE_PAGE_SIZE;
const size_t phy_size = 16lu * (1 << 30);
const size_t num_requests = 1000000;

int main(int argc, char **argv)
{
    if (argc <= 1)
    {
        printf("usage: %s cpu_id,file_path ...\n", argv[0]);
        return 0;
    }

    std::vector<size_t> server_cpus;
    std::vector<std::string> server_paths;

    for (int i = 1; i < argc; i++)
    {
        auto str = std::string(argv[i]);
        auto pos = str.find(",");
        auto cpu = std::stoul(str.substr(0, pos));
        auto path = str.substr(pos + 1);
        printf("%lu %s\n", cpu, path.c_str());
        server_cpus.emplace_back(cpu);
        server_paths.emplace_back(path);
    }

    scache::SharedCache cache(virt_size, phy_size, server_cpus, server_paths, std::thread::hardware_concurrency());

    std::vector<std::atomic_size_t> count(num_vpages);
    for (auto &c : count)
        c = 0;

    double sum_throughput = 0;
    #pragma omp parallel
    {
        auto my_cpu = sched_getcpu();
        bool is_server = false;
        for (auto cpu : server_cpus)
        {
            if (cpu == my_cpu || cpu + std::thread::hardware_concurrency() / 2 == my_cpu)
                is_server = true;
        }
        if (!is_server)
        {
            std::mt19937_64 rand(omp_get_thread_num());

            auto start = std::chrono::high_resolution_clock::now();

            const size_t num_fibers = scache::MAX_FIBERS_PER_THREAD;
            std::vector<boost::fibers::fiber> fibers;
            for (size_t i = 0; i < num_fibers; i++)
            {
                auto fid = 0;
                fibers.emplace_back(boost::fibers::launch::dispatch,
                                    [&, fid = i]()
                                    {
                                        for (size_t i = fid; i < num_requests; i += num_fibers)
                                        {
                                            auto key = rand() % num_vpages;
                                            auto pointer = cache.pin(key);
                                            // auto pre = __atomic_fetch_add((size_t*)pointer, 1, __ATOMIC_SEQ_CST);
                                            // printf("[%lu] %p: %lu %lu\n", key, pointer, pre, *(size_t*)pointer);
                                            // count[key]+=1;
                                            cache.unpin(key, true);
                                        }
                                    });
            }
            for (auto &f : fibers)
                f.join();

            auto end = std::chrono::high_resolution_clock::now();

            #pragma omp critical
            {
                double throughput = 1e9 * num_requests / (end - start).count();
                printf("\t[%d] : %lf ops/s\n", my_cpu, throughput);
                sum_throughput += throughput;
            }

            cache.del_client();
        }
    }

    printf("Request : %lf ops/s\n", sum_throughput);

    return 0;

    #pragma omp parallel for schedule(dynamic, 64)
    for (size_t i = 0; i < num_vpages; i++)
    {
        auto res = cache.get<size_t>(i * scache::CACHE_PAGE_SIZE);
        if (res != count[i])
        {
            printf("[%lu] %lu != %lu, \n", i, res, count[i].load());
            throw std::runtime_error("Check Error");
        }
    }

    #pragma omp parallel
    {
        cache.del_client();
    }
}
