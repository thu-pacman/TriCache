#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

export OMP_PROC_BIND=true

mkdir -p results_terasort_swap

for THREADS in 256
do
    export OMP_NUM_THREADS=$THREADS

    for MEM_GB in 512 256 128 64 32 16
    do
        export MEMORY=$(expr $MEM_GB \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        stdbuf -oL /usr/bin/time -v numactl -i all \
            $TRICACHE_ROOT/build/terasort_manual_orig /mnt/data/TriCache/terasort/terasort-400G $THREADS \
            2>&1 | tee results_terasort_swap/terasort_manual_400G_${MEM_GB}G_${THREADS}.txt
    done
done

for THREADS in 256
do
    export OMP_NUM_THREADS=$THREADS

    for MEM_GB in 512 256 128 64 32 16
    do
        export MEMORY=$(expr $MEM_GB \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        stdbuf -oL /usr/bin/time -v numactl -i all \
            $TRICACHE_ROOT/build/terasort_gnu_orig /mnt/data/TriCache/terasort/terasort-400G $THREADS \
            2>&1 | tee results_terasort_swap/terasort_gnu_400G_${MEM_GB}G_${THREADS}.txt
    done
done
