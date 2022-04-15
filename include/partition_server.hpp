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
#include "type.hpp"
#include "util.hpp"
#include <boost/circular_buffer.hpp>
#include <boost/fiber/all.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <numa.h>
#include <optional>
#include <pthread.h>
#include <queue>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>
#include <xmmintrin.h>

namespace scache
{
    class PartitionServer
    {
        friend class PartitionClient;

    public:
        PartitionServer(std::vector<size_t> _cpus, size_t _num_clients)
            : cpus(_cpus),
              nodes(),
              requests_pool(),
              requests(),
              responses_pool(),
              responses(),
              numa_threads(),
              threads(),
              num_ready_threads(0),
              num_clients(_num_clients),
              client_id_pool(),
              client_toggles(),
              mutex(),
              is_stop(false),
              is_run(false)
        {
            if (cpus.size() > MAX_THREADS)
                throw std::runtime_error("Number of server threads > MAX_THREADS");

            if (num_clients > MAX_THREADS)
                throw std::runtime_error("Number of client threads > MAX_THREADS");

            if (numa_max_node() >= MAX_NUMANODES)
                throw std::runtime_error("Number of numa nodes > MAX_NUMANODES");

            for (size_t i = 0; i < cpus.size(); i++)
            {
                auto node = numa_node_of_cpu(cpus[i]);
                if (node < 0)
                    throw std::runtime_error("Invalid CPU id");

                numa_threads[node].emplace_back(i);
                nodes.emplace_back(node);
                // printf("%d %d\n", cpus[i], node);
            }

            client_toggles = (bool *)mmap_alloc(num_clients * MAX_THREADS);
        };

        ~PartitionServer()
        {
            is_stop = true;
            for (auto &t : threads)
                t.join();

            for (size_t i = 0; i < num_clients; i++)
            {
                mmap_free(client_toggles, num_clients * MAX_THREADS);
            }

            for (size_t i = 0; i < MAX_NUMANODES; i++)
            {
                if (requests_pool[i] != nullptr)
                    mmap_free(requests_pool[i], numa_threads[i].size() * MAX_THREADS * sizeof(message_type));
                if (responses_pool[i] != nullptr)
                    mmap_free(responses_pool[i], numa_threads[i].size() * MAX_THREADS * sizeof(message_type));
            }
        }

        template <typename CreateContextFuncType,
                  typename PreProcessFuncType,
                  typename FirstProcessFuncType,
                  typename ProcessFuncType,
                  typename DestroyContextFuncType>
        void run(CreateContextFuncType create_context_func,
                 PreProcessFuncType pre_process_func,
                 FirstProcessFuncType first_process_func,
                 ProcessFuncType process_func,
                 DestroyContextFuncType destroy_context_func)
        {
            if (is_run)
                return;

            for (size_t i = 0; i < cpus.size(); i++)
            {
                threads.emplace_back(
                    [this, sid = i, create_context_func = create_context_func, pre_process_func = pre_process_func,
                     first_process_func = first_process_func, process_func = process_func,
                     destroy_context_func = destroy_context_func]()
                    {
                        this->server_loop(sid, create_context_func, pre_process_func, first_process_func, process_func,
                                          destroy_context_func);
                    });
            }

            while (num_ready_threads < cpus.size())
                spin_pause();

            int num_cpus = std::thread::hardware_concurrency();
            bool half_hyperthreading = numa_max_node() > 0;
            for (int cpu = 0; cpu < num_cpus / 2; cpu++)
            {
                if (numa_node_of_cpu(cpu) != numa_node_of_cpu(cpu + num_cpus / 2))
                {
                    half_hyperthreading = false;
                }
            }

            std::vector<int> cpu_mapping;

            if (half_hyperthreading)
            {
                for (int cpu = 0; cpu < num_cpus / 2; cpu++)
                {
                    cpu_mapping.emplace_back(cpu);
                    cpu_mapping.emplace_back(cpu + num_cpus / 2);
                }
            }
            else
            {
                for (int cpu = 0; cpu < num_cpus; cpu++)
                {
                    cpu_mapping.emplace_back(cpu);
                }
            }
            for (int cpu = 0; cpu < num_cpus; cpu++)
            {
                client_id_pool.emplace_back();
            }

            size_t id = 0;
            for (int cpu = 0; cpu < num_cpus; cpu++)
            {
                for (size_t i = 0; i < num_clients / num_cpus + (cpu < num_clients % num_cpus); i++)
                {
                    client_id_pool[cpu_mapping[cpu]].push(id);
                    id++;
                }
            }

            is_run = true;
        }

