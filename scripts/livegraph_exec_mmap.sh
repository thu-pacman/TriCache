echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs
export OMP_NUM_THREADS=$LIVEGRAPH_NUM_CLIENTS
TZ=UTC $TRICACHE_ROOT/ae-projects/LiveGraph-snb/build-mmap/snb_server /mnt/raid/temp/livegraph_$1g dummy 9090
