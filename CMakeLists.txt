cmake_minimum_required(VERSION 3.1)
project(TriCache CXX C)

set(CMAKE_CXX_STANDARD 17)
set(THREADS_PREFER_PTHREAD_FLAG ON)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

add_library(cache_header INTERFACE)

find_package(Threads REQUIRED)
target_link_libraries(cache_header INTERFACE Threads::Threads)

find_package(OpenMP)
target_link_libraries(cache_header INTERFACE OpenMP::OpenMP_CXX)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_DEBUG_LIBS         OFF)
set(Boost_USE_RELEASE_LIBS       ON)
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME     OFF)
find_package(Boost REQUIRED COMPONENTS fiber thread)
target_include_directories(cache_header INTERFACE ${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})
target_link_libraries(cache_header INTERFACE ${Boost_LIBRARIES})

find_package(numa REQUIRED)
target_include_directories(cache_header INTERFACE ${NUMA_INCLUDE_DIR})
link_directories(${NUMA_LIBRARY_DIRS})
target_link_libraries(cache_header INTERFACE ${NUMA_LIBRARIES})

find_package(aio REQUIRED)
target_include_directories(cache_header INTERFACE ${AIO_INCLUDE_DIR})
link_directories(${AIO_LIBRARY_DIRS})
target_link_libraries(cache_header INTERFACE ${AIO_LIBRARIES})

find_package(uring)
if(URING_FOUND)
    add_definitions(-DENABLE_URING)
    target_include_directories(cache_header INTERFACE ${URING_INCLUDE_DIR})
    link_directories(${URING_LIBRARY_DIRS})
    target_link_libraries(cache_header INTERFACE ${URING_LIBRARIES})
endif()

find_package(PkgConfig)
pkg_check_modules(SPDK spdk_nvme spdk_env_dpdk spdk_syslibs)
if(SPDK_FOUND)
    add_definitions(-DENABLE_SPDK)
    target_include_directories(cache_header INTERFACE ${SPDK_INCLUDE_DIRS})
    link_directories(${SPDK_LIBRARY_DIRS})
    message("(${SPDK_INCLUDE_DIRS})")
    message("(${SPDK_LIBRARIES_DIRS})")
    message("(${SPDK_LIBRARIES})")
    target_link_libraries(cache_header INTERFACE -Wl,--whole-archive ${SPDK_LIBRARIES} -Wl,--no-whole-archive)
endif()

add_library(mimalloc INTERFACE)
target_include_directories(mimalloc INTERFACE ${CMAKE_CURRENT_LIST_DIR}/deps/mimalloc/include)
target_sources(mimalloc INTERFACE ${CMAKE_CURRENT_LIST_DIR}/deps/mimalloc/src/static.c)

