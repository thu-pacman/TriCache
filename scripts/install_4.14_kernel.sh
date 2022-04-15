#!/bin/bash
TEMP_DIR=$(mktemp -d)
pushd $TEMP_DIR
wget https://kernel.ubuntu.com/\~kernel-ppa/mainline/v4.14.215/amd64/linux-headers-4.14.215-0414215-generic_4.14.215-0414215.202101122110_amd64.deb
wget https://kernel.ubuntu.com/\~kernel-ppa/mainline/v4.14.215/amd64/linux-image-unsigned-4.14.215-0414215-generic_4.14.215-0414215.202101122110_amd64.deb
wget https://kernel.ubuntu.com/\~kernel-ppa/mainline/v4.14.215/amd64/linux-modules-4.14.215-0414215-generic_4.14.215-0414215.202101122110_amd64.deb
wget https://kernel.ubuntu.com/\~kernel-ppa/mainline/v4.14.215/amd64/linux-headers-4.14.215-0414215_4.14.215-0414215.202101122110_all.deb
sudo dpkg -i *.deb
popd
rm $TEMP_DIR -r
