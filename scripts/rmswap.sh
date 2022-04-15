#!/bin/bash
source $TRICACHE_ROOT/scripts/config.sh

for i in ${!SSDArray[@]}; do
    id="${SSDArray[$i]}"
    path="/dev/disk/by-id/$id"
    name=$(basename $(readlink $path))
    numa=$(cat /sys/block/$name/device/numa_node)

    echo $i $id $path $name $numa

    sudo swapoff $path
done
