#!/bin/bash
source $TRICACHE_ROOT/scripts/config.sh
sudo PCI_ALLOWED="$SSDPCIe" HUGEMEM=$(expr $1 \* 1024) HUGE_EVEN_ALLOC=yes CLEAR_HUGE=yes $SPDK_ROOT/scripts/setup.sh
echo 1048576 | sudo tee /sys/module/vfio_iommu_type1/parameters/dma_entry_limit > /dev/null
