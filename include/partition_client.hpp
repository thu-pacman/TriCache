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
#include "partition_server.hpp"
#include "partition_type.hpp"
#include "type.hpp"
#include "util.hpp"
#include <boost/fiber/all.hpp>
#include <boost/fiber/operations.hpp>
#include <cassert>
#include <chrono>
#include <mutex>
#include <numa.h>

namespace scache
{
    class PartitionClient
    {
        struct message_status_type
        {
            uint8_t num_requests;
            response_type *responses[message_type::MAX_COMMS];
        };

    public:
        PartitionClient(PartitionServer &_server)
            : server(_server),
              cid(),
              local_message_pool(),
              local_message_statuses(),
              local_message_processing(),
              toggles(),
              epoches(),
              is_stop(false),
              timer_fiber()
        {
            {
                std::lock_guard lock(server.mutex);

                server_side_cpuid = sched_getcpu();
                if (server.client_id_pool[server_side_cpuid].empty())
                {
                    bool is_full = true;
                    std::vector<std::pair<int, int>> dis;
                    for (int i = 0; i < server.client_id_pool.size(); i++)
                    {
                        dis.emplace_back(numa_distance(numa_node_of_cpu(server_side_cpuid), numa_node_of_cpu(i)), i);
                    }
                    std::sort(dis.begin(), dis.end());
                    for (auto p : dis)
                    {
                        if (!server.client_id_pool[p.second].empty())
                        {
                            is_full = false;
                            server_side_cpuid = p.second;
                            break;
                        }
                    }
                    if (is_full)
                        throw std::runtime_error("Server cannot support more clients");
                }

                cid = server.client_id_pool[server_side_cpuid].front();
                server.client_id_pool[server_side_cpuid].pop();
                // printf("cpuid %d server_cpuid %d client_id %lu\n", sched_getcpu(), server_side_cpuid, cid);

                memcpy(toggles, server.client_toggles + cid * MAX_THREADS, sizeof(toggles));

                if constexpr (ENABLE_CLIENT_TIMER_FIBER)
                {
                    timer_fiber = boost::fibers::fiber(
                        [&]()
                        {
                            size_t pre_epoches[MAX_THREADS];
                            while (!is_stop)
                            {
                                for (size_t loops = 65536; loops; loops--)
                                    boost::this_fiber::yield();
                                for (size_t sid = 0; sid < server.cpus.size(); sid++)
                                {
                                    if (pre_epoches[sid] == epoches[sid])
                                        submit_message(sid);
                                    pre_epoches[sid] = epoches[sid];
                                }
                            }
                        });
                }
            }

            local_message_pool = (message_type *)mmap_alloc(server.cpus.size() * sizeof(message_type), CACHELINE_SIZE);
            memset(local_message_pool, 0, server.cpus.size() * sizeof(message_type));

            for (size_t i = 0; i < server.cpus.size(); i++)
                epoches[i] = 1;
        }

        ~PartitionClient()
        {
            is_stop = true;
            if (timer_fiber.joinable())
                timer_fiber.join();
            wait();
            mmap_free(local_message_pool, server.cpus.size() * sizeof(message_type));
            memcpy(server.client_toggles + cid * MAX_THREADS, toggles, sizeof(toggles));
            std::lock_guard lock(server.mutex);
            server.client_id_pool[server_side_cpuid].push(cid);
        }

        size_t request(const size_t &sid, const request_type &req, response_type *resps = nullptr)
        {
            poll_message(sid);

            auto &status = local_message_statuses[sid];
            auto &message = local_message_pool[sid];

            message.reqs[status.num_requests] = req;
            status.responses[status.num_requests] = resps;
            status.num_requests++;

            size_t epoch = epoches[sid];

            if (status.num_requests == message_type::MAX_COMMS)
                submit_message(sid);

            return epoch;
        }

        void progress()
        {
            for (size_t sid = 0; sid < server.cpus.size(); sid++)
            {
                process_message(sid);
                compiler_fence();
            }
        }

        void poll_message(size_t sid, size_t epoch = 0)
        {
            if (epoches[sid] <= epoch)
                submit_message(sid);

            size_t loops = 0;
            while (local_message_processing[sid])
            {
                process_message(sid);
                hybrid_spin(loops);
            }
        }

        void wait()
        {
            for (size_t sid = 0; sid < server.cpus.size(); sid++)
            {
                submit_message(sid);
                poll_message(sid);
            }
        }

    private:
        PartitionServer &server;
        size_t cid;

        message_type *local_message_pool;
        message_status_type local_message_statuses[MAX_THREADS];
        bool local_message_processing[MAX_THREADS];
        bool toggles[MAX_THREADS];
        size_t epoches[MAX_THREADS];
        bool is_stop;
        boost::fibers::fiber timer_fiber;
        int server_side_cpuid;

        void submit_message(size_t sid)
        {
            poll_message(sid);
            if (!local_message_statuses[sid].num_requests)
                return;

            toggles[sid] ^= 1;
            local_message_pool[sid].header.toggle = toggles[sid];
            local_message_pool[sid].header.num_comm = local_message_statuses[sid].num_requests;
            if constexpr (USING_SINGLE_CACHELINE)
            {
                toggles[sid] ^= 1;
            }

            local_message_processing[sid] = true;

            server.requests[sid][cid] = local_message_pool[sid];

            epoches[sid]++;
        }

        bool process_message(size_t sid)
        {
            if (local_message_processing[sid])
            {
                auto &status = local_message_statuses[sid];
                auto &message =
                    [&]() FORCE_INLINE
                {
                    if constexpr (USING_SINGLE_CACHELINE)
                        return std::ref(server.requests[sid][cid]);
                    else
                        return std::ref(server.responses[sid][cid]);
                }()
                        .get();

                if (message.header.toggle == toggles[sid])
                {
                    load_fence();
                    for (uint8_t i = 0; i < message.header.num_comm; i++)
                    {
                        if (status.responses[i] != nullptr && message.resps[i].pointer)
                            *status.responses[i] = message.resps[i];
                    }

                    status = message_status_type();
                    local_message_processing[sid] = false;
                }
                return true;
            }
            return false;
        }
    };
} // namespace scache
