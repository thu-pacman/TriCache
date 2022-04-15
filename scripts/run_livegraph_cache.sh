#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh

export LIVEGRAPH_NUM_CLIENTS=480

mkdir -p results_livegraph_cache

for i in 256 128 64 32 16
do
    echo $(expr \( $i \/ 8 \* 2 + 8 \) \* 1024 \* 1024 \* 1024) | sudo tee /sys/fs/cgroup/limit/memory.max
    export CACHE_PHY_SIZE=$(expr \( $i \/ 8 \* 6 - 8 \) \* 1024 \* 1024 \* 1024 )
    echo $CACHE_PHY_SIZE $CACHE_VIRT_SIZE
    $TRICACHE_ROOT/scripts/livegraph_cp_dataset_cache.sh 30
    $TRICACHE_ROOT/scripts/livegraph_exec_cache.sh 30 &
    sleep 10000000
    sleep 10
    java -Xms32g -Xmx32g -cp $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/target/livegraph-0.3.5-SNAPSHOT.jar com.ldbc.driver.Client -P $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/interactive-benchmark-cache-sf30.properties --results_dir results_livegraph_cache/results_all_16_sf30_cache_${i}G
    sudo killall -9 snb_server
    sleep 30
done

for i in 256 128 64 32 16
do
    echo $(expr \( $i \/ 8 \* 2 + 10 \) \* 1024 \* 1024 \* 1024) | sudo tee /sys/fs/cgroup/limit/memory.max
    export CACHE_PHY_SIZE=$(expr \( $i \/ 8 \* 6 - 10 \) \* 1024 \* 1024 \* 1024 )
    echo $CACHE_PHY_SIZE $CACHE_VIRT_SIZE
    $TRICACHE_ROOT/scripts/livegraph_cp_dataset_cache.sh 100
    $TRICACHE_ROOT/scripts/livegraph_exec_cache.sh 100 &
    sleep 10000000
    sleep 10
    java -Xms32g -Xmx32g -cp $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/target/livegraph-0.3.5-SNAPSHOT.jar com.ldbc.driver.Client -P $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/interactive-benchmark-cache-sf100.properties --results_dir results_livegraph_cache/results_all_16_sf100_cache_${i}G
    sudo killall -9 snb_server
    sleep 30
done
