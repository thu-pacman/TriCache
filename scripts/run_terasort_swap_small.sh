#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

export OMP_PROC_BIND=true

mkdir -p results_small

for THREADS in 256
do
    export OMP_NUM_THREADS=$THREADS

    for MEM_GB in 64
    do
        export MEMORY=$(expr $MEM_GB \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        stdbuf -oL /usr/bin/time -v numactl -i all \
           $TRICACHE_ROOT/build/terasort_gnu_orig /mnt/data/TriCache/terasort/terasort-150G $THREADS \
           2>&1 | tee results_small/terasort_gnu_swap.txt

        stdbuf -oL /usr/bin/time -v numactl -i all \
            $TRICACHE_ROOT/build/terasort_manual_orig /mnt/data/TriCache/terasort/terasort-150G $THREADS \
            2>&1 | tee results_small/terasort_manual_swap.txt
    done
done
