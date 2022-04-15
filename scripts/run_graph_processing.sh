#!/bin/bash
function run_ligra_cache
{
    $TRICACHE_ROOT/scripts/build_ligra.sh
    $TRICACHE_ROOT/scripts/setup_spdk.sh 362
    $TRICACHE_ROOT/scripts/run_ligra_cache.sh
    $TRICACHE_ROOT/scripts/reset_spdk.sh
}

function run_ligra_swap
{
    $TRICACHE_ROOT/scripts/build_ligra.sh
    $TRICACHE_ROOT/scripts/mkswap.sh
    $TRICACHE_ROOT/scripts/run_ligra_swap.sh
    $TRICACHE_ROOT/scripts/rmswap.sh
}

function run_ligra_swap_slow
{
    $TRICACHE_ROOT/scripts/build_ligra.sh
    $TRICACHE_ROOT/scripts/mkswap.sh
    $TRICACHE_ROOT/scripts/run_ligra_swap_slow.sh
    $TRICACHE_ROOT/scripts/rmswap.sh
}

function run_flashgraph
{
    $TRICACHE_ROOT/scripts/build_flashgraph.sh
    $TRICACHE_ROOT/scripts/mkxfs.sh
    $TRICACHE_ROOT/scripts/load_to_flashgraph.sh
    $TRICACHE_ROOT/scripts/run_flashgraph.sh
    $TRICACHE_ROOT/scripts/rmxfs.sh
}

case $1 in
    ligra_cache)
        run_ligra_cache
        ;;

    ligra_swap)
        run_ligra_swap
        ;;

    ligra_swap_slow)
        run_ligra_swap_slow
        ;;

    flashgraph)
        run_flashgraph
        ;;

    *)
        run_ligra_cache
        run_ligra_swap
        run_flashgraph
        ;;
esac
