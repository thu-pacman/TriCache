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
#include "partitioner.hpp"
#include "type.hpp"
#include "util.hpp"
#include <atomic>
#include <boost/algorithm/string.hpp>
#include <cassert>
#include <chrono>
#include <fcntl.h>
#include <libaio.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#ifdef ENABLE_URING
#include <liburing.h>
#endif

#ifdef ENABLE_SPDK
#include <numa.h>
#include <sched.h>
#include <spdk/env.h>
#include <spdk/nvme.h>
#include <spdk/nvme_intel.h>
extern "C" int rte_thread_register();
#endif

namespace scache
{
    class DummyIO
    {
    public:
        DummyIO(std::vector<std::string> _paths, block_id_type _num_blocks, ppage_id_type _num_ppages)
            : read_iocbs(), idle_read_iocbs(), write_iocbs(), idle_write_iocbs(), preparing_iocbs()
        {
            read_depth = 0;
            write_depth = 0;
            for (size_t i = 0; i < _paths.size(); i++)
            {
                std::vector<std::string> tokens;
                boost::split(tokens, _paths[i], boost::is_any_of(","));
                assert(tokens.size() == 2);
                read_depth += std::stoul(tokens[0]);
                write_depth += std::stoul(tokens[1]);
            }

            read_depth = std::min(read_depth, MAX_READ_DEPTH);
            write_depth = std::min(write_depth, MAX_READ_DEPTH);

            for (size_t i = 0; i < read_depth; i++)
                idle_read_iocbs.emplace_back(i);
            for (size_t i = 0; i < write_depth; i++)
                idle_write_iocbs.emplace_back(i);
        }

        DummyIO(std::string _path, block_id_type _num_blocks, ppage_id_type _num_ppages)
            : DummyIO(std::vector<std::string>({_path}), _num_blocks, _num_ppages)
        {
        }

        DummyIO(const DummyIO &) = delete;
        DummyIO(DummyIO &&) = delete;

        ~DummyIO() {}

        void *get_buffer() { return nullptr; }

        bool write(const block_id_type &id, void *data, bool *finish = nullptr)
        {
            // throw std::runtime_error("Swapping-out in DummyIO.");
            if (idle_write_iocbs.empty())
            {
                progress();
                return false;
            }
            auto idx = idle_write_iocbs.back();
            idle_write_iocbs.pop_back();
            write_iocbs[idx].finish = finish;
            preparing_iocbs.emplace_back(&write_iocbs[idx]);
            return true;
        }

        bool read(const block_id_type &id, void *data, bool *finish = nullptr)
        {
            // throw std::runtime_error("Swapping-in in DummyIO.");
            if (idle_read_iocbs.empty())
            {
                progress();
                return false;
            }
            auto idx = idle_read_iocbs.back();
            idle_read_iocbs.pop_back();
            read_iocbs[idx].finish = finish;
            preparing_iocbs.emplace_back(&read_iocbs[idx]);
            return true;
        }

        bool progress()
        {
            auto now = std::chrono::high_resolution_clock::now();
            if (!preparing_iocbs.empty())
            {
                for (auto cb : preparing_iocbs)
                {
                    cb->start = now;
                    cb->running = true;
                }
                preparing_iocbs.clear();
            }
            for (size_t i = 0; i < read_depth; i++)
            {
                if (read_iocbs[i].running && read_iocbs[i].start + READ_LATENCY < now)
                {
                    if (read_iocbs[i].finish)
                        *read_iocbs[i].finish = true;
                    read_iocbs[i].running = false;
                    idle_read_iocbs.emplace_back(i);
                }
            }
            for (size_t i = 0; i < write_depth; i++)
            {
                if (write_iocbs[i].running && write_iocbs[i].start + WRITE_LATENCY < now)
                {
                    if (write_iocbs[i].finish)
                        *write_iocbs[i].finish = true;
                    write_iocbs[i].running = false;
                    idle_write_iocbs.emplace_back(i);
                }
            }
            return (idle_read_iocbs.size() != read_depth) || (idle_write_iocbs.size() != write_depth);
        }

