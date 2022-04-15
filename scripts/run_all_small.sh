#!/bin/bash
$TRICACHE_ROOT/scripts/build.sh

$TRICACHE_ROOT/scripts/build_ligra.sh
$TRICACHE_ROOT/scripts/setup_spdk.sh 128
$TRICACHE_ROOT/scripts/run_ligra_cache_small.sh
$TRICACHE_ROOT/scripts/reset_spdk.sh

$TRICACHE_ROOT/scripts/build_ligra.sh
$TRICACHE_ROOT/scripts/mkswap.sh
$TRICACHE_ROOT/scripts/run_ligra_swap_small.sh
$TRICACHE_ROOT/scripts/rmswap.sh

$TRICACHE_ROOT/scripts/build_flashgraph.sh
$TRICACHE_ROOT/scripts/mkxfs.sh
$TRICACHE_ROOT/scripts/load_to_flashgraph.sh
$TRICACHE_ROOT/scripts/run_flashgraph_small.sh
$TRICACHE_ROOT/scripts/rmxfs.sh

$TRICACHE_ROOT/scripts/build_rocksdb.sh
$TRICACHE_ROOT/scripts/setup_spdk.sh 64
$TRICACHE_ROOT/scripts/run_rocksdb_cache_small.sh
$TRICACHE_ROOT/scripts/reset_spdk.sh

$TRICACHE_ROOT/scripts/build_rocksdb.sh
$TRICACHE_ROOT/scripts/mkraid.sh
$TRICACHE_ROOT/scripts/run_rocksdb_block_small.sh
$TRICACHE_ROOT/scripts/run_rocksdb_mmap_small.sh
$TRICACHE_ROOT/scripts/rmraid.sh

$TRICACHE_ROOT/scripts/setup_spdk.sh 64
$TRICACHE_ROOT/scripts/run_terasort_cache_small.sh
$TRICACHE_ROOT/scripts/reset_spdk.sh

$TRICACHE_ROOT/scripts/mkswap.sh
$TRICACHE_ROOT/scripts/run_terasort_swap_small.sh
$TRICACHE_ROOT/scripts/rmswap.sh

$TRICACHE_ROOT/scripts/build_terasort_spark.sh
$TRICACHE_ROOT/scripts/mkxfs.sh
$TRICACHE_ROOT/scripts/run_terasort_spark_small.sh
sleep 60
$TRICACHE_ROOT/scripts/rmxfs.sh

$TRICACHE_ROOT/scripts/build_livegraph.sh
$TRICACHE_ROOT/scripts/setup_spdk.sh 32
$TRICACHE_ROOT/scripts/run_livegraph_cache_small.sh
$TRICACHE_ROOT/scripts/reset_spdk.sh

$TRICACHE_ROOT/scripts/build_livegraph.sh
$TRICACHE_ROOT/scripts/mkraid.sh
$TRICACHE_ROOT/scripts/run_livegraph_mmap_small.sh
sleep 60
$TRICACHE_ROOT/scripts/rmraid.sh

$TRICACHE_ROOT/scripts/setup_spdk.sh 260
$TRICACHE_ROOT/scripts/run_microbenchmark_cache_small.sh
$TRICACHE_ROOT/scripts/reset_spdk.sh

$TRICACHE_ROOT/scripts/mkraid.sh
$TRICACHE_ROOT/scripts/run_microbenchmark_mmap_small.sh
$TRICACHE_ROOT/scripts/rmraid.sh
