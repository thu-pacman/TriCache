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

#pragma once

#include <atomic>
#include <x86intrin.h>

namespace
{
    inline uint64_t rdtsc()
    {
        unsigned int tmp;
        return __rdtscp(&tmp);
    }
} // namespace

#ifdef ENABLE_PROFILE
class AccessCounter
{
private:
    std::atomic_uint64_t profile_num_access, profile_num_miss, profile_cycle_access, profile_cycle_miss;

public:
    uint64_t get_profile_num_access() const { return profile_num_access; }
    uint64_t get_profile_num_miss() const { return profile_num_miss; }
    uint64_t get_profile_cycle_access() const { return profile_cycle_access; }
    uint64_t get_profile_cycle_miss() const { return profile_cycle_miss; }

    void count_access() { profile_num_access++; }
    void count_miss() { profile_num_miss++; }
    void count_access_with_cycles(uint64_t t)
    {
        count_access();
        profile_cycle_access += t;
    }
    void count_miss_with_cycles(uint64_t t)
    {
        count_miss();
        profile_cycle_miss += t;
    }
    void clear() { profile_num_access = profile_num_miss = profile_cycle_access = profile_cycle_miss = 0; }
    void flush(AccessCounter &other)
    {
        other.profile_num_access += profile_num_access;
        other.profile_num_miss += profile_num_miss;
        other.profile_cycle_access += profile_cycle_access;
        other.profile_cycle_miss += profile_cycle_miss;
        clear();
    }

    AccessCounter() { clear(); }
    AccessCounter(const AccessCounter &other)
    {
        profile_num_access = other.profile_num_access.load();
        profile_num_miss = other.profile_num_miss.load();
        profile_cycle_access = other.profile_cycle_access.load();
        profile_cycle_miss = other.profile_cycle_miss.load();
    };

    struct access_guard
    {
        AccessCounter &counter;
        uint64_t t;
        access_guard(AccessCounter &counter) : counter(counter), t(rdtsc()) {}
        ~access_guard() { counter.count_access_with_cycles(rdtsc() - t); }
    };
    access_guard guard_access() { return *this; }

    struct miss_guard
    {
        AccessCounter &counter;
        uint64_t t;
        miss_guard(AccessCounter &counter) : counter(counter), t(rdtsc()) {}
        ~miss_guard() { counter.count_miss_with_cycles(rdtsc() - t); }
    };
    miss_guard guard_miss() { return *this; }
};
#else
class AccessCounter
{
public:
    uint64_t get_profile_num_access() const { return 0; }
    uint64_t get_profile_num_miss() const { return 0; }
    uint64_t get_profile_cycle_access() const { return 0; }
    uint64_t get_profile_cycle_miss() const { return 0; }

    void count_access() {}
    void count_miss() {}
    void count_access_with_cycles(uint64_t t) {}
    void count_miss_with_cycles(uint64_t t) {}
    void clear() {}
    void flush(AccessCounter &other) {}

    struct empty_guard
    {
    };
    empty_guard guard_access() { return {}; }
    empty_guard guard_miss() { return {}; }
};
#endif

enum GlobalCounters
{
    GLOBAL_DIRECT,
    GLOBAL_PRIVATE,
    GLOBAL_COUNTER_NUM
};
namespace
{
    AccessCounter global_counters[GLOBAL_COUNTER_NUM];
}