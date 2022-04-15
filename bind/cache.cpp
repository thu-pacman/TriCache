// Copyright 2022 Guanyu Feng and Huanqi Cao, Tsinghua University
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

#include "cache.h"
#include "cached_allocator.hpp"
#include "cached_ptr.hpp"
#include "type.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include <dlfcn.h>
#include <numa.h>
#include <sched.h>
#include <unistd.h>

namespace
{
    constexpr uintptr_t OFFSET_MASK = 0x7fffffffffffffff;
    constexpr uintptr_t OFFSET_FLAG = 0x8000000000000000;

    scache::IntegratedCache *__global_cache = nullptr;
    scache::CachedAllocator<unsigned char> *__global_cached_allocator = nullptr;
    scache::CachedPtr<unsigned char> *__global_base_cached_ptr = nullptr;

    std::map<std::string, void *> *__mmap_file_dict;
    std::map<std::string, std::mutex *> *__mmap_file_mutex_dict;
    std::mutex __mmap_file_mutex;

    void *(*__real_memcpy)(void *__restrict, const void *__restrict, size_t) = nullptr;
    void *(*__real_memset)(void *, int, size_t) = nullptr;
    void *(*__real_memmove)(void *, const void *, size_t) = nullptr;
    void (*__real_free)(void *) = nullptr;
    int (*__real_munmap)(void *, size_t) = nullptr;
    int (*__real_madvise)(void *, size_t, int) = nullptr;
    int (*__real_msync)(void *, size_t, int) = nullptr;
    void (*__real_numa_interleave_memory)(void *, size_t, bitmask *) = nullptr;
    ssize_t (*__real_read)(int fd, void *buf, size_t count) = nullptr;
    ssize_t (*__real_write)(int fd, const void *buf, size_t count) = nullptr;
    int (*__real_pthread_create)(pthread_t *__restrict thread,
                                 const pthread_attr_t *__restrict attr,
                                 void *(*start_routine)(void *),
                                 void *__restrict arg) = nullptr;

    struct __hook_thread_func_arg_type
    {
        void *(*start_routine)(void *);
        void *__restrict arg;
    };

    std::mutex __threads_bind_to_cpu_mutex;
    std::atomic_size_t __current_cpu_id = 0;
    size_t *__threads_bind_to_cpu = nullptr;

    bool __disable_cache = false;

    size_t __malloc_threshold = 0x8000000000000000;
    size_t __mmap_file_threshold = 0x8000000000000000;

    size_t __mmap_size = 1ul << 32;

    bool __enable_pthread_create_hook = false;
    bool __disable_parallel_read_write = false;
    bool __disable_thread_bind = false;
    bool __disable_lazy_mmap_writeback = false;

    thread_local bool __is_client_threads = false;

    size_t __trace_real_alloc_threshold = 0x8000000000000000;
    size_t __total_real_alloc_threshold = 0x8000000000000000;
    std::atomic_size_t __total_real_alloc = 0;

    void bind_to_core(size_t cpuid)
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpuid, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    bool check_real_alloc_threshold(size_t size)
    {
        if (likely(size < __trace_real_alloc_threshold))
            return true;

        if (unlikely(__total_real_alloc + size > __total_real_alloc_threshold))
            return false;

        auto current_real_alloc = __total_real_alloc.fetch_add(size);
        if (current_real_alloc + size > __total_real_alloc_threshold)
        {
            __total_real_alloc.fetch_sub(size);
            return false;
        }

        return true;
    }

} // namespace

#define GET_REAL_SYMBOL(X)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!__real_##X)                                                                                               \
            __real_##X = (decltype(__real_##X))dlsym(RTLD_NEXT, #X);                                                   \
    } while (0)

size_t *__client_cpus = nullptr;
size_t __num_client_cpus = 0;

extern void cache_segfault_handler_init();

