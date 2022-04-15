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

#include "partition_client.hpp"
#include "partition_server.hpp"
#include "partition_type.hpp"
#include "partitioner.hpp"
#include <boost/fiber/all.hpp>
#include <boost/fiber/operations.hpp>
#include <chrono>
#include <numa.h>
#include <omp.h>
#include <optional>
#include <random>
#include <vector>

const size_t num_servers_per_numa = 2;
const size_t num_requests = 5000000;
const size_t range = 1lu << 48;
int main()
{
    auto num_numas = numa_num_configured_nodes();
    std::vector<size_t> num_servers(numa_max_node() + 1, 0);
    std::vector<size_t> server_cpus;
    for (size_t i = 0; i < std::thread::hardware_concurrency(); i++)
    {
        auto numa_id = numa_node_of_cpu(i);
        if (num_servers[numa_id] < num_servers_per_numa)
        {
            server_cpus.emplace_back(i);
            num_servers[numa_id]++;
        }
    }
    scache::RoundRobinPartitioner partioner(num_numas * num_servers_per_numa, 0);

    struct Context
    {
        Context(size_t _data) : data(_data) {}
        size_t data;
    };

    auto create_context = [](size_t sid) FORCE_INLINE { return Context(123); };

    struct RequestContext
    {
        int status;
    };

    auto preprocess_func = [](auto &context, const scache::request_type &req) FORCE_INLINE {};

    auto first_equal_func = [](auto &context, const scache::request_type &req, scache::response_type &resp) FORCE_INLINE
    {
        // resp.pointer = (void*)(uintptr_t)req.page_id;
        // return std::optional<RequestContext>(std::nullopt);
        resp.pointer = nullptr;
        return std::make_optional(RequestContext{0});
    };

    auto equal_func = [](auto &context, RequestContext &req_context, const scache::request_type &req) FORCE_INLINE
    {
        if (++req_context.status < 5)
            return false;

        req.resp->pointer = (void *)(uintptr_t)req.page_id;
        return true;
    };

    auto destroy_context = [](auto &context) FORCE_INLINE {};

    scache::PartitionServer server(server_cpus, omp_get_max_threads());
    server.run(create_context, preprocess_func, first_equal_func, equal_func, destroy_context);

    double sum_throughput = 0;
    #pragma omp parallel
    {
        scache::PartitionClient client(server);
        #pragma omp barrier

        auto my_cpu = sched_getcpu();
        bool is_server = false;
        for (auto cpu : server_cpus)
        {
            if (cpu == my_cpu)
                is_server = true;
        }
        if (!is_server)
        {
            std::mt19937_64 rand(omp_get_thread_num());

            auto start = std::chrono::high_resolution_clock::now();

            size_t ref = 0, sum = 0;

            const size_t num_fibers = scache::MAX_FIBERS_PER_THREAD;
            std::vector<boost::fibers::fiber> fibers;
            for (size_t i = 0; i < num_fibers; i++)
            {
                fibers.emplace_back(boost::fibers::launch::dispatch,
                                    [&, fid = i]()
                                    {
                                        for (size_t i = fid; i < num_requests; i += num_fibers)
                                        {
                                            auto key = rand() % range + 1;
                                            ref += key;
                                            scache::cacheline_aligned_type<scache::response_type> response;
                                            response().pointer = nullptr;
                                            scache::request_type request;
                                            request.page_id = key;
                                            request.resp = &response();
                                            auto [sid, _] = partioner(key);
                                            auto epoch = client.request(sid, request, &response());
                                            while (!response().pointer)
                                            {
                                                boost::this_fiber::yield();
                                                client.poll_message(sid, epoch);
                                            }
                                            sum += (size_t)response().pointer;
                                        }
                                    });
            }
            for (auto &f : fibers)
                f.join();
            client.wait();

            auto end = std::chrono::high_resolution_clock::now();

            #pragma omp critical
            {
                double throughput = 1e9 * num_requests / (end - start).count();
                printf("\t[%d] : %lf ops/s, %s : %lu, %lu\n", my_cpu, throughput, ref == sum ? "Success" : "Failed",
                       ref, sum);
                sum_throughput += throughput;
            }
        }
    }

    printf("Request : %lf ops/s\n", sum_throughput);
}
