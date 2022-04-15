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
#include <hopscotch-map/include/tsl/hopscotch_map.h>
#include <immintrin.h>
#include <optional>

namespace scache
{
    template <typename ValueType> class HashPageTable
    {
    public:
        using value_type = ValueType;
        HashPageTable(const vpage_id_type &_max_vpage_id,
                      const ppage_id_type &_max_ppage_id,
                      const value_type &_empty_value = value_type())
            : max_vpage_id(_max_vpage_id), max_ppage_id(_max_ppage_id), store(max_ppage_id)
        {
        }

        bool put(const vpage_id_type &vpage_id, const value_type &value)
        {
            auto iter = store.find(vpage_id);
            if (iter != store.end())
            {
                iter.value() = value;
                return false;
            }
            store.emplace_hint(iter, vpage_id, value);
            return true;
        }

        std::optional<value_type> get(const vpage_id_type &vpage_id) const
        {
            auto iter = store.find(vpage_id);
            if (iter == store.end())
                return {};
            return iter->second;
        }

        bool del(const vpage_id_type &vpage_id) { return store.erase(vpage_id); }

        void prefetch(const vpage_id_type &vpage_id) const {}

    private:
        const vpage_id_type max_vpage_id;
        const ppage_id_type max_ppage_id;
        tsl::hopscotch_map<vpage_id_type, value_type> store;
    };

    template <typename ValueType> class DirectPageTable
    {
    public:
        using value_type = ValueType;
        DirectPageTable(const vpage_id_type &_max_vpage_id,
                        const ppage_id_type &_max_ppage_id,
                        const value_type &_empty_value = value_type())
            : max_vpage_id(_max_vpage_id), max_ppage_id(_max_ppage_id), empty_value(_empty_value)
        {
            store = (value_type *)mmap_alloc(max_vpage_id * sizeof(value_type));
            for (size_t i = 0; i < max_vpage_id; i++)
            {
                store[i] = empty_value;
            }
        }

        DirectPageTable(const DirectPageTable &) = delete;
        DirectPageTable(DirectPageTable &&) = delete;

        ~DirectPageTable() { mmap_free(store, max_vpage_id * sizeof(value_type)); }

        bool put(const vpage_id_type &vpage_id, const value_type &value)
        {
            bool is_insert = store[vpage_id] == empty_value;
            store[vpage_id] = value;
            return is_insert;
        }

        std::optional<value_type> get(const vpage_id_type &vpage_id) const
        {
            if (store[vpage_id] == empty_value)
                return {};
            return store[vpage_id];
        }

        bool del(const vpage_id_type &vpage_id)
        {
            bool is_del = store[vpage_id] != empty_value;
            store[vpage_id] = empty_value;
            return is_del;
        }

        void prefetch(const vpage_id_type &vpage_id) const
        {
            // if(vpage_id < max_vpage_id)
            _mm_prefetch(&store[vpage_id], _MM_HINT_T1);
        }

    private:
        const vpage_id_type max_vpage_id;
        const ppage_id_type max_ppage_id;
        const value_type empty_value;
        value_type *store;
    };
} // namespace scache
