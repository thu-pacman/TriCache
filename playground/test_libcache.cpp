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

#include <bind/cache.h>
#include <cstring>
#include <random>
#include <stdexcept>

void swap(int *a, int *b)
{
    int t = *(int *)cache_get_raw_ptr_load(a);
    *(int *)cache_get_raw_ptr_store(a) = *(int *)cache_get_raw_ptr_load(b);
    *(int *)cache_get_raw_ptr_store(b) = t;
}

int partition(int arr[], int low, int high)
{
    int pivot = *(int *)cache_get_raw_ptr_load(arr + high);
    int i = (low - 1);

    for (int j = low; j <= high - 1; j++)
    {
        if (*(int *)cache_get_raw_ptr_load(arr + j) <= pivot)
        {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[high]);
    return (i + 1);
}

void quick_sort(int *arr, int low, int high)
{
    if (low < high)
    {

        if (high - low > 512)
            printf("quick_sort %d %d\n", low, high);

        int pi = partition(arr, low, high);

        quick_sort(arr, low, pi - 1);
        quick_sort(arr, pi + 1, high);
    }
}

int main()
{
    const size_t len = 4000000;
    const size_t num_ops = 2000;

    auto arr = (int *)cache_alloc(len * sizeof(int));
    auto ref_arr = (int *)malloc(len * sizeof(int));

    auto rd = std::mt19937();
    int64_t ref = 0, sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        int data = rd();
        ref += data;
        *(int *)cache_get_raw_ptr_store(arr + i) = data;
    }

    sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += *(int *)cache_get_raw_ptr_load(arr + i);
    if (ref != sum)
        throw std::runtime_error("before sum check error\n");

    quick_sort(arr, 0, len - 1);

    sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += *(int *)cache_get_raw_ptr_load(arr + i);
    if (ref != sum)
        throw std::runtime_error("later sum check error\n");

    for (size_t i = 0; i < len - 1; i++)
    {
        int l = *(int *)cache_get_raw_ptr_load(arr + i);
        int r = *(int *)cache_get_raw_ptr_load(arr + i + 1);
        if (l > r)
            throw std::runtime_error("compare check error\n");
    }

    for (size_t i = 0; i < len; i++)
        ref_arr[i] += *(int *)cache_get_raw_ptr_load(arr + i);

    for (size_t i = 0; i < num_ops; i++)
    {
        if (i % 100 == 0)
            printf("op %lu\n", i);

        switch (rd() % 3)
        {
        case 0:
        {
            int src = rd() % len;
            int dst = rd() % len;
            int n = rd() % std::min(len - src, len - dst);

            memmove(ref_arr + dst, ref_arr + src, n);
            cache_memmove(arr + dst, arr + src, n);

            break;
        }
        case 1:
        {
            int dst = rd() % len;
            int n = rd() % (len - dst);

            int set = rd();
            memset(ref_arr + dst, set, n);
            cache_memset(arr + dst, set, n);

            break;
        }
        case 2:
        {
            int src = rd() % (len / 2);
            int dst = rd() % (len / 2);
            int n = rd() % std::min(len / 2 - src, len / 2 - dst);

            if (rd() % 2 == 0)
                src += len / 2;
            else
                dst += len / 2;

            memcpy(ref_arr + dst, ref_arr + src, n);
            cache_memcpy(arr + dst, arr + src, n);
        }
        }
    }

    for (size_t i = 0; i < len; i++)
    {
        int data = *(int *)cache_get_raw_ptr_load(arr + i);
        int ref = ref_arr[i];
        if (data != ref)
            throw std::runtime_error("compare check error\n");
    }
}
