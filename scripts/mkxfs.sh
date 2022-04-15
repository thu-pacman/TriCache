#!/bin/bash
source $TRICACHE_ROOT/scripts/config.sh

for i in ${!SSDArray[@]}; do
    id="${SSDArray[$i]}"
    path="/dev/disk/by-id/$id"
    name=$(basename $(readlink $path))
    numa=$(cat /sys/block/$name/device/numa_node)

    echo $i $id $path $name $numa

    sudo mkfs.xfs -f $path
    sudo mkdir -p /mnt/ssd${i}
    sudo mount -t xfs $path /mnt/ssd${i}
    sudo mkdir /mnt/ssd${i}/shm
    sudo chmod a+rw /mnt/ssd${i}/shm
done
