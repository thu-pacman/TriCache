#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

export OMP_PROC_BIND=true
export CACHE_VIRT_SIZE=$(expr 32 \* 1024 \* 1024 \* 1024 \* 1024  )
source $TRICACHE_ROOT/scripts/config.sh
export CACHE_CONFIG=$CACHE_16_SERVER_CONFIG
export CACHE_DISABLE_PARALLEL_READ_WRITE=true

function set_schedule {
	SCHEDULE=$1
	export OMP_SCHEDULE="${SCHEDULE}"
}

mkdir -p results_breakdown

export CACHE_MALLOC_THRESHOLD=$(expr 4294967296 \* 2)
set_schedule "dynamic,64"
for threads in 960
do
    export CACHE_NUM_CLIENTS=$threads
    export OMP_NUM_THREADS=$threads

    for i in 64
    do
        echo $(expr \( $i \/ 8 + 27 \) \* 1024 \* 1024 \* 1024) | sudo tee /sys/fs/cgroup/limit/memory.max
        export CACHE_PHY_SIZE=$(expr \( $i - $i \/ 8 - 27 \) \* 1024 \* 1024 \* 1024 )
        echo $CACHE_PHY_SIZE $CACHE_VIRT_SIZE

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/ae-projects/ligra/apps/PageRankDelta-cache -maxiters 20 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_breakdown/PageRank_uk-2014.txt

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/ae-projects/ligra/apps/PageRankDelta-cache-profile -maxiters 20 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_breakdown/PageRank_uk-2014_profile.txt

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/ae-projects/ligra/apps/PageRankDelta-cache-disable-direct -maxiters 20 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_breakdown/PageRank_uk-2014_disable_direct.txt

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/ae-projects/ligra/apps/PageRankDelta-cache-disable-private -maxiters 20 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_breakdown/PageRank_uk-2014_disable_private.txt

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/ae-projects/ligra/apps/PageRankDelta-cache-disable-direct-private -maxiters 20 -rounds 1 -b /mnt/data/TriCache/ligra/uk-2014 2>&1 | tee results_breakdown/PageRank_uk-2014_disable_direct_private.txt

    done
done

