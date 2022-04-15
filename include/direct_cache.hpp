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
#include "access_counter.hpp"
#include "private_cache.hpp"
#include "shared_cache.hpp"
#include "type.hpp"
#include "util.hpp"

namespace scache
{
    template <typename Cache> class DirectCache
    {
    public:
        DirectCache(Cache &_cache)
            : cache(_cache),
              num_pinned(std::min(
                  MAX_NUM_PINNED,
                  std::max(nextPowerOf2(_cache.actual_num_ppages_per_thread) / 4 / MAX_FIBERS_PER_THREAD, 1lu))),
              num_pinned_mask(num_pinned - 1)
        {
            store = (std::pair<vpage_id_type, void *> *)mmap_alloc(
                num_pinned * sizeof(std::pair<vpage_id_type, void *>), CACHELINE_SIZE);
            dirty = (bool *)mmap_alloc(num_pinned * sizeof(bool), CACHELINE_SIZE);
            for (size_t i = 0; i < num_pinned; i++)
            {
                store[i] = {EMPTY, nullptr};
                dirty[i] = false;
            }
        }

        DirectCache(const DirectCache &) = delete;
        DirectCache(DirectCache &&) = delete;

        ~DirectCache()
        {
            flush();
            mmap_free(store, num_pinned * sizeof(std::pair<vpage_id_type, void *>));
            mmap_free(dirty, num_pinned * sizeof(bool));
        }

        FORCE_INLINE void flush()
        {
            counter.flush(global_counters[GLOBAL_DIRECT]);
            for (size_t i = 0; i < num_pinned; i++)
            {
                if (store[i].first != EMPTY)
                    cache.unpin(store[i].first, dirty[i]);
                store[i] = {EMPTY, nullptr};
                dirty[i] = false;
            }
        }

        // The pointer is safe before next memory access
        FORCE_INLINE void *access(vpage_id_type vpage_id, bool is_write)
        {
            auto ga = counter.guard_access();
            if (store[vpage_id & num_pinned_mask].first != vpage_id)
            {
                auto gm = counter.guard_miss();
                if (store[vpage_id & num_pinned_mask].first != EMPTY)
                    cache.unpin(store[vpage_id & num_pinned_mask].first, dirty[vpage_id & num_pinned_mask]);
                store[vpage_id & num_pinned_mask].first = vpage_id;
                store[vpage_id & num_pinned_mask].second = cache.pin(vpage_id);
                dirty[vpage_id & num_pinned_mask] = false;
            }
            dirty[vpage_id & num_pinned_mask] |= is_write;
            return store[vpage_id & num_pinned_mask].second;
        }

    private:
        constexpr static vpage_id_type EMPTY = std::numeric_limits<vpage_id_type>::max();
        constexpr static size_t MAX_NUM_PINNED = 1 << 30;
        Cache &cache;
        const size_t num_pinned;
        const size_t num_pinned_mask;
        std::pair<vpage_id_type, void *> *store;
        bool *dirty;

        AccessCounter counter;
    };
} // namespace scache
