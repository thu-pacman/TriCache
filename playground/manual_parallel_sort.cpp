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
#include "integrated_cache.hpp"
#include <chrono>
#include <cstring>
#include <omp.h>
#include <random>
#include <stdexcept>

const size_t virt_size = 128lu * (1 << 30);
const size_t phy_size = 128lu * (1 << 30);

template <typename T> void swap(T a, T b)
{
    auto t = *a;
    *a = *b;
    *b = t;
}

template <typename T> int partition(T arr, int low, int high)
{
    auto pivot = arr[high];
    int i = (low - 1);

    for (int j = low; j <= high - 1; j++)
    {
        if (arr[j] <= pivot)
        {
            i++;
            swap(arr + i, arr + j);
        }
    }
    swap(arr + i + 1, arr + high);
    return (i + 1);
}

template <typename T> void quick_sort(T arr, int low, int high)
{
    if (low < high)
    {
        int pi = partition(arr, low, high);

        quick_sort(arr, low, pi - 1);
        quick_sort(arr, pi + 1, high);
    }
}

int main(int argc, char **argv)
{
    const size_t len = std::atoi(argv[1]);

    if (argc <= 2)
    {
        printf("usage: %s len cpu_id,file_path ...\n", argv[0]);
        return 0;
    }

    std::vector<size_t> server_cpus;
    std::vector<std::string> server_paths;

    for (int i = 2; i < argc; i++)
    {
        auto str = std::string(argv[i]);
        auto pos = str.find(",");
        auto cpu = std::stoul(str.substr(0, pos));
        auto path = str.substr(pos + 1);
        printf("%lu %s\n", cpu, path.c_str());
        server_cpus.emplace_back(cpu);
        server_paths.emplace_back(path);
    }

    auto func = [&](auto &allocator)
    {
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
                auto t1 = std::chrono::high_resolution_clock::now();
                auto arr = allocator.allocate(len);

                auto rd = std::mt19937();
                int64_t ref = 0, sum = 0;
                for (size_t i = 0; i < len; i++)
                {
                    int data = rd();
                    ref += data;
                    arr[i] = data;
                }

                sum = 0;
                for (size_t i = 0; i < len; i++)
                    sum += arr[i];
                if (ref != sum)
                    throw std::runtime_error("before sum check error\n");

                #pragma omp barrier
                auto t2 = std::chrono::high_resolution_clock::now();
                quick_sort(arr, 0, len - 1);
                #pragma omp barrier
                auto t3 = std::chrono::high_resolution_clock::now();

                sum = 0;
                for (size_t i = 0; i < len; i++)
                    sum += arr[i];
                if (ref != sum)
                    throw std::runtime_error("later sum check error\n");

                for (size_t i = 0; i < len - 1; i++)
                {
                    int l = arr[i];
                    int r = arr[i + 1];
                    if (l > r)
                        throw std::runtime_error("compare check error\n");
                }
                #pragma omp barrier
                auto t4 = std::chrono::high_resolution_clock::now();

                #pragma omp single
                printf("exec time: %lf %lf %lf\n", 1e-9 * (t2 - t1).count(), 1e-9 * (t3 - t2).count(),
                       1e-9 * (t4 - t3).count());

                allocator.deallocate(arr, len);
            }
            else
            {
                #pragma omp barrier
                #pragma omp barrier
                #pragma omp barrier
                #pragma omp barrier
            }
        }
        printf("end\n");
    };

    scache::IntegratedCache cache(virt_size, phy_size, server_cpus, server_paths, omp_get_max_threads());
    scache::CachedAllocator<int> cache_allocator(&cache);
    std::allocator<int> std_allocator;
    func(std_allocator);
    func(cache_allocator);
}
