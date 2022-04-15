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
#include "integrated_cache.hpp"
#include "type.hpp"
#include <cstddef>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace scache
{
    template <typename PointedType> class CachedPtr
    {
        template <typename> friend class CachedPtr;
        template <typename> friend class CachedAllocator;

    public:
        using element_type = PointedType;
        using pointer = PointedType *;
        using reference = std::add_lvalue_reference_t<PointedType>;
        using value_type = std::remove_volatile_t<std::remove_const_t<PointedType>>;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::random_access_iterator_tag;
        using offset_type = std::size_t;
        template <typename U> using rebind = CachedPtr<U>;

        CachedPtr() : cache(nullptr), offset(NULL_OFFSET) {}

        CachedPtr(IntegratedCache *_cache) : cache(_cache), offset(NULL_OFFSET) {}

        CachedPtr(IntegratedCache *_cache, offset_type _offset) : cache(_cache), offset(_offset)
        {
            // if (offset % element_size != 0)
            //{
            //    throw std::runtime_error("Pointer is not aligned");
            //}
        }

        template <typename T2> CachedPtr(const CachedPtr<T2> &ptr) : CachedPtr(ptr.cache, ptr.offset) {}

        template <typename T2> CachedPtr &operator=(const CachedPtr<T2> &ptr)
        {
            cache = ptr.cache;
            offset = ptr.offset;
            // if (offset % element_size != 0)
            //{
            //    throw std::runtime_error("Pointer is not aligned");
            //}
            return *this;
        }

        FORCE_INLINE offset_type get_offset() const { return offset; }

        FORCE_INLINE pointer get() const
        {
            if (unlikely(!cache || offset == NULL_OFFSET))
                return nullptr;
            auto page_id = offset >> CACHE_PAGE_BITS;
            auto page_offset = offset & CACHE_PAGE_MASK;
            auto ptr = cache->access(page_id, !is_const);
            return (pointer)((uint8_t *)ptr + page_offset);
        }

        FORCE_INLINE pointer operator->() const
        {
            auto ptr = get();
            // printf("%lu %s raw ptr %p\n", offset, is_const ? "const" : "non-const", ptr);
            return ptr;
        }

        FORCE_INLINE reference operator*() const
        {
            auto ptr = get();
            // printf("%lu %s access %p\n", offset, is_const ? "const" : "non-const", ptr);
            return *ptr;
        }

        FORCE_INLINE reference operator[](difference_type diff) const
        {
            // printf("%lu access [%lu]\n", offset, diff);
            // Wnull-dereference
            // if (unlikely(!cache || offset == NULL_OFFSET))
            //     return *(pointer) nullptr;
            auto tmp_off = offset + diff * element_size;
            auto page_id = tmp_off >> CACHE_PAGE_BITS;
            auto page_offset = tmp_off & CACHE_PAGE_MASK;
            auto ptr = cache->access(page_id, !is_const);
            return *(pointer)((uint8_t *)ptr + page_offset);
        }

        FORCE_INLINE CachedPtr &operator+=(difference_type diff)
        {
            offset += diff * element_size;
            return *this;
        }
        FORCE_INLINE CachedPtr &operator-=(difference_type diff)
        {
            offset -= diff * element_size;
            return *this;
        }
        FORCE_INLINE CachedPtr &operator++(void)
        {
            offset += element_size;
            return *this;
        }
        FORCE_INLINE CachedPtr operator++(int)
        {
            CachedPtr tmp(*this);
            offset += element_size;
            return tmp;
        }
        FORCE_INLINE CachedPtr &operator--(void)
        {
            offset -= element_size;
            return *this;
        }
        FORCE_INLINE CachedPtr operator--(int)
        {
            CachedPtr tmp(*this);
            offset -= element_size;
            return tmp;
        }
        FORCE_INLINE explicit operator bool() const { return !(!cache || offset == NULL_OFFSET); }

        FORCE_INLINE friend CachedPtr operator+(CachedPtr ptr, difference_type diff)
        {
            ptr += diff;
            return ptr;
        }
        FORCE_INLINE friend CachedPtr operator+(difference_type diff, CachedPtr ptr)
        {
            ptr += diff;
            return ptr;
        }
        FORCE_INLINE friend CachedPtr operator-(CachedPtr ptr, difference_type diff)
        {
            ptr -= diff;
            return ptr;
        }
        FORCE_INLINE friend CachedPtr operator-(difference_type diff, CachedPtr ptr)
        {
            ptr -= diff;
            return ptr;
        }
        FORCE_INLINE friend difference_type operator-(const CachedPtr &left, const CachedPtr &right)
        {
            return ((difference_type)left.offset - (difference_type)right.offset) / (difference_type)element_size;
        }
        FORCE_INLINE friend bool operator==(const CachedPtr &left, const CachedPtr &right)
        {
            return (left.offset == NULL_OFFSET && right.offset == NULL_OFFSET) ||
                   (left.cache == right.cache && left.offset == right.offset);
        }
        FORCE_INLINE friend bool operator!=(const CachedPtr &left, const CachedPtr &right) { return !(left == right); }
        FORCE_INLINE friend bool operator<(const CachedPtr &left, const CachedPtr &right)
        {
            return left.cache == right.cache && left.offset < right.offset;
        }
        FORCE_INLINE friend bool operator>(const CachedPtr &left, const CachedPtr &right)
        {
            return left.cache == right.cache && left.offset > right.offset;
        }
        FORCE_INLINE friend bool operator<=(const CachedPtr &left, const CachedPtr &right)
        {
            return left < right || left == right;
        }
        FORCE_INLINE friend bool operator>=(const CachedPtr &left, const CachedPtr &right)
        {
            return left > right || left == right;
        }

        FORCE_INLINE void flush() { cache->flush(); }

        FORCE_INLINE CachedPtr<std::add_const_t<value_type>> to_const() const { return *this; }

        FORCE_INLINE offset_type page_offset() const
        {
            if (unlikely(!cache || offset == NULL_OFFSET))
                return 0;
            auto page_id = offset >> CACHE_PAGE_BITS;
            return offset & CACHE_PAGE_MASK;
        }

        FORCE_INLINE pointer pin() const
        {
            if (unlikely(!cache || offset == NULL_OFFSET))
                return nullptr;
            auto page_id = offset >> CACHE_PAGE_BITS;
            auto page_offset = offset & CACHE_PAGE_MASK;
            auto ptr = cache->pin(page_id);
            return (pointer)((char *)ptr + page_offset);
        }

        FORCE_INLINE void unpin(bool is_write = !is_const) const
        {
            if (unlikely(!cache || offset == NULL_OFFSET))
                return;
            auto page_id = offset >> CACHE_PAGE_BITS;
            cache->unpin(page_id, is_write);
        }

        FORCE_INLINE pointer shared_pin() const
        {
            if (unlikely(!cache || offset == NULL_OFFSET))
                return nullptr;
            auto page_id = offset >> CACHE_PAGE_BITS;
            auto page_offset = offset & CACHE_PAGE_MASK;
            auto ptr = cache->shared_pin(page_id);
            return (pointer)ptr;
        }

        FORCE_INLINE void shared_unpin(bool is_write = !is_const) const
        {
            if (unlikely(!cache || offset == NULL_OFFSET))
                return;
            auto page_id = offset >> CACHE_PAGE_BITS;
            cache->shared_unpin(page_id, is_write);
        }

    private:
        constexpr static offset_type NULL_OFFSET = std::numeric_limits<offset_type>::max();
        constexpr static bool is_const = std::is_const_v<element_type>;
        constexpr static size_t element_size = sizeof(element_type);
        static_assert(element_size < CACHE_PAGE_SIZE);
        IntegratedCache *cache;
        offset_type offset;
    };
} // namespace scache