__attribute__((constructor(99))) void init()
{
    GET_REAL_SYMBOL(memset);
    GET_REAL_SYMBOL(memcpy);
    GET_REAL_SYMBOL(memmove);
    GET_REAL_SYMBOL(free);
    GET_REAL_SYMBOL(munmap);
    GET_REAL_SYMBOL(madvise);
    GET_REAL_SYMBOL(msync);
    GET_REAL_SYMBOL(read);
    GET_REAL_SYMBOL(write);
    GET_REAL_SYMBOL(pthread_create);

    auto env_disable_cache = std::getenv("DISABLE_CACHE");
    if (env_disable_cache)
    {
        __disable_cache = true;
        __malloc_threshold = std::numeric_limits<size_t>::max();
        __mmap_file_threshold = std::numeric_limits<size_t>::max();
        __enable_pthread_create_hook = false;
        return;
    }

    auto env_phy_size = std::getenv("CACHE_PHY_SIZE");
    auto env_virt_size = std::getenv("CACHE_VIRT_SIZE");
    auto env_num_clients = std::getenv("CACHE_NUM_CLIENTS");
    auto env_config = std::getenv("CACHE_CONFIG");
    auto env_malloc_threshold = std::getenv("CACHE_MALLOC_THRESHOLD");

    if (!env_phy_size)
        throw std::runtime_error("env CACHE_PHY_SIZE is empty.");
    if (!env_virt_size)
        throw std::runtime_error("env CACHE_VIRT_SIZE is empty.");
    if (!env_num_clients)
        throw std::runtime_error("env CACHE_NUM_CLIENTS is empty.");
    if (!env_config)
        throw std::runtime_error("env CACHE_CONFIG is empty.");
    if (!env_malloc_threshold)
        throw std::runtime_error("env CACHE_MALLOC_THRESHOLD is empty.");

    auto phy_size = std::stoul(env_phy_size);
    auto virt_size = std::stoul(env_virt_size);
    auto num_clients = std::stoul(env_num_clients);
    __malloc_threshold = std::stoul(env_malloc_threshold);

    auto env_disable_parallel_read_write = std::getenv("CACHE_DISABLE_PARALLEL_READ_WRITE");
    if (env_disable_parallel_read_write)
        __disable_parallel_read_write = true;

    auto env_disable_thread_bind = std::getenv("CACHE_DISABLE_THREAD_BIND");
    if (env_disable_thread_bind)
        __disable_thread_bind = true;

    auto env_disable_lazy_mmap_writeback = std::getenv("CACHE_DISABLE_LAZY_MMAP_WRITEBACK");
    if (env_disable_lazy_mmap_writeback)
        __disable_lazy_mmap_writeback = true;

    auto env_mmap_file_threshold = std::getenv("CACHE_MMAP_FILE_THRESHOLD");
    __mmap_file_threshold = env_mmap_file_threshold ? std::stoul(env_mmap_file_threshold) : __malloc_threshold;

    auto env_mmap_size = std::getenv("CACHE_MMAP_SIZE");
    if (env_mmap_size)
        __mmap_size = std::stoul(env_mmap_size);

    auto env_trace_real_alloc_threshold = std::getenv("CACHE_TRACE_REAL_ALLOC_THRESHOLD");
    if (env_trace_real_alloc_threshold)
        __trace_real_alloc_threshold = std::stoul(env_trace_real_alloc_threshold);

    auto env_total_real_alloc_threshold = std::getenv("CACHE_TOTAL_REAL_ALLOC_THRESHOLD");
    if (env_total_real_alloc_threshold)
        __total_real_alloc_threshold = std::stoul(env_total_real_alloc_threshold);

    std::vector<std::string> servers;
    boost::split(servers, env_config, boost::is_any_of(" "));

    std::vector<size_t> server_cpus;
    std::vector<std::string> server_paths;

    for (auto str : servers)
    {
        auto pos = str.find(",");
        auto cpu = std::stoul(str.substr(0, pos));
        auto path = pos == std::string::npos ? "" : str.substr(pos + 1);
        fprintf(stderr, "%lu %s\n", cpu, path.c_str());
        server_cpus.emplace_back(cpu);
        server_paths.emplace_back(path);
    }

    __global_cache = new scache::IntegratedCache(virt_size, phy_size, server_cpus, server_paths, num_clients);
    __global_cached_allocator = new scache::CachedAllocator<unsigned char>(__global_cache);
    __global_base_cached_ptr = new scache::CachedPtr<unsigned char>(__global_cache, 0);

    __client_cpus = new size_t[std::thread::hardware_concurrency()]();
    __threads_bind_to_cpu = new size_t[std::thread::hardware_concurrency()]();
    __num_client_cpus = 0;
    for (size_t i = 0; i < std::thread::hardware_concurrency(); i++)
    {
        if (std::find(server_cpus.begin(), server_cpus.end(), i) == server_cpus.end())
        {
            __client_cpus[__num_client_cpus++] = i;
        }
    }

    __current_cpu_id = 0;

    #pragma omp parallel
    __current_cpu_id++;
    // bind_to_core(__client_cpus[(__current_cpu_id++)%__num_client_cpus]);

    __threads_bind_to_cpu[0]++;
    __current_cpu_id = 1;

    __enable_pthread_create_hook = true;

    __mmap_file_dict = new std::map<std::string, void *>();
    __mmap_file_mutex_dict = new std::map<std::string, std::mutex *>();

    #pragma omp parallel
    __is_client_threads = true;

    std::atomic_thread_fence(std::memory_order_release);

    auto env_enable_segfault_handler = std::getenv("CACHE_ENABLE_SEGFAULT_HANDLER");
    if (env_enable_segfault_handler)
    {
        cache_segfault_handler_init();
    }
}

