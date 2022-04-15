#!/bin/bash
export CACHE_VIRT_SIZE=$(expr 4 \* 1024 \* 1024 \* 1024)
export CACHE_PHY_SIZE=$(expr 1 \* 1024 \* 1024 \* 1024)
export CACHE_MALLOC_THRESHOLD=$(expr 16 \* 1024 \* 1024)
export CACHE_NUM_CLIENTS=1

source $TRICACHE_ROOT/scripts/config.sh
export CACHE_CONFIG=$CACHE_16_SERVER_CONFIG

sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
    stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/build/test_instrument_mmap
