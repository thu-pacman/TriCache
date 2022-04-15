#!/bin/bash
function run_terasort_cache
{
    $TRICACHE_ROOT/scripts/setup_spdk.sh 362
    $TRICACHE_ROOT/scripts/run_terasort_cache.sh
    $TRICACHE_ROOT/scripts/reset_spdk.sh
}

function run_terasort_swap
{
    $TRICACHE_ROOT/scripts/mkswap.sh
    $TRICACHE_ROOT/scripts/run_terasort_swap.sh
    $TRICACHE_ROOT/scripts/rmswap.sh
}

function run_terasort_swap_slow
{
    $TRICACHE_ROOT/scripts/mkswap.sh
    $TRICACHE_ROOT/scripts/run_terasort_swap_slow.sh
    $TRICACHE_ROOT/scripts/rmswap.sh
}

function run_terasort_spark
{
    $TRICACHE_ROOT/scripts/build_terasort_spark.sh
    $TRICACHE_ROOT/scripts/mkxfs.sh
    $TRICACHE_ROOT/scripts/run_terasort_spark.sh
    sleep 60
    $TRICACHE_ROOT/scripts/rmxfs.sh
}

case $1 in
    terasort_cache)
        run_terasort_cache
        ;;

    terasort_swap)
        run_terasort_swap
        ;;

    terasort_swap_slow)
        run_terasort_swap_slow
        ;;

    terasort_spark)
        run_terasort_spark
        ;;

    *)
        run_terasort_cache
        run_terasort_swap
        run_terasort_spark
        ;;
esac
