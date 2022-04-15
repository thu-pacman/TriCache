#!/bin/bash

$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

export THREADS=256
export BACKGROUND=4

export REQUESTS=100000000
export REQUESTS_PER_THREAD=$(expr $REQUESTS \/ \( $THREADS - 2 \* $BACKGROUND \))

mkdir -p results_rocksdb_block

for MEM_GB in 256
do
    export MEMORY=$(expr $MEM_GB \* 1024 \* 1024 \* 1024)
    echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

    sudo rsync -avhP --delete /mnt/data/TriCache/rocksdb/2000M_block_4/ /mnt/raid/temp/2000M_block_run

    stdbuf -oL /usr/bin/time -v /usr/bin/time -v numactl -i all \
        $TRICACHE_ROOT/ae-projects/rocksdb/build-orig/db_bench -benchmarks="levelstats,readrandom,mixgraph,levelstats" -disable_wal \
        -use_direct_io_for_flush_and_compaction=true -use_direct_reads=true -cache_size=$(expr \( $MEM_GB \/ 8 \* 7 \) \* 1024 \* 1024 \* 1024) -cache_numshardbits=10 -compression_type=none \
        -key_size=48 -value_size=43 \
        -keyrange_dist_a=14.18 -keyrange_dist_b=-2.917 -keyrange_dist_c=0.0164 -keyrange_dist_d=-0.08082 -keyrange_num=30 \
        -value_k=0.2615 -value_sigma=25.45 -iter_k=2.517 -iter_sigma=14.236 \
        -mix_get_ratio=0.83 -mix_put_ratio=0.14 -mix_seek_ratio=0.03 \
        -sine_mix_rate_interval_milliseconds=5000 -sine_a=1000 -sine_b=0.000073 -sine_d=45000 \
        -num=2000000000 -reads=$REQUESTS_PER_THREAD \
         -prefix_size=4 -use_hash_search=true \
        -target_file_size_multiplier=10 \
        -threads=$(expr $THREADS - 2 \* $BACKGROUND) \
        -max_background_flushes=$BACKGROUND \
        -max_background_compactions=$BACKGROUND \
        -write_thread_max_yield_usec=100000000 \
        -write_thread_slow_yield_usec=10000000 \
        -write_buffer_size=1073741824 \
        -db=/mnt/raid/temp/2000M_block_run -use_existing_db=true \
        2>&1 | tee results_rocksdb_block/block_${MEM_GB}G_${THREADS}.txt
done

for THREADS in 256
do
    export BACKGROUND=4
    export REQUESTS_PER_THREAD=$(expr $REQUESTS \/ \( $THREADS - 2 \* $BACKGROUND \))
    for MEM_GB in 128 64
    do
        export MEMORY=$(expr $MEM_GB \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        sudo rsync -avhP --delete /mnt/data/TriCache/rocksdb/2000M_block_4/ /mnt/raid/temp/2000M_block_run

        stdbuf -oL /usr/bin/time -v /usr/bin/time -v numactl -i all \
            $TRICACHE_ROOT/ae-projects/rocksdb/build-orig/db_bench -benchmarks="levelstats,readrandom,mixgraph,levelstats" -disable_wal \
            -use_direct_io_for_flush_and_compaction=true -use_direct_reads=true -cache_size=$(expr \( $MEM_GB \/ 4 \* 2 \) \* 1024 \* 1024 \* 1024) -cache_numshardbits=10 -compression_type=none \
            -key_size=48 -value_size=43 \
            -keyrange_dist_a=14.18 -keyrange_dist_b=-2.917 -keyrange_dist_c=0.0164 -keyrange_dist_d=-0.08082 -keyrange_num=30 \
            -value_k=0.2615 -value_sigma=25.45 -iter_k=2.517 -iter_sigma=14.236 \
            -mix_get_ratio=0.83 -mix_put_ratio=0.14 -mix_seek_ratio=0.03 \
            -sine_mix_rate_interval_milliseconds=5000 -sine_a=1000 -sine_b=0.000073 -sine_d=45000 \
            -num=2000000000 -reads=$REQUESTS_PER_THREAD \
             -prefix_size=4 -use_hash_search=true \
            -target_file_size_multiplier=10 \
            -threads=$(expr $THREADS - 2 \* $BACKGROUND) \
            -max_background_flushes=$BACKGROUND \
            -max_background_compactions=$BACKGROUND \
            -write_thread_max_yield_usec=100000000 \
            -write_thread_slow_yield_usec=10000000 \
            -write_buffer_size=1073741824 \
            -db=/mnt/raid/temp/2000M_block_run -use_existing_db=true \
            2>&1 | tee results_rocksdb_block/block_${MEM_GB}G_${THREADS}.txt
    done
done

export REQUESTS=20000000
export REQUESTS_PER_THREAD=$(expr $REQUESTS \/ \( $THREADS - 2 \* $BACKGROUND \))
for THREADS in 256
do
    export BACKGROUND=4
    export REQUESTS_PER_THREAD=$(expr $REQUESTS \/ \( $THREADS - 2 \* $BACKGROUND \))
    for MEM_GB in 32 16
    do
        export MEMORY=$(expr $MEM_GB \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        sudo rsync -avhP --delete /mnt/data/TriCache/rocksdb/2000M_block_4/ /mnt/raid/temp/2000M_block_run

        stdbuf -oL /usr/bin/time -v /usr/bin/time -v numactl -i all \
            $TRICACHE_ROOT/ae-projects/rocksdb/build-orig/db_bench -benchmarks="levelstats,readrandom,mixgraph,levelstats" -disable_wal \
            -use_direct_io_for_flush_and_compaction=true -use_direct_reads=true -cache_size=$(expr \( $MEM_GB \/ 8 \* 2 \) \* 1024 \* 1024 \* 1024) -cache_numshardbits=10 -compression_type=none \
            -key_size=48 -value_size=43 \
            -keyrange_dist_a=14.18 -keyrange_dist_b=-2.917 -keyrange_dist_c=0.0164 -keyrange_dist_d=-0.08082 -keyrange_num=30 \
            -value_k=0.2615 -value_sigma=25.45 -iter_k=2.517 -iter_sigma=14.236 \
            -mix_get_ratio=0.83 -mix_put_ratio=0.14 -mix_seek_ratio=0.03 \
            -sine_mix_rate_interval_milliseconds=5000 -sine_a=1000 -sine_b=0.000073 -sine_d=45000 \
            -num=2000000000 -reads=$REQUESTS_PER_THREAD \
             -prefix_size=4 -use_hash_search=true \
            -target_file_size_multiplier=10 \
            -threads=$(expr $THREADS - 2 \* $BACKGROUND) \
            -max_background_flushes=$BACKGROUND \
            -max_background_compactions=$BACKGROUND \
            -write_thread_max_yield_usec=100000000 \
            -write_thread_slow_yield_usec=10000000 \
            -write_buffer_size=1073741824 \
            -db=/mnt/raid/temp/2000M_block_run -use_existing_db=true \
            2>&1 | tee results_rocksdb_block/block_${MEM_GB}G_${THREADS}.txt
    done
done
