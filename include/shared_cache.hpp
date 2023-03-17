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
#include "access_counter.hpp"
#include "compact_hash_page_table.hpp"
#include "io_backend.hpp"
#include "memory_pool.hpp"
#include "page_table.hpp"
#include "partition_client.hpp"
#include "partition_server.hpp"
#include "partition_type.hpp"
#include "partitioner.hpp"
#include "replacement.hpp"
#include "shared_single_thread_cache.hpp"
#include "single_thread_cache.hpp"
#include "type.hpp"
#include <boost/fiber/operations.hpp>
#include <boost/thread.hpp>
#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

namespace scache
{
    class SharedCache
    {
        friend class PrivateCache;
        template <typename> friend class DirectCache;

        struct EmptyState
        {
        };

        struct IOContext
        {
            bool first = true;
            bool processing = false;
            bool finish = false;
        };

    public:
        SharedCache(size_t _virt_size,
                    size_t _phy_size,
                    std::vector<size_t> _server_cpus,
                    std::vector<std::string> _server_paths,
                    size_t _max_num_clients)
            : virt_size(_virt_size),
              phy_size(_phy_size),
              num_vpages(virt_size / CACHE_PAGE_SIZE),
              num_ppages(phy_size / CACHE_PAGE_SIZE),
              num_partitions(_server_cpus.size()),
              num_ppages_per_partition(num_ppages / num_partitions + (num_ppages % num_partitions != 0)),
              actual_num_ppages_per_thread(num_ppages_per_partition * num_partitions / _max_num_clients),
              server_cpus(_server_cpus),
              server_paths(_server_paths),
              max_num_clients(_max_num_clients),
              partitioner(num_partitions, num_vpages),
              server(server_cpus, max_num_clients),
              clients()
        {
            if (virt_size < phy_size || virt_size % CACHE_PAGE_SIZE != 0 || phy_size % CACHE_PAGE_SIZE != 0 ||
                server_cpus.empty())
                throw std::runtime_error("Parameter Error");
            init_server();
        }

        SharedCache(const SharedCache &) = delete;
        SharedCache(SharedCache &&) = delete;

        std::shared_ptr<PartitionClient> get_client_shared_ptr()
        {
            if (!clients.get())
                clients.reset(new std::shared_ptr<PartitionClient>(std::make_shared<PartitionClient>(server)));
            return *clients.get();
        }

        PartitionClient *get_client()
        {
            if (!clients.get())
                clients.reset(new std::shared_ptr<PartitionClient>(std::make_shared<PartitionClient>(server)));
            return clients.get()->get();
        }

        void del_client() { clients.reset(nullptr); }

        void *pin(vpage_id_type vpage_id, PartitionClient *client = nullptr)
        {
            if (vpage_id >= num_vpages)
                throw std::runtime_error("Virtual Page ID Error");

            counter.count_access();

            auto [sid, block_id] = partitioner(vpage_id);

            if constexpr (ENABLE_DIRECT_PIN)
            {
                auto [success, ppage_id, pre_ref_count] = page_tables[sid]->pin(block_id);
                if (success)
                {
                    if (pre_ref_count == 0)
                    {
                        if (!client)
                            client = get_client();

                        scache::request_type req = {request_type::Type::NotifyDirectPin, vpage_id};
                        auto epoch = client->request(sid, req);
                    }
                    return phy_memory_pools[sid]->from_page_id(ppage_id);
                }
            }

            if (!client)
                client = get_client();

            cacheline_aligned_type<scache::response_type> resp;
            resp() = {nullptr};
            scache::request_type req = {request_type::Type::Pin, vpage_id, &resp()};

            size_t retry_loops = 0;
            while (!resp().pointer)
            {
                auto epoch = client->request(sid, req, &resp());
                size_t loops = 0;
                while (!resp().pointer)
                {
                    hybrid_spin(loops);
                    client->poll_message(sid, epoch);
                }
                if (reinterpret_cast<uintptr_t>(resp().pointer) == EMPTY_POINTER)
                {
                    resp() = {nullptr};
                    nano_spin();
                    if (++retry_loops > (1 << 20))
                        throw std::runtime_error("oom");
                }
            }
            return resp().pointer;
        }

