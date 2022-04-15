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
    int t = *a;
    *a = *b;
    *b = t;
}

int partition(int arr[], int low, int high)
{
    int pivot = *(arr + high);
    int i = (low - 1);

    for (int j = low; j <= high - 1; j++)
    {
        if (*(arr + j) <= pivot)
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
        int pi = partition(arr, low, high);

        quick_sort(arr, low, pi - 1);
        quick_sort(arr, pi + 1, high);
    }
}

extern "C" void cache_reset_profile();
extern "C" void cache_dump_profile();

int main()
{
    const size_t len = 4000000;
    const size_t num_ops = 2000;

    // auto arr = (int *)mmap(NULL, len * sizeof(int), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    auto arr = (int *)cache_alloc(len * sizeof(int));
    auto ref_arr = (int *)malloc(len * sizeof(int));

    cache_reset_profile();
    arr[0] = 0;
    arr[100000] = 100000;
    cache_dump_profile();

    auto rd = std::mt19937();
    int64_t ref = 0, sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        int data = rd();
        ref += data;
        *(arr + i) = data;
    }

    sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += *(arr + i);
    if (ref != sum)
        throw std::runtime_error("before sum check error\n");

    cache_reset_profile();
    quick_sort(arr, 0, len - 1);
    cache_dump_profile();

    sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += *(arr + i);
    if (ref != sum)
        throw std::runtime_error("later sum check error\n");

    for (size_t i = 0; i < len - 1; i++)
    {
        int l = *(arr + i);
        int r = *(arr + i + 1);
        if (l > r)
            throw std::runtime_error("compare check error\n");
    }

    return 0;
}
