#/bin/bash
pushd $TRICACHE_ROOT/ae-projects/ligra/apps
OPENMP=1 LONG=1 make -j
popd
