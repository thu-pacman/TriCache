#!/bin/bash
vmemory=256
export CACHE_VIRT_SIZE=$(expr $vmemory \* 1024 \* 1024 \* 1024)

# will has better ooc performance
# export OMP_PROC_BIND=true

for i in $(seq 128 255)
do
    echo 0 | sudo tee /sys/devices/system/cpu/cpu$i/online
done

pushd $TRICACHE_ROOT/ae-projects/FastMap
sudo make -j
popd

mkdir -p results_microbenchmark_fastmap

function fastmap_up
{
    pushd $TRICACHE_ROOT/ae-projects/FastMap/scripts
    export CACHE_PAGES=$(echo "print(int($CACHE_VIRT_SIZE / 4096 * $1))" | python3)
    sudo numactl -i all ./load-it-blkdev.sh $CACHE_PAGES
    popd
}

function fastmap_down
{
    pushd $TRICACHE_ROOT/ae-projects/FastMap/scripts
    sudo ./unload-it-blkdev.sh
    popd
}

case "$1" in
    1.0)
        export i=1.0
        export OMP_NUM_THREADS=128
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            5000000 \
            $vmemory \
            $i \
            0 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    0.9)
        export i=0.9
        export OMP_NUM_THREADS=1024
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            200000 \
            $vmemory \
            $i \
            0 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    0.8)
        export i=0.8
        export OMP_NUM_THREADS=512
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            200000 \
            $vmemory \
            $i \
            0 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    0.7)

        export i=0.7
        export OMP_NUM_THREADS=1024
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            200000 \
            $vmemory \
            $i \
            0 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    0.6)

        export i=0.6
        export OMP_NUM_THREADS=2048
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            100000 \
            $vmemory \
            $i \
            0 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    0.5)

        export i=0.5
        export OMP_NUM_THREADS=2048
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            50000 \
            $vmemory \
            $i \
            0 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    0.4)

        export i=0.4
        export OMP_NUM_THREADS=1024
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            50000 \
            $vmemory \
            $i \
            0 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    0.3)

        export i=0.3
        export OMP_NUM_THREADS=2048
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            50000 \
            $vmemory \
            $i \
            0 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    0.2)

        export i=0.2
        export OMP_NUM_THREADS=1024
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            50000 \
            $vmemory \
            $i \
            0 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    0.1)

        export i=0.1
        export OMP_NUM_THREADS=1024
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            50000 \
            $vmemory \
            $i \
            0 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    allpage_1.0)
        export i=1.0
        export OMP_NUM_THREADS=128
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            500000 \
            $vmemory \
            $i \
            1 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_allpage_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    allpage_0.9)
        export i=0.9
        export OMP_NUM_THREADS=2048
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            200000 \
            $vmemory \
            $i \
            1 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_allpage_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    allpage_0.8)
        export i=0.8
        export OMP_NUM_THREADS=1024
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            200000 \
            $vmemory \
            $i \
            1 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_allpage_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    allpage_0.7)

        export i=0.7
        export OMP_NUM_THREADS=2048
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            200000 \
            $vmemory \
            $i \
            1 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_allpage_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    allpage_0.6)

        export i=0.6
        export OMP_NUM_THREADS=2048
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            100000 \
            $vmemory \
            $i \
            1 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_allpage_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    allpage_0.5)

        export i=0.5
        export OMP_NUM_THREADS=2048
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            50000 \
            $vmemory \
            $i \
            1 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_allpage_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    allpage_0.4)

        export i=0.4
        export OMP_NUM_THREADS=2048
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            50000 \
            $vmemory \
            $i \
            1 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_allpage_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    allpage_0.3)

        export i=0.3
        export OMP_NUM_THREADS=2048
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            50000 \
            $vmemory \
            $i \
            1 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_allpage_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    allpage_0.2)
        export i=0.2
        export OMP_NUM_THREADS=1024
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            50000 \
            $vmemory \
            $i \
            1 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_allpage_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    allpage_0.1)
        export i=0.1
        export OMP_NUM_THREADS=1024
        fastmap_up $i
        sudo -E stdbuf -oL numactl -i all $TRICACHE_ROOT/build/bench_hitrate_fastmap \
            50000 \
            $vmemory \
            $i \
            1 \
            /dev/dmap/dmap1 \
            2>&1 | tee results_microbenchmark_fastmap/micro_allpage_${i}_$OMP_NUM_THREADS.log
        fastmap_down
        ;;

    finish)
        sudo reboot
        ;;
esac