        void unpin(vpage_id_type vpage_id, bool is_write = false, PartitionClient *client = nullptr)
        {
            if (vpage_id >= num_vpages)
                throw std::runtime_error("Virtual Page ID Error");

            auto [sid, block_id] = partitioner(vpage_id);

            if constexpr (ENABLE_DIRECT_UNPIN)
            {
                auto pre_ref_count = page_tables[sid]->unpin(block_id, is_write);
                if (pre_ref_count == 1)
                {
                    if (!client)
                        client = get_client();

                    scache::request_type req = {request_type::Type::NotifyDirectUnpin, vpage_id};
                    auto epoch = client->request(sid, req);
                }
                return;
            }

            if (!client)
                client = get_client();

            scache::request_type req = {is_write ? request_type::Type::DirtyUnpin : request_type::Type::Unpin,
                                        vpage_id};

            auto epoch = client->request(sid, req);
            // nano_spin();
            // client->poll_message(sid, epoch);

            return;
        }

        void get(uintptr_t addr, size_t size, void *data, PartitionClient *client = nullptr)
        {
            check_addr(addr, size);
            auto vpage_id = addr >> CACHE_PAGE_BITS;
            void *page_pointer = pin(vpage_id, client);
            auto addr_offset = addr & CACHE_PAGE_MASK;
            std::memcpy(data, (char *)page_pointer + addr_offset, size);
            unpin(vpage_id, false, client);
        }

        void set(uintptr_t addr, size_t size, const void *data, PartitionClient *client = nullptr)
        {
            check_addr(addr, size);
            auto vpage_id = addr >> CACHE_PAGE_BITS;
            void *page_pointer = pin(vpage_id, client);
            auto addr_offset = addr & CACHE_PAGE_MASK;
            std::memcpy((char *)page_pointer + addr_offset, data, size);
            unpin(vpage_id, true, client);
        }

        template <typename T> T get(uintptr_t addr, PartitionClient *client = nullptr)
        {
            T data;
            get(addr, sizeof(T), &data, client);
            return data;
        }

        template <typename T> void set(uint64_t addr, const T &data, PartitionClient *client = nullptr)
        {
            set(addr, sizeof(T), &data, nullptr);
        }

        AccessCounter &get_access_counter() { return counter; }

