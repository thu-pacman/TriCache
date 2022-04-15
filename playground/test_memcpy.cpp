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

#include "cache.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <stdexcept>

const size_t TOTAL_SIZE = 1lu << 15;
const size_t TOTAL_TESTS = 1lu << 15;

char *buf_dst, *buf_dst_2, *buf_dst_mem, *buf_dst_ref, *buf_src, *buf_src_ref;

void validate_data()
{
    for (size_t i = 0; i < TOTAL_SIZE; i++)
    {
        if (buf_dst[i] != buf_dst_ref[i])
        {
            throw std::runtime_error("check dst error");
        }
        if (buf_dst_2[i] != buf_dst_ref[i])
        {
            throw std::runtime_error("check dst2 error");
        }
        if (buf_dst_mem[i] != buf_dst_ref[i])
        {
            throw std::runtime_error("check mem error");
        }
        if (buf_src[i] != buf_src_ref[i])
        {
            throw std::runtime_error("check src error");
        }
    }
}

int main()
{
    buf_dst = (char *)cache_alloc(TOTAL_SIZE);
    buf_dst_2 = (char *)cache_alloc(TOTAL_SIZE);
    buf_dst_mem = (char *)malloc(TOTAL_SIZE);
    buf_dst_ref = (char *)malloc(TOTAL_SIZE);
    buf_src = (char *)cache_alloc(TOTAL_SIZE);
    buf_src_ref = (char *)malloc(TOTAL_SIZE);

    auto rand = std::mt19937();
    for (size_t i = 0; i < TOTAL_SIZE; i++)
    {
        char c = (char)rand();
        buf_dst_ref[i] = c;
        buf_dst[i] = c;
        buf_dst_2[i] = c;
        buf_dst_mem[i] = c;
    }
    for (size_t i = 0; i < TOTAL_SIZE; i++)
    {
        char c = (char)rand();
        buf_src_ref[i] = c;
        buf_src[i] = c;
    }

    validate_data();

    printf("inited\n");

    for (size_t i = 0; i < TOTAL_TESTS; i++)
    {
        auto len = std::rand() % TOTAL_SIZE + 1;
        auto dst_start = std::rand() % (TOTAL_SIZE - len + 1);
        auto src_start = std::rand() % (TOTAL_SIZE - len + 1);

        memcpy(buf_dst + dst_start, buf_src + src_start, len);
        memcpy(buf_dst_2 + dst_start, buf_src_ref + src_start, len);
        memcpy(buf_dst_mem + dst_start, buf_src + src_start, len);
        memcpy(buf_dst_ref + dst_start, buf_src_ref + src_start, len);

        validate_data();
    }

    return 0;
}