__attribute__((destructor(99))) void deinit()
{
    if (__disable_cache)
    {
        return;
    }

#if 0
    if (!__disable_lazy_mmap_writeback)
    {
        std::lock_guard g{__mmap_file_mutex};
        std::cerr << "in deinit" << std::endl;
        for (auto [file, p] : *__mmap_file_dict)
            std::cerr << "    mmap file: " << file << " at " << p << std::endl;
        for (auto [file, p] : *__mmap_file_dict)
        {
            auto fd = open(file.c_str(), O_WRONLY);
            struct stat fd_stat;
            fstat(fd, &fd_stat);
            write(fd, p, std::min((unsigned long)fd_stat.st_size, __mmap_size));
            close(fd);
        }
        for (auto [file, p] : *__mmap_file_mutex_dict)
        {
            delete p;
        }
    }
#endif

    // Just to speedup SPDK deinit
    exit(0);

    delete __global_base_cached_ptr;
    __global_base_cached_ptr = nullptr;
    delete __global_cached_allocator;
    __global_cached_allocator = nullptr;
    delete __global_cache;
    __global_cache = nullptr;

    __num_client_cpus = 0;
    delete[] __client_cpus;
    delete[] __threads_bind_to_cpu;

    __enable_pthread_create_hook = false;
    std::atomic_thread_fence(std::memory_order_release);
}

__attribute__((always_inline)) bool cache_space_ptr(const void *ptr) { return (uintptr_t(ptr) & OFFSET_FLAG); }

__attribute__((always_inline)) const void *cache_get_raw_ptr_load(const void *ptr)
{
    auto offset = (uintptr_t)ptr & OFFSET_MASK;
    return ((*__global_base_cached_ptr) + offset).to_const().get();
}

__attribute__((always_inline)) void *cache_get_raw_ptr_store(void *ptr)
{
    auto offset = (uintptr_t)ptr & OFFSET_MASK;
    return ((*__global_base_cached_ptr) + offset).get();
}

void *cache_alloc(size_t size)
{
    fprintf(stderr, "cache alloc %lu bytes\n", size);
    auto ptr = __global_cached_allocator->allocate(size);
    return (void *)(ptr.get_offset() | OFFSET_FLAG);
}

void cache_free(void *ptr, size_t size)
{
    auto offset = (uintptr_t)ptr & OFFSET_MASK;
    __global_cached_allocator->deallocate(((*__global_base_cached_ptr) + offset).to_const(), size);
}

void *cache_pin(void *ptr)
{
    auto offset = (uintptr_t)ptr & OFFSET_MASK;
    auto d = (*__global_base_cached_ptr) + offset;
    return d.pin();
}

void cache_unpin(void *ptr, bool is_write)
{
    auto offset = (uintptr_t)ptr & OFFSET_MASK;
    auto d = (*__global_base_cached_ptr) + offset;
    d.unpin(is_write);
}

void cache_flush() { __global_cache->flush(); }

void *cache_malloc_hook(size_t size)
{
    if (size >= __malloc_threshold && __is_client_threads && check_real_alloc_threshold(size))
        return cache_alloc(size);
    return malloc(size);
}

void *cache_calloc_hook(size_t n_elem, size_t elem_sz)
{
    size_t size = n_elem * elem_sz;
    if (size >= __malloc_threshold && __is_client_threads && check_real_alloc_threshold(size))
    {
        void *addr = cache_alloc(size);
        cache_memset(addr, 0, size);
        return addr;
    }
    return calloc(n_elem, elem_sz);
}

void *cache_realloc_hook(void *ptr, size_t size)
{
    if (uintptr_t(ptr) & OFFSET_FLAG)
        throw std::runtime_error("Realloc on cached address");
    if (size >= __malloc_threshold && __is_client_threads && check_real_alloc_threshold(size))
        throw std::runtime_error("Realloc too large space, should go into cache but unimplemented");
    return realloc(ptr, size);
}

void *cache_aligned_alloc_hook(size_t alignment, size_t size)
{
    if (size >= __malloc_threshold && __is_client_threads && check_real_alloc_threshold(size))
    {
        void *addr = cache_alloc(size);
        if (uintptr_t(addr) % alignment)
        {
            std::stringstream ss;
            ss << "Unaligned address returned by cache_alloc: 0x" << std::hex << uintptr_t(addr) << ", should align by "
               << alignment << std::endl;
            throw std::runtime_error(ss.str());
        }
    }
    return aligned_alloc(alignment, size);
}

void free(void *ptr)
{
    GET_REAL_SYMBOL(free);

    // TODO: remove traced alloc
    if (((uintptr_t)ptr & OFFSET_FLAG) == 0)
        __real_free(ptr);
    // TODO: free cached address
}

