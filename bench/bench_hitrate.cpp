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

#include "boost/fiber/operations.hpp"
#include "direct_cache.hpp"
#include "private_cache.hpp"
#include "shared_cache.hpp"
#include "type.hpp"
#include <atomic>
#include <omp.h>
#include <stdexcept>
#include <string>
#include <thread>

int main(int argc, char **argv)
{

    if (argc <= 4)
    {
        printf("usage: %s num_requests phy_size_in_GB partition_hit_rate cpu_id,file_path ...\n", argv[0]);
        return 0;
    }

    const size_t num_requests = std::stoul(argv[1]);

    // const size_t phy_size = std::stoul(argv[2]) * (1lu << 30);
    // const size_t num_ppages = phy_size / scache::CACHE_PAGE_SIZE;

    const size_t virt_size = std::stoul(argv[2]) * (1lu << 30);
    const size_t num_vpages = virt_size / scache::CACHE_PAGE_SIZE;

    const double partition_hit_rate = std::stod(argv[3]);

    // const size_t num_vpages = (double) num_ppages / partition_hit_rate;
    // const size_t virt_size = num_vpages * scache::CACHE_PAGE_SIZE;

    const size_t num_ppages = (double)num_vpages * partition_hit_rate;
    const size_t phy_size = num_ppages * scache::CACHE_PAGE_SIZE;

    using item_type = uint64_t;
    const size_t items_per_page = scache::CACHE_PAGE_SIZE / sizeof(item_type);

    std::vector<size_t> server_cpus;
    std::vector<std::string> server_paths;

    printf("NUM_THREADS: %d\n", omp_get_max_threads());

    for (int i = 4; i < argc; i++)
    {
        auto str = std::string(argv[i]);
        auto pos = str.find(",");
        auto cpu = std::stoul(str.substr(0, pos));
        auto path = str.substr(pos + 1);
        printf("%lu %s\n", cpu, path.c_str());
        server_cpus.emplace_back(cpu);
        server_paths.emplace_back(path);
    }

    scache::SharedCache shared_cache(virt_size, phy_size, server_cpus, server_paths, omp_get_max_threads());

    double sum_throughput = 0;
    #pragma omp parallel
    {
        auto my_cpu = sched_getcpu();

        const size_t num_fibers = scache::MAX_FIBERS_PER_THREAD;

        scache::PrivateCache private_cache(shared_cache, partition_hit_rate > 0.95 ? 1.0 : 0.9);

        auto pre_access = [&](bool is_write)
        {
            const size_t thread_range = (num_vpages + omp_get_num_threads() - 1) / omp_get_num_threads();
            const size_t fiber_range = (thread_range + num_fibers - 1) / num_fibers;

            std::vector<boost::fibers::fiber> fibers;
            for (size_t i = 0; i < num_fibers; i++)
            {
                fibers.emplace_back(boost::fibers::launch::dispatch,
                                    [&, tid = omp_get_thread_num(), fid = i]()
                                    {
                                        for (size_t i = 0; i < fiber_range; i++)
                                        {
                                            if (i % 1000 == 0)
                                            {
                                                printf("%d %lu %lu / %lu\n", tid, fid, i, fiber_range);
                                            }
                                            auto page_id =
                                                tid * thread_range + fid * fiber_range + (i + fid) % fiber_range;
                                            if (page_id >= num_vpages)
                                                break;
                                            auto pointer = private_cache.pin(page_id);
                                            private_cache.unpin(page_id, is_write);
                                            boost::this_fiber::yield();
                                        }
                                    });
            }
            for (auto &f : fibers)
                f.join();
        };

        // pre_access(true);
        // pre_access(false);

        item_type check_sum = 0;
        std::mt19937_64 rand(omp_get_thread_num());

        auto private_base = (num_ppages / omp_get_max_threads()) * omp_get_thread_num();

        for (size_t private_hit_rate = 0; private_hit_rate <= 100; private_hit_rate += 5)
        {
            // const double private_hit_rate = std::stod(argv[4]);
            const size_t private_range =
                (private_hit_rate == 0) ? num_ppages : (num_ppages / omp_get_max_threads() * 100 / private_hit_rate);

            #pragma omp master
            sum_throughput = 0;

            if (false)
            {
                std::vector<boost::fibers::fiber> fibers;
                for (size_t i = 0; i < num_fibers; i++)
                {
                    auto fid = 0;
                    // fibers.emplace_back(boost::fibers::launch::dispatch,
                    //                     [&, fid = i]()
                    //                     {
                    // auto direct_cache = scache::DirectCache(private_cache);
                    for (size_t i = fid; i < private_range * 2; i += num_fibers)
                    {
                        auto page_id = (i % private_range + private_base) % num_ppages;
                        // auto pointer = direct_cache.access(page_id, false);
                        auto pointer = private_cache.pin(page_id);
                        check_sum += ((item_type *)pointer)[0];
                        private_cache.unpin(page_id);
                        boost::this_fiber::yield();
                        // ((item_type*)pointer)[rand() % items_per_page] += rand();
                    }
                    //                     });
                }
                for (auto &f : fibers)
                    f.join();
            }

            #pragma omp barrier
            auto start = std::chrono::high_resolution_clock::now();

            std::vector<boost::fibers::fiber> fibers;
            for (size_t i = 0; i < num_fibers; i++)
            {
                auto fid = 0;
                // fibers.emplace_back(boost::fibers::launch::dispatch,
                //                     [&, fid = i]()
                //                     {
                // auto direct_cache = scache::DirectCache(private_cache);
                for (size_t i = fid; i < num_requests; i += num_fibers)
                {
                    auto page_id = rand() % num_vpages;
                    if (page_id < num_ppages)
                        page_id = (page_id % private_range + private_base) % num_ppages;
                    // auto pointer = direct_cache.access(page_id, false);
                    auto pointer = private_cache.pin(page_id);
                    check_sum += ((item_type *)pointer)[rand() % items_per_page];
                    private_cache.unpin(page_id, false);
                    // ((item_type*)pointer)[rand() % items_per_page] += i;
                    // private_cache.unpin(page_id, true);
                    boost::this_fiber::yield();
                }
                //                     });
            }
            for (auto &f : fibers)
                f.join();

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

    return 0;
}