    private:
        struct callback_type
        {
            std::chrono::high_resolution_clock::time_point start;
            bool *finish;
            bool running;
        };
        constexpr static size_t MAX_READ_DEPTH = 4096;
        constexpr static size_t MAX_WRITE_DEPTH = 4096;
        size_t read_depth;
        size_t write_depth;
        constexpr static auto READ_LATENCY = std::chrono::microseconds(80);
        constexpr static auto WRITE_LATENCY = std::chrono::microseconds(20);
        callback_type read_iocbs[MAX_READ_DEPTH];
        std::vector<size_t> idle_read_iocbs;
        callback_type write_iocbs[MAX_READ_DEPTH];
        std::vector<size_t> idle_write_iocbs;
        std::vector<callback_type *> preparing_iocbs;
    };

    class MemCopy
    {
    public:
        MemCopy(std::vector<std::string> _paths, block_id_type _num_blocks, ppage_id_type _num_ppages)
            : num_blocks(_num_blocks),
              num_ppages(_num_ppages),
              read_iocbs(),
              idle_read_iocbs(),
              write_iocbs(),
              idle_write_iocbs(),
              preparing_iocbs()
        {
            mempool = (char *)mmap_alloc(_num_blocks * CACHE_PAGE_SIZE);

            for (size_t i = 0; i < MAX_READ_DEPTH; i++)
                idle_read_iocbs.emplace_back(i);
            for (size_t i = 0; i < MAX_WRITE_DEPTH; i++)
                idle_write_iocbs.emplace_back(i);
        }

        MemCopy(std::string _path, block_id_type _num_blocks, ppage_id_type _num_ppages)
            : MemCopy(std::vector<std::string>({_path}), _num_blocks, _num_ppages)
        {
        }

        MemCopy(const MemCopy &) = delete;
        MemCopy(MemCopy &&) = delete;

        ~MemCopy() {}

        void *get_buffer() { return nullptr; }

        bool write(const block_id_type &id, void *data, bool *finish = nullptr)
        {
            if (idle_write_iocbs.empty())
            {
                progress();
                return false;
            }
            auto idx = idle_write_iocbs.back();
            idle_write_iocbs.pop_back();
            write_iocbs[idx].finish = finish;
            write_iocbs[idx].from = data;
            write_iocbs[idx].to = mempool + id * CACHE_PAGE_SIZE;
            preparing_iocbs.emplace_back(&write_iocbs[idx]);
            return true;
        }

        bool read(const block_id_type &id, void *data, bool *finish = nullptr)
        {
            if (idle_read_iocbs.empty())
            {
                progress();
                return false;
            }
            auto idx = idle_read_iocbs.back();
            idle_read_iocbs.pop_back();
            read_iocbs[idx].finish = finish;
            read_iocbs[idx].from = mempool + id * CACHE_PAGE_SIZE;
            read_iocbs[idx].to = data;
            preparing_iocbs.emplace_back(&read_iocbs[idx]);
            return true;
        }

        bool progress()
        {
            auto now = std::chrono::high_resolution_clock::now();
            if (!preparing_iocbs.empty())
            {
                for (auto cb : preparing_iocbs)
                {
                    cb->running = true;
                }
                preparing_iocbs.clear();
            }
            for (size_t i = 0; i < MAX_READ_DEPTH; i++)
            {
                if (read_iocbs[i].running)
                {
                    load_fence();
                    memcpy(read_iocbs[i].to, read_iocbs[i].from, CACHE_PAGE_SIZE);
                    save_fence();
                    if (read_iocbs[i].finish)
                        *read_iocbs[i].finish = true;
                    read_iocbs[i].running = false;
                    idle_read_iocbs.emplace_back(i);
                }
            }
            for (size_t i = 0; i < MAX_WRITE_DEPTH; i++)
            {
                if (write_iocbs[i].running)
                {
                    load_fence();
                    memcpy(write_iocbs[i].to, write_iocbs[i].from, CACHE_PAGE_SIZE);
                    save_fence();
                    if (write_iocbs[i].finish)
                        *write_iocbs[i].finish = true;
                    write_iocbs[i].running = false;
                    idle_write_iocbs.emplace_back(i);
                }
            }
            return (idle_read_iocbs.size() != MAX_READ_DEPTH) || (idle_write_iocbs.size() != MAX_WRITE_DEPTH);
        }

