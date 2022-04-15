#!/bin/bash
$TRICACHE_ROOT/scripts/build.sh
case $1 in
    graph_processing)
        $TRICACHE_ROOT/scripts/run_graph_processing.sh $2
        ;;

    rocksdb)
        $TRICACHE_ROOT/scripts/run_rocksdb.sh $2
        ;;

    terasort)
        $TRICACHE_ROOT/scripts/run_terasort.sh $2
        ;;

    livegraph)
        $TRICACHE_ROOT/scripts/run_livegraph.sh $2
        ;;

    breakdown)
        $TRICACHE_ROOT/scripts/run_breakdown.sh $2
        ;;

    microbenchmark)
        $TRICACHE_ROOT/scripts/run_microbenchmark.sh $2
        ;;

    *)
        $TRICACHE_ROOT/scripts/run_graph_processing.sh
        $TRICACHE_ROOT/scripts/run_rocksdb.sh
        $TRICACHE_ROOT/scripts/run_terasort.sh
        $TRICACHE_ROOT/scripts/run_livegraph.sh
        $TRICACHE_ROOT/scripts/run_microbenchmark.sh
        $TRICACHE_ROOT/scripts/run_breakdown.sh
        ;;
esac
