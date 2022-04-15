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

#include "io_backend.hpp"
#include "partition_type.hpp"
#include "type.hpp"
#include "util.hpp"
#include <chrono>
#include <memory>
#include <random>
#include <stdexcept>

const size_t size = 4lu * (1lu << 30);
const size_t num_blocks = size / scache::CACHE_PAGE_SIZE;
const size_t num_requests = 4000000;

int main(int argc, char **argv)
{
    if (argc <= 1)
    {
        printf("usage: %s file_paths\n", argv[0]);
    }

    std::vector<std::string> paths;
    for (int i = 1; i < argc; i++)
        paths.emplace_back(argv[i]);

    scache::AIO io(paths, num_blocks, size / scache::CACHE_PAGE_SIZE * 2);

    char *write, *read;
    if (io.get_buffer())
    {
        write = (char *)io.get_buffer();
        read = write + size;
    }
    else
    {
        write = (char *)scache::mmap_alloc(size);
        read = (char *)scache::mmap_alloc(size);
    }

    bool *finishes = (bool *)scache::mmap_alloc(std::max(num_blocks, num_requests));

    {
        printf("Preparing\n");
        #pragma omp parallel for
        for (size_t i = 0; i < size; i++)
        {
            write[i] = i;
        }
    }

    {
        memset(finishes, 0, num_blocks);
        printf("Sequential Write Test\n");
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < num_blocks; i++)
        {
            auto res = io.write(i, write + i * scache::CACHE_PAGE_SIZE, &finishes[i]);
            while (!res)
            {
                res = io.write(i, write + i * scache::CACHE_PAGE_SIZE, &finishes[i]);
            }
        }
        for (size_t i = 0; i < num_blocks; i++)
        {
            while (!finishes[i])
                io.progress();
        }
        auto end = std::chrono::high_resolution_clock::now();

        #pragma omp parallel for
        for (size_t i = 0; i < num_blocks; i++)
        {
            if (!finishes[i])
                throw std::runtime_error("finishes check error");
        }
        printf("Throughput: %lf ops/s %lf GB/s\n", 1e9 * num_blocks / (end - start).count(),
               1e9 * num_blocks / (end - start).count() * 4096 / 1024 / 1024 / 1024);
    }

    {
        memset(finishes, 0, num_blocks);
        printf("Sequential Read Test\n");
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < num_blocks; i++)
        {
            auto res = io.read(i, read + i * scache::CACHE_PAGE_SIZE, &finishes[i]);
            while (!res)
            {
                res = io.read(i, read + i * scache::CACHE_PAGE_SIZE, &finishes[i]);
            }
        }
        for (size_t i = 0; i < num_blocks; i++)
        {
            while (!finishes[i])
                io.progress();
        }
        auto end = std::chrono::high_resolution_clock::now();

        #pragma omp parallel for
        for (size_t i = 0; i < num_blocks; i++)
        {
            if (!finishes[i])
                throw std::runtime_error("finishes check error");
        }
        #pragma omp parallel for
        for (size_t i = 0; i < size; i++)
        {
            if (write[i] != read[i])
                throw std::runtime_error("read write check error");
        }
        printf("Throughput: %lf ops/s %lf GB/s\n", 1e9 * num_blocks / (end - start).count(),
               1e9 * num_blocks / (end - start).count() * 4096 / 1024 / 1024 / 1024);
    }

    {
        memset(finishes, 0, num_requests);
        printf("Random Write\n");
        std::random_device rd;
        std::mt19937_64 rand(rd());
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t req = 0; req < num_requests; req++)
        {
            auto i = rand() % num_blocks;
            auto res = io.write(i, write + i * scache::CACHE_PAGE_SIZE, &finishes[req]);
            while (!res)
            {
                res = io.write(i, write + i * scache::CACHE_PAGE_SIZE, &finishes[req]);
            }
        }
        for (size_t i = 0; i < num_requests; i++)
        {
            while (!finishes[i])
                io.progress();
        }
        auto end = std::chrono::high_resolution_clock::now();

        printf("Throughput: %lf ops/s %lf GB/s\n", 1e9 * num_requests / (end - start).count(),
               1e9 * num_requests / (end - start).count() * 4096 / 1024 / 1024 / 1024);
    }

    {
        memset(finishes, 0, num_requests);
        printf("Random Read\n");
        std::random_device rd;
        std::mt19937_64 rand(rd());
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t req = 0; req < num_requests; req++)
        {
            auto i = rand() % num_blocks;
            auto res = io.read(i, read + i * scache::CACHE_PAGE_SIZE, &finishes[req]);
            while (!res)
            {
                res = io.read(i, read + i * scache::CACHE_PAGE_SIZE, &finishes[req]);
            }
        }
        for (size_t i = 0; i < num_requests; i++)
        {
            while (!finishes[i])
                io.progress();
        }
        auto end = std::chrono::high_resolution_clock::now();

        printf("Throughput: %lf ops/s %lf GB/s\n", 1e9 * num_requests / (end - start).count(),
               1e9 * num_requests / (end - start).count() * 4096 / 1024 / 1024 / 1024);
    }

    {
        memset(finishes, 0, num_requests);
        printf("Random Write/Read\n");
        std::random_device rd;
        std::mt19937_64 rand(rd());
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t req = 0; req < num_requests; req++)
        {
            if (rand() % 2)
            {
                auto i = rand() % num_blocks;
                auto res = io.write(i, write + i * scache::CACHE_PAGE_SIZE, &finishes[req]);
                while (!res)
                {
                    res = io.write(i, write + i * scache::CACHE_PAGE_SIZE, &finishes[req]);
                }
            }
            else
            {
                auto i = rand() % num_blocks;
                auto res = io.read(i, read + i * scache::CACHE_PAGE_SIZE, &finishes[req]);
                while (!res)
                {
                    res = io.read(i, read + i * scache::CACHE_PAGE_SIZE, &finishes[req]);
                }
            }
        }
        for (size_t i = 0; i < num_requests; i++)
        {
            while (!finishes[i])
                io.progress();
        }
        auto end = std::chrono::high_resolution_clock::now();

        printf("Throughput: %lf ops/s %lf GB/s\n", 1e9 * num_requests / (end - start).count(),
               1e9 * num_requests / (end - start).count() * 4096 / 1024 / 1024 / 1024);
    }
}
