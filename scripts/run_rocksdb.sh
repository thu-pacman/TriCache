#!/bin/bash
function run_rocksdb_cache
{
    $TRICACHE_ROOT/scripts/build_rocksdb.sh
    $TRICACHE_ROOT/scripts/setup_spdk.sh 260
    $TRICACHE_ROOT/scripts/run_rocksdb_cache.sh
    $TRICACHE_ROOT/scripts/reset_spdk.sh
}

function run_rocksdb_block
{
    $TRICACHE_ROOT/scripts/build_rocksdb.sh
    $TRICACHE_ROOT/scripts/mkraid.sh
    $TRICACHE_ROOT/scripts/run_rocksdb_block.sh
    $TRICACHE_ROOT/scripts/rmraid.sh
}

function run_rocksdb_mmap
{
    $TRICACHE_ROOT/scripts/build_rocksdb.sh
    $TRICACHE_ROOT/scripts/mkraid.sh
    $TRICACHE_ROOT/scripts/run_rocksdb_mmap.sh
    $TRICACHE_ROOT/scripts/rmraid.sh
}

case $1 in
    rocksdb_cache)
        run_rocksdb_cache
        ;;

    rocksdb_block)
        run_rocksdb_block
        ;;

    rocksdb_mmap)
        run_rocksdb_mmap
        ;;

    *)
        run_rocksdb_cache
        run_rocksdb_block
        run_rocksdb_mmap
        ;;
esac