    private:
        void init_server()
        {
            struct IOPS_Stats
            {
                std::chrono::high_resolution_clock::time_point last_time_point;
                size_t write_ops, read_ops;
            };

            auto create_context = [&](size_t sid) FORCE_INLINE
            {
                auto virt_io_backend =
                    std::make_shared<AIO>(server_paths[sid], partitioner.num_blocks(sid), num_ppages_per_partition);
                auto phy_memory_pool =
                    std::make_shared<MemoryPool>(num_ppages_per_partition, virt_io_backend->get_buffer());
                phy_memory_pools[sid] = phy_memory_pool.get();

                auto iops_stats =
                    std::make_shared<IOPS_Stats>(IOPS_Stats{std::chrono::high_resolution_clock::now(), 0lu, 0lu});

                auto evict_func = [=](IOContext &async_context, scache::vpage_id_type vpage_id,
                                      scache::ppage_id_type ppage_id, bool dirty, const auto &) FORCE_INLINE
                {
                    if (!dirty)
                        return true;
                    if (async_context.first)
                    {
                        async_context.first = false;
                        return false;
                    }
                    if (!async_context.processing)
                    {
                        auto ret = virt_io_backend->write(vpage_id, phy_memory_pool->from_page_id(ppage_id),
                                                          &async_context.finish);
                        while (!ret)
                        {
                            ret = virt_io_backend->write(vpage_id, phy_memory_pool->from_page_id(ppage_id),
                                                         &async_context.finish);
                        }
                        async_context.processing = ret;
                        // virt_io_backend->progress();
                        return false;
                    }
                    if (!async_context.finish)
                    {
                        virt_io_backend->progress();
                        return false;
                    }

                    if constexpr (ENABLE_IOPS_STATS)
                    {
                        auto time_point = std::chrono::high_resolution_clock::now();
                        iops_stats->write_ops++;
                        if (time_point - iops_stats->last_time_point > std::chrono::seconds(1))
                        {
                            printf("Partition %lu IOPS: read %lf iops, write %lf iops, total %lf iops\n", sid,
                                   1e9 * (double)iops_stats->read_ops /
                                       (time_point - iops_stats->last_time_point).count(),
                                   1e9 * (double)iops_stats->write_ops /
                                       (time_point - iops_stats->last_time_point).count(),
                                   1e9 * (double)(iops_stats->read_ops + iops_stats->write_ops) /
                                       (time_point - iops_stats->last_time_point).count());
                            iops_stats->last_time_point = time_point;
                            iops_stats->write_ops = 0;
                            iops_stats->read_ops = 0;
                        }
                    }
                    // printf("Evict %lu\n", vpage_id);
                    return true;
                };

                auto load_func = [=](IOContext &async_context, scache::vpage_id_type vpage_id,
                                     scache::ppage_id_type ppage_id, auto &) FORCE_INLINE
                {
                    if (!phy_memory_pool->loaded(ppage_id))
                        return true;
                    if (async_context.first)
                    {
                        async_context.first = false;
                        return false;
                    }
                    if (!async_context.processing)
                    {
                        auto ret = virt_io_backend->read(vpage_id, phy_memory_pool->from_page_id(ppage_id),
                                                         &async_context.finish);
                        while (!ret)
                        {
                            ret = virt_io_backend->read(vpage_id, phy_memory_pool->from_page_id(ppage_id),
                                                        &async_context.finish);
                        }
                        async_context.processing = ret;
                        // virt_io_backend->progress();
                        return false;
                    }
                    if (!async_context.finish)
                    {
                        virt_io_backend->progress();
                        return false;
                    }

                    counter.count_miss();
                    if constexpr (ENABLE_IOPS_STATS)
                    {
                        auto time_point = std::chrono::high_resolution_clock::now();
                        iops_stats->read_ops++;
                        if (time_point - iops_stats->last_time_point > std::chrono::seconds(1))
                        {
                            printf("Partition %lu IOPS: read %lf iops, write %lf iops, total %lf iops\n", sid,
                                   1e9 * (double)iops_stats->read_ops /
                                       (time_point - iops_stats->last_time_point).count(),
                                   1e9 * (double)iops_stats->write_ops /
                                       (time_point - iops_stats->last_time_point).count(),
                                   1e9 * (double)(iops_stats->read_ops + iops_stats->write_ops) /
                                       (time_point - iops_stats->last_time_point).count());
                            iops_stats->last_time_point = time_point;
                            iops_stats->write_ops = 0;
                            iops_stats->read_ops = 0;
                        }
                    }
                    // printf("Load %lu\n", vpage_id);
                    return true;
                };

                // auto single_thread_cache =
                //     std::make_shared<SingleThreadCache<DirectPageTable, Clock, IOContext, EmptyState,
                //                                        decltype(evict_func), decltype(load_func), true>>(
                //         partitioner.num_blocks(sid), num_ppages_per_partition, evict_func, load_func);

                auto single_thread_cache = std::make_shared<
                    SharedSingleThreadCache<Clock, IOContext, EmptyState, decltype(evict_func), decltype(load_func)>>(
                    partitioner.num_blocks(sid), num_ppages_per_partition, evict_func, load_func);

                page_tables[sid] = &single_thread_cache->page_table;

                return std::make_tuple(phy_memory_pool, virt_io_backend, single_thread_cache);
            };

            auto pre_processing_func = [&](auto &context, const scache::request_type &req) FORCE_INLINE
            {
                auto &[phy_memory_pool, virt_io_backend, single_thread_cache] = context;
                auto [sid, vpage_id] = partitioner(req.page_id);
                single_thread_cache->prefetch(vpage_id);
            };

            auto first_processing_func =
                [&](auto &context, const scache::request_type &req, scache::response_type &resp) FORCE_INLINE
            {
                auto &[phy_memory_pool, virt_io_backend, single_thread_cache] = context;
                auto [sid, vpage_id] = partitioner(req.page_id);
                using req_context_type = typename std::decay_t<decltype(*single_thread_cache)>::context_type;
                resp.pointer = nullptr;
                switch (req.type)
                {
                case request_type::Type::Pin:
                {
                    auto ret = single_thread_cache->pin(vpage_id);
                    if (ret.phase != req_context_type::Phase::End)
                        return std::make_optional(ret);
                    if (ret.ppage_id == req_context_type::EMPTY_PPAGE_ID)
                        resp.pointer = reinterpret_cast<void *>(EMPTY_POINTER);
                    else
                        resp.pointer = phy_memory_pool->from_page_id(ret.ppage_id);
                    break;
                }
                case request_type::Type::Unpin:
                {
                    auto ret = single_thread_cache->unpin(vpage_id, false);
                    if (ret.phase != req_context_type::Phase::End)
                        return std::make_optional(ret);
                    resp.pointer = (void *)1;
                    break;
                }
                case request_type::Type::DirtyUnpin:
                {
                    auto ret = single_thread_cache->unpin(vpage_id, true);
                    if (ret.phase != req_context_type::Phase::End)
                        return std::make_optional(ret);
                    resp.pointer = (void *)1;
                    break;
                }
                case request_type::Type::NotifyDirectPin:
                {
                    single_thread_cache->notify_pin(vpage_id);
                    resp.pointer = (void *)1;
                    break;
                }
                case request_type::Type::NotifyDirectUnpin:
                {
                    single_thread_cache->notify_unpin(vpage_id);
                    resp.pointer = (void *)1;
                    break;
                }
                case request_type::Type::None:
                {
                    assert(false);
                }
                }
                return std::optional<req_context_type>{};
            };

            auto processing_func = [&](auto &context, auto &req_context, const scache::request_type &req) FORCE_INLINE
            {
                auto &[phy_memory_pool, virt_io_backend, single_thread_cache] = context;
                auto [sid, vpage_id] = partitioner(req.page_id);
                using req_context_type = typename std::decay_t<decltype(*single_thread_cache)>::context_type;

                single_thread_cache->process(req_context);

                if (req_context.phase != req_context_type::Phase::End)
                    return false;

                if (req.resp == nullptr)
                    return true;

                switch (req.type)
                {
                case request_type::Type::Pin:
                {
                    if (req_context.ppage_id == req_context_type::EMPTY_PPAGE_ID)
                        req.resp->pointer = reinterpret_cast<void *>(EMPTY_POINTER);
                    else
                        req.resp->pointer = phy_memory_pool->from_page_id(req_context.ppage_id);
                    break;
                }
                case request_type::Type::Unpin:
                {
                    // printf("Unpin %lu\n", vpage_id);
                    req.resp->pointer = (void *)1;
                    break;
                }
                case request_type::Type::DirtyUnpin:
                {
                    // printf("DirtyUnpin %lu -- %lu\n", req.page_id, vpage_id);
                    req.resp->pointer = (void *)1;
                    break;
                }
                case request_type::Type::NotifyDirectPin:
                case request_type::Type::NotifyDirectUnpin:
                case request_type::Type::None:
                {
                    assert(false);
                }
                }

                return true;
            };

            auto destroy_context = [&](auto &context) FORCE_INLINE
            {
                auto &[phy_memory_pool, virt_io_backend, single_thread_cache] = context;
                if (single_thread_cache->num_pinned())
                    printf("SharedCache destructs with pinned pages.\n");
                // single_thread_cache->flush();
            };

            server.run(create_context, pre_processing_func, first_processing_func, processing_func, destroy_context);
        }

