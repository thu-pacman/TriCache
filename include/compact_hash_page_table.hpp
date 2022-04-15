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
#include "partition_type.hpp"
#include "util.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <optional>

namespace scache
{
    class CompactHashPageTable
    {
    public:
        struct unpacked_pte
        {
            bool exist;
            bool busy;
            bool dirty;
            uint16_t ref_count;
            ppage_id_type ppage_id;
        };
        struct alignas(sizeof(uint16_t)) pte_header
        {
            bool exist : 1;
            bool busy : 1;
            bool dirty : 1;
            uint16_t ref_count : 13;

            uint16_t &as_packed() { return (uint16_t &)*this; }

            const uint16_t &as_packed() const { return (const uint16_t &)*this; }

            static inline pte_header &from_packed(uint16_t &packed) { return (pte_header &)packed; }

            std::tuple<bool, uint16_t> inc_ref_count()
            {
                std::atomic<uint16_t> &atomic_packed = as_atomic(as_packed());
                uint16_t old_packed = atomic_packed.load(std::memory_order_relaxed), new_packed;
                uint16_t old_ref_count;

                do
                {
                    new_packed = old_packed;
                    auto &new_header = pte_header::from_packed(new_packed);

                    if (!new_header.exist || new_header.busy)
                        return {false, 0};

                    assert(new_header.ref_count < std::numeric_limits<uint16_t>::max() - 1);

                    old_ref_count = new_header.ref_count++;

                } while (!atomic_packed.compare_exchange_weak(old_packed, new_packed, std::memory_order_acquire,
                                                              std::memory_order_relaxed));

                return {true, old_ref_count};
            }

            std::tuple<bool, uint16_t> dec_ref_count(bool is_write = false)
            {
                std::atomic<uint16_t> &atomic_packed = as_atomic(as_packed());
                uint16_t old_packed = atomic_packed.load(std::memory_order_relaxed), new_packed;
                uint16_t old_ref_count;

                do
                {
                    new_packed = old_packed;
                    auto &new_header = pte_header::from_packed(new_packed);

                    assert(new_header.exist && !new_header.busy && new_header.ref_count > 0);

                    old_ref_count = new_header.ref_count--;

                    if (is_write)
                        new_header.dirty = true;

                } while (!atomic_packed.compare_exchange_weak(old_packed, new_packed, std::memory_order_release,
                                                              std::memory_order_relaxed));

                return {true, old_ref_count};
            }

            bool lock()
            {
                std::atomic<uint16_t> &atomic_packed = as_atomic(as_packed());
                uint16_t old_packed = atomic_packed.load(std::memory_order_acquire), new_packed;

                do
                {
                    new_packed = old_packed;
                    auto &new_header = pte_header::from_packed(new_packed);

                    if (new_header.ref_count != 0 || new_header.busy)
                        return false;

                    new_header.busy = true;

                } while (!atomic_packed.compare_exchange_weak(old_packed, new_packed, std::memory_order_release,
                                                              std::memory_order_relaxed));

                return true;
            }

            bool unlock()
            {
                std::atomic<uint16_t> &atomic_packed = as_atomic(as_packed());
                uint16_t old_packed = atomic_packed.load(std::memory_order_acquire), new_packed;

                do
                {
                    new_packed = old_packed;
                    auto &new_header = pte_header::from_packed(new_packed);

                    if (!new_header.busy)
                        return false;

                    new_header.busy = false;

                } while (!atomic_packed.compare_exchange_weak(old_packed, new_packed, std::memory_order_release,
                                                              std::memory_order_relaxed));

                return true;
            }
        };
        static_assert(sizeof(pte_header) == sizeof(uint16_t));

        struct alignas(CACHELINE_SIZE) packed_cache_line
        {
            uint64_t tag : 58;
            uint8_t num_using : 4;
            pte_header headers[8];
            uint32_t ppage_ids[8];
            packed_cache_line *next;

            constexpr static size_t NUM_PACK_PAGES = 8;
            constexpr static pte_header EMPTY_HEADER = {false, false, false, 0};
            constexpr static uint32_t EMPTY_PPAGE_ID = std::numeric_limits<uint32_t>::max();