    private:
        struct callback_type
        {
            bool running;
            bool *finish;
            void *from, *to;
        };
        constexpr static size_t MAX_READ_DEPTH = 4096;
        constexpr static size_t MAX_WRITE_DEPTH = 4096;
        const block_id_type num_blocks;
        const ppage_id_type num_ppages;
        char *mempool;
        callback_type read_iocbs[MAX_READ_DEPTH];
        std::vector<size_t> idle_read_iocbs;
        callback_type write_iocbs[MAX_READ_DEPTH];
        std::vector<size_t> idle_write_iocbs;
        std::vector<callback_type *> preparing_iocbs;
    };

    class AIO
    {
    public:
        AIO(std::vector<std::string> _paths, block_id_type _num_blocks, ppage_id_type _num_ppages)
            : paths(_paths),
              num_blocks(_num_blocks),
              fds(),
              partitioner(paths.size(), num_blocks),
              iocbs(),
              events(),
              finishes(),
              idle_iocbs(),
              preparing_iocbs()
        {
            for (size_t i = 0; i < paths.size(); i++)
            {
                int fd = open(paths[i].c_str(), O_RDWR | O_CREAT | O_DIRECT, 0644);
                if (fd < 0)
                    throw std::runtime_error("Open File Error");
                struct stat st;
                fstat(fd, &st);
                auto size = st.st_size;
                const size_t flush_size = 4096;
                auto data = mmap_alloc(flush_size);
                for (size_t j = (size + flush_size - 1) / flush_size;
                     j * flush_size < partitioner.num_blocks(i) * CACHE_PAGE_SIZE; j++)
                {
                    if (pwrite(fd, data, flush_size, j * flush_size) != flush_size)
                        throw std::runtime_error("Init File Error");
                }
                mmap_free(data, flush_size);

                fds.emplace_back(fd);
            }

            memset(&ctx, 0, sizeof(io_context_t));
            if (io_setup(MAX_DEPTH, &ctx) != 0)
                throw std::runtime_error("AIO Setup Error");

            for (size_t i = 0; i < MAX_DEPTH; i++)
                idle_iocbs.emplace_back(i);
        }

        AIO(std::string _path, block_id_type _num_blocks, ppage_id_type _num_ppages)
            : AIO(std::vector<std::string>({_path}), _num_blocks, _num_ppages)
        {
        }

        AIO(const AIO &) = delete;
        AIO(AIO &&) = delete;

        ~AIO()
        {
            io_destroy(ctx);
            for (auto &fd : fds)
                close(fd);
        }

        void *get_buffer() { return nullptr; }

        bool write(const block_id_type &id, void *data, bool *finish = nullptr)
        {
            assert(id < num_blocks && ((uintptr_t)data) % CACHE_PAGE_SIZE == 0);
            if (idle_iocbs.empty())
            {
                progress();
                return false;
            }
            auto idx = idle_iocbs.back();
            idle_iocbs.pop_back();
            auto [file_id, block_id] = partitioner(id);
            auto fd = fds[file_id];
            io_prep_pwrite(&iocbs[idx], fd, data, CACHE_PAGE_SIZE, block_id * CACHE_PAGE_SIZE);
            finishes[idx] = finish;
            preparing_iocbs.emplace_back(&iocbs[idx]);
            return true;
        }

        bool read(const block_id_type &id, void *data, bool *finish = nullptr)
        {
            assert(id < num_blocks && ((uintptr_t)data) % CACHE_PAGE_SIZE == 0);
            if (idle_iocbs.empty())
            {
                progress();
                return false;
            }
            auto idx = idle_iocbs.back();
            idle_iocbs.pop_back();
            auto [file_id, block_id] = partitioner(id);
            auto fd = fds[file_id];
            io_prep_pread(&iocbs[idx], fd, data, CACHE_PAGE_SIZE, block_id * CACHE_PAGE_SIZE);
            finishes[idx] = finish;
            preparing_iocbs.emplace_back(&iocbs[idx]);
            return true;
        }