        void check_addr(uintptr_t addr, size_t size) const
        {
            assert(addr + size < virt_size);
            assert((addr & CACHE_PAGE_MASK) + size <= CACHE_PAGE_SIZE);
            // if (addr + size >= virt_size)
            //     throw std::runtime_error("Address Error");
            // if ((addr & CACHE_PAGE_MASK) + size > CACHE_PAGE_SIZE)
            //     throw std::runtime_error("Cross-Page Access"); // TODO
        }

        const size_t virt_size;
        const size_t phy_size;
        const size_t num_vpages;
        const size_t num_ppages;
        const size_t num_partitions;
        const size_t num_ppages_per_partition;
        const size_t actual_num_ppages_per_thread;
        const std::vector<size_t> server_cpus;
        const std::vector<std::string> server_paths;
        const size_t max_num_clients;
        RoundRobinPartitioner partitioner;
        PartitionServer server;
        boost::thread_specific_ptr<std::shared_ptr<PartitionClient>> clients;

        MemoryPool *phy_memory_pools[MAX_THREADS];
        CompactHashPageTable *page_tables[MAX_THREADS];

        constexpr static uintptr_t EMPTY_POINTER = std::numeric_limits<uintptr_t>::max();

        AccessCounter counter;
    };
} // namespace scache
