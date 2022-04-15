#!/bin/bash
function run_livegraph_cache
{
    $TRICACHE_ROOT/scripts/build_livegraph.sh
    $TRICACHE_ROOT/scripts/setup_spdk.sh 260
    $TRICACHE_ROOT/scripts/run_livegraph_cache.sh
    $TRICACHE_ROOT/scripts/reset_spdk.sh
}

function run_livegraph_mmap
{
    $TRICACHE_ROOT/scripts/build_livegraph.sh
    $TRICACHE_ROOT/scripts/mkraid.sh
    $TRICACHE_ROOT/scripts/run_livegraph_mmap.sh
    sleep 60
    $TRICACHE_ROOT/scripts/rmraid.sh
}

case $1 in
    livegraph_cache)
        run_livegraph_cache
        ;;

    livegraph_mmap)
        run_livegraph_mmap
        ;;

    *)
        run_livegraph_cache
        run_livegraph_mmap
        ;;
esac
