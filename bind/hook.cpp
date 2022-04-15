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

extern "C"
{
    extern void *memcpy(void *__restrict dst, const void *__restrict src, size_t size);
    extern void *memset(void *dst, int ch, size_t size);
    extern void *memmove(void *dst, const void *src, size_t size);
    extern void *memchr(const void *str, int c, size_t n);
}

void *memcpy(void *__restrict dst, const void *__restrict src, size_t n) { return cache_memcpy(dst, src, n); }

void *memset(void *dst, int c, size_t n) { return cache_memset(dst, c, n); }

void *memmove(void *dst, const void *src, size_t n) { return cache_memmove(dst, src, n); }

void *memchr(const void *str, int c, size_t n) { return cache_memchr(str, c, n); }

// void* operator new(size_t size) { return cache_malloc_hook(size); }
// void* operator new[](size_t size) { return cache_malloc_hook(size); }
void operator delete(void *ptr) noexcept { free(ptr); }
void operator delete[](void *ptr) noexcept { free(ptr); }