        bool progress()
        {
            if (!preparing_iocbs.empty())
            {
                if (io_submit(ctx, preparing_iocbs.size(), preparing_iocbs.data()) != preparing_iocbs.size())
                    throw std::runtime_error("AIO Submit Error");
                preparing_iocbs.clear();
            }
            auto num_ready = io_getevents(ctx, 0, MAX_DEPTH, events, nullptr);
            // auto num_ready = user_io_getevents(ctx, MAX_DEPTH, events);
            for (int i = 0; i < num_ready; i++)
            {
                auto idx = events[i].obj - iocbs;
                idle_iocbs.emplace_back(idx);
                if (finishes[idx])
                    *finishes[idx] = true;
            }
            return (idle_iocbs.size() != MAX_DEPTH);
        }

    private:
        constexpr static size_t MAX_DEPTH = 4096;
        const std::vector<std::string> paths;
        const block_id_type num_blocks;
        std::vector<int> fds;
        RoundRobinPartitioner partitioner;
        io_context_t ctx;
        iocb iocbs[MAX_DEPTH];
        io_event events[MAX_DEPTH];
        bool *finishes[MAX_DEPTH];
        std::vector<size_t> idle_iocbs;
        std::vector<iocb *> preparing_iocbs;

        constexpr static unsigned AIO_RING_MAGIC = 0xa10a10a1;

        struct aio_ring
        {
            unsigned id; /** kernel internal index number */
            unsigned nr; /** number of io_events */
            unsigned head;
            unsigned tail;

            unsigned magic;
            unsigned compat_features;
            unsigned incompat_features;
            unsigned header_length; /** size of aio_ring */

            struct io_event events[0];
        };

        static int user_io_getevents(io_context_t aio_ctx, unsigned int max, io_event *events)
        {
            long i = 0;
            unsigned head;
            aio_ring *ring = (aio_ring *)aio_ctx;
            assert(ring->magic == AIO_RING_MAGIC);
            while (i < max)
            {
                head = ring->head;

                if (head == ring->tail)
                {
                    /* There are no more completions */
                    break;
                }
                else
                {
                    /* There is another completion to reap */
                    events[i] = ring->events[head];
                    ring->head = (head + 1) % ring->nr;
                    std::atomic_thread_fence(std::memory_order_release);
                    i++;
                }
            }
            return i;
        }
    };

#ifdef ENABLE_URING
    class IOURing
    {
    public:
        IOURing(std::vector<std::string> _paths, block_id_type _num_blocks, ppage_id_type _num_ppages)
            : paths(_paths),
              num_blocks(_num_blocks),
              fds(),
              partitioner(paths.size(), num_blocks),
              ring(),
              cqes(),
              num_preparing(),
              num_processing()
        {
            for (size_t i = 0; i < paths.size(); i++)
            {
                int fd = open(paths[i].c_str(), O_RDWR | O_CREAT | O_DIRECT, 0644);
                if (fd < 0)
                    throw std::runtime_error("Open File Error");
                struct stat st;
                fstat(fd, &st);
                auto size = st.st_size;
                const size_t flush_size = 4096;
                auto data = mmap_alloc(flush_size);
                for (size_t j = (size + flush_size - 1) / flush_size;
                     j * flush_size < partitioner.num_blocks(i) * CACHE_PAGE_SIZE; j++)
                {
                    if (pwrite(fd, data, flush_size, j * flush_size) != flush_size)
                        throw std::runtime_error("Init File Error");
                }
                mmap_free(data, flush_size);

                fds.emplace_back(fd);
            }

            if (io_uring_queue_init(MAX_DEPTH, &ring, 0 /*IORING_SETUP_IOPOLL*/) != 0)
                throw std::runtime_error("IOURing Setup Error");
        }

        IOURing(std::string _path, block_id_type _num_blocks, ppage_id_type _num_ppages)
            : IOURing(std::vector<std::string>({_path}), _num_blocks, _num_ppages)
        {
        }

        IOURing(const IOURing &) = delete;
        IOURing(IOURing &&) = delete;

        ~IOURing()
        {
            io_uring_queue_exit(&ring);
            for (auto &fd : fds)
                close(fd);
        }

        void *get_buffer() { return nullptr; }

        bool write(const block_id_type &id, const void *data, bool *finish = nullptr)
        {
            assert(id < num_blocks && ((uintptr_t)data) % CACHE_PAGE_SIZE == 0);
            auto sqe = io_uring_get_sqe(&ring);
            if (!sqe)
            {
                progress();
                return false;
            }
            auto [file_id, block_id] = partitioner(id);
            auto fd = fds[file_id];
            io_uring_prep_write(sqe, fd, data, CACHE_PAGE_SIZE, block_id * CACHE_PAGE_SIZE);
            io_uring_sqe_set_data(sqe, finish);
            num_preparing++;
            return true;
        }

