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
#include "direct_cache.hpp"
#include "private_cache.hpp"
#include "shared_cache.hpp"
#include <boost/fiber/fss.hpp>
#include <boost/thread.hpp>
#include <boost/thread/tss.hpp>
#include <memory>
#include <stdexcept>

namespace scache
{
    class IntegratedCache
    {
    public:
        IntegratedCache(size_t _virt_size,
                        size_t _phy_size,
                        std::vector<size_t> _server_cpus,
                        std::vector<std::string> _server_paths,
                        size_t _max_num_clients,
                        double _private_occupy_ratio = 1.0)
            : virt_size(_virt_size),
              shared_cache(_virt_size, _phy_size, _server_cpus, _server_paths, _max_num_clients),
              private_occupy_ratio(_private_occupy_ratio),
              cache_id(get_cache_id())
        {
        }

        IntegratedCache(const IntegratedCache &) = delete;
        IntegratedCache(IntegratedCache &&) = delete;

        ~IntegratedCache() {}

        FORCE_INLINE void flush()
        {
            if constexpr (MAX_FIBERS_PER_THREAD == 1)
            {
                if (thread_direct_cache())
                {
                    thread_direct_cache()->flush();
                    delete thread_direct_cache();
                    thread_direct_cache() = nullptr;
                }
            }
            else
            {
                if (fiber_direct_cache.get())
                {
                    fiber_direct_cache->flush();
                    fiber_direct_cache.reset(nullptr);
                }
            }
            if (internal_private_cache())
            {
                internal_private_cache()->flush();
                delete internal_private_cache();
                internal_private_cache() = nullptr;
            }
        }

        FORCE_INLINE void *access(vpage_id_type vpage_id, bool is_write)
        {
#ifndef DISABLE_DIRECT_CACHE
            return get_direct_cache()->access(vpage_id, is_write);
#else
            auto ret = pin(vpage_id);
            unpin(vpage_id, is_write);
            return ret;
#endif
        }

        FORCE_INLINE void *pin(vpage_id_type vpage_id)
        {
#ifndef DISABLE_PRIVATE_CACHE
            return get_private_cache()->pin(vpage_id);
#else
            return shared_pin(vpage_id);
#endif
        }

        FORCE_INLINE void unpin(vpage_id_type vpage_id, bool is_write = false)
        {
#ifndef DISABLE_PRIVATE_CACHE
            return get_private_cache()->unpin(vpage_id, is_write);
#else
            return shared_unpin(vpage_id, is_write);
#endif
        }

        FORCE_INLINE void *shared_pin(vpage_id_type vpage_id) { return shared_cache.pin(vpage_id); }

        FORCE_INLINE void shared_unpin(vpage_id_type vpage_id, bool is_write = false)
        {
            return shared_cache.unpin(vpage_id, is_write);
        }

        FORCE_INLINE size_t size() const { return virt_size; }

        std::array<AccessCounter *, 3> get_access_counters()
        {
            return {&global_counters[GLOBAL_DIRECT], &global_counters[GLOBAL_PRIVATE],
                    &shared_cache.get_access_counter()};
        }

    private:
        FORCE_INLINE PrivateCache *get_private_cache()
        {
            auto &pointer = internal_private_cache();
            if (unlikely(!pointer))
            {
                pointer = new PrivateCache(shared_cache, private_occupy_ratio);
            }
            return pointer;
        }

        FORCE_INLINE PrivateCache *&internal_private_cache()
        {
            static thread_local PrivateCache *private_cache[MAX_CACHES];
            return private_cache[cache_id];
        }

#ifndef DISABLE_PRIVATE_CACHE

        FORCE_INLINE DirectCache<PrivateCache> *get_direct_cache()
        {
            if constexpr (MAX_FIBERS_PER_THREAD == 1)
            {
                auto &pointer = thread_direct_cache();
                if (unlikely(!pointer))
                {
                    pointer = new DirectCache(*get_private_cache());
                }
                return pointer;
            }
            else
            {
                auto pointer = fiber_direct_cache.get();
                if (unlikely(!pointer))
                {
                    pointer = new DirectCache(*get_private_cache());
                    fiber_direct_cache.reset(pointer);
                }
                return pointer;
            }
        }

        FORCE_INLINE DirectCache<PrivateCache> *&thread_direct_cache()
        {
            static thread_local DirectCache<PrivateCache> *direct_cache[MAX_CACHES];
            return direct_cache[cache_id];
        }

#else

        FORCE_INLINE DirectCache<SharedCache> *get_direct_cache()
        {
            if constexpr (MAX_FIBERS_PER_THREAD == 1)
            {
                auto &pointer = thread_direct_cache();
                if (unlikely(!pointer))
                {
                    pointer = new DirectCache(shared_cache);
                }
                return pointer;
            }
            else
            {
                auto pointer = fiber_direct_cache.get();
                if (unlikely(!pointer))
                {
                    pointer = new DirectCache(shared_cache);
                    fiber_direct_cache.reset(pointer);
                }
                return pointer;
            }
        }

        FORCE_INLINE DirectCache<SharedCache> *&thread_direct_cache()
        {
            static thread_local DirectCache<SharedCache> *direct_cache[MAX_CACHES];
            return direct_cache[cache_id];
        }

#endif

        FORCE_INLINE static size_t get_cache_id()
        {
            static std::atomic_size_t global_cache_id = 0;
            auto cache_id = global_cache_id++;
            if (cache_id >= MAX_CACHES)
                throw std::runtime_error("Not support more caches");
            return cache_id;
        }

        const size_t virt_size;
        const double private_occupy_ratio;
        const size_t cache_id;
        SharedCache shared_cache;
#ifndef DISABLE_PRIVATE_CACHE
        boost::fibers::fiber_specific_ptr<DirectCache<PrivateCache>> fiber_direct_cache;
#else
        boost::fibers::fiber_specific_ptr<DirectCache<SharedCache>> fiber_direct_cache;
#endif
    };

} // namespace scache