    private:
        template <typename CreateContextFuncType,
                  typename PreProcessFuncType,
                  typename FirstProcessFuncType,
                  typename ProcessFuncType,
                  typename DestroyContextFuncType>
        void server_loop(size_t sid,
                         CreateContextFuncType create_context_func,
                         PreProcessFuncType pre_process_func,
                         FirstProcessFuncType first_process_func,
                         ProcessFuncType process_func,
                         DestroyContextFuncType destroy_context_func)
        {
            {
                struct bitmask *mask = numa_bitmask_alloc(numa_num_possible_nodes());
                numa_bitmask_clearall(mask);
                numa_bitmask_setbit(mask, numa_node_of_cpu(cpus[sid]));
                numa_bind(mask);
                numa_bitmask_free(mask);
                while (numa_node_of_cpu(sched_getcpu()) != numa_node_of_cpu(cpus[sid]))
                {
                    std::this_thread::yield();
                }
            }

            auto node = nodes[sid];
            if (numa_threads[node].front() == sid)
            {
                requests_pool[node] =
                    (message_type *)mmap_alloc(numa_threads[node].size() * MAX_THREADS * sizeof(message_type));
                responses_pool[node] =
                    (message_type *)mmap_alloc(numa_threads[node].size() * MAX_THREADS * sizeof(message_type));
            }
            else
            {
                while (requests_pool[node] == nullptr || responses_pool[node] == nullptr)
                {
                    spin_pause();
                }
            }

            for (size_t i = 0; i < numa_threads[node].size(); i++)
            {
                if (numa_threads[node][i] == sid)
                {
                    requests[sid] = &requests_pool[node][i * MAX_THREADS];
                    responses[sid] = &responses_pool[node][i * MAX_THREADS];
                }
            }
            memset(requests[sid], 0, MAX_THREADS * sizeof(message_type));
            memset(responses[sid], 0, MAX_THREADS * sizeof(message_type));

            {
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpus[sid], &cpuset);
                auto ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

                if (ret != 0)
                    throw std::runtime_error("Set affinity error");

                while (sched_getcpu() != cpus[sid])
                {
                    std::this_thread::yield();
                }
            }

            bool toggles[MAX_THREADS];
            memset(toggles, 0, sizeof(toggles));

            message_type resp_messages[MAX_THREADS];

            auto &&context = create_context_func(sid);

            printf("Init %03lu at %03lu CPU, %02lu Node\n", sid, cpus[sid], node);
            num_ready_threads++;

            size_t count = 0;

            int num_async_processing[MAX_THREADS];
            memset(num_async_processing, 0, sizeof(num_async_processing));
            bool need_async_processing[MAX_THREADS];
            memset(need_async_processing, 0, sizeof(need_async_processing));
            bool has_message[MAX_THREADS];
            memset(has_message, 0, sizeof(has_message));

            auto send_response = [&](size_t cid) FORCE_INLINE
            {
                auto &resp_message = resp_messages[cid];
                if constexpr (USING_SINGLE_CACHELINE)
                {
                    resp_message.header.toggle = toggles[cid];
                    requests[sid][cid] = resp_message;
                }
                else
                {
                    toggles[cid] ^= 1;
                    resp_message.header.toggle = toggles[cid];
                    responses[sid][cid] = resp_message;
                }
            };

            using context_type = std::invoke_result_t<CreateContextFuncType, size_t>;
            using async_context_type = typename std::invoke_result_t<FirstProcessFuncType, context_type &, request_type,
                                                                     response_type &>::value_type;
            using async_request_fiber_type = std::tuple<request_type, async_context_type>;
            using async_request_type = std::tuple<size_t, request_type, async_context_type>;

            std::vector<std::optional<async_request_type>> async_requests;

            size_t num_async_fiber_processing = 0;
            boost::fibers::buffered_channel<async_request_fiber_type> async_channel(FIBER_CHANNEL_DEPTH);

