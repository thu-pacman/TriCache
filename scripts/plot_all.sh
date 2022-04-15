#!/bin/bash
case $1 in
    graph_processing)
        python3 $TRICACHE_ROOT/ae-plot-scripts/draw_graph_processing.py $2
        ;;

    rocksdb)
        python3 $TRICACHE_ROOT/ae-plot-scripts/draw_rocksdb.py $2
        ;;

    terasort)
        python3 $TRICACHE_ROOT/ae-plot-scripts/draw_terasort.py $2
        ;;

    livegraph)
        python3 $TRICACHE_ROOT/ae-plot-scripts/draw_livegraph.py $2
        ;;

    microbenchmark)
        python3 $TRICACHE_ROOT/ae-plot-scripts/draw_heatmap.py $2
        ;;

    breakdown)
        python3 $TRICACHE_ROOT/ae-plot-scripts/calc_breakdown.py $2
        ;;

    small)
        python3 $TRICACHE_ROOT/ae-plot-scripts/calc_small.py $2
        ;;

    *)
        echo ----------------
        echo graph_processing
        echo
        python3 $TRICACHE_ROOT/ae-plot-scripts/draw_graph_processing.py $1
        echo ----------------
        echo rocksdb
        echo
        python3 $TRICACHE_ROOT/ae-plot-scripts/draw_rocksdb.py $1
        echo ----------------
        echo terasort
        echo
        python3 $TRICACHE_ROOT/ae-plot-scripts/draw_terasort.py $1
        echo ----------------
        echo livegraph
        echo
        python3 $TRICACHE_ROOT/ae-plot-scripts/draw_livegraph.py $1
        echo ----------------
        echo microbenchmark
        echo
        python3 $TRICACHE_ROOT/ae-plot-scripts/draw_heatmap.py $1
        echo ----------------
        echo breakdown
        echo
        python3 $TRICACHE_ROOT/ae-plot-scripts/calc_breakdown.py $1
        echo ----------------
        ;;
esac
