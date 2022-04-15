#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

export OMP_PROC_BIND=true

function set_schedule {
	SCHEDULE=$1
	export OMP_SCHEDULE="${SCHEDULE}"
}

mkdir -p results_ligra_swap

for threads in 256
do
    export OMP_NUM_THREADS=$threads

    for i in 512
    do
        echo $(expr \( $i \) \* 1024 \* 1024 \* 1024) | sudo tee /sys/fs/cgroup/limit/memory.max
        set_schedule "dynamic,256"
        stdbuf -oL /usr/bin/time -v numactl -i all $TRICACHE_ROOT/ae-projects/ligra/apps/BFS -r 5 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_ligra_swap/BFS_uk-2014_${i}G_${threads}.txt
        set_schedule "guided"
        stdbuf -oL /usr/bin/time -v numactl -i all $TRICACHE_ROOT/ae-projects/ligra/apps/PageRankDelta -maxiters 20 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_ligra_swap/PageRankDelta_uk-2014_${i}G_${threads}.txt
        stdbuf -oL /usr/bin/time -v numactl -i all $TRICACHE_ROOT/ae-projects/ligra/apps/Components -s -b /mnt/data/TriCache/ligra/uk-2014-sym 2>&1 | tee results_ligra_swap/CC_uk-2014_${i}G_${threads}.txt
    done
done

for threads in 256
do
    export OMP_NUM_THREADS=$threads

    for i in 256
    do
        echo $(expr \( $i \) \* 1024 \* 1024 \* 1024) | sudo tee /sys/fs/cgroup/limit/memory.max
        set_schedule "dynamic,64"
        stdbuf -oL /usr/bin/time -v numactl -i all $TRICACHE_ROOT/ae-projects/ligra/apps/BFS -r 5 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_ligra_swap/BFS_uk-2014_${i}G_${threads}.txt
        set_schedule "static,64"
        stdbuf -oL /usr/bin/time -v numactl -i all $TRICACHE_ROOT/ae-projects/ligra/apps/PageRankDelta -maxiters 20 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_ligra_swap/PageRankDelta_uk-2014_${i}G_${threads}.txt
        set_schedule "dynamic,64"
        stdbuf -oL /usr/bin/time -v numactl -i all $TRICACHE_ROOT/ae-projects/ligra/apps/Components -rounds 1 -s -b /mnt/data/TriCache/ligra/uk-2014-sym 2>&1 | tee results_ligra_swap/CC_uk-2014_${i}G_${threads}.txt
    done
done

set_schedule "dynamic,64"
for threads in 256
do
    export OMP_NUM_THREADS=$threads

    for i in 128 #64 32 16
    do
        echo $(expr \( $i \) \* 1024 \* 1024 \* 1024) | sudo tee /sys/fs/cgroup/limit/memory.max
        stdbuf -oL /usr/bin/time -v numactl -i all $TRICACHE_ROOT/ae-projects/ligra/apps/BFS -rounds 1 -r 5 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_ligra_swap/BFS_uk-2014_${i}G_${threads}.txt
        stdbuf -oL /usr/bin/time -v numactl -i all $TRICACHE_ROOT/ae-projects/ligra/apps/PageRankDelta -rounds 1 -maxiters 20 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_ligra_swap/PageRankDelta_uk-2014_${i}G_${threads}.txt
        stdbuf -oL /usr/bin/time -v numactl -i all $TRICACHE_ROOT/ae-projects/ligra/apps/Components -rounds 1 -s -b /mnt/data/TriCache/ligra/uk-2014-sym 2>&1 | tee results_ligra_swap/CC_uk-2014_${i}G_${threads}.txt
    done
done
