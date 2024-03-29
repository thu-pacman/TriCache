#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

export OMP_PROC_BIND=true
export CACHE_VIRT_SIZE=$(expr 32 \* 1024 \* 1024 \* 1024 \* 1024)
source $TRICACHE_ROOT/scripts/config.sh
export CACHE_CONFIG=$CACHE_16_SERVER_CONFIG
export CACHE_NUM_CLIENTS=1024

export CACHE_MALLOC_THRESHOLD=$(expr 500 \* 1024 \* 1024)
export CACHE_MMAP_FILE_THRESHOLD=$(expr 16 \* 1024 \* 1024)

export CACHE_DISABLE_PARALLEL_READ_WRITE=true

mkdir -p results_small

sudo mkdir -p /mnt/data/TriCache/temp

export CACHE_NUM_CLIENTS=1024
export REQUESTS=100000000
export THREADS=940
export BACKGROUND=4
export REQUESTS_PER_THREAD=$(expr $REQUESTS \/ \( $THREADS - 2 \* $BACKGROUND \))
for MEM_GB in 64
do
    export CACHE_PHY_SIZE=$(expr \( $MEM_GB \/ 16 \* 6 \) \* 1024 \* 1024 \* 1024)
    export MEMORY=$(expr \( $MEM_GB \/ 16 \* 10 \) \* 1024 \* 1024 \* 1024)
    echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

    sudo rsync -avhP --delete /mnt/data/TriCache/rocksdb/2000M_plain_4/ /mnt/data/TriCache/temp/2000M_plain_run

    sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" stdbuf -oL /usr/bin/time -v numactl -i all -C !$CACHE_16_SERVER_CORES \
        $TRICACHE_ROOT/ae-projects/rocksdb/build-cache/db_bench -benchmarks="levelstats,mixgraph,levelstats" -disable_wal \
        -use_plain_table -mmap_read -mmap_write -compression_type=none \
        -key_size=48 -value_size=43 \
        -keyrange_dist_a=14.18 -keyrange_dist_b=-2.917 -keyrange_dist_c=0.0164 -keyrange_dist_d=-0.08082 -keyrange_num=30 \
        -value_k=0.2615 -value_sigma=25.45 -iter_k=2.517 -iter_sigma=14.236 \
        -mix_get_ratio=0.83 -mix_put_ratio=0.14 -mix_seek_ratio=0.03 \
        -sine_mix_rate_interval_milliseconds=5000 -sine_a=1000 -sine_b=0.000073 -sine_d=45000 \
        -num=2000000000 -reads=$REQUESTS_PER_THREAD \
        -prefix_size=4 \
        -target_file_size_multiplier=10 \
        -threads=$(expr $THREADS - 2 \* $BACKGROUND) \
        -max_background_flushes=$BACKGROUND \
        -max_background_compactions=$BACKGROUND \
        -write_thread_max_yield_usec=100000000 \
        -write_thread_slow_yield_usec=10000000 \
        -write_buffer_size=1073741824 \
        -db=/mnt/data/TriCache/temp/2000M_plain_run -use_existing_db=true \
        2>&1 | tee results_small/rocksdb_plain_cache.txt

done
