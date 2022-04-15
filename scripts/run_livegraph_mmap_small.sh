#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh

export LIVEGRAPH_NUM_CLIENTS=512

mkdir -p results_small

for i in 32
do
    echo ${i}G | sudo tee /sys/fs/cgroup/limit/memory.max
    $TRICACHE_ROOT/scripts/livegraph_cp_dataset_mmap.sh 30
    $TRICACHE_ROOT/scripts/livegraph_exec_mmap.sh 30 &
    sleep 10000000
    sleep 10
    java -Xms32g -Xmx32g -cp $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/target/livegraph-0.3.5-SNAPSHOT.jar com.ldbc.driver.Client -P $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/interactive-benchmark-mmap-sf30-slow.properties --results_dir results_small/livegraph_mmap
    sudo killall -9 snb_server
done
