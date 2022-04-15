/*
 * Copyright 2022 Guanyu Feng and Huanqi Cao, Tsinghua University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif
    extern size_t *__client_cpus;
    extern size_t __num_client_cpus;

    // load env: CACHE_PHY_SIZE, CACHE_VIRT_SIZE, CACHE_CONFIG, CACHE_NUM_CLIENTS
    extern __attribute__((constructor)) void init();
    extern __attribute__((destructor)) void deinit();
    extern bool cache_space_ptr(const void *ptr);
    extern const void *cache_get_raw_ptr_load(const void *ptr);
    extern void *cache_get_raw_ptr_store(void *ptr);
    extern void *cache_alloc(size_t size);
    extern void cache_free(void *ptr, size_t size);
    extern void *cache_pin(void *ptr);
    extern void cache_unpin(void *ptr, bool is_write);
    extern void cache_flush();

    extern void *cache_memcpy(void *__restrict dst, const void *__restrict src, size_t size);
    extern void *cache_memset(void *dst, int ch, size_t size);
    extern void *cache_memmove(void *dst, const void *src, size_t size);
    extern void *cache_memchr(const void *str, int c, size_t n);

    extern void *cache_malloc_hook(size_t size);
    extern void *cache_calloc_hook(size_t n_elem, size_t elem_sz);
    extern void *cache_realloc_hook(void *ptr, size_t size);
    extern void *cache_aligned_alloc_hook(size_t alignment, size_t size);
    extern void *cache_mmap_hook(void *addr, size_t len, int prot, int flags, int fd, off_t offset);

    extern void cache_reset_profile();
    extern void cache_dump_profile();

#ifdef __cplusplus
}
#endif
