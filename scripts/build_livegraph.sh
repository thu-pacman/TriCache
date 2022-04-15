#!/bin/bash
cmake -S $TRICACHE_ROOT/ae-projects/LiveGraph-snb -B $TRICACHE_ROOT/ae-projects/LiveGraph-snb/build-mmap -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-13 -DCMAKE_C_COMPILER=clang-13 -DWITH_TRICACHE=OFF
cmake -S $TRICACHE_ROOT/ae-projects/LiveGraph-snb -B $TRICACHE_ROOT/ae-projects/LiveGraph-snb/build-cache -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-13 -DCMAKE_C_COMPILER=clang-13 -DWITH_TRICACHE=ON
cmake --build $TRICACHE_ROOT/ae-projects/LiveGraph-snb/build-mmap -j
cmake --build $TRICACHE_ROOT/ae-projects/LiveGraph-snb/build-cache -j
pushd $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations
./dependency.sh
./build.sh
popd