            // non-atomic
            unpacked_pte to_unpacked(size_t offset)
            {
                auto packed_header = as_atomic(headers[offset].as_packed()).load(std::memory_order_relaxed);
                auto header = pte_header::from_packed(packed_header);
                auto ppage_id = ppage_ids[offset];
                return {header.exist, header.busy, header.dirty, header.ref_count, ppage_id};
            }
        };
        static_assert(sizeof(packed_cache_line) == CACHELINE_SIZE);

        CompactHashPageTable(const vpage_id_type &_max_vpage_id, const ppage_id_type &_max_ppage_id)
            : max_vpage_id(_max_vpage_id), max_ppage_id(_max_ppage_id), bucket_size(max_ppage_id * 2)
        {
            store = (packed_cache_line *)mmap_alloc((bucket_size + max_ppage_id) * sizeof(packed_cache_line),
                                                    CACHELINE_SIZE);
            pool.resize(max_ppage_id);
            for (size_t i = 0; i < bucket_size + max_ppage_id; i++)
            {
                store[i].tag = EMPTY_TAG;
                store[i].num_using = 0;
                for (size_t j = 0; j < packed_cache_line::NUM_PACK_PAGES; j++)
                {
                    store[i].headers[j] = packed_cache_line::EMPTY_HEADER;
                    store[i].ppage_ids[j] = packed_cache_line::EMPTY_PPAGE_ID;
                }
                store[i].next = nullptr;
            }
            for (size_t i = 0; i < max_ppage_id; i++)
                pool[i] = store + bucket_size + i;
            // std::reverse(pool.begin(), pool.end());
        }

        CompactHashPageTable(const CompactHashPageTable &) = delete;
        CompactHashPageTable(CompactHashPageTable &&) = delete;

        ~CompactHashPageTable() { mmap_free(store, (bucket_size + max_ppage_id) * sizeof(packed_cache_line)); }

        std::tuple<bool, ppage_id_type, uint16_t> pin(vpage_id_type vpage_id, packed_cache_line *hint = nullptr)
        {
            uint64_t tag = vpage_id / packed_cache_line::NUM_PACK_PAGES;
            uint64_t offset = vpage_id % packed_cache_line::NUM_PACK_PAGES;

            auto cacheline = hint ? hint : find_cacheline(tag);
            assert(!cacheline || cacheline->tag == tag);

            if (!cacheline)
                return {};

            auto [has_inc, pre_ref_count] = cacheline->headers[offset].inc_ref_count();

            if (!has_inc)
                return {};

            if (cacheline->tag != tag)
            {
                printf("Check Tag Error\n");
                auto [has_dec, _] = cacheline->headers[offset].dec_ref_count();
                assert(has_dec == true);
                return {};
            }

            return {true, cacheline->ppage_ids[offset], pre_ref_count};
        }

        uint16_t unpin(vpage_id_type vpage_id, bool is_write = false, packed_cache_line *hint = nullptr)
        {
            uint64_t tag = vpage_id / packed_cache_line::NUM_PACK_PAGES;
            uint64_t offset = vpage_id % packed_cache_line::NUM_PACK_PAGES;

            auto cacheline = hint ? hint : find_cacheline(tag, true);
            assert(find_cacheline(tag, true) == cacheline);
            assert(cacheline && cacheline->tag == tag);

            auto [has_dec, pre_ref_count] = cacheline->headers[offset].dec_ref_count(is_write);

            assert(has_dec == true);

            return pre_ref_count;
        }

        // With lock
        bool create_mapping(vpage_id_type vpage_id,
                            ppage_id_type ppage_id,
                            uint16_t ref_count = 1,
                            packed_cache_line *hint = nullptr)
        {
            uint64_t tag = vpage_id / packed_cache_line::NUM_PACK_PAGES;
            uint64_t offset = vpage_id % packed_cache_line::NUM_PACK_PAGES;

            auto cacheline = hint ? hint : find_cacheline(tag);

            assert(cacheline && cacheline->tag == tag);

            assert(cacheline->headers[offset].exist == false);

            auto is_lock = cacheline->headers[offset].lock();
            if (!is_lock)
                return false;

            assert(cacheline->headers[offset].exist == false);
            assert(cacheline->headers[offset].busy == true);
            assert(cacheline->headers[offset].dirty == false);
            assert(cacheline->headers[offset].ref_count == 0);

            assert(ppage_id < (ppage_id_type)packed_cache_line::EMPTY_PPAGE_ID);
            cacheline->headers[offset].exist = true;
            cacheline->ppage_ids[offset] = ppage_id;
            cacheline->headers[offset].ref_count = ref_count;

            cacheline->num_using++;

            // printf("Create Mapping %lu @ %lu -> %lu\n", vpage_id, tag, ppage_id);

            return true;
        }

