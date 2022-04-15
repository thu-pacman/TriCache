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

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <parallel/algorithm>
#include <thread>
#include <vector>

#ifdef WITH_CACHE
extern "C" void cache_reset_profile();
extern "C" void cache_dump_profile();
#else
void cache_reset_profile() {}
void cache_dump_profile() {}
#endif

const size_t item_size = 100;
const size_t key_size = 10;

struct Item
{
    char str[item_size];
};

static_assert(sizeof(Item) == item_size);

bool cmp_item(const Item &a, const Item &b)
{
    for (int i = 0; i < key_size; i++)
    {
        if (a.str[i] < b.str[i])
            return true;
        if (a.str[i] > b.str[i])
            return false;
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc <= 2)
        printf("usage: %s path num_threads\n", argv[0]);

    size_t num_threads = std::stoul(argv[2]);

    size_t total_items = 0;

    std::vector<std::tuple<std::filesystem::path, size_t, size_t>> files;
    auto directory = std::filesystem::path(argv[1]);
    for (auto f : std::filesystem::directory_iterator(directory))
    {
        if (f.is_regular_file() && f.file_size() > 0 && f.file_size() % item_size == 0)
        {
            files.emplace_back(f.path(), total_items, f.file_size() / item_size);
            total_items += f.file_size() / item_size;
        }
    }

    printf("%lu items\n", total_items);

    Item *items = (Item *)malloc(total_items * sizeof(Item));

    const size_t read_num_threads = 32;
    for (size_t i = 0; i < files.size(); i += read_num_threads)
    {
        printf("load %lu files\n", i);
        std::vector<std::thread> load_threads;
        for (size_t j = 0; j < read_num_threads && i + j < files.size(); j++)
        {
            auto [path, start, num_items] = files[i + j];
            load_threads.emplace_back(
                [&, path = path, start = start, num_items = num_items]()
                {
                    auto f = std::ifstream(path);
                    f.read((char *)(items + start), num_items * item_size);
                });
        }
        for (auto &t : load_threads)
            t.join();
    }

    cache_reset_profile();
    auto start = std::chrono::high_resolution_clock::now();
    __gnu_parallel::sort(items, items + total_items, cmp_item, __gnu_parallel::multiway_mergesort_tag());
    auto end = std::chrono::high_resolution_clock::now();
    cache_dump_profile();

#if (!DISABLE_DIRECT_CACHE && !DISABLE_PRIVATE_CACHE)
    size_t checksum = 0;
    #pragma omp parallel for reduction(+:checksum)
    for (size_t i = 0; i < total_items; i++)
    {
        for (size_t j = 0; j < item_size; j++)
        {
            checksum += j ^ items[i].str[j];
        }
    }

    size_t errors = 0;
    #pragma omp parallel for reduction(+:errors)
    for (size_t i = 0; i < total_items - 1; i++)
    {
        if (cmp_item(items[i + 1], items[i]))
            errors += 1;
    }

    printf("checksum %lu, errors %lu\n", checksum, errors);
#endif

    printf("exec %lf s\n", 1e-9 * (end - start).count());

    free(items);
    return 0;
}
