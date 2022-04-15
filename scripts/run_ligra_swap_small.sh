#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

export OMP_PROC_BIND=true

function set_schedule {
	SCHEDULE=$1
	export OMP_SCHEDULE="${SCHEDULE}"
}

mkdir -p results_small

set_schedule "dynamic,64"
for threads in 256
do
    export OMP_NUM_THREADS=$threads

    for i in 128
    do
        echo $(expr \( $i \) \* 1024 \* 1024 \* 1024) | sudo tee /sys/fs/cgroup/limit/memory.max
        stdbuf -oL /usr/bin/time -v numactl -i all $TRICACHE_ROOT/ae-projects/ligra/apps/PageRankDelta -rounds 1 -maxiters 20 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_small/PageRank_ligra_swap.txt
    done
done
