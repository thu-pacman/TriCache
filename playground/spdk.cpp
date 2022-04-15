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

#include "util.hpp"
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <limits>
#include <numa.h>
#include <omp.h>
#include <random>
#include <spdk/env.h>
#include <spdk/nvme.h>
#include <stdexcept>
#include <vector>
extern "C" int rte_thread_register();

using namespace scache;

struct nvme_device_context
{
    spdk_nvme_ctrlr *ctrlr;
    std::vector<spdk_nvme_ns *> nses;
};

bool probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr_opts *opts)
{
    printf("Attaching to %s\n", trid->traddr);
    return true;
}

void attach_cb(void *cb_ctx,
               const struct spdk_nvme_transport_id *trid,
               struct spdk_nvme_ctrlr *ctrlr,
               const struct spdk_nvme_ctrlr_opts *opts)
{
    auto &contextes = *(std::vector<nvme_device_context> *)cb_ctx;
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

        printf("  Namespace ID: %d size: %juGB\n", spdk_nvme_ns_get_id(ns), spdk_nvme_ns_get_size(ns) / (1lu << 30));
    }
}

void callback(void *arg, const spdk_nvme_cpl *completion)
{
    if (spdk_nvme_cpl_is_error(completion))
    {
        printf("I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&completion->status));
    }
    *(bool *)arg = true;
}

int main(int argc, char **argv)
{
    cpu_set_t cpuset;
    pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    spdk_env_opts opts;
    spdk_env_opts_init(&opts);

    if (spdk_env_init(&opts) < 0)
        throw std::runtime_error("Unable to initialize SPDK env");

    std::vector<nvme_device_context> contextes;

    int rc = spdk_nvme_probe(nullptr, &contextes, probe_cb, attach_cb, nullptr);

    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    double sum_throughput = 0;
    #pragma omp parallel num_threads(argc-1)
    {
        int tid = omp_get_thread_num();
        std::vector<std::string> tokens;
        boost::split(tokens, argv[tid + 1], boost::is_any_of(","));
        assert(tokens.size() == 4);

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(std::stoi(tokens[0]), &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

        if (tid)
            rte_thread_register();

        char pci_addr_str[32];
        spdk_pci_addr pci_addr;
        if (spdk_pci_addr_parse(&pci_addr, tokens[1].c_str()) < 0 ||
            spdk_pci_addr_fmt(pci_addr_str, sizeof(pci_addr_str), &pci_addr) < 0)
            throw std::runtime_error("Unable to parse pci address");

        auto nsid = std::stoi(tokens[2]);

        auto offset = std::stoul(tokens[3]);

        spdk_nvme_ctrlr *ctrlr = nullptr;
        spdk_nvme_ns *ns = nullptr;

        for (const auto context : contextes)
        {
            if (strcmp(pci_addr_str, spdk_nvme_ctrlr_get_transport_id(context.ctrlr)->traddr))
                continue;
            for (const auto &cns : context.nses)
            {
                if (spdk_nvme_ns_get_id(cns) != nsid)
                    continue;
                ctrlr = context.ctrlr;
                ns = cns;
            }
        }

        if (ctrlr && ns)
        {
            spdk_nvme_io_qpair_opts opts;
            spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr, &opts, sizeof(opts));
            opts.delay_cmd_submit = true;

            auto qpair = spdk_nvme_ctrlr_alloc_io_qpair(ctrlr, &opts, sizeof(opts));
            if (!qpair)
                throw std::runtime_error("Unable to create qpair");

            const size_t num_requests = 2000000;
            const size_t buf_size = 4lu * (1 << 30);
            const size_t range = 4lu * (1 << 30);
            const size_t io_size = 4 * 1024;
            auto numa_id = numa_node_of_cpu(sched_getcpu());
            const size_t sector_size = spdk_nvme_ns_get_sector_size(ns);
            const size_t num_sector_per_page = io_size / sector_size;
            const size_t sector_offset = (offset + sector_size - 1) / sector_size;

            char *buf =
                (char *)spdk_malloc(buf_size, io_size, nullptr, numa_id /*SPDK_ENV_SOCKET_ID_ANY*/, SPDK_MALLOC_DMA);
            if (!buf)
                throw std::runtime_error("Unable to allocate DMA memory");
            bool *finishes = (bool *)mmap_alloc(num_requests * 10);

            std::mt19937_64 rand(tid);

            #pragma omp barrier

            {
                auto start = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < num_requests; i++)
                {
                    auto blk_id = rand() % (range / io_size);
                    auto phy_id = rand() % (buf_size / io_size);
                    auto ret = spdk_nvme_ns_cmd_write(ns, qpair, buf + (phy_id * io_size),
                                                      sector_offset + blk_id * num_sector_per_page, num_sector_per_page,
                                                      callback, &finishes[i], 0);
                    while (ret)
                    {
                        spdk_nvme_qpair_process_completions(qpair, 0);
                        ret = spdk_nvme_ns_cmd_write(ns, qpair, buf + (phy_id * io_size),
                                                     sector_offset + blk_id * num_sector_per_page, num_sector_per_page,
                                                     callback, &finishes[i], 0);
                    }
                }
                for (size_t i = 0; i < num_requests; i++)
                {
                    while (!finishes[i])
                        spdk_nvme_qpair_process_completions(qpair, 0);
                    finishes[i] = false;
                }
                auto end = std::chrono::high_resolution_clock::now();
                double throughput = 1e9 * num_requests / (end - start).count();
                #pragma omp critical
                sum_throughput += throughput;
                printf("\t[%d @ %d : %s] Write Throughput: %lf IOPS, %lf MB/s\n", tid, numa_node_of_cpu(sched_getcpu()),
                       pci_addr_str, throughput, throughput * io_size / (1lu << 20));
            }

            #pragma omp barrier
            #pragma omp master
            {
                printf("Global Write Throughput: %lf IOPS, %lf MB/s\n", sum_throughput,
                       sum_throughput * io_size / (1lu << 20));
                sum_throughput = 0;
            }
            #pragma omp barrier

            {
                auto start = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < num_requests * 10; i++)
                {
                    auto blk_id = rand() % (range / io_size);
                    auto phy_id = rand() % (buf_size / io_size);
                    auto ret = spdk_nvme_ns_cmd_read(ns, qpair, buf + (phy_id * io_size),
                                                     sector_offset + blk_id * num_sector_per_page, num_sector_per_page,
                                                     callback, &finishes[i], 0);
                    while (ret)
                    {
                        spdk_nvme_qpair_process_completions(qpair, 0);
                        ret = spdk_nvme_ns_cmd_read(ns, qpair, buf + (phy_id * io_size),
                                                    sector_offset + blk_id * num_sector_per_page, num_sector_per_page,
                                                    callback, &finishes[i], 0);
                    }
                }
                for (size_t i = 0; i < num_requests * 10; i++)
                {
                    while (!finishes[i])
                        spdk_nvme_qpair_process_completions(qpair, 0);
                    finishes[i] = false;
                }
                auto end = std::chrono::high_resolution_clock::now();
                double throughput = 1e9 * num_requests * 10 / (end - start).count();
                #pragma omp critical
                sum_throughput += throughput;
                printf("\t[%d @ %d : %s] Read Throughput: %lf IOPS, %lf MB/s\n", tid, numa_node_of_cpu(sched_getcpu()),
                       pci_addr_str, throughput, throughput * io_size / (1lu << 20));
            }

            #pragma omp barrier
            #pragma omp master
            {
                printf("Global Read Throughput: %lf IOPS, %lf MB/s\n", sum_throughput,
                       sum_throughput * io_size / (1lu << 20));
                sum_throughput = 0;
            }
            #pragma omp barrier

            {
                auto start = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < num_requests; i++)
                {
                    auto is_write = rand() % 2;
                    auto blk_id = rand() % (range / io_size);
                    auto phy_id = rand() % (buf_size / io_size);
                    if (is_write)
                    {
                        auto ret = spdk_nvme_ns_cmd_write(ns, qpair, buf + (phy_id * io_size),
                                                          sector_offset + blk_id * num_sector_per_page,
                                                          num_sector_per_page, callback, &finishes[i], 0);
                        while (ret)
                        {
                            spdk_nvme_qpair_process_completions(qpair, 0);
                            ret = spdk_nvme_ns_cmd_write(ns, qpair, buf + (phy_id * io_size),
                                                         sector_offset + blk_id * num_sector_per_page,
                                                         num_sector_per_page, callback, &finishes[i], 0);
                        }
                    }
                    else
                    {
                        auto ret = spdk_nvme_ns_cmd_read(ns, qpair, buf + (phy_id * io_size),
                                                         sector_offset + blk_id * num_sector_per_page,
                                                         num_sector_per_page, callback, &finishes[i], 0);
                        while (ret)
                        {
                            spdk_nvme_qpair_process_completions(qpair, 0);
                            ret = spdk_nvme_ns_cmd_read(ns, qpair, buf + (phy_id * io_size),
                                                        sector_offset + blk_id * num_sector_per_page,
                                                        num_sector_per_page, callback, &finishes[i], 0);
                        }
                    }
                }
                for (size_t i = 0; i < num_requests; i++)
                {
                    while (!finishes[i])
                        spdk_nvme_qpair_process_completions(qpair, 0);
                    finishes[i] = false;
                }
                auto end = std::chrono::high_resolution_clock::now();
                double throughput = 1e9 * num_requests / (end - start).count();
                #pragma omp critical
                sum_throughput += throughput;
                printf("\t[%d @ %d : %s] 50/50 Read/Write Throughput: %lf IOPS, %lf MB/s\n", tid,
                       numa_node_of_cpu(sched_getcpu()), pci_addr_str, throughput, throughput * io_size / (1lu << 20));
            }

            #pragma omp barrier
            #pragma omp master
            {
                printf("Global 50/50 Read/Write Throughput: %lf IOPS, %lf MB/s\n", sum_throughput,
                       sum_throughput * io_size / (1lu << 20));
                sum_throughput = 0;
            }
            #pragma omp barrier

            //{
            //    spdk_nvme_dsm_range dsm_range;
            //    dsm_range.starting_lba = sector_offset;
            //    dsm_range.length = range/sector_size;
            //    auto rc = spdk_nvme_ns_cmd_dataset_management(ns, qpair, SPDK_NVME_DSM_ATTR_DEALLOCATE, &dsm_range, 1,
            //    callback, &finishes[0]); while(!finishes[0])
            //        spdk_nvme_qpair_process_completions(qpair, 0);
            //    finishes[0] = false;
            //}

            mmap_free(finishes, num_requests);
            spdk_free(buf);
            spdk_nvme_ctrlr_free_io_qpair(qpair);
        }
    }

    return 0;

    for (const auto context : contextes)
    {
        spdk_nvme_detach(context.ctrlr);
    }
    spdk_env_fini();
}
