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
#include "util.hpp"
#include <cassert>

namespace scache
{
    class MemoryPool
    {
    public:
        MemoryPool(ppage_id_type _num_pages, void *_pool = nullptr) : num_pages(_num_pages)
        {
            if (_pool)
            {
                pool = (uint8_t *)_pool;
                is_external = true;
            }
            else
            {
                mmap_pool = (uint8_t *)mmap_alloc(num_pages * CACHE_PAGE_SIZE + CACHE_PAGE_SIZE);
                pool = mmap_pool + (CACHE_PAGE_SIZE - (uintptr_t)mmap_pool % CACHE_PAGE_SIZE) % CACHE_PAGE_SIZE;
                is_external = false;
            }

            first_loaded = (bool *)mmap_alloc(num_pages);
        }

        MemoryPool(const MemoryPool &) = delete;
        MemoryPool(MemoryPool &&) = delete;

        ~MemoryPool()
        {
            if (!is_external)
                mmap_free(mmap_pool, num_pages * CACHE_PAGE_SIZE + CACHE_PAGE_SIZE);

            mmap_free(first_loaded, num_pages);
        }

        void *from_page_id(const ppage_id_type &id) const
        {
            assert(id < num_pages);
            return pool + id * CACHE_PAGE_SIZE;
        }

        ppage_id_type to_page_id(void *ptr) const
        {
            assert((uintptr_t)ptr % num_pages == 0 && ptr > pool && ptr < pool + num_pages * CACHE_PAGE_SIZE);
            return ((uint8_t *)ptr - pool) / num_pages;
        }

        bool loaded(const ppage_id_type &id)
        {
            auto before = first_loaded[id];
            first_loaded[id] = true;
            return before;
        }

    private:
        ppage_id_type num_pages;
        uint8_t *pool = nullptr;
        uint8_t *mmap_pool = nullptr;
        bool *first_loaded = nullptr;
        bool is_external;
    };
} // namespace scache
