#!/bin/bash
sudo sync
sudo sync
sudo sync
sudo kexec -l /boot/vmlinuz-4.14.215-0414215-generic --initrd=/boot/initrd.img-4.14.215-0414215-generic --command-line="$( cat /proc/cmdline )"
sudo kexec -e
