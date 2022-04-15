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
#include <cstddef>
#include <cstdint>

#define FORCE_INLINE __attribute__((always_inline))
#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

namespace scache
{
    using vpage_id_type = uint64_t;
    using ppage_id_type = uint64_t;
    using block_id_type = uint64_t;
    using partition_id_type = uint64_t;
    constexpr size_t CACHELINE_SIZE = 64;
#ifndef DEF_PAGE_BITS
    constexpr size_t CACHE_PAGE_BITS = 12;
#else
    constexpr size_t CACHE_PAGE_BITS = DEF_PAGE_BITS;
#endif
    constexpr size_t CACHE_PAGE_SIZE = 1lu << CACHE_PAGE_BITS;
    constexpr uintptr_t CACHE_PAGE_MASK = CACHE_PAGE_SIZE - 1;
} // namespace scache
