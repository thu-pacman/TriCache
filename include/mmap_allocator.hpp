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
#include <boost/thread/tss.hpp>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <type_traits>
#include <typeinfo>
#include <unistd.h>
#include <vector>
#ifdef ENABLE_UMAP
#include <umap/umap.h>
#endif

namespace scache
{
    class InternalMMapAllocator
    {
    public:
        using difference_type = std::ptrdiff_t;
        using size_type = std::size_t;
        using offset_type = std::size_t;

        using recycle_type = std::vector<std::vector<offset_type>>;

        InternalMMapAllocator(std::string _path, size_type _total_size, size_type _base_offset = 0)
            : path(_path),
              total_size(_total_size),
              base_offset(_base_offset),
              mutex(),
              free_blocks(),
              large_free_blocks(MAX_ORDER),
              used_size(0)
        {
            fd = open(path.c_str(), O_CREAT | O_RDWR, 0640);
            if (fd == -1)
                throw std::runtime_error(std::string("open path ") + path + " error.");
            if (ftruncate(fd, total_size) != 0)
                throw std::runtime_error("ftruncate error.");
#ifdef ENABLE_UMAP
            pool = (char *)umap(nullptr, total_size, PROT_READ | PROT_WRITE, UMAP_PRIVATE, fd, 0);
#else
            pool = (char *)mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
#endif
            if (pool == MAP_FAILED)
                throw std::runtime_error("mmap error.");
            int ret = madvise(pool, total_size, MADV_RANDOM);
            if (ret != 0)
                throw std::runtime_error("madvise error.");
        }

        ~InternalMMapAllocator()
        {
#ifdef ENABLE_UMAP
            uunmap(pool, total_size);
#else
            munmap(pool, total_size);
#endif
            close(fd);
        }

        template <typename T> T *allocate(size_t n)
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
            return (T *)(pool + offset);
        }

        template <typename T> void deallocate(T *data, size_t n) noexcept
        {
            size_t size = n * sizeof(T);
            size_t order = size_to_order(size);
            if (order < LARGE_BLOCK_ORDER_THRESHOLD)
            {
                push(get_private_free_blocks(), order, (char *)data - pool);
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex);
                push(large_free_blocks, order, (char *)data - pool);
            }
            // printf("deallocate %lu * %s(%lu) at %lu-%lu\n", n, boost::core::demangle(typeid(T).name()).c_str(),
            // sizeof(T), data.get_offset(), data.get_offset()+size);
        }

    private:
        inline static size_t size_to_order(size_t size)
        {
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
            if (!pointer)
            {
                pointer = new recycle_type(LARGE_BLOCK_ORDER_THRESHOLD);
                free_blocks.reset(pointer);
            }
            return *pointer;
        }

        constexpr static size_t MAX_ORDER = 64;
        constexpr static size_t LARGE_BLOCK_ORDER_THRESHOLD = 20;
        constexpr static offset_type NULL_OFFSET = std::numeric_limits<offset_type>::max();
        const std::string path;
        const size_type total_size;
        const offset_type base_offset;
        int fd;
        char *pool;
        std::mutex mutex;
        boost::thread_specific_ptr<std::vector<std::vector<offset_type>>> free_blocks;
        std::vector<std::vector<offset_type>> large_free_blocks;
        std::atomic<offset_type> used_size;
    };

    template <typename ValueType> class MMapAllocator
    {
        template <typename> friend class MMapAllocator;

    public:
        using value_type = ValueType;
        using pointer = ValueType *;
        using const_pointer = const ValueType *;
        using void_pointer = void *;
        using const_void_pointer = const void *;
        using difference_type = InternalMMapAllocator::difference_type;
        using size_type = InternalMMapAllocator::size_type;
        using offset_type = InternalMMapAllocator::offset_type;
        template <typename U> using rebind = MMapAllocator<U>;

        using recycle_type = std::vector<std::vector<offset_type>>;

        MMapAllocator(std::string _path, size_type _total_size, size_type _base_offset = 0)
            : internal(std::make_shared<InternalMMapAllocator>(_path, _total_size, _base_offset))
        {
        }

        MMapAllocator(std::shared_ptr<InternalMMapAllocator> _internal) : internal(_internal) {}

        template <typename T2> MMapAllocator(const MMapAllocator<T2> &allocator) : MMapAllocator(allocator.internal) {}

        template <typename T2> MMapAllocator &operator=(const MMapAllocator<T2> &allocator)
        {
            internal = allocator.internal;
            return *this;
        }

        pointer allocate(size_t n) { return internal->allocate<value_type>(n); }

        void deallocate(pointer data, size_t n) noexcept { internal->deallocate(data, n); }

        template <class U> bool operator==(const MMapAllocator<U> &right) { return internal == right.internal; }
        template <class U> bool operator!=(const MMapAllocator<U> &right) { return !(*this == right); }

    private:
        std::shared_ptr<InternalMMapAllocator> internal;
    };

} // namespace scache