        bool read(const block_id_type &id, void *data, bool *finish = nullptr)
        {
            assert(id < num_blocks && ((uintptr_t)data) % CACHE_PAGE_SIZE == 0);
            auto sqe = io_uring_get_sqe(&ring);
            if (!sqe)
            {
                progress();
                return false;
            }
            auto [file_id, block_id] = partitioner(id);
            auto fd = fds[file_id];
            io_uring_prep_read(sqe, fd, data, CACHE_PAGE_SIZE, block_id * CACHE_PAGE_SIZE);
            io_uring_sqe_set_data(sqe, finish);
            num_preparing++;
            return true;
        }

        bool progress()
        {
            if (num_preparing)
            {
                auto ret = io_uring_submit(&ring);
                if (ret >= 0)
                {
                    num_processing += ret;
                    num_preparing -= ret;
                }
            }
            auto num_ready = io_uring_peek_batch_cqe(&ring, cqes, MAX_DEPTH);
            for (int i = 0; i < num_ready; i++)
            {
                bool *finish = (bool *)io_uring_cqe_get_data(cqes[i]);
                if (finish)
                    *finish = true;
            }
            io_uring_cq_advance(&ring, num_ready);
            num_processing -= num_ready;
            // if(num_processing && !num_ready && !io_uring_wait_cqe(&ring, cqes))
            //{
            //    bool *finish = (bool*)io_uring_cqe_get_data(cqes[0]);
            //    if(finish) *finish = true;
            //    io_uring_cqe_seen(&ring, cqes[0]);
            //    num_processing--;
            //}
            return num_processing;
        }

    private:
        constexpr static size_t MAX_DEPTH = 4096;
        const std::vector<std::string> paths;
        const block_id_type num_blocks;
        std::vector<int> fds;
        RoundRobinPartitioner partitioner;
        io_uring ring;
        io_uring_cqe *cqes[MAX_DEPTH];
        size_t num_preparing;
        size_t num_processing;
    };
#endif

#ifdef ENABLE_SPDK
    class SPDK
    {
        class Env
        {
            struct nvme_device_context_type
            {
                spdk_nvme_ctrlr *ctrlr;
                std::vector<spdk_nvme_ns *> nses;
            };

            inline static Env *singleton = nullptr;
            inline static std::mutex singleton_mutex;

        public:
            static Env &get()
            {
                // fast but unsafe check
                if (!singleton)
                {
                    std::lock_guard g{singleton_mutex};
                    // slow but safe check
                    if (!singleton)
                        singleton = new Env();
                }
                return *singleton;
            }
            __attribute__((destructor(101))) static void release()
            {
                if (singleton)
                    delete singleton;
                singleton = nullptr;
            }
            Env(Env const &) = delete;
            Env(Env &&) = delete;
            Env &operator=(Env const &) = delete;
            Env &operator=(Env &&) = delete;

            std::tuple<spdk_nvme_ctrlr *, spdk_nvme_ns *> get_handle(const char *addr, int nsid)
            {
                char pci_addr_str[32];
                spdk_pci_addr pci_addr;
                if (spdk_pci_addr_parse(&pci_addr, addr) < 0 ||
                    spdk_pci_addr_fmt(pci_addr_str, sizeof(pci_addr_str), &pci_addr) < 0)
                    throw std::runtime_error("Unable to parse pci address");

                for (const auto context : contextes)
                {
                    if (strcmp(pci_addr_str, spdk_nvme_ctrlr_get_transport_id(context.ctrlr)->traddr))
                        continue;
                    for (const auto &ns : context.nses)
                    {
                        if (spdk_nvme_ns_get_id(ns) != nsid)
                            continue;
                        return {context.ctrlr, ns};
                    }
                }

                return {nullptr, nullptr};
            };

