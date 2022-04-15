#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

export CACHE_PHY_SIZE=137438953472
export CACHE_VIRT_SIZE=2199023255552
export OMP_PROC_BIND=true
source $TRICACHE_ROOT/scripts/config.sh
export CACHE_CONFIG=$CACHE_32_SERVER_CONFIG

vmemory=256
export CACHE_VIRT_SIZE=$(expr $vmemory \* 1024 \* 1024 \* 1024)
export CACHE_MALLOC_THRESHOLD=$(expr 1024 \* 1024 \* 1024)

mkdir -p results_microbenchmark_cache

export THREADS=1792
export CACHE_NUM_CLIENTS=$THREADS
export OMP_NUM_THREADS=$THREADS

for i in 1.0
do
    export CACHE_PHY_SIZE=$(echo "print(int($CACHE_VIRT_SIZE / 4096 * $i) * 4096)" | python3)
    echo $CACHE_PHY_SIZE

    sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl --localalloc -C !$CACHE_32_SERVER_CORES \
        stdbuf -oL $TRICACHE_ROOT/build/bench_hitrate_cache \
        5000000 \
        $vmemory \
        $i \
        0 \
        2>&1 | tee results_microbenchmark_cache/micro_${i}_$THREADS.log
done

for i in 0.9
do
    export CACHE_PHY_SIZE=$(echo "print(int($CACHE_VIRT_SIZE / 4096 * $i) * 4096)" | python3)
    echo $CACHE_PHY_SIZE

    sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl --localalloc -C !$CACHE_32_SERVER_CORES \
        stdbuf -oL $TRICACHE_ROOT/build/bench_hitrate_cache \
        800000 \
        $vmemory \
        $i \
        0 \
        2>&1 | tee results_microbenchmark_cache/micro_${i}_$THREADS.log

done

export THREADS=896
export CACHE_NUM_CLIENTS=$THREADS
export OMP_NUM_THREADS=$THREADS

for i in 1.0
do
    export CACHE_PHY_SIZE=$(echo "print(int($CACHE_VIRT_SIZE / 4096 * $i) * 4096)" | python3)
    echo $CACHE_PHY_SIZE

    sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl --localalloc -C !$CACHE_32_SERVER_CORES \
        stdbuf -oL $TRICACHE_ROOT/build/bench_hitrate_cache \
        1000000 \
        $vmemory \
        $i \
        1 \
        2>&1 | tee results_microbenchmark_cache/micro_allpage_${i}_$THREADS.log
done

for i in 0.9
do
    export CACHE_PHY_SIZE=$(echo "print(int($CACHE_VIRT_SIZE / 4096 * $i) * 4096)" | python3)
    echo $CACHE_PHY_SIZE

    sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl --localalloc -C !$CACHE_32_SERVER_CORES \
        stdbuf -oL $TRICACHE_ROOT/build/bench_hitrate_cache \
        800000 \
        $vmemory \
        $i \
        1 \
        2>&1 | tee results_microbenchmark_cache/micro_allpage_${i}_$THREADS.log

done

export THREADS=1792
export CACHE_NUM_CLIENTS=$THREADS
export OMP_NUM_THREADS=$THREADS

for i in 0.8 0.7
do
    export CACHE_PHY_SIZE=$(echo "print(int($CACHE_VIRT_SIZE / 4096 * $i) * 4096)" | python3)
    echo $CACHE_PHY_SIZE

    sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl --localalloc -C !$CACHE_32_SERVER_CORES \
        stdbuf -oL $TRICACHE_ROOT/build/bench_hitrate_cache \
        400000 \
        $vmemory \
        $i \
        0 \
        2>&1 | tee results_microbenchmark_cache/micro_${i}_$THREADS.log

    sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl --localalloc -C !$CACHE_32_SERVER_CORES \
        stdbuf -oL $TRICACHE_ROOT/build/bench_hitrate_cache \
        400000 \
        $vmemory \
        $i \
        1 \
        2>&1 | tee results_microbenchmark_cache/micro_allpage_${i}_$THREADS.log

done

for i in 0.6 0.5 0.4 0.3 0.2 0.1
do
    export CACHE_PHY_SIZE=$(echo "print(int($CACHE_VIRT_SIZE / 4096 * $i) * 4096)" | python3)
    echo $CACHE_PHY_SIZE

    sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl --localalloc -C !$CACHE_32_SERVER_CORES \
        stdbuf -oL $TRICACHE_ROOT/build/bench_hitrate_cache \
        200000 \
        $vmemory \
        $i \
        0 \
        2>&1 | tee results_microbenchmark_cache/micro_${i}_$THREADS.log

    sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl --localalloc -C !$CACHE_32_SERVER_CORES \
        stdbuf -oL $TRICACHE_ROOT/build/bench_hitrate_cache \
        200000 \
        $vmemory \
        $i \
        1 \
        2>&1 | tee results_microbenchmark_cache/micro_allpage_${i}_$THREADS.log
done
