#!/bin/bash
cmake -S $TRICACHE_ROOT/ae-projects/rocksdb -B $TRICACHE_ROOT/ae-projects/rocksdb/build-orig -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-13 -DCMAKE_C_COMPILER=clang-13 -DWITH_TRICACHE=OFF -DWITH_SNAPPY=OFF -DWITH_TESTS=OFF -DWITH_ALL_TESTS=OFF -DFAIL_ON_WARNINGS=OFF
cmake -S $TRICACHE_ROOT/ae-projects/rocksdb -B $TRICACHE_ROOT/ae-projects/rocksdb/build-cache -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-13 -DCMAKE_C_COMPILER=clang-13 -DWITH_TRICACHE=ON -DWITH_SNAPPY=OFF -DWITH_TESTS=OFF -DWITH_ALL_TESTS=OFF -DFAIL_ON_WARNINGS=OFF
cmake --build $TRICACHE_ROOT/ae-projects/rocksdb/build-orig -j --target db_bench
cmake --build $TRICACHE_ROOT/ae-projects/rocksdb/build-cache -j --target db_bench