        // With lock
        bool delete_mapping(vpage_id_type vpage_id, packed_cache_line *hint = nullptr)
        {
            uint64_t tag = vpage_id / packed_cache_line::NUM_PACK_PAGES;
            uint64_t offset = vpage_id % packed_cache_line::NUM_PACK_PAGES;

            auto cacheline = hint ? hint : find_cacheline(tag);

            assert(cacheline && cacheline->tag == tag);

            assert(cacheline->headers[offset].exist == true);

            auto is_lock = cacheline->headers[offset].lock();
            if (!is_lock)
                return false;

            assert(cacheline->headers[offset].exist == true);
            assert(cacheline->headers[offset].busy == true);
            assert(cacheline->headers[offset].ref_count == 0);

            // printf("Delete Mapping %lu @ %lu -> %u\n", vpage_id, tag, cacheline->ppage_ids[offset]);

            cacheline->headers[offset].exist = false;
            cacheline->headers[offset].dirty = false;
            cacheline->ppage_ids[offset] = packed_cache_line::EMPTY_PPAGE_ID;

            return true;
        }

        bool release_mapping_lock(vpage_id_type vpage_id, packed_cache_line *hint = nullptr)
        {
            uint64_t tag = vpage_id / packed_cache_line::NUM_PACK_PAGES;
            uint64_t offset = vpage_id % packed_cache_line::NUM_PACK_PAGES;

            auto cacheline = hint ? hint : find_cacheline(tag);

            assert(cacheline && cacheline->tag == tag);

            auto is_unlock = cacheline->headers[offset].unlock();
            if (!is_unlock)
                return false;

            if (!cacheline->headers[offset].exist)
                cacheline->num_using--;

            if (cacheline->num_using == 0)
                delete_cacheline(cacheline);

            return true;
        }

        unpacked_pte get_pte(vpage_id_type vpage_id, packed_cache_line *hint = nullptr) const
        {
            uint64_t tag = vpage_id / packed_cache_line::NUM_PACK_PAGES;
            uint64_t offset = vpage_id % packed_cache_line::NUM_PACK_PAGES;

            auto cacheline = hint ? hint : find_cacheline(tag);
            assert(find_cacheline(tag) == cacheline);
            if (!cacheline || cacheline->tag != tag)
            {
                return {false, false, false, 0, packed_cache_line::EMPTY_PPAGE_ID};
            }

            return cacheline->to_unpacked(offset);
        }

        packed_cache_line *find_or_create_hint(vpage_id_type vpage_id)
        {
            uint64_t tag = vpage_id / packed_cache_line::NUM_PACK_PAGES;
            uint64_t offset = vpage_id % packed_cache_line::NUM_PACK_PAGES;

            auto [exist, cacheline] = find_cacheline_hint(tag);

            if (!exist)
                cacheline = create_cacheline(tag, cacheline);

            return cacheline;
        }

        packed_cache_line *find_hint(vpage_id_type vpage_id)
        {
            uint64_t tag = vpage_id / packed_cache_line::NUM_PACK_PAGES;
            uint64_t offset = vpage_id % packed_cache_line::NUM_PACK_PAGES;

            return find_cacheline(tag);
        }

        void prefetch(vpage_id_type vpage_id) const
        {
            uint64_t tag = vpage_id / packed_cache_line::NUM_PACK_PAGES;
            uint64_t offset = vpage_id % packed_cache_line::NUM_PACK_PAGES;

            _mm_prefetch(&store[get_bucket(tag)], _MM_HINT_T1);
        }

    private:
        constexpr static uint64_t EMPTY_TAG = (1lu << 58) - 1;
        const vpage_id_type max_vpage_id;
        const ppage_id_type max_ppage_id;
        const size_t bucket_size;
        packed_cache_line *store;
        std::vector<packed_cache_line *> pool;

        size_t get_bucket(uint64_t tag) const { return tag % bucket_size; }

