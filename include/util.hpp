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
#include "type.hpp"
#include <atomic>
#include <boost/fiber/operations.hpp>
#include <immintrin.h>
#include <list>
#include <mimalloc.h>
#include <numa.h>
#include <stdexcept>
#include <sys/mman.h>
#include <thread>

namespace scache
{
    inline void save_fence()
    {
        std::atomic_thread_fence(std::memory_order_release);
        // asm volatile("" ::: "memory");
        // _mm_sfence();
        // _mm_mfence();
        // _mm_pause();
    }

    inline void load_fence()
    {
        std::atomic_thread_fence(std::memory_order_acquire);
        // asm volatile("" ::: "memory");
        // _mm_lfence();
        // _mm_mfence();
        // _mm_pause();
    }

    inline void spin_pause()
    {
        // asm volatile("" ::: "memory");
        _mm_pause();
    }

    inline void compiler_fence() { asm volatile("" ::: "memory"); }

    constexpr bool MMAP_HUGEPAGE = false;
    constexpr size_t MMAP_PAGE_SIZE = 4096;
    constexpr size_t MMAP_HUGEPAGE_SIZE = 2 * 1024 * 1024;

    // inline void *mmap_alloc(size_t length)
    // {
    //     void *addr = nullptr;

    //     if (length < MMAP_HUGEPAGE_SIZE || !MMAP_HUGEPAGE)
    //     {
    //         length += (MMAP_PAGE_SIZE - length % MMAP_PAGE_SIZE) % MMAP_PAGE_SIZE;
    //         addr = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    //     }
    //     else
    //     {
    //         length += (MMAP_HUGEPAGE_SIZE - length % MMAP_HUGEPAGE_SIZE) % MMAP_HUGEPAGE_SIZE;
    //         addr = mmap(nullptr, length, PROT_READ | PROT_WRITE,
    //                     MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE | MAP_HUGETLB, -1, 0);
    //     }

    //     if (addr == nullptr)
    //         throw std::runtime_error("mmap alloc error");

    //     return addr;
    // }

    // inline void mmap_free(void *addr, size_t length)
    // {
    //     if (length < MMAP_HUGEPAGE_SIZE || !MMAP_HUGEPAGE)
    //     {
    //         length += (MMAP_PAGE_SIZE - length % MMAP_PAGE_SIZE) % MMAP_PAGE_SIZE;
    //     }
    //     else
    //     {
    //         length += (MMAP_HUGEPAGE_SIZE - length % MMAP_HUGEPAGE_SIZE) % MMAP_HUGEPAGE_SIZE;
    //     }

    //     auto ret = munmap(addr, length);

    //     if (ret != 0)
    //         throw std::runtime_error("mmap free error");
    // }

    class ThreadLocalSparseAllocator
    {
        unsigned char *new_segment(size_t sz = PRE_MMAP_SIZE)
        {
            auto seg = (unsigned char *)mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            if (seg == MAP_FAILED)
                throw std::runtime_error("mmap allocate " + std::to_string(sz) + " bytes segment error");
            numa_setlocal_memory(seg, sz);
            return seg;
        }
        void *new_shared_segment()
        {
            auto seg = new_segment();
            shared_segments.push_back(seg);
            return seg;
        }
        void *new_exclusive_segment(size_t sz)
        {
            auto seg = new_segment(sz);
            exclusive_segments.emplace_back(seg, sz);
            return seg;
        }

    public:
        ThreadLocalSparseAllocator() { new_shared_segment(); }
        ~ThreadLocalSparseAllocator()
        {
            for (auto seg : shared_segments)
                munmap(seg, PRE_MMAP_SIZE);
            for (auto [seg, sz] : exclusive_segments)
                munmap(seg, sz);
        }
        ThreadLocalSparseAllocator(const ThreadLocalSparseAllocator &) = delete;
        ThreadLocalSparseAllocator(ThreadLocalSparseAllocator &&) = delete;

        void *alloc(size_t length, size_t alignment)
        {
            if (length > PRE_MMAP_SIZE)
            {
                auto data = new_exclusive_segment(length);
                if ((uintptr_t)data % alignment)
                    throw std::runtime_error("mmaped data not aligned enough (" + std::to_string(alignment) +
                                             " required)");
                return data;
            }
            offset += (alignment - offset % alignment) % alignment;
            auto base = offset;
            if (offset + length > PRE_MMAP_SIZE)
            {
                new_shared_segment();
                offset = 0;
                base = 0;
            }
            offset += length;
            memset(shared_segments.back() + base, 0, length);
            return (void *)(shared_segments.back() + base);
        }

    private:
        std::list<unsigned char *> shared_segments;
        std::list<std::pair<unsigned char *, size_t>> exclusive_segments;
        size_t offset = 0;
        constexpr static size_t PRE_MMAP_SIZE = 1lu << 35;
    };

    inline void *mmap_alloc(size_t length, size_t alignment = CACHELINE_SIZE)
    {
#if 0
        thread_local static ThreadLocalSparseAllocator allocator;
        return allocator.alloc(length, alignment);
#else
        return mi_malloc_aligned(length, alignment);
#endif
    }

    inline void mmap_free(void *addr, size_t length) { mi_free(addr); }

    template <typename T> inline std::atomic<T> &as_atomic(T &t) { return (std::atomic<T> &)t; }

    inline uint64_t nextPowerOf2(uint64_t n)
    {
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        n++;
        return n;
    }

} // namespace scache