        private:
            Env() : contextes()
            {
                cpu_set_t cpuset;
                pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

                spdk_env_opts opts;
                spdk_env_opts_init(&opts);

                if (spdk_env_init(&opts) < 0)
                    throw std::runtime_error("Unable to initialize SPDK env");

                if (spdk_nvme_probe(nullptr, &contextes, probe_cb, attach_cb, nullptr))
                    throw std::runtime_error("Unable to probe nvme devices");

                pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

                if (ENABLE_LATENCY_TRACKING)
                {
                    for (const auto context : contextes)
                    {
                        auto is_enabled = spdk_nvme_intel_feat_latency_tracking();
                        is_enabled.bits.enable = 0x00;
                        bool finish = false;
                        auto ret =
                            spdk_nvme_ctrlr_cmd_set_feature(context.ctrlr, SPDK_NVME_INTEL_FEAT_LATENCY_TRACKING,
                                                            is_enabled.raw, 0, nullptr, 0, cmd_callback, &finish);
                        if (ret)
                            throw std::runtime_error("Set SSD Latency tracking Error");
                        while (!finish)
                            spdk_nvme_ctrlr_process_admin_completions(context.ctrlr);

                        is_enabled.bits.enable = 0x01;
                        finish = false;
                        ret = spdk_nvme_ctrlr_cmd_set_feature(context.ctrlr, SPDK_NVME_INTEL_FEAT_LATENCY_TRACKING,
                                                              is_enabled.raw, 0, nullptr, 0, cmd_callback, &finish);
                        if (ret)
                            throw std::runtime_error("Set SSD Latency tracking Error");
                        while (!finish)
                            spdk_nvme_ctrlr_process_admin_completions(context.ctrlr);
                    }
                }
            }

            ~Env()
            {
                if (ENABLE_LATENCY_TRACKING)
                {
                    for (const auto context : contextes)
                    {
                        printf("\n%s Latency Statistics:\n", spdk_nvme_ctrlr_get_transport_id(context.ctrlr)->traddr);
                        printf("========================================================\n");
                        auto read_page = (spdk_nvme_intel_rw_latency_page *)spdk_dma_zmalloc(
                            sizeof(spdk_nvme_intel_rw_latency_page), CACHE_PAGE_SIZE, nullptr);
                        auto write_page = (spdk_nvme_intel_rw_latency_page *)spdk_dma_zmalloc(
                            sizeof(spdk_nvme_intel_rw_latency_page), CACHE_PAGE_SIZE, nullptr);

                        bool finish = false;
                        auto ret = spdk_nvme_ctrlr_cmd_get_log_page(
                            context.ctrlr, SPDK_NVME_INTEL_LOG_READ_CMD_LATENCY, SPDK_NVME_GLOBAL_NS_TAG, read_page,
                            sizeof(spdk_nvme_intel_rw_latency_page), 0, cmd_callback, &finish);
                        if (ret)
                            continue;

                        while (!finish)
                            spdk_nvme_ctrlr_process_admin_completions(context.ctrlr);

                        finish = false;
                        ret = spdk_nvme_ctrlr_cmd_get_log_page(
                            context.ctrlr, SPDK_NVME_INTEL_LOG_WRITE_CMD_LATENCY, SPDK_NVME_GLOBAL_NS_TAG, write_page,
                            sizeof(spdk_nvme_intel_rw_latency_page), 0, cmd_callback, &finish);
                        if (ret)
                            continue;

                        while (!finish)
                            spdk_nvme_ctrlr_process_admin_completions(context.ctrlr);

                        for (int i = 0; i < 32; i++)
                        {
                            if (read_page->buckets_32us[i] || write_page->buckets_32us[i])
                            {
                                printf("Bucket %dus - %dus: READ %d WRITE %d\n", i * 32, (i + 1) * 32,
                                       read_page->buckets_32us[i], write_page->buckets_32us[i]);
                            }
                        }
                        for (int i = 0; i < 31; i++)
                        {
                            if (read_page->buckets_1ms[i] || write_page->buckets_1ms[i])
                            {
                                printf("Bucket %dms - %dms: READ %d WRITE %d\n", i + 1, i + 2,
                                       read_page->buckets_1ms[i], write_page->buckets_1ms[i]);
                            }
                        }
                        for (int i = 0; i < 31; i++)
                        {
                            if (read_page->buckets_32ms[i] || write_page->buckets_32ms[i])
                            {
                                printf("Bucket %dms - %dms: READ %d WRITE %d\n", (i + 1) * 32, (i + 2) * 32,
                                       read_page->buckets_32ms[i], write_page->buckets_32ms[i]);
                            }
                        }
                        printf("========================================================\n\n");

                        spdk_dma_free(read_page);
                        spdk_dma_free(write_page);
                    }
                    for (const auto context : contextes)
                    {
                        auto is_enabled = spdk_nvme_intel_feat_latency_tracking();
                        is_enabled.bits.enable = 0x00;
                        bool finish = false;
                        auto ret =
                            spdk_nvme_ctrlr_cmd_set_feature(context.ctrlr, SPDK_NVME_INTEL_FEAT_LATENCY_TRACKING,
                                                            is_enabled.raw, 0, nullptr, 0, cmd_callback, &finish);
                        while (!ret && !finish)
                            spdk_nvme_ctrlr_process_admin_completions(context.ctrlr);
                    }
                }

                printf("Deconstructing SPDK Env\n");
                fflush(stdout);
                for (const auto context : contextes)
                {
                    spdk_nvme_detach(context.ctrlr);
                }
                spdk_env_fini();
            }