        packed_cache_line *find_cacheline(const uint64_t tag, bool should_exist = false) const
        {
            constexpr size_t MAX_NUM_RETRY = 1 << 30;
            for (size_t num_retry = 0; num_retry < MAX_NUM_RETRY; num_retry++)
            {
                packed_cache_line *cacheline = &store[get_bucket(tag)];
                while (cacheline != nullptr)
                {
                    if (cacheline->tag == tag)
                        return cacheline;
                    cacheline = cacheline->next;
                }
                std::atomic_thread_fence(std::memory_order_acquire);
                if (!should_exist)
                    break;
            }

            if (should_exist)
                fprintf(stderr, "cacheline should be found");

            return nullptr;
        }

        // nullptr means creating at &store[get_bucket(tag)]
        std::tuple<bool, packed_cache_line *> find_cacheline_hint(const uint64_t tag) const
        {
            packed_cache_line *cacheline = &store[get_bucket(tag)];
            packed_cache_line *head = cacheline;
            while (cacheline != nullptr)
            {
                if (cacheline->tag == tag)
                    return {true, cacheline};
                if (cacheline->next == nullptr)
                    return {false, head->tag == EMPTY_TAG ? nullptr : cacheline};
                cacheline = cacheline->next;
            }
            return {false, nullptr};
        }

        packed_cache_line *find_pre_cacheline(const uint64_t tag, const packed_cache_line *end) const
        {
            packed_cache_line *cacheline = &store[get_bucket(tag)];
            while (cacheline != nullptr)
            {
                if (cacheline->next == end)
                    return cacheline;
                cacheline = cacheline->next;
            }
            return nullptr;
        }

        packed_cache_line *create_cacheline(const uint64_t tag, packed_cache_line *pre_cacheline = nullptr)
        {
            assert(find_cacheline_hint(tag) == std::make_tuple(false, pre_cacheline));
            assert(pre_cacheline == nullptr || pre_cacheline->next == nullptr);

            packed_cache_line *cacheline = &store[get_bucket(tag)];
            if (pre_cacheline)
            {
                assert(pool.size() > 0);
                cacheline = pool.back();
                pool.pop_back();
            }

            assert(empty_cacheline(cacheline) == true);
            for (size_t i = 0; i < packed_cache_line::NUM_PACK_PAGES; i++)
            {
                assert(cacheline->headers[i].as_packed() == packed_cache_line::EMPTY_HEADER.as_packed());
                assert(cacheline->ppage_ids[i] == packed_cache_line::EMPTY_PPAGE_ID);
            }

            cacheline->tag = tag;

            if (pre_cacheline)
                pre_cacheline->next = cacheline;

            // printf("Create Cacheline %lu, left %lu\n", tag, pool.size());

            return cacheline;
        }

        void delete_cacheline(packed_cache_line *cacheline = nullptr)
        {
            assert(find_cacheline_hint(cacheline->tag) == std::make_tuple(true, cacheline));
            auto pre_cacheline = find_pre_cacheline(cacheline->tag, cacheline);
            assert(pre_cacheline == nullptr || &store[get_bucket(cacheline->tag)] == cacheline ||
                   pre_cacheline->next == cacheline);

            assert(empty_cacheline(cacheline) == true);
            for (size_t i = 0; i < packed_cache_line::NUM_PACK_PAGES; i++)
            {
                assert(cacheline->headers[i].as_packed() == packed_cache_line::EMPTY_HEADER.as_packed());
                assert(cacheline->ppage_ids[i] == packed_cache_line::EMPTY_PPAGE_ID);
            }

            if (pre_cacheline)
            {
                pre_cacheline->next = cacheline->next;
                cacheline->next = nullptr;
                std::atomic_thread_fence(std::memory_order_release);
                pool.emplace_back(cacheline);
            }

            // printf("Delete Cacheline %lu, left %lu\n", cacheline->tag, pool.size());

            cacheline->tag = EMPTY_TAG;
        }

        bool empty_cacheline(packed_cache_line *cacheline) const
        {
            bool has_pte = false;
            for (size_t i = 0; i < packed_cache_line::NUM_PACK_PAGES; i++)
            {
                has_pte |= cacheline->headers[i].as_packed() != packed_cache_line::EMPTY_HEADER.as_packed() ||
                           cacheline->ppage_ids[i] != packed_cache_line::EMPTY_PPAGE_ID;
            }
            return !has_pte;
        }
    };

} // namespace scache