void *cache_mmap_hook(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    if (!__is_client_threads)
        goto end;

    if (flags & MAP_ANONYMOUS)
    {
        if (flags & MAP_FIXED)
        {
            if (uintptr_t(addr) & OFFSET_FLAG)
                return addr;
            else
                goto end;
        }
        if (len >= __malloc_threshold && __is_client_threads && check_real_alloc_threshold(len))
            return cache_alloc(len);
        goto end;
    }
    else
    {
        if (fd == -1 || len < __mmap_file_threshold) //__malloc_threshold)
            goto end;

        char filename_c[256];
        std::string filename;

        void *base;
        bool new_data;
        std::mutex *file_mutex = nullptr;
        struct stat fd_stat;
        {
            std::lock_guard g{__mmap_file_mutex};

            memset(filename_c, 0, sizeof(filename_c));
            readlink(("/proc/self/fd/" + std::to_string(fd)).c_str(), filename_c, sizeof filename_c);
            filename = filename_c;

            new_data = (__mmap_file_dict->count(filename) == 0);
            if (!new_data)
            {
                base = (*__mmap_file_dict)[filename];
                file_mutex = (*__mmap_file_mutex_dict)[filename];
                if (__disable_lazy_mmap_writeback)
                    file_mutex->lock();
            }
            else
            {
                //                 (*__mmap_file_dict)[filename] = base = cache_alloc(len + offset);
                fstat(fd, &fd_stat);
                (*__mmap_file_dict)[filename] = base = cache_alloc(std::max(__mmap_size, (size_t)fd_stat.st_size));
                (*__mmap_file_mutex_dict)[filename] = file_mutex = new std::mutex();
                file_mutex->lock();
            }
        }
        if (new_data)
        {
            auto cur = lseek(fd, 0, SEEK_CUR);
            lseek(fd, 0, SEEK_SET);
            // read(fd, base, len + offset);
            read(fd, base, fd_stat.st_size);
            lseek(fd, cur, SEEK_SET);
            file_mutex->unlock();
        }
        else if (__disable_lazy_mmap_writeback)
        {
            auto cur = lseek(fd, 0, SEEK_CUR);
            lseek(fd, offset, SEEK_SET);
            read(fd, (char *)base + offset, len);
            lseek(fd, cur, SEEK_SET);
            file_mutex->unlock();
        }

        std::lock_guard g{*file_mutex};
#if 1
        std::cerr << "scalablecache mmap " << filename_c << " to " << base << " is_new " << (int)new_data << std::endl;
        std::cerr << "    recorded name = " << filename << std::endl;
        std::cerr << "    offset = " << offset << " len = " << len << std::endl;
#endif
        // TODO: trace mmap size
        // if (offset + len > __mmap_size)
        //     throw std::runtime_error("mmap a file at position larger than __mmap_size = " +
        //     std::to_string(__mmap_size) + ".");
        //         if (new_data) {
        //             struct stat fd_stat;
        //             fstat(fd, &fd_stat);
        //             read(fd, base, std::min((unsigned long)fd_stat.st_size, __mmap_size));
        //         }
        return (char *)base + offset;
    }
end:
    return mmap(addr, len, prot, flags, fd, offset);
}

int munmap(void *addr, size_t len)
{
    GET_REAL_SYMBOL(munmap);
    if (uintptr_t(addr) & OFFSET_FLAG)
    {
        if (__disable_lazy_mmap_writeback)
        {
            std::string filename;
            std::size_t offset;
            void *base;
            {
                std::lock_guard g{__mmap_file_mutex};
                for (auto &[file, p] : *__mmap_file_dict)
                {
                    if (addr >= p && addr < (char *)p + __mmap_size)
                    {
                        filename = file;
                        offset = (char *)addr - (char *)p;
                        base = p;
                        break;
                    }
                }
            }

            std::cerr << "scalablecache munmmap " << addr << " " << len << std::endl;
            std::cerr << "    filename = " << filename << std::endl;
            std::cerr << "    base = " << offset << " len = " << len << std::endl;

            auto fd = open(filename.c_str(), O_WRONLY);
            lseek(fd, offset, SEEK_SET);
            write(fd, addr, len);
            close(fd);
        }
        else
        {
            std::cerr << "scalablecache lazy munmap" << std::endl;
        }

        return 0;
    }
    return __real_munmap(addr, len);
}

int madvise(void *addr, size_t len, int adv)
{
    GET_REAL_SYMBOL(madvise);
    if (uintptr_t(addr) & OFFSET_FLAG)
        return 0;
    return __real_madvise(addr, len, adv);
}

int msync(void *addr, size_t len, int sync)
{
    GET_REAL_SYMBOL(msync);
    if (uintptr_t(addr) & OFFSET_FLAG)
    {
        if (__disable_lazy_mmap_writeback)
        {
            std::string filename;
            std::size_t offset;
            void *base;
            {
                std::lock_guard g{__mmap_file_mutex};
                for (auto &[file, p] : *__mmap_file_dict)
                {
                    if (addr >= p && addr < (char *)p + __mmap_size)
                    {
                        filename = file;
                        offset = (char *)addr - (char *)p;
                        base = p;
                        break;
                    }
                }
            }

            std::cerr << "scalablecache msync " << addr << " " << len << std::endl;
            std::cerr << "    filename = " << filename << std::endl;
            std::cerr << "    base = " << offset << " len = " << len << std::endl;

            auto fd = open(filename.c_str(), O_WRONLY);
            lseek(fd, offset, SEEK_SET);
            write(fd, addr, len);
            fsync(fd);
            close(fd);
        }
        else
        {
            std::cerr << "scalablecache lazy msync" << std::endl;
        }

        return 0;
    }
    return __real_msync(addr, len, sync);
}

