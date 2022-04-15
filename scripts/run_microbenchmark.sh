#!/bin/bash
function run_microbenchmark_cache
{
    $TRICACHE_ROOT/scripts/setup_spdk.sh 260
    $TRICACHE_ROOT/scripts/run_microbenchmark_cache.sh
    $TRICACHE_ROOT/scripts/reset_spdk.sh
}

function run_microbenchmark_mmap
{
    $TRICACHE_ROOT/scripts/mkraid.sh
    $TRICACHE_ROOT/scripts/run_microbenchmark_mmap.sh
    $TRICACHE_ROOT/scripts/rmraid.sh
}

case $1 in
    microbenchmark_cache)
        run_microbenchmark_cache
        ;;

    microbenchmark_mmap)
        run_microbenchmark_mmap
        ;;

    *)
        run_microbenchmark_cache
        run_microbenchmark_mmap
        ;;
esac
