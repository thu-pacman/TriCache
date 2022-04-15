#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

vmemory=256
export OMP_NUM_THREADS=256
export OMP_PROC_BIND=true

mkdir -p results_small

dd if=/dev/zero of=/mnt/raid/temp/data.mmap bs=1G count=256
sudo $TRICACHE_ROOT/scripts/clearcache.sh

for i in 0.9
do
    echo "print(int($vmemory * 1024 * 1024 * 1024 * $i) + 2 * 1024 * 1024 * 1024)" | python3 | sudo tee /sys/fs/cgroup/limit/memory.max

    stdbuf -oL numactl --interleave all $TRICACHE_ROOT/build/bench_hitrate_mmap \
        100000 \
        $vmemory \
        $i \
        0 \
        /mnt/raid/temp/data.mmap \
        2>&1 | tee results_small/micro_mmap.txt

    stdbuf -oL numactl --interleave all $TRICACHE_ROOT/build/bench_hitrate_mmap \
        50000 \
        $vmemory \
        $i \
        1 \
        /mnt/raid/temp/data.mmap \
        2>&1 | tee results_small/micro_allpage_mmap.txt
done