            static bool
            probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr_opts *opts)
            {
                printf("Attaching to %s\n", trid->traddr);
                return true;
            }

            static void attach_cb(void *cb_ctx,
                                  const struct spdk_nvme_transport_id *trid,
                                  struct spdk_nvme_ctrlr *ctrlr,
                                  const struct spdk_nvme_ctrlr_opts *opts)
            {
                auto &contextes = *(std::vector<nvme_device_context_type> *)cb_ctx;
                contextes.emplace_back();
                auto &context = contextes.back();

                printf("Attached to %s\n", trid->traddr);

                auto cdata = spdk_nvme_ctrlr_get_data(ctrlr);
                char name[1024];
                snprintf(name, sizeof(name), "%-20.20s (%-20.20s)", cdata->mn, cdata->sn);

                context.ctrlr = ctrlr;

                // namespace IDs start at 1
                auto num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
                printf("Using controller %s with %d namespaces.\n", name, num_ns);
                for (auto nsid = 1; nsid <= num_ns; nsid++)
                {
                    auto ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
                    if (!ns || !spdk_nvme_ns_is_active(ns))
                        continue;
                    context.nses.emplace_back(ns);

                    printf("  Namespace ID: %d size: %juGB\n", spdk_nvme_ns_get_id(ns),
                           spdk_nvme_ns_get_size(ns) / (1lu << 30));
                }
            }

            std::vector<nvme_device_context_type> contextes;
            constexpr static bool ENABLE_LATENCY_TRACKING = false;
        };

    public:
        SPDK(std::vector<std::string> _paths, block_id_type _num_blocks, ppage_id_type _num_ppages)
            : paths(_paths), num_blocks(_num_blocks), handles(), partitioner(paths.size(), num_blocks)
        {
            for (size_t i = 0; i < paths.size(); i++)
            {
                handles.emplace_back();
                auto &handle = handles.back();

                std::vector<std::string> tokens;
                boost::split(tokens, paths[i], boost::is_any_of(","));
                assert(tokens.size() == 3);

                auto nsid = std::stoi(tokens[1]);
                auto byte_base = std::stoul(tokens[2]);

                std::tie(handle.ctrlr, handle.ns) = Env::get().get_handle(tokens[0].c_str(), nsid);
                rte_thread_register();
                if (!handle.ctrlr || !handle.ns)
                    throw std::runtime_error("Cannot find device");

                size_t ns_size = spdk_nvme_ns_get_size(handle.ns);
                handle.sector_size = spdk_nvme_ns_get_sector_size(handle.ns);

                if (ns_size < partitioner.num_blocks(i) * CACHE_PAGE_SIZE + byte_base ||
                    CACHE_PAGE_SIZE < handle.sector_size || CACHE_PAGE_SIZE % handle.sector_size ||
                    byte_base % CACHE_PAGE_SIZE)
                    throw std::runtime_error("Device size error");

                handle.num_sector_per_page = CACHE_PAGE_SIZE / handle.sector_size;
                handle.sector_base = (byte_base + handle.sector_size - 1) / handle.sector_size;

                spdk_nvme_io_qpair_opts opts;
                spdk_nvme_ctrlr_get_default_io_qpair_opts(handle.ctrlr, &opts, sizeof(opts));
                opts.delay_cmd_submit = true;

                handle.qpair = spdk_nvme_ctrlr_alloc_io_qpair(handle.ctrlr, &opts, sizeof(opts));
                if (!handle.qpair)
                    throw std::runtime_error("Unable to create qpair");
            }

            buffer = spdk_malloc(_num_ppages * CACHE_PAGE_SIZE, CACHE_PAGE_SIZE, nullptr,
                                 numa_node_of_cpu(sched_getcpu()), SPDK_MALLOC_DMA);

            if (!buffer)
                throw std::runtime_error("Unable to allocate DMA memory");

            clear();
        }

