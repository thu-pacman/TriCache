echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs
export OMP_PROC_BIND=true
export CACHE_VIRT_SIZE=$(expr 32 \* 1024 \* 1024 \* 1024 \* 1024)
source $TRICACHE_ROOT/scripts/config.sh
export CACHE_CONFIG=$CACHE_16_SERVER_CONFIG
export CACHE_NUM_CLIENTS=$(expr $LIVEGRAPH_NUM_CLIENTS + 1)
export OMP_NUM_THREADS=32

export CACHE_MALLOC_THRESHOLD=$(expr 64 \* 1024 \* 1024 \* 1024)

TZ=UTC sudo -E LD_LIBRARY_PATH="$LD_LIBRARY_PATH" numactl -C !$CACHE_16_SERVER_CORES \
    stdbuf -oL /usr/bin/time -v $TRICACHE_ROOT/ae-projects/LiveGraph-snb/build-cache/snb_server /mnt/data/TriCache/temp/livegraph_$1g dummy 9090