target_include_directories(cache_header INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(cache_header INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(cache_header INTERFACE ${CMAKE_CURRENT_LIST_DIR}/deps)
target_include_directories(cache_header INTERFACE ${CMAKE_CURRENT_LIST_DIR}/deps/fadec ${CMAKE_CURRENT_LIST_DIR}/deps/fadec_output)

set(ENABLE_BENCHMARK OFF CACHE BOOL "Enable benchmarks.")

if(ENABLE_BENCHMARK)
    foreach(B
            bench_single_thread_cache
            bench_compact_hash_page_table
            bench_partition_server_client
            bench_io
            bench_shared_cache
            bench_private_cache
            bench_cached_vector
            bench_hitrate)
        add_executable(${B} bench/${B}.cpp)
        target_link_libraries(${B} cache_header mimalloc)
    endforeach()

    foreach(B
            bench_hitrate_mmap
            bench_hitrate_fastmap)
        add_executable(${B} bench/${B}.cpp)
        target_link_libraries(${B} OpenMP::OpenMP_CXX Threads::Threads)
    endforeach()

    if(SPDK_FOUND)
        add_executable(spdk playground/spdk.cpp)
        target_link_libraries(spdk cache_header mimalloc)
    endif()
    # conflict with liburing's fs.h
    # add_executable(parallel_sort playground/parallel_sort.cpp)
    # target_link_libraries(parallel_sort cache_header mimalloc)
    add_executable(manual_parallel_sort playground/manual_parallel_sort.cpp)
    target_link_libraries(manual_parallel_sort cache_header mimalloc)
endif()

set(ENABLE_LIBCACHE (CMAKE_CXX_COMPILER_ID STREQUAL "Clang") CACHE BOOL "Enable building libcache, the binding interface between instrument plugin and the cache library.")
set(ENABLE_LLVM_PLUGIN ENABLE_LIBCACHE CACHE BOOL "Enable instrumentation plugin based on LLVM.")

if(ENABLE_LIBCACHE)
    # Workaround ar & ranlib issue
    set(CMAKE_AR "${CMAKE_CXX_COMPILER_AR}")
    set(CMAKE_RANLIB "${CMAKE_CXX_COMPILER_RANLIB}")

    set(CACHE_SRCS
        bind/cache.cpp
        bind/hook.cpp
        bind/segfault_handler.cpp
        ${CMAKE_CURRENT_LIST_DIR}/deps/fadec/decode.c
        ${CMAKE_CURRENT_LIST_DIR}/deps/fadec/format.c)

    add_library(cache ${CACHE_SRCS})
    target_compile_options(cache PUBLIC -flto=thin)
    target_link_libraries(cache PUBLIC dl cache_header -flto=thin)
    target_link_libraries(cache PRIVATE mimalloc)

    add_library(cache-profile ${CACHE_SRCS})
    target_compile_options(cache-profile PUBLIC -flto=thin -DENABLE_PROFILE)
    target_link_libraries(cache-profile PUBLIC dl cache_header -flto=thin)
    target_link_libraries(cache-profile PRIVATE mimalloc)

    add_library(cache-disable-direct ${CACHE_SRCS})
    target_compile_options(cache-disable-direct PUBLIC -flto=thin -DDISABLE_DIRECT_CACHE)
    target_link_libraries(cache-disable-direct PUBLIC dl cache_header -flto=thin)
    target_link_libraries(cache-disable-direct PRIVATE mimalloc)

    add_library(cache-disable-private ${CACHE_SRCS})
    target_compile_options(cache-disable-private PUBLIC -flto=thin -DDISABLE_PRIVATE_CACHE)
    target_link_libraries(cache-disable-private PUBLIC dl cache_header -flto=thin)
    target_link_libraries(cache-disable-private PRIVATE mimalloc)

    add_library(cache-disable-direct-private ${CACHE_SRCS})
    target_compile_options(cache-disable-direct-private PUBLIC -flto=thin -DDISABLE_DIRECT_CACHE -DDISABLE_PRIVATE_CACHE)
    target_link_libraries(cache-disable-direct-private PUBLIC dl cache_header -flto=thin)
    target_link_libraries(cache-disable-direct-private PRIVATE mimalloc)

    add_library(cache-128KB ${CACHE_SRCS})
    target_compile_options(cache-128KB PUBLIC -flto=thin -DDEF_PAGE_BITS=17)
    target_link_libraries(cache-128KB PUBLIC dl cache_header -flto=thin)
    target_link_libraries(cache-128KB PRIVATE mimalloc)

    add_library(cache-128KB-profile ${CACHE_SRCS})
    target_compile_options(cache-128KB-profile PUBLIC -flto=thin -DDEF_PAGE_BITS=17 -DENABLE_PROFILE)
    target_link_libraries(cache-128KB-profile PUBLIC dl cache_header -flto=thin)
    target_link_libraries(cache-128KB-profile PRIVATE mimalloc)

    add_library(cache-128KB-disable-direct ${CACHE_SRCS})
    target_compile_options(cache-128KB-disable-direct PUBLIC -flto=thin -DDEF_PAGE_BITS=17 -DDISABLE_DIRECT_CACHE)
    target_link_libraries(cache-128KB-disable-direct PUBLIC dl cache_header -flto=thin)
    target_link_libraries(cache-128KB-disable-direct PRIVATE mimalloc)

    add_library(cache-128KB-disable-private ${CACHE_SRCS})
    target_compile_options(cache-128KB-disable-private PUBLIC -flto=thin -DDEF_PAGE_BITS=17 -DDISABLE_PRIVATE_CACHE)
    target_link_libraries(cache-128KB-disable-private PUBLIC dl cache_header -flto=thin)
    target_link_libraries(cache-128KB-disable-private PRIVATE mimalloc)

    add_library(cache-128KB-disable-direct-private ${CACHE_SRCS})
    target_compile_options(cache-128KB-disable-direct-private PUBLIC -flto=thin -DDEF_PAGE_BITS=17 -DDISABLE_DIRECT_CACHE -DDISABLE_PRIVATE_CACHE)
    target_link_libraries(cache-128KB-disable-direct-private PUBLIC dl cache_header -flto=thin)
    target_link_libraries(cache-128KB-disable-direct-private PRIVATE mimalloc)

    add_executable(test_libcache playground/test_libcache.cpp)
    target_link_libraries(test_libcache cache)
endif()

if(ENABLE_LLVM_PLUGIN)
    add_subdirectory(llvm-plugin)

    add_executable(test_instrument playground/test_instrument.cpp)
    target_link_libraries(test_instrument instrument cache)

    add_executable(test_instrument_mmap playground/test_instrument_mmap.cpp)
    target_link_libraries(test_instrument_mmap instrument-alloc cache)

    if(ENABLE_BENCHMARK)
        add_executable(terasort_gnu bench/bench_terasort_gnu.cpp)
        target_link_libraries(terasort_gnu instrument-alloc cache)
        target_compile_options(terasort_gnu PUBLIC -DWITH_CACHE)

        add_executable(terasort_gnu_profile bench/bench_terasort_gnu.cpp)
        target_link_libraries(terasort_gnu_profile instrument-alloc cache-profile)
        target_compile_options(terasort_gnu_profile PUBLIC -DWITH_CACHE)

        add_executable(terasort_gnu_disable_direct bench/bench_terasort_gnu.cpp)
        target_link_libraries(terasort_gnu_disable_direct instrument-alloc cache-disable-direct)
        target_compile_options(terasort_gnu_disable_direct PUBLIC -DWITH_CACHE)

        add_executable(terasort_gnu_disable_private bench/bench_terasort_gnu.cpp)
        target_link_libraries(terasort_gnu_disable_private instrument-alloc cache-disable-private)
        target_compile_options(terasort_gnu_disable_private PUBLIC -DWITH_CACHE)

        add_executable(terasort_gnu_disable_direct_private bench/bench_terasort_gnu.cpp)
        target_link_libraries(terasort_gnu_disable_direct_private instrument-alloc cache-disable-direct-private)
        target_compile_options(terasort_gnu_disable_direct_private PUBLIC -DWITH_CACHE)

        add_executable(terasort_manual bench/bench_terasort_manual.cpp)
        target_link_libraries(terasort_manual instrument-alloc cache-128KB)
        target_compile_options(terasort_manual PUBLIC -DWITH_CACHE)

        add_executable(terasort_manual_profile bench/bench_terasort_manual.cpp)
        target_link_libraries(terasort_manual_profile instrument-alloc cache-128KB-profile)
        target_compile_options(terasort_manual_profile PUBLIC -DWITH_CACHE)

        add_executable(terasort_manual_disable_direct bench/bench_terasort_manual.cpp)
        target_link_libraries(terasort_manual_disable_direct instrument-alloc cache-128KB-disable-direct)
        target_compile_options(terasort_manual_disable_direct PUBLIC -DWITH_CACHE)

        add_executable(terasort_manual_disable_private bench/bench_terasort_manual.cpp)
        target_link_libraries(terasort_manual_disable_private instrument-alloc cache-128KB-disable-private)
        target_compile_options(terasort_manual_disable_private PUBLIC -DWITH_CACHE)

        add_executable(terasort_manual_disable_direct_private bench/bench_terasort_manual.cpp)
        target_link_libraries(terasort_manual_disable_direct_private instrument-alloc cache-128KB-disable-direct-private)
        target_compile_options(terasort_manual_disable_direct_private PUBLIC -DWITH_CACHE)

        add_executable(terasort_gnu_orig bench/bench_terasort_gnu.cpp)
        target_link_libraries(terasort_gnu_orig Threads::Threads OpenMP::OpenMP_CXX)

        add_executable(terasort_manual_orig bench/bench_terasort_manual.cpp)
        target_link_libraries(terasort_manual_orig Threads::Threads OpenMP::OpenMP_CXX)

        add_executable(bench_hitrate_cache bench/bench_hitrate_mmap.cpp)
        target_link_libraries(bench_hitrate_cache instrument-alloc cache)
        target_compile_options(bench_hitrate_cache PUBLIC -DWITH_CACHE)
    endif()
endif()
