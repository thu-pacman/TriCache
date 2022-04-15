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
#include <boost/pool/pool_alloc.hpp>
#include <list>

namespace scache
{
    class LRU
    {
    public:
        using list_type = std::list<ppage_id_type, boost::fast_pool_allocator<ppage_id_type>>;
        using state_type = typename list_type::iterator;

        LRU(const ppage_id_type &_max_ppage_id) : max_ppage_id(_max_ppage_id)
        {
            states = (state_type *)mmap_alloc(max_ppage_id * sizeof(state_type), CACHELINE_SIZE);
            for (size_t i = 0; i < max_ppage_id; i++)
                states[i] = list.end();
        }

        LRU(const LRU &) = delete;
        LRU(LRU &&) = delete;

        ~LRU() { mmap_free(states, max_ppage_id * sizeof(state_type)); }

        void push(const ppage_id_type &ppage_id)
        {
            if (states[ppage_id] == list.end())
            {
                list.emplace_front(ppage_id);
                states[ppage_id] = list.begin();
            }
            else
            {
                access(ppage_id);
            }
        }

        std::pair<ppage_id_type, ppage_id_type> pop()
        {
            assert(list.size() > 0);
            auto ppage_id = list.back();
            states[ppage_id] = list.end();
            list.pop_back();
            auto next_id = 0;
            if (!list.empty())
                next_id = list.back();
            return {ppage_id, next_id};
        }

        void access(const ppage_id_type &ppage_id)
        {
            assert(states[ppage_id] != list.end());
            list.splice(list.begin(), list, states[ppage_id]);
        }

        void erase(const ppage_id_type &ppage_id)
        {
            if (states[ppage_id] != list.end())
            {
                list.erase(states[ppage_id]);
                states[ppage_id] = list.end();
            }
        }

        ppage_id_type size() const { return list.size(); }

    private:
        const ppage_id_type max_ppage_id;
        list_type list;
        state_type *states;
    };

    class Clock
    {
    public:
        using state_type = uint8_t;

        Clock(const ppage_id_type &_max_ppage_id) : max_ppage_id(_max_ppage_id), hand(0), count(0)
        {
            states = (state_type *)mmap_alloc(max_ppage_id * sizeof(state_type), CACHELINE_SIZE);
            for (size_t i = 0; i < max_ppage_id; i++)
                states[i] = 0;
        }

        Clock(const Clock &) = delete;
        Clock(Clock &&) = delete;

        ~Clock() { mmap_free(states, max_ppage_id * sizeof(state_type)); }

        void push(const ppage_id_type &ppage_id)
        {
            if (states[ppage_id] == 0)
            {
                count++;
                states[ppage_id] = 2;
            }
            else
            {
                access(ppage_id);
            }
        }

        std::pair<ppage_id_type, ppage_id_type> pop()
        {
            assert(count > 0);
            while (states[hand] != 1)
            {
                if (states[hand] == 2)
                    states[hand] = 1;
                hand = (hand + 1) % max_ppage_id;
            }
            states[hand] = 0;
            count--;
            auto del_hand = hand;
            size_t loops = 1024;
            while (states[hand] != 1 && --loops)
            {
                hand = (hand + 1) % max_ppage_id;
            }
            return {del_hand, hand};
        }

        void access(const ppage_id_type &ppage_id)
        {
            assert(states[ppage_id] != 0);
            states[ppage_id] = 2;
        }

        void erase(const ppage_id_type &ppage_id)
        {
            if (states[ppage_id] != 0)
            {
                states[ppage_id] = 0;
                count--;
            }
        }

        ppage_id_type size() const { return count; }

    private:
        const ppage_id_type max_ppage_id;
        ppage_id_type hand;
        ppage_id_type count;
        state_type *states;
    };

} // namespace scache
