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

#include "cached_allocator.hpp"
#include <boost/sort/sample_sort/sample_sort.hpp>
#include <boost/sort/sort.hpp>
#include <omp.h>
#include <ostream>
#include <random>
#include <sched.h>
#include <thread>

const size_t virt_size = 128lu * (1 << 30);
const size_t num_vpages = virt_size / scache::CACHE_PAGE_SIZE;
const size_t phy_size = 128lu * (1 << 30);
const size_t num_ppages = phy_size / scache::CACHE_PAGE_SIZE;

const size_t size = 8lu << 30;

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

    const size_t num_threads = omp_get_max_threads() - server_cpus.size();

    scache::IntegratedCache global_cache(virt_size, phy_size, server_cpus, server_paths, num_threads * 5);

    scache::CachedAllocator<size_t> allocator(&global_cache);
    // std::allocator<size_t> allocator;

    auto ptr = allocator.allocate(size);

    // #pragma omp parallel
    // {
    //     std::mt19937_64 rand(omp_get_thread_num());
    //     #pragma omp for schedule(dynamic, 4096)
    //     for(size_t i=0;i<size;i++)
    //     {
    //         ptr[i] = rand();
    //     }
    //     ptr.flush();
    // }

    // cpu_set_t cpuset_full;
    // CPU_ZERO(&cpuset_full);
    // for(int i=0;i<std::thread::hardware_concurrency();i++)
    //     CPU_SET(i, &cpuset_full);
    // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset_full);

    std::vector<std::thread> threads;
    for (size_t i = 0; i < num_threads; i++)
    {
        threads.emplace_back(
            [&, tid = i]()
            {
                std::mt19937_64 rand(tid);
                auto per_size = (size + num_threads - 1) / num_threads;
                auto start = tid * per_size;
                auto end = std::min((tid + 1) * per_size, size);
                for (size_t i = start; i < end; i++)
                {
                    ptr[i] = rand();
                }
                ptr.flush();
            });
    }
    for (auto &t : threads)
        t.join();
    threads.clear();

    printf("Finish init\n");

    auto start = std::chrono::high_resolution_clock::now();
    boost::sort::sample_sort(ptr, ptr + size, num_threads);
    auto end = std::chrono::high_resolution_clock::now();

    printf("%lf s\n", 1e-9 * (end - start).count());

    for (size_t i = 0; i < num_threads; i++)
    {
        threads.emplace_back(
            [&, tid = i]()
            {
                std::mt19937_64 rand(tid);
                auto per_size = (size + num_threads - 1) / num_threads;
                auto start = tid * per_size;
                auto end = std::min((tid + 1) * per_size, size);
                for (size_t i = start; i < end - 1; i++)
                {
                    if (ptr[i] > ptr[i + 1])
                    {
                        printf("Check Error, %lu %lu\n", ptr[i], ptr[i + 1]);
                    }
                }
                ptr.flush();
            });
    }
    for (auto &t : threads)
        t.join();
    threads.clear();

    printf("Finish Check\n");

    allocator.deallocate(ptr, size);
}
