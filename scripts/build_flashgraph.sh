#!/bin/bash
cmake -S $TRICACHE_ROOT/ae-projects/FlashX -B $TRICACHE_ROOT/ae-projects/FlashX/build -DCMAKE_BUILD_TYPE=Release
cmake --build $TRICACHE_ROOT/ae-projects/FlashX/build -j

