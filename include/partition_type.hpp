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
#include <boost/fiber/context.hpp>
#include <boost/fiber/operations.hpp>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>

namespace scache
{
    constexpr size_t MAX_THREADS = 2048;
    constexpr size_t MAX_NUMANODES = 8;
    constexpr bool PURE_THREADING = true;
    constexpr size_t MAX_FIBERS_PER_THREAD = PURE_THREADING ? 1 : 16;
    constexpr size_t MAX_CACHES = 1;
    constexpr size_t MESSAGE_SIZE = PURE_THREADING ? CACHELINE_SIZE * 1 : CACHELINE_SIZE * 2;
    constexpr bool USING_SINGLE_CACHELINE = false;
    constexpr bool USING_FIBER_ASYNC_RESPONSE = true; // PURE_THREADING ? false : true;
    constexpr size_t HYBRID_SPIN_THRESHOLD = PURE_THREADING ? (1lu << 0) : (1lu << 30);
    constexpr size_t FIBER_CHANNEL_DEPTH = MAX_THREADS * MESSAGE_SIZE / sizeof(vpage_id_type);
    constexpr bool ENABLE_SERVER_PRE_PROCESSING = PURE_THREADING ? false : true;
    constexpr bool ENABLE_CLIENT_TIMER_FIBER = false;
    constexpr bool ENABLE_DIRECT_PIN = true;
    constexpr bool ENABLE_DIRECT_UNPIN = true;
    constexpr bool ENABLE_IOPS_STATS = true;

    struct header_type
    {
        bool toggle;
        uint8_t num_comm;
    };

    struct response_type
    {
        void *pointer;
    };

    struct request_type
    {
        enum class Type : uint8_t
        {
            None = 0,
            Pin = 1,
            Unpin = 2,
            DirtyUnpin = 3,
            NotifyDirectPin = 4,
            NotifyDirectUnpin = 5,
        } type;
        vpage_id_type page_id : (sizeof(vpage_id_type) * 8 - CACHE_PAGE_BITS);
        response_type *resp;
    };
    static_assert(sizeof(request_type) == 2 * sizeof(vpage_id_type));
    static_assert(sizeof(request_type) == 2 * sizeof(response_type));

    struct alignas(MESSAGE_SIZE) message_type
    {
        static constexpr uint8_t MAX_COMMS = MESSAGE_SIZE / sizeof(request_type) - 1;

        header_type header;
        union
        {
            request_type reqs[MAX_COMMS];
            response_type resps[MAX_COMMS];
        };
        message_type &operator=(const message_type &c)
        {
            uint64_t *l = (uint64_t *)this;
            uint64_t *r = (uint64_t *)&c;
            constexpr size_t num_uint64 = sizeof(request_type) / sizeof(uint64_t);
            #pragma unroll
            for (int8_t i = 1; i < MAX_COMMS * num_uint64 + 1; i++)
            {
                l[i] = r[i];
            }
            save_fence();
            l[0] = r[0];
            return *this;
        }
    };
    static_assert(sizeof(message_type) == MESSAGE_SIZE);

    template <typename T> class alignas(CACHELINE_SIZE) cacheline_aligned_type
    {
    public:
        T &operator()() { return data; }

    private:
        T data;
    };

    void inline nano_spin()
    {
        if constexpr (PURE_THREADING)
        {
            compiler_fence();
        }
        else
        {
            if (likely(boost::fibers::context::active() != nullptr))
                boost::this_fiber::yield();
            else
                compiler_fence();
        }
    }

    void inline hybrid_spin(size_t &loops)
    {
        if (loops++ < HYBRID_SPIN_THRESHOLD)
        {
            nano_spin();
        }
        else
        {
            std::this_thread::yield();
            loops = 0;
        }
    }

} // namespace scache