void numa_interleave_memory(void *mem, size_t size, bitmask *mask)
{
    GET_REAL_SYMBOL(numa_interleave_memory);
    if (uintptr_t(mem) & OFFSET_FLAG)
        return;
    __real_numa_interleave_memory(mem, size, mask);
}

// TODO: Opt memcpy/set/move, copy from https://git.musl-libc.org/cgit/musl/tree/src/string/
void *cache_memcpy(void *__restrict dst, const void *__restrict src, size_t n)
{
    GET_REAL_SYMBOL(memcpy);

    auto func = [](auto d, auto s, auto n) __attribute__((always_inline))
    {
        for (; n; n--)
            *d++ = *s++;
    };

    if (((uintptr_t)dst & OFFSET_FLAG) != 0 && ((uintptr_t)src & OFFSET_FLAG) != 0)
    {
        auto dst_offset = (uintptr_t)dst & OFFSET_MASK;
        auto d = (*__global_base_cached_ptr) + dst_offset;

        auto src_offset = (uintptr_t)src & OFFSET_MASK;
        auto s = ((*__global_base_cached_ptr) + src_offset).to_const();

        auto dst_page_offset = dst_offset % scache::CACHE_PAGE_SIZE;
        auto src_page_offset = src_offset % scache::CACHE_PAGE_SIZE;
        auto dst_page_ptr = d.pin();
        auto src_page_ptr = s.pin();
        size_t copied_len = 0;

        while (copied_len < n)
        {
            auto step_len =
                std::min(scache::CACHE_PAGE_SIZE - dst_page_offset, scache::CACHE_PAGE_SIZE - src_page_offset);
            step_len = std::min(step_len, n - copied_len);

            __real_memcpy(dst_page_ptr, src_page_ptr, step_len);

            copied_len += step_len;
            dst_page_offset += step_len;
            src_page_offset += step_len;

            dst_page_ptr += step_len;
            src_page_ptr += step_len;

            if (dst_page_offset == scache::CACHE_PAGE_SIZE)
            {
                d.unpin();
                dst_page_offset = 0;
                dst_page_ptr = (d + step_len).pin();
            }

            if (src_page_offset == scache::CACHE_PAGE_SIZE)
            {
                s.unpin();
                src_page_offset = 0;
                src_page_ptr = (s + step_len).pin();
            }

            d += step_len;
            s += step_len;
        }

        d.unpin();
        s.unpin();

        // func(d, s, n);
    }
    else if (((uintptr_t)dst & OFFSET_FLAG) != 0 && ((uintptr_t)src & OFFSET_FLAG) == 0)
    {
        auto dst_offset = (uintptr_t)dst & OFFSET_MASK;
        auto d = (*__global_base_cached_ptr) + dst_offset;

        auto s = (unsigned char *)src;

        auto dst_page_offset = dst_offset % scache::CACHE_PAGE_SIZE;
        auto dst_page_ptr = d.pin();
        size_t copied_len = 0;

        while (copied_len < n)
        {
            auto step_len = scache::CACHE_PAGE_SIZE - dst_page_offset;
            step_len = std::min(step_len, n - copied_len);

            __real_memcpy(dst_page_ptr, s, step_len);

            copied_len += step_len;
            dst_page_offset += step_len;

            dst_page_ptr += step_len;

            if (dst_page_offset == scache::CACHE_PAGE_SIZE)
            {
                d.unpin();
                dst_page_offset = 0;
                dst_page_ptr = (d + step_len).pin();
            }

            d += step_len;
            s += step_len;
        }

        d.unpin();

        // func(d, s, n);
    }
    else if (((uintptr_t)dst & OFFSET_FLAG) == 0 && ((uintptr_t)src & OFFSET_FLAG) != 0)
    {
        auto d = (unsigned char *)dst;

        auto src_offset = (uintptr_t)src & OFFSET_MASK;
        auto s = ((*__global_base_cached_ptr) + src_offset).to_const();

        auto src_page_offset = src_offset % scache::CACHE_PAGE_SIZE;
        auto src_page_ptr = s.pin();
        size_t copied_len = 0;

        while (copied_len < n)
        {
            auto step_len = scache::CACHE_PAGE_SIZE - src_page_offset;
            step_len = std::min(step_len, n - copied_len);

            __real_memcpy(d, src_page_ptr, step_len);

            copied_len += step_len;
            src_page_offset += step_len;

            src_page_ptr += step_len;

            if (src_page_offset == scache::CACHE_PAGE_SIZE)
            {
                s.unpin();
                src_page_offset = 0;
                src_page_ptr = (s + step_len).pin();
            }

            d += step_len;
            s += step_len;
        }

        s.unpin();

        // func(d, s, n);
    }
    else
    {
        return __real_memcpy(dst, src, n);
    }

    return dst;
}