        SPDK(std::string _path, block_id_type _num_blocks, ppage_id_type _num_ppages)
            : SPDK(std::vector<std::string>({_path}), _num_blocks, _num_ppages)
        {
        }

        SPDK(const SPDK &) = delete;
        SPDK(SPDK &&) = delete;

        ~SPDK()
        {
            clear();
            for (size_t i = 0; i < handles.size(); i++)
            {
                auto &handle = handles[i];
                spdk_nvme_ctrlr_free_io_qpair(handle.qpair);
            }
            spdk_free(buffer);
        }

        void *get_buffer() { return buffer; }

        bool write(const block_id_type &id, void *data, bool *finish = nullptr)
        {
            assert(id < num_blocks && ((uintptr_t)data) % CACHE_PAGE_SIZE == 0);

            auto [file_id, block_id] = partitioner(id);
            auto &handle = handles[file_id];

            auto ret = spdk_nvme_ns_cmd_write(handle.ns, handle.qpair, data,
                                              handle.sector_base + block_id * handle.num_sector_per_page,
                                              handle.num_sector_per_page, cmd_callback, finish, 0);

            if (ret)
            {
                progress();
                return false;
            }

            return true;
        }

        bool read(const block_id_type &id, void *data, bool *finish = nullptr)
        {
            assert(id < num_blocks && ((uintptr_t)data) % CACHE_PAGE_SIZE == 0);

            auto [file_id, block_id] = partitioner(id);
            auto &handle = handles[file_id];

            auto ret = spdk_nvme_ns_cmd_read(handle.ns, handle.qpair, data,
                                             handle.sector_base + block_id * handle.num_sector_per_page,
                                             handle.num_sector_per_page, cmd_callback, finish, 0);

            if (ret)
            {
                progress();
                return false;
            }

            return true;
        }

        bool progress()
        {
            std::atomic_thread_fence(std::memory_order_acq_rel);
            for (auto &handle : handles)
            {
                spdk_nvme_qpair_process_completions(handle.qpair, 0);
            }
            return true;
        }

    private:
        static void cmd_callback(void *arg, const spdk_nvme_cpl *completion)
        {
            if (spdk_nvme_cpl_is_error(completion))
            {
                printf("I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&completion->status));
                return;
            }
            if (arg)
                *(bool *)arg = true;
            std::atomic_thread_fence(std::memory_order_acq_rel);
        }

        struct nvme_qpair_handle_type
        {
            spdk_nvme_ctrlr *ctrlr;
            spdk_nvme_ns *ns;
            spdk_nvme_qpair *qpair;
            block_id_type sector_size;
            block_id_type num_sector_per_page;
            block_id_type sector_base;
        };

        void clear()
        {
            for (size_t i = 0; i < handles.size(); i++)
            {
                auto &handle = handles[i];

                {
                    spdk_nvme_dsm_range dsm_range;
                    dsm_range.starting_lba = handle.sector_base;
                    dsm_range.length = partitioner.num_blocks(i) * handle.num_sector_per_page;

                    bool finish = false;

                    auto rc = spdk_nvme_ns_cmd_dataset_management(
                        handle.ns, handle.qpair, SPDK_NVME_DSM_ATTR_DEALLOCATE, &dsm_range, 1, cmd_callback, &finish);

                    while (!finish)
                        spdk_nvme_qpair_process_completions(handle.qpair, 0);
                }
            }
        }

        const std::vector<std::string> paths;
        const block_id_type num_blocks;
        std::vector<nvme_qpair_handle_type> handles;
        RoundRobinPartitioner partitioner;
        void *buffer;
    };
#endif

} // namespace scache
