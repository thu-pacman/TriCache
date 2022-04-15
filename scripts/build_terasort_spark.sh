#!/bin/bash
pushd $TRICACHE_ROOT/ae-projects/terasort_spark
./dependency.sh
./config.sh
./build.sh
popd