void *cache_memset(void *dst, int c, size_t n)
{
    GET_REAL_SYMBOL(memset);

    auto func = [](auto s, auto c, auto n) __attribute__((always_inline))
    {
        for (; n; n--, s++)
            *s = c;
    };

    if (((uintptr_t)dst & OFFSET_FLAG) != 0)
    {
        auto dst_offset = (uintptr_t)dst & OFFSET_MASK;
        auto s = (*__global_base_cached_ptr) + dst_offset;

        func(s, c, n);
    }
    else
    {
        return __real_memset(dst, c, n);
    }

    return dst;
}

void *cache_memmove(void *dst, const void *src, size_t n)
{
    GET_REAL_SYMBOL(memmove);

    if (((uintptr_t)dst & OFFSET_FLAG) != 0 && ((uintptr_t)src & OFFSET_FLAG) != 0)
    {
        auto dst_offset = (uintptr_t)dst & OFFSET_MASK;
        auto d = (*__global_base_cached_ptr) + dst_offset;

        auto src_offset = (uintptr_t)src & OFFSET_MASK;
        auto s = ((*__global_base_cached_ptr) + src_offset).to_const();

        if (d.to_const() == s)
            return dst;
        if (s - d.to_const() - n <= -2 * n)
            return cache_memcpy(dst, src, n);
        if (d.to_const() < s)
        {
            for (; n; n--)
                *d++ = *s++;
        }
        else
        {
            while (n)
                n--, d[n] = s[n];
        }

        return dst;
    }
    else if (((uintptr_t)dst & OFFSET_FLAG) != 0 && ((uintptr_t)src & OFFSET_FLAG) == 0)
    {
        return cache_memcpy(dst, src, n);
    }
    else if (((uintptr_t)dst & OFFSET_FLAG) == 0 && ((uintptr_t)src & OFFSET_FLAG) != 0)
    {
        return cache_memcpy(dst, src, n);
    }
    else
    {
        return __real_memmove(dst, src, n);
    }
}

void *cache_memchr(const void *str, int c, size_t n)
{
    auto func = [](auto a, unsigned char c, size_t n) __attribute__((always_inline))
    {
        auto s = a;
        for (; n && *s != c; s++, n--)
            ;

        std::optional<ptrdiff_t> ret = std::nullopt;
        if (n)
            ret = s - a;
        return ret;
    };

    if (((uintptr_t)str & OFFSET_FLAG) != 0)
    {
        auto str_offset = (uintptr_t)str & OFFSET_MASK;
        auto s = scache::CachedPtr<const unsigned char>(*__global_base_cached_ptr) + str_offset;

        auto ret = func(s, c, n);
        if (ret.has_value())
            return (void *)((const unsigned char *)str + ret.value());
        else
            return nullptr;
    }
    else
    {
        auto s = (const unsigned char *)str;

        auto ret = func(s, c, n);
        if (ret.has_value())
            return (void *)((const unsigned char *)str + ret.value());
        else
            return nullptr;
    }
}

int memcmp(const void *vl, const void *vr, size_t n)
{
    auto func = [](auto l, auto r, auto n) __attribute__((always_inline))
    {
        for (; n && *l == *r; n--, l++, r++)
            ;
        return n ? *l - *r : 0;
    };

    if (((uintptr_t)vl & OFFSET_FLAG) != 0 && ((uintptr_t)vr & OFFSET_FLAG) != 0)
    {
        auto vl_offset = (uintptr_t)vl & OFFSET_MASK;
        auto l = (*__global_base_cached_ptr).to_const() + vl_offset;

        auto vr_offset = (uintptr_t)vr & OFFSET_MASK;
        auto r = (*__global_base_cached_ptr).to_const() + vr_offset;

        return func(l, r, n);
    }
    else if (((uintptr_t)vl & OFFSET_FLAG) != 0 && ((uintptr_t)vr & OFFSET_FLAG) == 0)
    {
        auto vl_offset = (uintptr_t)vl & OFFSET_MASK;
        auto l = (*__global_base_cached_ptr).to_const() + vl_offset;

        auto r = (unsigned char *)vr;

        return func(l, r, n);
    }
    else if (((uintptr_t)vl & OFFSET_FLAG) == 0 && ((uintptr_t)vr & OFFSET_FLAG) != 0)
    {
        auto l = (unsigned char *)vl;

        auto vr_offset = (uintptr_t)vr & OFFSET_MASK;
        auto r = (*__global_base_cached_ptr).to_const() + vr_offset;

        return func(l, r, n);
    }
    else
    {
        auto l = (const unsigned char *)vl;

        auto r = (const unsigned char *)vr;

        return func(l, r, n);
    }
}

int bcmp(const void *s1, const void *s2, size_t n) { return memcmp(s1, s2, n); }