            auto async_fiber = boost::fibers::fiber(
                boost::fibers::launch::dispatch,
                [&]()
                {
                    boost::circular_buffer<std::optional<async_request_fiber_type>> async_requests(FIBER_CHANNEL_DEPTH);
                    async_request_fiber_type async_request;
                    while (boost::fibers::channel_op_status::success == async_channel.pop(async_request))
                    {
                        async_requests.push_back(async_request);
                        while (!async_requests.empty())
                        {
                            while (!async_requests.full() &&
                                   boost::fibers::channel_op_status::success == async_channel.try_pop(async_request))
                                async_requests.push_back(async_request);

                            for (auto &req : async_requests)
                            {
                                if (!req.has_value())
                                    continue;
                                auto &[request, request_context] = req.value();
                                // while(!process_func(context, request_context, request));
                                if (process_func(context, request_context, request))
                                    req.reset();
                                // boost::this_fiber::yield();
                            }
                            while (!async_requests.empty() && !async_requests.front().has_value())
                            {
                                num_async_fiber_processing--;
                                async_requests.pop_front();
                            }

                            // save_fence();
                            boost::this_fiber::yield();
                        }
                    }
                });

            while (!is_stop)
            {
                int left_async_processing = 0;
                async_requests.clear();

                for (size_t cid = 0; cid < num_clients; cid++)
                {
                    _mm_prefetch(&requests[sid][cid], _MM_HINT_T1);
                }

                // spin_pause();

                if constexpr (ENABLE_SERVER_PRE_PROCESSING)
                {
                    for (size_t cid = 0; cid < num_clients; cid++)
                    {
                        auto &message = requests[sid][cid];
                        if (message.header.toggle != toggles[cid])
                        {
                            load_fence();
                            has_message[cid] = true;
                            for (uint8_t i = 0; i < message.header.num_comm; i++)
                            {
                                pre_process_func(context, message.reqs[i]);
                            }
                        }
                    }
                }

                // spin_pause();

                for (size_t cid = 0; cid < num_clients; cid++)
                {
                    auto &message = requests[sid][cid];
                    auto &resp_message = resp_messages[cid];

                    if constexpr (!ENABLE_SERVER_PRE_PROCESSING)
                    {
                        if (message.header.toggle != toggles[cid])
                        {
                            load_fence();
                            has_message[cid] = true;
                        }
                    }

                    if (has_message[cid])
                    {
                        has_message[cid] = false;
                        count += message.header.num_comm;
                        resp_message.header = message.header;
                        for (uint8_t i = 0; i < message.header.num_comm; i++)
                        {
                            auto ret = first_process_func(context, message.reqs[i], resp_message.resps[i]);
                            if (ret.has_value())
                            {
                                if constexpr (USING_FIBER_ASYNC_RESPONSE)
                                {
                                    num_async_fiber_processing++;
                                    async_channel.push(std::make_tuple(message.reqs[i], ret.value()));
                                }
                                else
                                {
                                    need_async_processing[cid] = true;
                                    num_async_processing[cid]++;
                                    auto request = message.reqs[i];
                                    request.resp = &resp_message.resps[i];
                                    async_requests.emplace_back(std::make_tuple(cid, request, ret.value()));
                                }
                            }
                        }
                        if constexpr (USING_FIBER_ASYNC_RESPONSE)
                        {
                            send_response(cid);
                        }
                        else
                        {
                            if (!need_async_processing[cid])
                                send_response(cid);
                            else
                                left_async_processing++;
                        }
                    }
                }

                if constexpr (USING_FIBER_ASYNC_RESPONSE)
                {
                    if (num_async_fiber_processing)
                        boost::this_fiber::yield();
                }
                else
                {
                    while (left_async_processing)
                    {
                        for (auto &req : async_requests)
                        {
                            if (!req.has_value())
                                continue;
                            auto &[cid, request, request_context] = req.value();
                            if (process_func(context, request_context, request))
                            {
                                if (--num_async_processing[cid] == 0)
                                {
                                    send_response(cid);
                                    left_async_processing--;
                                    need_async_processing[cid] = false;
                                }
                                req.reset();
                            }
                        }
                        // while(!async_requests.empty() && !async_requests.front().has_value())
                        //    async_requests.pop_front();
                    }
                }
            }

            destroy_context_func(context);

            async_channel.close();
            async_fiber.join();

            printf("Partition [%03lu] processes %lu operations\n", sid, count);
        }

        std::vector<size_t> cpus, nodes;

        message_type *requests_pool[MAX_NUMANODES];
        message_type *requests[MAX_THREADS]; // requests[server_id][client_id]
        message_type *responses_pool[MAX_NUMANODES];
        message_type *responses[MAX_THREADS]; // responses[server_id][client_id]
        std::vector<size_t> numa_threads[MAX_NUMANODES];

        std::vector<std::thread> threads;
        std::atomic_size_t num_ready_threads;

        const size_t num_clients;
        std::vector<std::queue<size_t>> client_id_pool;
        bool *client_toggles;
        std::mutex mutex;

        bool is_stop;
        bool is_run;
    };

} // namespace scache
