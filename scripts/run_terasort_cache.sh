#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh
echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

export OMP_PROC_BIND=true
export CACHE_VIRT_SIZE=$(expr 32 \* 1024 \* 1024 \* 1024 \* 1024)

export CACHE_MALLOC_THRESHOLD=$(expr 32 \* 1024 \* 1024 \* 1024)

export CACHE_DISABLE_PARALLEL_READ_WRITE=true

source $TRICACHE_ROOT/scripts/config.sh
export CACHE_CONFIG=$CACHE_16_SERVER_CONFIG

mkdir -p results_terasort_cache

for THREADS in 240
do
    export CACHE_NUM_CLIENTS=$(expr $THREADS \+ 16 \* 4)
    export OMP_NUM_THREADS=$THREADS

    for MEM_GB in 512 256 128 64 32 16
    do
        export CACHE_PHY_SIZE=$(expr \( $MEM_GB \/ 8 \* 5 \) \* 1024 \* 1024 \* 1024)
        export MEMORY=$(expr \( $MEM_GB \/ 8 \* 3 \) \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/build/terasort_manual /mnt/data/TriCache/terasort/terasort-150G $THREADS \
            2>&1 | tee results_terasort_cache/terasort_manual_150G_${MEM_GB}G_${THREADS}.txt
    done
done

for THREADS in 240
do
    export CACHE_NUM_CLIENTS=$(expr $THREADS \+ 16 \* 4)
    export OMP_NUM_THREADS=$THREADS

    for MEM_GB in 512 256 128 64 32 16
    do
        export CACHE_PHY_SIZE=$(expr \( $MEM_GB \/ 8 \* 5 \) \* 1024 \* 1024 \* 1024)
        export MEMORY=$(expr \( $MEM_GB \/ 8 \* 3 \) \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/build/terasort_manual /mnt/data/TriCache/terasort/terasort-400G $THREADS \
            2>&1 | tee results_terasort_cache/terasort_manual_400G_${MEM_GB}G_${THREADS}.txt
    done
done

export CACHE_MALLOC_THRESHOLD=$(expr 128 \* 1024 \* 1024)

for THREADS in 240
do
    export CACHE_NUM_CLIENTS=$(expr $THREADS \+ 16 \* 4)
    export OMP_NUM_THREADS=$THREADS

    for MEM_GB in 512
    do
        export CACHE_PHY_SIZE=$(expr \( $MEM_GB \/ 8 \* 5 \) \* 1024 \* 1024 \* 1024)
        export MEMORY=$(expr \( $MEM_GB \/ 8 \* 3 \) \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/build/terasort_gnu /mnt/data/TriCache/terasort/terasort-150G $THREADS \
            2>&1 | tee results_terasort_cache/terasort_gnu_150G_${MEM_GB}G_${THREADS}.txt
    done
done

for THREADS in 480
do
    export CACHE_NUM_CLIENTS=$(expr $THREADS \+ 16 \* 4)
    export OMP_NUM_THREADS=$THREADS

    for MEM_GB in 256 128 64
    do
        export CACHE_PHY_SIZE=$(expr \( $MEM_GB \/ 8 \* 7 \) \* 1024 \* 1024 \* 1024)
        export MEMORY=$(expr \( $MEM_GB \/ 8 \* 1 \) \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/build/terasort_gnu /mnt/data/TriCache/terasort/terasort-150G $THREADS \
            2>&1 | tee results_terasort_cache/terasort_gnu_150G_${MEM_GB}G_${THREADS}.txt
    done
done

for THREADS in 960
do
    export CACHE_NUM_CLIENTS=$(expr $THREADS \+ 16 \* 4)
    export OMP_NUM_THREADS=$THREADS

    for MEM_GB in 32
    do
        export CACHE_PHY_SIZE=$(expr \( $MEM_GB \/ 8 \* 7 \) \* 1024 \* 1024 \* 1024)
        export MEMORY=$(expr \( $MEM_GB \/ 8 \* 1 \) \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/build/terasort_gnu /mnt/data/TriCache/terasort/terasort-150G $THREADS \
            2>&1 | tee results_terasort_cache/terasort_gnu_150G_${MEM_GB}G_${THREADS}.txt
    done

    for MEM_GB in 16
    do
        export CACHE_PHY_SIZE=$(expr \( $MEM_GB \/ 8 \* 6 \) \* 1024 \* 1024 \* 1024)
        export MEMORY=$(expr \( $MEM_GB \/ 8 \* 2 \) \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/build/terasort_gnu /mnt/data/TriCache/terasort/terasort-150G $THREADS \
            2>&1 | tee results_terasort_cache/terasort_gnu_150G_${MEM_GB}G_${THREADS}.txt
    done
done

for THREADS in 480
do
    export CACHE_NUM_CLIENTS=$(expr $THREADS \+ 16 \* 4)
    export OMP_NUM_THREADS=$THREADS

    for MEM_GB in 512 256 128
    do
        export CACHE_PHY_SIZE=$(expr \( $MEM_GB \/ 8 \* 5 \) \* 1024 \* 1024 \* 1024)
        export MEMORY=$(expr \( $MEM_GB \/ 8 \* 3 \) \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/build/terasort_gnu /mnt/data/TriCache/terasort/terasort-400G $THREADS \
            2>&1 | tee results_terasort_cache/terasort_gnu_400G_${MEM_GB}G_${THREADS}.txt
    done
done

for THREADS in 960
do
    export CACHE_NUM_CLIENTS=$(expr $THREADS \+ 16 \* 4)
    export OMP_NUM_THREADS=$THREADS

    for MEM_GB in 64 32 16
    do
        export CACHE_PHY_SIZE=$(expr \( $MEM_GB \/ 8 \* 5 \) \* 1024 \* 1024 \* 1024)
        export MEMORY=$(expr \( $MEM_GB \/ 8 \* 3 \) \* 1024 \* 1024 \* 1024)
        echo $MEMORY | sudo tee /sys/fs/cgroup/limit/memory.max

        sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -i all -C !$CACHE_16_SERVER_CORES \
            stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/build/terasort_gnu /mnt/data/TriCache/terasort/terasort-400G $THREADS \
            2>&1 | tee results_terasort_cache/terasort_gnu_400G_${MEM_GB}G_${THREADS}.txt
    done
done
