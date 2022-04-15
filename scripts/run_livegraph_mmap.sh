#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh

export LIVEGRAPH_NUM_CLIENTS=512

mkdir -p results_livegraph_mmap

for i in 256 128 64
do
    echo ${i}G | sudo tee /sys/fs/cgroup/limit/memory.max
    $TRICACHE_ROOT/scripts/livegraph_cp_dataset_mmap.sh 30
    $TRICACHE_ROOT/scripts/livegraph_exec_mmap.sh 30 &
    sleep 10000000
    sleep 10
    java -Xms32g -Xmx32g -cp $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/target/livegraph-0.3.5-SNAPSHOT.jar com.ldbc.driver.Client -P $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/interactive-benchmark-mmap-sf30.properties --results_dir results_livegraph_mmap/results_all_16_sf30_mmap_${i}G
    sudo killall -9 snb_server
done

for i in 32 16
do
    echo ${i}G | sudo tee /sys/fs/cgroup/limit/memory.max
    $TRICACHE_ROOT/scripts/livegraph_cp_dataset_mmap.sh 30
    $TRICACHE_ROOT/scripts/livegraph_exec_mmap.sh 30 &
    sleep 10000000
    sleep 10
    java -Xms32g -Xmx32g -cp $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/target/livegraph-0.3.5-SNAPSHOT.jar com.ldbc.driver.Client -P $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/interactive-benchmark-mmap-sf30-slow.properties --results_dir results_livegraph_mmap/results_all_16_sf30_mmap_${i}G
    sudo killall -9 snb_server
done

for i in 256 128
do
    echo ${i}G | sudo tee /sys/fs/cgroup/limit/memory.max
    $TRICACHE_ROOT/scripts/livegraph_cp_dataset_mmap.sh 100
    $TRICACHE_ROOT/scripts/livegraph_exec_mmap.sh 100 &
    sleep 10000000
    sleep 10
    java -Xms32g -Xmx32g -cp $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/target/livegraph-0.3.5-SNAPSHOT.jar com.ldbc.driver.Client -P $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/interactive-benchmark-mmap-sf100.properties --results_dir results_livegraph_mmap/results_all_16_sf100_mmap_${i}G
    sudo killall -9 snb_server
done

for i in 64 32 16
do
    echo ${i}G | sudo tee /sys/fs/cgroup/limit/memory.max
    $TRICACHE_ROOT/scripts/livegraph_cp_dataset_mmap.sh 100
    $TRICACHE_ROOT/scripts/livegraph_exec_mmap.sh 100 &
    sleep 10000000
    sleep 10
    java -Xms32g -Xmx32g -cp $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/target/livegraph-0.3.5-SNAPSHOT.jar com.ldbc.driver.Client -P $TRICACHE_ROOT/ae-projects/ldbc_snb_implementations/livegraph/interactive-benchmark-mmap-sf100-slow.properties --results_dir results_livegraph_mmap/results_all_16_sf100_mmap_${i}G
    sudo killall -9 snb_server
done

