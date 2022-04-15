#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

mkdir -p results_small

function update_config
{
    sed -i "s:root_conf=.*:root_conf=$TRICACHE_ROOT/ae-projects/FlashX/flash-graph/conf/data_files.txt:" $1
}

for i in 128
do
    echo $(expr $i \* 1024 \* 1024 \* 1024) | sudo tee /sys/fs/cgroup/limit/memory.max

    update_config $TRICACHE_ROOT/ae-projects/FlashX/flash-graph/conf/run_test_${i}G.txt

	stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/ae-projects/FlashX/build/flash-graph/test-algs/test_algs \
        $TRICACHE_ROOT/ae-projects/FlashX/flash-graph/conf/run_test_${i}G.txt uk-2014.adj uk-2014.index \
        pagerank -i 20 2>&1 | tee results_small/PageRank_flashgraph.txt

done
