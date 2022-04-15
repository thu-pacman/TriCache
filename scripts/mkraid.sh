#!/bin/bash
source $TRICACHE_ROOT/scripts/config.sh

dev=""
for i in ${!SSDArray[@]}; do
    id="${SSDArray[$i]}"
    path="/dev/disk/by-id/$id"
    name=$(basename $(readlink $path))
    numa=$(cat /sys/block/$name/device/numa_node)

    echo $i $id $path $name $numa

    dev="$dev $path"
done

sudo mdadm --stop /dev/md*
echo yes | sudo mdadm --create --force --verbose /dev/md0 --level=0 --raid-devices=16 $dev
sudo mkfs.xfs -f /dev/md0
sudo mkdir -p /mnt/raid
sudo mount /dev/md0 /mnt/raid
sudo mkdir /mnt/raid/temp
sudo chmod a+rw -R /mnt/raid/temp

