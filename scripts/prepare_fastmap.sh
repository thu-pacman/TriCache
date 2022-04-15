#!/bin/bash
source $TRICACHE_ROOT/scripts/config.sh

dev=""
for i in ${!SSDArray[@]}; do
    id="${SSDArray[$i]}"
    path="/dev/disk/by-id/$id"
    name=$(basename $(readlink $path))

    echo $i $id $path $name

    dev="$dev $path"
done

sudo mdadm --stop /dev/md*
echo yes | sudo mdadm --create --verbose /dev/md0 --level=0 --raid-devices=16 $dev
sudo sgdisk -n 0:0:+1TB /dev/md0
sudo dd if=/dev/zero of=/dev/md0p1 bs=1G count=256
sudo $TRICACHE_ROOT/scripts/clearcache.sh
