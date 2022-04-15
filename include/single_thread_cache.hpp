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
#include <boost/fiber/all.hpp>
#include <cstdint>
#include <functional>
#include <immintrin.h>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace scache
{
    template <bool> struct template_page_table_state_type;

    template <> struct template_page_table_state_type<false>
    {
        ppage_id_type ppage_id;
        bool exist;
        bool busy;
        bool dirty;
        uint32_t ref_count;
        bool operator==(const template_page_table_state_type &p) const
        {
            return ppage_id == p.ppage_id && exist == p.exist;
        }
        bool operator!=(const template_page_table_state_type &p) const { return !(*this == p); }
    };
    static_assert(sizeof(template_page_table_state_type<false>) == sizeof(ppage_id_type) * 2);

    template <> struct template_page_table_state_type<true>
    {
        ppage_id_type ppage_id : sizeof(ppage_id_type) * 8 - CACHE_PAGE_BITS;
        bool exist : 1;
        bool busy : 1;
        bool dirty : 1;
        uint8_t ref_count : 8;
        // ppage_id_type padding : CACHE_PAGE_BITS-11;
        bool operator==(const template_page_table_state_type &p) const
        {
            return *(ppage_id_type *)this == *(ppage_id_type *)&p;
        }
        bool operator!=(const template_page_table_state_type &p) const
        {
            return *(ppage_id_type *)this != *(ppage_id_type *)&p;
        }
    };
    static_assert(sizeof(template_page_table_state_type<true>) == sizeof(ppage_id_type));

    template <template <typename> typename PageTableType,
              typename ReplacementType,
              typename ExternalContextType,
              typename ExternalStateType,
              typename EvictFuncType,
              typename LoadFuncType,
              bool CompactState = false>
    class SingleThreadCache
    {
    public:
        using page_table_state_type = template_page_table_state_type<CompactState>;
        using page_table_type = PageTableType<page_table_state_type>;
        using replacement_type = ReplacementType;
        using external_context_type = ExternalContextType;
        using external_state_type = ExternalStateType;
        using evict_func_type = EvictFuncType;
        using load_func_type = LoadFuncType;

        struct internal_state_type
        {
            vpage_id_type vpage_id;
            constexpr static vpage_id_type EMPTY_VPAGE_ID = std::numeric_limits<vpage_id_type>::max();
        };
        struct state_type
        {
            internal_state_type internal;
            external_state_type external;
        };

        struct context_type
        {
            enum class Type
            {
                Pin,
                UnPin
            } type;
            enum class Phase
            {
                Begin,
                Initing,
                Evicting,
                Loading,
                End
            } phase;
            bool processing;
            bool dirty;
            bool is_write;
            bool is_unpin;
            vpage_id_type vpage_id;
            vpage_id_type pre_vpage_id;
            ppage_id_type ppage_id;
            ppage_id_type prefetch_ppage_id;
            std::optional<page_table_state_type> page_table_state;
            external_state_type *external_state;
            std::optional<external_state_type> pre_external_state;
            external_context_type external_context;
            constexpr static ppage_id_type EMPTY_PPAGE_ID = std::numeric_limits<ppage_id_type>::max();
        };

        SingleThreadCache(const vpage_id_type &_max_vpage_id,
                          const ppage_id_type &_max_ppage_id,
                          EvictFuncType _evict_func,
                          LoadFuncType _load_func,
                          external_context_type _default_external_context = external_context_type())
            : max_vpage_id(_max_vpage_id),
              max_ppage_id(_max_ppage_id),
              page_table(max_vpage_id, max_ppage_id),
              pinned_size(0),
              cur_id(0),
              recycle_pool(),
              replacement(max_ppage_id),
              evict_func(_evict_func),
              load_func(_load_func),
              default_external_context(_default_external_context)
        {
            states = (state_type *)mmap_alloc(max_ppage_id * sizeof(state_type), CACHELINE_SIZE);
            for (size_t i = 0; i < max_ppage_id; i++)
            {
                init_state(i);
            }
        }

        SingleThreadCache(const SingleThreadCache &) = delete;
        SingleThreadCache(SingleThreadCache &&) = delete;

        ~SingleThreadCache() { mmap_free(states, max_ppage_id * sizeof(state_type)); }

        ppage_id_type size() const { return cur_id - recycle_pool.size(); }

        bool empty() const { return cur_id + recycle_pool.size() == max_ppage_id; }

        bool full() const { return cur_id >= max_ppage_id && recycle_pool.empty(); }

        bool full_pin() const { return pinned_size >= max_ppage_id; }

        size_t num_pinned() const { return pinned_size; }

        context_type pin(const vpage_id_type &vpage_id)
        {
            context_type context;
            context.type = context_type::Type::Pin;
            context.phase = context_type::Phase::Begin;
            context.vpage_id = vpage_id;
            process(context);
            return context;
        }

        context_type unpin(const vpage_id_type &vpage_id, bool is_write = false)
        {
            context_type context;
            context.type = context_type::Type::UnPin;
            context.phase = context_type::Phase::Begin;
            context.vpage_id = vpage_id;
            context.is_write = is_write;
            process(context);
            return context;
        }

        void process(context_type &context)
        {
            while (true)
            {
                switch (context.phase)
                {
                case context_type::Phase::Begin:
                {
                    context.page_table_state = page_table.get(context.vpage_id);
                    if (context.page_table_state.has_value())
                    {
                        if (!context.page_table_state.value().exist)
                        {
                            context.phase = context_type::Phase::Initing;
                            context.page_table_state.reset();
                            continue;
                        }
                        if (context.page_table_state.value().busy)
                        {
                            return;
                        }
                        context.phase = context_type::Phase::Initing;
                        continue;
                    }
                    else
                    {
                        context.phase = context_type::Phase::Initing;
                        continue;
                    }
                    break;
                }
                case context_type::Phase::Initing:
                {
                    if (context.type == context_type::Type::UnPin)
                    {
                        if (context.page_table_state.has_value())
                        {
                            assert(context.page_table_state.value().exist);
                            auto &state = states[context.page_table_state.value().ppage_id];
                            assert(state.internal.vpage_id == context.vpage_id);
                            if (context.is_write)
                                context.page_table_state.value().dirty = true;
                            auto flag = false;
                            if (context.page_table_state.value().ref_count > 0)
                            {
                                if ((--context.page_table_state.value().ref_count) == 0)
                                {
                                    pinned_size--;
                                    replacement.push(context.page_table_state.value().ppage_id);
                                }
                                flag = true;
                            }
                            page_table.put(context.vpage_id, context.page_table_state.value());
                            context.is_unpin = flag;
                        }
                        else
                        {
                            context.is_unpin = false;
                        }
                        context.phase = context_type::Phase::End;
                        return;
                    }
                    else
                    {
                        if (context.page_table_state.has_value())
                        {
                            assert(context.page_table_state.value().exist);
                            auto &state = states[context.page_table_state.value().ppage_id];
                            assert(state.internal.vpage_id == context.vpage_id);

                            if (context.page_table_state.value().ref_count == 0)
                            {
                                replacement.erase(context.page_table_state.value().ppage_id);
                                pinned_size++;
                            }
                            context.page_table_state.value().ref_count++;

                            page_table.put(context.vpage_id, context.page_table_state.value());

                            context.ppage_id = (ppage_id_type)context.page_table_state.value().ppage_id;
                            context.external_state = &state.external;
                            context.pre_external_state = std::nullopt;

                            context.phase = context_type::Phase::End;

                            return;
                        }
                        else
                        {
                            if (full_pin())
                            {
                                context.ppage_id = context_type::EMPTY_PPAGE_ID;
                                context.phase = context_type::Phase::End;
                                return;
                            }

                            pinned_size++;

                            if (full())
                            {
                                std::tie(context.ppage_id, context.prefetch_ppage_id) = replacement.pop();
                                _mm_prefetch(&states[context.prefetch_ppage_id], _MM_HINT_T1);
                                auto &state = states[context.ppage_id];
                                context.pre_vpage_id = state.internal.vpage_id;
                                context.pre_external_state = state.external;

                                auto pre_page_table_state = page_table.get(context.pre_vpage_id);
                                pre_page_table_state.value().busy = true;
                                page_table.put(context.pre_vpage_id, pre_page_table_state.value());

                                page_table_state_type page_table_state;
                                page_table_state.exist = true;
                                page_table_state.busy = true;
                                page_table_state.ppage_id = context.ppage_id;
                                page_table.put(context.vpage_id, page_table_state);

                                context.dirty = pre_page_table_state.value().dirty;
                                context.phase = context_type::Phase::Evicting;
                                // context.processing = context.dirty;
                                context.external_context = default_external_context;

                                context.processing = !evict_func(context.external_context, context.pre_vpage_id,
                                                                 context.ppage_id, context.dirty, state.external);
                                if (context.processing)
                                    return;
                                else
                                    continue;
                            }
                            else
                            {
                                context.ppage_id = alloc();
                                context.phase = context_type::Phase::Evicting;
                                context.processing = false;
                                continue;
                            }
                        }
                    }
                    break;
                }
                case context_type::Phase::Evicting:
                {
                    if (context.processing)
                    {
                        auto &state = states[context.ppage_id];

                        context.processing = !evict_func(context.external_context, context.pre_vpage_id,
                                                         context.ppage_id, context.dirty, state.external);

                        if (context.processing)
                            return;
                        else
                            continue;
                    }

                    if (context.pre_external_state.has_value())
                    {
                        page_table.del(context.pre_vpage_id);
                    }

                    init_state(context.ppage_id);
                    auto &state = states[context.ppage_id];

                    context.page_table_state = page_table_state_type();
                    context.page_table_state.value().exist = true;
                    context.page_table_state.value().busy = true;
                    context.page_table_state.value().ppage_id = context.ppage_id;
                    page_table.put(context.vpage_id, context.page_table_state.value());
                    state.internal.vpage_id = context.vpage_id;

                    context.phase = context_type::Phase::Loading;
                    // context.processing = true;
                    context.external_state = &state.external;
                    context.external_context = default_external_context;

                    context.processing = !load_func(context.external_context, context.vpage_id, context.ppage_id,
                                                    *context.external_state);

                    if (context.processing)
                        return;
                    else
                        continue;
                }
                case context_type::Phase::Loading:
                {
                    if (context.processing)
                    {
                        context.processing = !load_func(context.external_context, context.vpage_id, context.ppage_id,
                                                        *context.external_state);

                        if (context.processing)
                            return;
                        else
                            continue;
                    }

                    context.page_table_state.value().dirty = false;
                    context.page_table_state.value().ref_count = 1;
                    context.page_table_state.value().busy = false;

                    page_table.put(context.vpage_id, context.page_table_state.value());

                    context.phase = context_type::Phase::End;

                    if (context.pre_external_state.has_value())
                        page_table.prefetch(states[context.prefetch_ppage_id].internal.vpage_id);

                    return;
                }
                }
            }
        }

        void prefetch(const vpage_id_type &vpage_id) const { page_table.prefetch(vpage_id); }

        void flush()
        {
            std::vector<context_type> contextes(max_ppage_id);
            size_t size = 0;
            for (size_t i = 0; i < max_ppage_id; i++)
            {
                auto &context = contextes[size];
                context.ppage_id = i;
                auto &state = states[context.ppage_id];
                if (state.internal.vpage_id == internal_state_type::EMPTY_VPAGE_ID)
                    continue;
                context.pre_vpage_id = state.internal.vpage_id;
                context.pre_external_state = state.external;

                auto pre_page_table_state = page_table.get(context.pre_vpage_id);
                if (pre_page_table_state.value().ref_count)
                {
                    context.dirty = true;
                }
                else
                {
                    replacement.pop();
                    context.dirty = pre_page_table_state.value().dirty;
                }

                context.external_context = default_external_context;

                context.processing = !evict_func(context.external_context, context.pre_vpage_id, context.ppage_id,
                                                 context.dirty, state.external);
                size++;
            }

            size_t start, min_processing = 0;
            do
            {
                start = min_processing;
                min_processing = max_ppage_id;
                for (size_t i = start; i < size; i++)
                {
                    auto &context = contextes[i];
                    if (context.processing)
                    {
                        auto &state = states[context.ppage_id];

                        context.processing = !evict_func(context.external_context, context.pre_vpage_id,
                                                         context.ppage_id, context.dirty, state.external);
                        min_processing = i;
                    }
                }

            } while (min_processing != max_ppage_id);

            for (size_t i = 0; i < size; i++)
            {
                auto &context = contextes[i];
                page_table.del(context.pre_vpage_id);
                init_state(context.ppage_id);
                free(context.ppage_id);
            }
        }

    private:
        ppage_id_type alloc()
        {
            assert(!full());

            if (!recycle_pool.empty())
            {
                auto id = recycle_pool.back();
                recycle_pool.pop_back();
                return id;
            }

            if (cur_id < max_ppage_id)
            {
                auto id = cur_id++;
                return id;
            }

            return 0;
        }

        void free(const ppage_id_type &ppage_id) { recycle_pool.push_back(ppage_id); }

        void init_state(const ppage_id_type &ppage_id)
        {
            states[ppage_id].internal = {internal_state_type::EMPTY_VPAGE_ID};
            states[ppage_id].external = external_state_type();
        }

        const ppage_id_type max_vpage_id;
        const ppage_id_type max_ppage_id;
        page_table_type page_table;
        ppage_id_type pinned_size;
        ppage_id_type cur_id;
        std::vector<ppage_id_type> recycle_pool;
        state_type *states;
        replacement_type replacement;
        evict_func_type evict_func;
        load_func_type load_func;
        external_context_type default_external_context;
    };
} // namespace scache
