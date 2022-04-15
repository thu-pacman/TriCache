#!/bin/bash
$TRICACHE_ROOT/scripts/build.sh
$TRICACHE_ROOT/scripts/build_ligra.sh

$TRICACHE_ROOT/scripts/setup_spdk.sh 64
$TRICACHE_ROOT/scripts/run_terasort_breakdown.sh
$TRICACHE_ROOT/scripts/run_ligra_breakdown.sh
$TRICACHE_ROOT/scripts/reset_spdk.sh