int strcmp(const char *vl, const char *vr)
{
    auto func = [](auto l, auto r) __attribute__((always_inline))
    {
        for (; *l == *r && *l; l++, r++)
            ;
        return *l - *r;
    };

    if (((uintptr_t)vl & OFFSET_FLAG) != 0 && ((uintptr_t)vr & OFFSET_FLAG) != 0)
    {
        auto vl_offset = (uintptr_t)vl & OFFSET_MASK;
        auto l = (*__global_base_cached_ptr).to_const() + vl_offset;

        auto vr_offset = (uintptr_t)vr & OFFSET_MASK;
        auto r = (*__global_base_cached_ptr).to_const() + vr_offset;

        return func(l, r);
    }
    else if (((uintptr_t)vl & OFFSET_FLAG) != 0 && ((uintptr_t)vr & OFFSET_FLAG) == 0)
    {
        auto vl_offset = (uintptr_t)vl & OFFSET_MASK;
        auto l = (*__global_base_cached_ptr).to_const() + vl_offset;

        auto r = (unsigned char *)vr;

        return func(l, r);
    }
    else if (((uintptr_t)vl & OFFSET_FLAG) == 0 && ((uintptr_t)vr & OFFSET_FLAG) != 0)
    {
        auto l = (unsigned char *)vl;

        auto vr_offset = (uintptr_t)vr & OFFSET_MASK;
        auto r = (*__global_base_cached_ptr).to_const() + vr_offset;

        return func(l, r);
    }
    else
    {
        auto l = (const unsigned char *)vl;

        auto r = (const unsigned char *)vr;

        return func(l, r);
    }
}

char *stpcpy(char *__restrict vl, const char *__restrict vr)
{
    auto func = [](auto d, auto s) __attribute__((always_inline))
    {
        for (; (*d = *s); s++, d++)
            ;
    };

    if (((uintptr_t)vl & OFFSET_FLAG) != 0 && ((uintptr_t)vr & OFFSET_FLAG) != 0)
    {
        auto vl_offset = (uintptr_t)vl & OFFSET_MASK;
        auto l = scache::CachedPtr<char>(*__global_base_cached_ptr) + vl_offset;

        auto vr_offset = (uintptr_t)vr & OFFSET_MASK;
        auto r = scache::CachedPtr<const char>(*__global_base_cached_ptr) + vr_offset;

        func(l, r);
    }
    else if (((uintptr_t)vl & OFFSET_FLAG) != 0 && ((uintptr_t)vr & OFFSET_FLAG) == 0)
    {
        auto vl_offset = (uintptr_t)vl & OFFSET_MASK;
        auto l = scache::CachedPtr<char>(*__global_base_cached_ptr) + vl_offset;

        auto r = (unsigned char *)vr;

        func(l, r);
    }
    else if (((uintptr_t)vl & OFFSET_FLAG) == 0 && ((uintptr_t)vr & OFFSET_FLAG) != 0)
    {
        auto l = (unsigned char *)vl;

        auto vr_offset = (uintptr_t)vr & OFFSET_MASK;
        auto r = scache::CachedPtr<const char>(*__global_base_cached_ptr) + vr_offset;

        func(l, r);
    }
    else
    {
        func(vl, vr);
    }

    return vl;
}

char *strcpy(char *__restrict dest, const char *__restrict src)
{
    stpcpy(dest, src);
    return dest;
}

char *strcat(char *__restrict dest, const char *__restrict src)
{
    strcpy(dest + strlen(dest), src);
    return dest;
}

size_t strlen(const char *str)
{
    auto func = [](auto a) __attribute__((always_inline))
    {
        auto s = a;
        for (; *s; s++)
            ;
        return s - a;
    };

    if (((uintptr_t)str & OFFSET_FLAG) != 0)
    {
        auto str_offset = (uintptr_t)str & OFFSET_MASK;
        auto s = scache::CachedPtr<const char>(*__global_base_cached_ptr) + str_offset;

        return func(s);
    }
    else
    {
        auto s = str;

        return func(s);
    }
}

ssize_t read(int fd, void *buf, size_t count)
{
    GET_REAL_SYMBOL(read);

    if (((uintptr_t)buf & OFFSET_FLAG) != 0)
    {
        const size_t local_buf_len = __disable_parallel_read_write ? (2 * 1024 * 1024) : (128 * 1024 * 1024);
        auto local_buf = std::make_unique<char[]>(local_buf_len);

        ssize_t ret = 0;
        char *dst = (char *)buf;

        for (size_t off = 0; off < count; off += local_buf_len)
        {
            size_t len = std::min(count - off, local_buf_len);
            auto local_ret = __real_read(fd, local_buf.get(), len);
            if (local_ret < 0)
            {
                ret = local_ret;
                break;
            }
            if (__disable_parallel_read_write)
            {
                cache_memcpy(dst, local_buf.get(), local_ret);
            }
            else
            {
                #pragma omp parallel for schedule(static, 1)
                for (size_t local_off = 0; local_off < local_ret; local_off += scache::CACHE_PAGE_SIZE)
                {
                    cache_memcpy(dst + local_off, local_buf.get() + local_off,
                                 std::min(scache::CACHE_PAGE_SIZE, local_ret - local_off));
                }
            }
            ret += local_ret;
            dst += local_ret;
            if (local_ret < len || ret == count)
                break;
        }

        if (__disable_parallel_read_write)
        {
            if (count >= 32 * 1024 * 1024)
                cache_flush();
        }
        else
        {
            #pragma omp parallel
            cache_flush();
        }

        return ret;
    }
    else
    {
        return __real_read(fd, buf, count);
    }
}

