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
#include "page_table.hpp"
#include "partitioner.hpp"
#include "replacement.hpp"
#include "shared_cache.hpp"
#include "shared_single_thread_cache.hpp"
#include "single_thread_cache.hpp"
#include "type.hpp"
#include <atomic>
#include <boost/fiber/operations.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <vector>

namespace scache
{
    class PrivateCache
    {
        template <typename> friend class DirectCache;

        struct PointerState
        {
            void *pointer;
        };

        struct SharedCacheContext
        {
            PrivateCache *cache;
            size_t pid;
        };

        static bool evict_func(SharedCacheContext &async_context,
                               scache::vpage_id_type vpage_id,
                               scache::ppage_id_type ppage_id,
                               bool dirty,
                               const PointerState &) FORCE_INLINE
        {
            std::atomic_thread_fence(std::memory_order_release);

            auto global_vpage_id = async_context.cache->shared_cache.partitioner(async_context.pid, vpage_id);
            async_context.cache->shared_cache.unpin(global_vpage_id, dirty,
                                                    async_context.cache->partition_client.get());
            // printf("Evict %lu\n", vpage_id);
            return true;
        };

        static bool load_func(SharedCacheContext &async_context,
                              scache::vpage_id_type vpage_id,
                              scache::ppage_id_type ppage_id,
                              PointerState &state) FORCE_INLINE
        {
            auto g = async_context.cache->counter.guard_miss();
            auto global_vpage_id = async_context.cache->shared_cache.partitioner(async_context.pid, vpage_id);
            auto pointer =
                async_context.cache->shared_cache.pin(global_vpage_id, async_context.cache->partition_client.get());
            std::atomic_thread_fence(std::memory_order_acquire);
            state.pointer = pointer;
            // printf("Load %lu\n", vpage_id);
            return true;
        };

        // using single_thread_cache_type = SingleThreadCache<HashPageTable,
        //                                                    Clock,
        //                                                    SharedCacheContext,
        //                                                    PointerState,
        //                                                    decltype(&evict_func),
        //                                                    decltype(&load_func),
        //                                                    true>;

        using single_thread_cache_type = SharedSingleThreadCache<Clock,
                                                                 SharedCacheContext,
                                                                 PointerState,
                                                                 decltype(&evict_func),
                                                                 decltype(&load_func)>;

    public:
        PrivateCache(SharedCache &_shared_cache, const double occupy_ratio = 1.0)
            : shared_cache(_shared_cache),
              partition_client(shared_cache.get_client_shared_ptr()),
              num_local_ppages_per_partition(shared_cache.num_ppages_per_partition * occupy_ratio /
                                             shared_cache.max_num_clients),
              actual_num_ppages_per_thread(num_local_ppages_per_partition * shared_cache.num_partitions)
        {
            for (size_t i = 0; i < shared_cache.num_partitions; i++)
            {
                auto default_context = SharedCacheContext{this, i};
                private_caches.emplace_back(std::make_unique<single_thread_cache_type>(
                    shared_cache.partitioner.num_blocks(i), num_local_ppages_per_partition, evict_func, load_func,
                    default_context));
            }
        }

        PrivateCache(const PrivateCache &) = delete;
        PrivateCache(PrivateCache &&) = delete;

        ~PrivateCache()
        {
            flush();
            shared_cache.del_client();
        }

        FORCE_INLINE void flush()
        {
            counter.flush(global_counters[GLOBAL_PRIVATE]);
            for (auto &cache : private_caches)
            {
                // if (cache->num_pinned())
                //     printf("PrivateCache destructs with pinned pages.\n");
                cache->flush();
            }
            partition_client->wait();
            private_caches.clear();
            for (size_t i = 0; i < shared_cache.num_partitions; i++)
            {
                auto default_context = SharedCacheContext{this, i};
                private_caches.emplace_back(std::make_unique<single_thread_cache_type>(
                    shared_cache.partitioner.num_blocks(i), num_local_ppages_per_partition, evict_func, load_func,
                    default_context));
            }
        }

        FORCE_INLINE void *pin(vpage_id_type vpage_id)
        {
            auto g = counter.guard_access();
            if (vpage_id >= shared_cache.num_vpages)
                throw std::runtime_error("Virtual Page ID Error");
            auto [pid, shared_vpage_id] = shared_cache.partitioner(vpage_id);
            auto &cache = *private_caches[pid];
            single_thread_cache_type::context_type ret;
            size_t retry_loops = 0;
            do
            {
                ret = cache.pin(shared_vpage_id);
                while (ret.phase != decltype(ret)::Phase::End)
                {
                    cache.process(ret);
                    nano_spin();
                }
                if (++retry_loops > (1 << 30))
                    throw std::runtime_error("oom");
            } while (ret.ppage_id == single_thread_cache_type::context_type::EMPTY_PPAGE_ID);
            return ret.external_state->pointer;
        }

        FORCE_INLINE void unpin(vpage_id_type vpage_id, bool is_write = false)
        {
            if (vpage_id >= shared_cache.num_vpages)
                throw std::runtime_error("Virtual Page ID Error");
            auto [pid, shared_vpage_id] = shared_cache.partitioner(vpage_id);
            auto &cache = *private_caches[pid];
            auto ret = cache.unpin(shared_vpage_id, is_write);
            while (ret.phase != decltype(ret)::Phase::End)
            {
                cache.process(ret);
                nano_spin();
            }
        }

        FORCE_INLINE void get(uintptr_t addr, size_t size, void *data)
        {
            check_addr(addr, size);
            auto vpage_id = addr >> CACHE_PAGE_BITS;
            void *page_pointer = pin(vpage_id);
            auto addr_offset = addr & CACHE_PAGE_MASK;
            std::memcpy(data, (char *)page_pointer + addr_offset, size);
            unpin(vpage_id, false);
        }

        FORCE_INLINE void set(uintptr_t addr, size_t size, const void *data)
        {
            check_addr(addr, size);
            auto vpage_id = addr >> CACHE_PAGE_BITS;
            void *page_pointer = pin(vpage_id);
            auto addr_offset = addr & CACHE_PAGE_MASK;
            std::memcpy((char *)page_pointer + addr_offset, data, size);
            unpin(vpage_id, true);
        }

        template <typename T> FORCE_INLINE T get(uintptr_t addr)
        {
            T data;
            get(addr, sizeof(T), &data);
            return data;
        }

        template <typename T> FORCE_INLINE void set(uint64_t addr, const T &data) { set(addr, sizeof(T), &data); }

    private:
        FORCE_INLINE void check_addr(uintptr_t addr, size_t size) const { shared_cache.check_addr(addr, size); }

        SharedCache &shared_cache;
        std::shared_ptr<PartitionClient> partition_client;
        const size_t num_local_ppages_per_partition;
        const size_t actual_num_ppages_per_thread;

        std::vector<std::unique_ptr<single_thread_cache_type>> private_caches;

        AccessCounter counter;
    };
} // namespace scache
