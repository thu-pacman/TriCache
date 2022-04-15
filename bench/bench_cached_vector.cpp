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

#include "boost/fiber/mutex.hpp"
#include "cached_allocator.hpp"
#include "partition_type.hpp"
#include <mutex>
#include <omp.h>
#include <random>

const size_t virt_size = 128lu * (1 << 30);
const size_t num_vpages = virt_size / scache::CACHE_PAGE_SIZE;
const size_t phy_size = 128lu * (1 << 30);
const size_t num_ppages = phy_size / scache::CACHE_PAGE_SIZE;
const size_t num_requests = 100000000;

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

    scache::IntegratedCache cache(virt_size, phy_size, server_cpus, server_paths, std::thread::hardware_concurrency());

    scache::CachedAllocator<size_t> allocator(&cache);

    double sum_tp1 = 0, sum_tp2 = 0, sum_tp3 = 0, sum_tp4 = 0, sum_tp5 = 0;
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
            const size_t private_range = virt_size / sizeof(size_t) / scache::nextPowerOf2(omp_get_num_threads());
            std::vector<size_t, scache::CachedAllocator<size_t>> data(allocator);
            // std::vector<size_t> data;
            const size_t num_fibers = scache::MAX_FIBERS_PER_THREAD;
            const size_t fiber_private_range = (private_range + num_fibers - 1) / num_fibers;

            auto t0 = std::chrono::high_resolution_clock::now();
            {
                std::vector<boost::fibers::fiber> fibers;
                data.resize(private_range);
                for (size_t i = 0; i < num_fibers; i++)
                {
                    fibers.emplace_back(
                        [&, fid = i]()
                        {
                            size_t begin = fiber_private_range * fid;
                            size_t end = std::min(fiber_private_range * (fid + 1), private_range);
                            for (size_t i = begin; i < end; i++)
                            {
                                data[i] = i;
                            }
                        });
                }
                for (auto &f : fibers)
                    f.join();
            }
            #pragma omp barrier
            auto t1 = std::chrono::high_resolution_clock::now();
            {
                std::vector<boost::fibers::fiber> fibers;
                for (size_t i = 0; i < num_fibers; i++)
                {
                    fibers.emplace_back(
                        [&, fid = i]()
                        {
                            size_t begin = fiber_private_range * fid;
                            size_t end = std::min(fiber_private_range * (fid + 1), private_range);
                            for (size_t i = begin; i < end; i++)
                            {
                                data[i] = i;
                            }
                        });
                }
                for (auto &f : fibers)
                    f.join();
            }
            #pragma omp barrier
            auto t2 = std::chrono::high_resolution_clock::now();
            {
                std::vector<boost::fibers::fiber> fibers;
                auto cdata = data.cbegin();
                for (size_t i = 0; i < num_fibers; i++)
                {
                    fibers.emplace_back(
                        [&, fid = i]()
                        {
                            size_t begin = fiber_private_range * fid;
                            size_t end = std::min(fiber_private_range * (fid + 1), private_range);
                            for (size_t i = begin; i < end; i++)
                            {
                                if (cdata[i] != i)
                                {
                                    throw std::runtime_error("Check Error");
                                }
                            }
                        });
                }
                for (auto &f : fibers)
                    f.join();
            }
            #pragma omp barrier
            auto t3 = std::chrono::high_resolution_clock::now();
            {
                std::vector<boost::fibers::fiber> fibers;
                for (size_t i = 0; i < num_fibers; i++)
                {
                    fibers.emplace_back(
                        [&, fid = i]()
                        {
                            for (size_t i = fid; i < num_requests; i += num_fibers)
                            {
                                auto key = rand() % private_range;
                                data[key] = key;
                            }
                        });
                }
                for (auto &f : fibers)
                    f.join();
            }
            #pragma omp barrier
            auto t4 = std::chrono::high_resolution_clock::now();
            {
                std::vector<boost::fibers::fiber> fibers;
                auto cdata = data.cbegin();
                for (size_t i = 0; i < num_fibers; i++)
                {
                    fibers.emplace_back(
                        [&, fid = i]()
                        {
                            for (size_t i = fid; i < num_requests; i += num_fibers)
                            {
                                auto key = rand() % private_range;
                                if (cdata[key] != key)
                                {
                                    throw std::runtime_error("Check Error");
                                }
                            }
                        });
                }
                for (auto &f : fibers)
                    f.join();
            }
            #pragma omp barrier
            auto t5 = std::chrono::high_resolution_clock::now();

            #pragma omp critical
            {
                double tp1 = 1e9 * private_range / (t1 - t0).count();
                double tp2 = 1e9 * private_range / (t2 - t1).count();
                double tp3 = 1e9 * private_range / (t3 - t2).count();
                double tp4 = 1e9 * num_requests / (t4 - t3).count();
                double tp5 = 1e9 * num_requests / (t5 - t4).count();
                sum_tp1 += tp1;
                sum_tp2 += tp2;
                sum_tp3 += tp3;
                sum_tp4 += tp4;
                sum_tp5 += tp5;
                printf("\t[%d] : resize %.2lf ops/s, seq_write %.2lf ops/s, seq_read %.2lf ops/s, rand_write %.2lf "
                       "ops/s, rand_read %.2lf ops/s\n",
                       my_cpu, tp1, tp2, tp3, tp4, tp5);
            }
        }
        else
        {
            #pragma omp barrier
            #pragma omp barrier
            #pragma omp barrier
            #pragma omp barrier
            #pragma omp barrier
        }
        cache.flush();
    }
    printf("Sum : resize %.2lf ops/s, seq_write %.2lf ops/s, seq_read %.2lf ops/s, rand_write %.2lf ops/s, rand_read "
           "%.2lf ops/s\n",
           sum_tp1, sum_tp2, sum_tp3, sum_tp4, sum_tp5);
}
