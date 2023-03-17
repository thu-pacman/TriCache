#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh

export LIVEGRAPH_NUM_CLIENTS=480

mkdir -p results_small

for i in 64
do
    echo $(expr \( $i \) \* 1024 \* 1024 \* 1024) | sudo tee /sys/fs/cgroup/limit/memory.max
    export CACHE_PHY_SIZE=$(expr \( $i \/ 8 \* 6 - 8 \) \* 1024 \* 1024 \* 1024 )
    echo $CACHE_PHY_SIZE $CACHE_VIRT_SIZE
    $TRICACHE_ROOT/scripts/livegraph_cp_dataset_cache.sh 100
    $TRICACHE_ROOT/scripts/livegraph_exec_cache.sh 100 &
    sleep 10000000
    sleep 10
    java -Xms32g -Xmx32g -cp $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/target/livegraph-0.3.5-SNAPSHOT.jar com.ldbc.driver.Client -P $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/interactive-benchmark-cache-sf100.properties --results_dir results_small/livegraph_cache
    sudo killall -9 snb_server
    sleep 30
done
