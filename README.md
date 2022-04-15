# TriCache

## Build with Clang

```
cmake -B build -DCMAKE_C_COMPILER=/usr/bin/clang-13 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-13 -DCMAKE_AR=/usr/bin/llvm-ar-13 -DCMAKE_RANLIB=/usr/bin/llvm-ranlib-13 -DCMAKE_BUILD_TYPE=Release -DENABLE_LIBCACHE=ON -DENABLE_BENCHMARK=on
```
