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
#include "cached_ptr.hpp"
#include "type.hpp"
#include <boost/thread/tss.hpp>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <typeinfo>

namespace scache
{
    class InternalCachedAllocator
    {
    public:
        using difference_type = std::ptrdiff_t;
        using size_type = std::size_t;
        using offset_type = std::size_t;

        using recycle_type = std::vector<std::vector<offset_type>>;

        InternalCachedAllocator(IntegratedCache *_cache, size_type _total_size = 0, size_type _base_offset = 0)
            : cache(_cache),
              total_size(_total_size ? _total_size : cache->size()),
              base_offset(_base_offset),
              mutex(),
              free_blocks(),
              large_free_blocks(MAX_ORDER),
              used_size(0)
        {
        }

        template <typename T> CachedPtr<T> allocate(size_t n)
        {
            size_t size = n * sizeof(T);
            size_t order = size_to_order(size);
            offset_type offset = NULL_OFFSET;
            if (order < LARGE_BLOCK_ORDER_THRESHOLD)
            {
                offset = pop(get_private_free_blocks(), order);
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex);
                offset = pop(large_free_blocks, order);
            }

            if (offset == NULL_OFFSET)
            {
                size_t block_size = 1ul << order;
                offset = used_size.fetch_add(block_size);
                if (offset >= total_size)
                {
                    offset = used_size.fetch_sub(block_size);
                    throw std::bad_alloc();
                }
                offset += base_offset;
            }

            // printf("allocate %lu * %s(%lu) at %lu-%lu\n", n, boost::core::demangle(typeid(T).name()).c_str(),
            // sizeof(T), offset, offset+size);
            return CachedPtr<T>(cache, offset);
        }

        template <typename T> void deallocate(const CachedPtr<T> &data, size_t n) noexcept
        {
            size_t size = n * sizeof(T);
            size_t order = size_to_order(size);
            if (order < LARGE_BLOCK_ORDER_THRESHOLD)
            {
                push(get_private_free_blocks(), order, data.get_offset());
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex);
                push(large_free_blocks, order, data.get_offset());
            }
            // printf("deallocate %lu * %s(%lu) at %lu-%lu\n", n, boost::core::demangle(typeid(T).name()).c_str(),
            // sizeof(T), data.get_offset(), data.get_offset()+size);
        }

        void flush() { cache->flush(); }

    private:
        inline static size_t size_to_order(size_t size)
        {
            size = std::max(scache::CACHE_PAGE_SIZE, size);
            size_t order = ((size & (size - 1)) != 0);
            while (size > 1)
            {
                order += 1;
                size >>= 1;
            }
            return order;
        }

        offset_type pop(recycle_type &free_block, size_t order)
        {
            offset_type offset = NULL_OFFSET;
            if (free_block[order].size())
            {
                offset = free_block[order].back();
                free_block[order].pop_back();
            }
            return offset;
        }

        void push(recycle_type &free_block, size_t order, offset_type pointer) { free_block[order].push_back(pointer); }

        recycle_type &get_private_free_blocks()
        {
            auto pointer = free_blocks.get();
            if (unlikely(!pointer))
            {
                pointer = new recycle_type(LARGE_BLOCK_ORDER_THRESHOLD);
                free_blocks.reset(pointer);
            }
            return *pointer;
        }

        constexpr static size_t MAX_ORDER = 64;
        constexpr static size_t LARGE_BLOCK_ORDER_THRESHOLD = 20;
        constexpr static offset_type NULL_OFFSET = std::numeric_limits<offset_type>::max();
        IntegratedCache *const cache;
        const size_type total_size;
        const offset_type base_offset;
        std::mutex mutex;
        boost::thread_specific_ptr<std::vector<std::vector<offset_type>>> free_blocks;
        std::vector<std::vector<offset_type>> large_free_blocks;
        std::atomic<offset_type> used_size;
    };

    template <typename ValueType> class CachedAllocator
    {
        template <typename> friend class CachedAllocator;

    public:
        using value_type = ValueType;
        static_assert(sizeof(value_type) < CACHE_PAGE_SIZE);
        using pointer = CachedPtr<ValueType>;
        using const_pointer = CachedPtr<std::add_const_t<ValueType>>;
        using void_pointer = CachedPtr<void>;
        using const_void_pointer = CachedPtr<const void>;
        using difference_type = InternalCachedAllocator::difference_type;
        using size_type = InternalCachedAllocator::size_type;
        using offset_type = InternalCachedAllocator::offset_type;
        template <typename U> using rebind = CachedAllocator<U>;

        using recycle_type = std::vector<std::vector<offset_type>>;

        CachedAllocator(IntegratedCache *_cache, size_type _total_size = 0, size_type _base_offset = 0)
            : internal(std::make_shared<InternalCachedAllocator>(_cache, _total_size, _base_offset))
        {
        }

        CachedAllocator(std::shared_ptr<InternalCachedAllocator> _internal) : internal(_internal) {}

        template <typename T2>
        CachedAllocator(const CachedAllocator<T2> &allocator) : CachedAllocator(allocator.internal)
        {
        }

        template <typename T2> CachedAllocator &operator=(const CachedAllocator<T2> &allocator)
        {
            internal = allocator.internal;
            return *this;
        }

        pointer allocate(size_t n) { return internal->allocate<value_type>(n); }

        void deallocate(const pointer &data, size_t n) noexcept { internal->deallocate(data, n); }

        void flush() { internal->flush(); }

        template <class U> bool operator==(const CachedAllocator<U> &right) { return internal == right.internal; }
        template <class U> bool operator!=(const CachedAllocator<U> &right) { return !(*this == right); }

    private:
        std::shared_ptr<InternalCachedAllocator> internal;
    };

} // namespace scache
