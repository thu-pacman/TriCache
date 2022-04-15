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

#include "page_table.hpp"
#include "replacement.hpp"
#include "single_thread_cache.hpp"
#include "type.hpp"
#include <chrono>
#include <random>
#include <thread>

const uint64_t num = 1 * (1lu << 30);
const uint64_t size = 64 * (1lu << 30) / (1 << 12);
const uint64_t range = 4096 * (1lu << 30) / (1 << 12);

int main()
{
    struct Empty
    {
    };
    auto evict_func = [](Empty, scache::vpage_id_type, scache::ppage_id_type, bool, const Empty &) { return true; };
    auto load_func = [](Empty, scache::vpage_id_type, scache::ppage_id_type, Empty &) { return true; };
    scache::SingleThreadCache<scache::DirectPageTable, scache::Clock, Empty, Empty, decltype(evict_func),
                              decltype(load_func)>
        cache(range, size, evict_func, load_func);
    std::mt19937 rand;
    static_assert(std::mt19937::max() > range);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto start = std::chrono::high_resolution_clock::now();
        for (uint64_t i = 0; i < num; i++)
        {
            auto key = rand() % range;
            auto pin = cache.pin(key);
            while (pin.phase != decltype(pin)::Phase::End)
                cache.process(pin);
            auto unpin = cache.unpin(key);
            while (unpin.phase != decltype(unpin)::Phase::End)
                cache.process(unpin);
        }
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "Pin/Unpin %.3lf OPs/s\n",
                1e9 * num / std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }
    return 0;
}
