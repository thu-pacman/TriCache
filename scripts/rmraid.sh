#!/bin/bash
sudo umount /mnt/raid
sudo mdadm --stop /dev/md*
