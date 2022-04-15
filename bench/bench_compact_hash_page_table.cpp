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

#include "compact_hash_page_table.hpp"
#include <chrono>
#include <numa.h>
#include <omp.h>
#include <random>
#include <thread>

const size_t virt_size = 8 * 128lu * (1 << 30);
const size_t num_vpages = virt_size / scache::CACHE_PAGE_SIZE;
const size_t phy_size = 128lu * (1 << 30);
const size_t num_ppages = phy_size / scache::CACHE_PAGE_SIZE;
const size_t num_requests = 50000000;

int main()
{
    auto read_only = [&](size_t stride)
    {
        numa_set_interleave_mask(numa_all_nodes_ptr);
        scache::CompactHashPageTable page_table(num_vpages, num_ppages);

        for (uint64_t ppage_id = 0; ppage_id < num_ppages; ppage_id++)
        {
            auto vpage_id = ppage_id * stride;
            auto hint = page_table.find_or_create_hint(vpage_id);
            auto success = page_table.create_mapping(vpage_id, ppage_id);
            assert(success == true);
            success = page_table.release_mapping_lock(vpage_id);
            assert(success == true);
        }

        size_t sum_checksum = 0;
        double sum_throughput = 0;
        #pragma omp parallel
        {
            std::mt19937_64 rand(omp_get_thread_num());
            size_t checksum = 0;
            auto start = std::chrono::high_resolution_clock::now();
            for (uint64_t i = 0; i < num_requests; i++)
            {
                auto key = rand() % num_ppages;
                {
                    auto [success, ppage_id, pre_ref_count] = page_table.pin(key * stride);
                    assert(success == true);
                    assert(ppage_id == key);
                    checksum += ppage_id + pre_ref_count;
                }
                {
                    auto pre_ref_count = page_table.unpin(key * stride);
                    checksum += pre_ref_count;
                }
            }
            auto end = std::chrono::high_resolution_clock::now();

            #pragma omp critical
            {
                double throughput = 1e9 * num_requests / (end - start).count();
                // printf("\t[%d] : %lf ops/s : %lu\n", omp_get_thread_num(), throughput, checksum);
                sum_checksum += checksum;
                sum_throughput += throughput;
            }
        }
        printf("Shared Stride : %lu \n, Request : %lf ops/s, checksum : %lu\n", stride, sum_throughput, sum_checksum);
    };

    auto private_read_only = [&, num_vpages = num_vpages / omp_get_max_threads(),
                              num_ppages = num_ppages / omp_get_max_threads()](size_t stride)
    {
        numa_set_localalloc();
        size_t sum_checksum = 0;
        double sum_throughput = 0;
        #pragma omp parallel
        {
            scache::CompactHashPageTable page_table(num_vpages, num_ppages);

            for (uint64_t ppage_id = 0; ppage_id < num_ppages; ppage_id++)
            {
                auto vpage_id = ppage_id * stride;
                auto hint = page_table.find_or_create_hint(vpage_id);
                auto success = page_table.create_mapping(vpage_id, ppage_id);
                assert(success == true);
                success = page_table.release_mapping_lock(vpage_id);
                assert(success == true);
            }

            std::mt19937_64 rand(omp_get_thread_num());
            size_t checksum = 0;
            auto start = std::chrono::high_resolution_clock::now();
            for (uint64_t i = 0; i < num_requests; i++)
            {
                auto key = rand() % num_ppages;
                {
                    auto [success, ppage_id, pre_ref_count] = page_table.pin(key * stride);
                    assert(success == true);
                    assert(ppage_id == key);
                    checksum += ppage_id + pre_ref_count;
                }
                {
                    auto pre_ref_count = page_table.unpin(key * stride);
                    checksum += pre_ref_count;
                }
            }
            auto end = std::chrono::high_resolution_clock::now();

            #pragma omp critical
            {
                double throughput = 1e9 * num_requests / (end - start).count();
                // printf("\t[%d] : %lf ops/s : %lu\n", omp_get_thread_num(), throughput, checksum);
                sum_checksum += checksum;
                sum_throughput += throughput;
            }
        }
        printf("Private Stride : %lu \n, Request : %lf ops/s, checksum : %lu\n", stride, sum_throughput, sum_checksum);
    };

    for (size_t i = 1; i <= 8; i++)
    {
        read_only(i);
        private_read_only(i);
    }
    return 0;
}
