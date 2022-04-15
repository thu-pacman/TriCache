#!/bin/bash
export OMP_PROC_BIND=true
export CACHE_VIRT_SIZE=$(expr 32 \* 1024 \* 1024 \* 1024 \* 1024)

export CACHE_MALLOC_THRESHOLD=$(expr 32 \* 1024 \* 1024)

source $TRICACHE_ROOT/scripts/config.sh
export CACHE_CONFIG=$CACHE_16_SERVER_CONFIG

export THREADS=240
export CACHE_NUM_CLIENTS=$(expr $THREADS \+ 16 \* 4)
export OMP_NUM_THREADS=$THREADS

export CACHE_PHY_SIZE=$(expr 250 \* 1024 \* 1024 \* 1024)

sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" stdbuf -oL /usr/bin/time -v numactl -i all -C !$CACHE_16_SERVER_CORES \
    $TRICACHE_ROOT/ae-projects/ligra/apps/convert_to_ligra /mnt/data/TriCache/graph/uk-2014.bin /mnt/data/TriCache/ligra/uk-2014-new