ssize_t write(int fd, const void *buf, size_t count)
{
    GET_REAL_SYMBOL(write);

    if (((uintptr_t)buf & OFFSET_FLAG) != 0)
    {
        const size_t local_buf_len = __disable_parallel_read_write ? (2 * 1024 * 1024) : (128 * 1024 * 1024);
        auto local_buf = std::make_unique<char[]>(local_buf_len);

        ssize_t ret = 0;
        char *src = (char *)buf;

        for (size_t off = 0; off < count; off += local_buf_len)
        {
            size_t len = std::min(count - off, local_buf_len);
            if (__disable_parallel_read_write)
            {
                cache_memcpy(local_buf.get(), src, len);
            }
            else
            {
                #pragma omp parallel for schedule(static, 1)
                for (size_t local_off = 0; local_off < len; local_off += scache::CACHE_PAGE_SIZE)
                {
                    cache_memcpy(local_buf.get() + local_off, src + local_off,
                                 std::min(scache::CACHE_PAGE_SIZE, len - local_off));
                }
            }
            auto local_ret = __real_write(fd, local_buf.get(), len);
            if (local_ret < 0)
            {
                ret = local_ret;
                break;
            }
            ret += local_ret;
            src += local_ret;
            if (local_ret < len || ret == count)
                break;
        }
        if (__disable_parallel_read_write)
        {
            if (count >= 32 * 1024 * 1024)
                cache_flush();
        }
        else
        {
            #pragma omp parallel
            cache_flush();
        }
        return ret;
    }
    else
    {
        return __real_write(fd, buf, count);
    }
}

static void *__hook_thread_func(void *arg)
{
    struct __hook_thread_func_arg_type *hook_arg = (__hook_thread_func_arg_type *)arg;
    size_t myid = 0;
    if (!__disable_thread_bind)
    {
        std::lock_guard g{__threads_bind_to_cpu_mutex};
        for (size_t i = 0; i < __num_client_cpus; i++)
        {
            auto current_id = (__current_cpu_id + i) % __num_client_cpus;
            if (__threads_bind_to_cpu[current_id] < __threads_bind_to_cpu[myid])
                myid = current_id;
        }
        __threads_bind_to_cpu[myid]++;
        __current_cpu_id++;

        // bind_to_core(__client_cpus[(__current_cpu_id++) % __num_client_cpus]);
        bind_to_core(__client_cpus[myid]);
    }

    __is_client_threads = true;

    auto ret = hook_arg->start_routine(hook_arg->arg);

    // fprintf(stderr, "On thread exits, cleanup thread_local private cache\n");
    std::atomic_thread_fence(std::memory_order_release);
    if (__enable_pthread_create_hook)
        __global_base_cached_ptr->flush();
    // free(arg);

    if (!__disable_thread_bind)
    {
        std::lock_guard g{__threads_bind_to_cpu_mutex};
        __threads_bind_to_cpu[myid]--;
    }
    return ret;
};

int pthread_create(pthread_t *__restrict thread,
                   const pthread_attr_t *__restrict attr,
                   void *(*start_routine)(void *),
                   void *__restrict arg)
{
    GET_REAL_SYMBOL(pthread_create);

    if (__enable_pthread_create_hook)
    {
        auto hook_arg = (__hook_thread_func_arg_type *)malloc(sizeof(__hook_thread_func_arg_type));
        hook_arg->start_routine = start_routine;
        hook_arg->arg = arg;

        return __real_pthread_create(thread, attr, __hook_thread_func, hook_arg);
    }
    else
    {
        return __real_pthread_create(thread, attr, start_routine, arg);
    }
}

void cache_reset_profile()
{
#ifdef ENABLE_PROFILE
    #pragma omp parallel
    cache_flush();

    for (auto c : __global_cache->get_access_counters())
        c->clear();
#endif
}

void cache_dump_profile()
{
#ifdef ENABLE_PROFILE
    #pragma omp parallel
    cache_flush();

    auto counters = __global_cache->get_access_counters();
    const char *names[3] = {"Direct SATC", "Private SATC", "Shared"};
    std::cerr << "########################################" << std::endl;
    for (int i = 0; i < 3; ++i)
    {
        std::cerr << "# Profile of " << names[i] << ":" << std::endl;
        std::cerr << "#   access number: " << counters[i]->get_profile_num_access() << std::endl;
        std::cerr << "#   access cycles: " << counters[i]->get_profile_cycle_access() << std::endl;
        std::cerr << "#     miss number: " << counters[i]->get_profile_num_miss() << std::endl;
        std::cerr << "#     miss cycles: " << counters[i]->get_profile_cycle_miss() << std::endl;
        counters[i]->clear();
    }
    std::cerr << "########################################" << std::endl;
#endif
}

#undef GET_REAL_SYMBOL
