# TriCache Artifact Evaluation

## Hardware & Software Recommendations
* CPU: 2x AMD EPYC 7742 CPUs with hyper-threading
* Memory: >= 512GB
* Testing Storage: 8x Intel P4618 DC SSDs
* Data & System Storage: Any PCI-e attached SSD without MD RAIDs
* OS: Debian 11.1 with Linux kernel 5.10

 
## System Setup

### Clone TriCache
```bash
git clone -b osdi-ae https://github.com/thu-pacman/TriCache.git
cd TriCache
# add to ~/.bashrc
export TRICACHE_ROOT=$HOME/TriCache
git submodule update --init --recursive
cd ..
```

### Install dependency
```bash
# Tested under Debian 11.1
# 	Boost: 1.74.0
# 	Thrift: 0.13.0
# 	TBB: 2020.3
sudo apt install vim-nox tmux rsync wget htop numactl time \
	xfsprogs psmisc
sudo apt install build-essential cmake git pkg-config libnuma-dev \
	libboost-all-dev libaio-dev libhwloc-dev libatlas-base-dev \
	zlib1g-dev thrift-compiler libthrift-dev libtbb-dev \
	libgflags-dev openjdk-11-jdk maven gdisk kexec-tools \
	python3 python3-matplotlib python3-numpy
```
```bash
# Install clang-13 from LLVM source
sudo apt install lsb-release software-properties-common
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 13
sudo apt install libomp-13-dev
```
```bash
# Build & install SPDK with TriCache's patch
git clone -b v21.04 https://github.com/spdk/spdk.git
cd spdk
git submodule update --init
git apply $TRICACHE_ROOT/deps/spdk.patch
sudo ./scripts/pkgdep.sh
./configure --prefix=$HOME/spdk-install --with-shared
make -j
make install -j
cd ..

# add to ~/.bashrc
export \
	PKG_CONFIG_PATH=$HOME/spdk-install/lib/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=$HOME/spdk-install/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$HOME/spdk/dpdk/build/lib:$LD_LIBRARY_PATH
export SPDK_ROOT=$HOME/spdk

# testing SPDK examples (spdk_nvme_identify, spdk_nvme_perf)
```
```bash
# Install 4.14 kernel for FastMap
$TRICACHE_ROOT/install_4.14_kernel.sh
```

### OS configurations
```bash
# Making sudo password-free
# add to /etc/sudoers 
%sudo   ALL=(ALL) NOPASSWD:ALL

# VM configures for SPDK and tunning swapping in-memory performance
# add following lines to /etc/sysctl.conf
vm.max_map_count=262144
vm.dirty_ratio=99
vm.dirty_background_ratio=99
vm.dirty_expire_centisecs=360000
vm.dirty_writeback_centisecs=360000
vm.swappiness=1

# ulimit configures for SPDK
# add following lines to /etc/security/limits.conf
* hard memlock unlimited
* soft memlock unlimited
* soft nofile 1048576

# disable transparent_hugepage for SPDK
# edit /etc/default/grub
# add transparent_hugepage=never to GRUB_CMDLINE_LINUX 
sudo update-grub

# Support key-based ssh to localhost
ssh-keygen
ssh-copy-id localhost
# test
ssh localhost

# reboot
sudo reboot
```


## Build TriCache

```bash
cd $TRICACHE_ROOT
scripts/build.sh
```


## Configure TriCache
Edit `$TRICACHE_ROOT/scripts/config.sh` to configure ssds for TriCache.

* `SSDArray` lists disk ids for testing disks. `ls -lh /dev/disk/by-id` will list all disk ids and their block id (nvmeXnY).
* `SSDPCIe` lists PCIe addresses testing disks. `cat /sys/block/nvmeXnY/device/address` will show PCIe the address.
* `CACHE_16_SERVER_CONFIG` lists disks and cores used by TriCache. Each item is formed like `server-core-id,disk-pci-address,nsid,disk-offset`.
* `CACHE_16_SERVER_CORES` lists cores used by TriCache. 
* `CACHE_32_SERVER_CONFIG` and `CACHE_32_SERVER_CORES` a 32-server configurations.
* **All the above disks will be formatted multiple times. YOU WILL LOSE THEIR DATA!**
* **All the above disks will be heavily written. THEY MAY BE BROKEN!**
* Guide for selecting cores: 
	1. Distribute the servers as evenly as possible among multiple numa-nodes
	2. Use hyperthreading to bind servers to as few physical cores as possible while avoiding over-subscribe for servers.
	3. For each server, find the closest SSDs binding to it.

## Test TriCache
```bash
# setup SPDK, with 4GB DMA memory
$TRICACHE_ROOT/scripts/setup_spdk.sh 4

# "hello world" testing
$TRICACHE_ROOT/scripts/test_build.sh
# it should exit without error
# Reference log:
# Init 000 at 000 CPU, 00 Node
# Init 001 at 128 CPU, 00 Node
# Init 002 at 016 CPU, 01 Node
# Init 003 at 144 CPU, 01 Node
# Init 006 at 048 CPU, 03 Node
# Init 007 at 176 CPU, 03 Node
# Init 005 at 160 CPU, 02 Node
# Init 004 at 032 CPU, 02 Node
# Init 013 at 224 CPU, 06 Node
# Init 012 at 096 CPU, 06 Node
# Init 014 at 112 CPU, 07 Node
# Init 015 at 240 CPU, 07 Node
# Init 008 at 064 CPU, 04 Node
# Init 009 at 192 CPU, 04 Node
# Init 010 at 080 CPU, 05 Node
# Init 011 at 208 CPU, 05 Node
# Deconstructing SPDK Env

# reset devices from SPDK
$TRICACHE_ROOT/scripts/reset_spdk.sh
```


## Generating and Preprocessing Datasets

* The experiments about graph processing use the `uk-2014` dataset which is available [here](https://law.di.unimi.it/webdata/uk-2014/).
* The experiments about key-value stores use the mixgraph (prefix-dist) workload and the `db_bench` tool from [RocksDB](https://github.com/facebook/rocksdb/).
* The experiments about big-data analytics use terasort datasets generated by `teragen` from [Hadoop](https://hadoop.apache.org/).
* The experiments about graph database use LDBC SNB interactive benchmark generated by [LDBC SNB Datagen](https://github.com/ldbc/ldbc_snb_datagen_hadoop/).

The preprocessed datasets to reproduce the experimental results are available at: [https://pacman.cs.tsinghua.edu.cn/public/TriCacheAEData.tar.zst](https://pacman.cs.tsinghua.edu.cn/public/TriCacheAEData.tar.zst).

```bash
# Unzip the datasets and put them into /mnt/data/TriCache
# /mnt/data/TriCache
# ├── flashgraph
# ├── ligra
# ├── livegraph
# ├── rocksdb
# └── terasort

sudo mkdir -p /mnt/data/TriCache
sudo tar --zstd -xf TriCache.tar.zst -C /mnt/data/TriCache
```


## Reproduce Evaluations

* **All the configured disked will be formatted multiple times. YOU WILL LOSE THEIR DATA!**
* **MAKE SURE** there is no md raid like `/dev/md*`. **MD RAIDS WILL BE BROKEN!**
* `/mnt/data/TriCache/temp`,  `/mnt/raid` and `/mnt/ssd[0-15]` will be used as temporary directories, please **MAKE SURE** no important data is stored in these directories.
* Please execute scripts in `tmux` or `screen`
* We recommend plotting the figures by following the later Plot Figures Section after running each part of the experiments.

Make an empty directory for storing logs, such as `$HOME/results`, and cd into it.

```bash
mkdir -p $HOME/results
cd $HOME/results
```

### "Quick & Small" Experiments
It covers important cases in our evaluations. We think these results can support our claims within a short period.

```bash
# Run "Quick & Small" Experiments
# Taking about 7 hours
$TRICACHE_ROOT/scripts/run_all_small.sh
```


### "One-click" Script for All Experiments
It combines all experiments (except FastMap and long-running parts) in one script. The following sections will describe how to run each experiment separately.

```bash
# Run "one-click" script
# Taking about 60 hours
$TRICACHE_ROOT/scripts/run_all.sh
```

### Graph Processing (Section 4.1)

#### All-in-one
It combines all experiments (except long-running experiments) in one script.

```bash
# Taking about 11 hours 30 minutes
# It will process PageRank, WCC, BFS for uk-2014 dataset with 
# 	Ligra (TriCache), Ligra (Swapping), and FlashGraph.
# We move 64GB, 32GB, and 16GB with Ligra (Swapping)
# 	to Long-running Experiments
# Logs are placed in results_ligra_cache, results_ligra_swap, 
# 	and results_flashgraph
$TRICACHE_ROOT/scripts/run_all.sh graph_processing 
```

#### One-by-one
```bash
# Ligra (TriCache)
# Taking about 4 hours 30 minutes
# Logs are placed in results_ligra_cache
$TRICACHE_ROOT/scripts/run_all.sh graph_processing ligra_cache

# Ligra (Swapping)
# Taking about 5 hours 30 minutes
# Logs are placed in results_ligra_swap
$TRICACHE_ROOT/scripts/run_all.sh graph_processing ligra_swap

# FlashGraph
# Taking about 1 hour 30 minutes
# Logs are placed in results_flashgraph
$TRICACHE_ROOT/scripts/run_all.sh graph_processing flashgraph
```

#### Long-running Experiments
It will force to execute long-time experiments. These experiments will take more than **16** hours, so **PLEASE** skip them first.

```bash
# Ligra (Swapping) Long-running Parts
# Logs are placed in results_ligra_swap
$TRICACHE_ROOT/scripts/run_all.sh graph_processing ligra_swap_slow
```

### Key-Value Stores (RocksDB: Section 4.2)

#### All-in-one
```bash
# Taking about 5 hours
# It will execute mixgraph workload with db_bench of RocksDB 
# 	PlainTable (TriCache), BlockBasedTable and PlainTable (mmap).
# We reduce some requests number to limit total execution time
# 	for long-running cases with BlockBasedTable and PlainTable (mmap)
# Logs are placed in results_rocksdb_cache, results_rocksdb_block, 
# 	and results_rocksdb_mmap
$TRICACHE_ROOT/scripts/run_all.sh rocksdb 
```
#### One-by-one
```bash
# PlainTable (TriCache)
# Taking about 2 hours
# Logs are placed in results_rocksdb_cache
$TRICACHE_ROOT/scripts/run_all.sh rocksdb rocksdb_cache

# BlockBasedTable
# Taking about 1 hour
# Logs are placed in results_rocksdb_block
$TRICACHE_ROOT/scripts/run_all.sh rocksdb rocksdb_block

# PlainTable (mmap)
# Taking about 2 hours
# Logs are placed in results_rocksdb_mmap
$TRICACHE_ROOT/scripts/run_all.sh rocksdb rocksdb_mmap
```

### Big-Data Analytics (Terasort: Section 4.3)

#### All-in-one
```bash
# Taking about 14 hours
# It will execute 150GB and 400GB Terasort with 
# 	ShuffleSort (TriCache), GNUSort (TriCache), and Spark,
# 	and will execute 150GB dataset Terasort with
# 	ShuffleSort (Swapping), GNUSort (Swapping).
# We move the 400GB with ShuffleSort/GNUSort (Swapping)
# 	to Long-running Experiments
# Logs are placed in results_terasort_cache, results_terasort_swap, 
# 	and results_terasort_spark
$TRICACHE_ROOT/scripts/run_all.sh terasort 
```
#### One-by-one
```bash
# ShuffleSort (TriCache) and GNUSort (TriCache)
# Taking about 3 hours
# Logs are placed in results_terasort_cache
$TRICACHE_ROOT/scripts/run_all.sh terasort terasort_cache

# ShuffleSort (Swapping) and GNUSort (Swapping)
# Taking about 7 hours 30 minutes
# Logs are placed in results_terasort_swap
$TRICACHE_ROOT/scripts/run_all.sh terasort terasort_swap

# Spark
# Taking about 3 hours 30 minutes
# Logs are placed in results_terasort_spark
$TRICACHE_ROOT/scripts/run_all.sh terasort terasort_spark
```

#### Long-running Experiments
It will force to execute long-time experiments. These experiments will take more than **48** hours, so **PLEASE** skip them first.

```bash
# GNUSort (Swapping) Long-running Parts
# Logs are placed in results_terasort_swap
$TRICACHE_ROOT/scripts/run_all.sh terasort terasort_swap_slow
```

### Graph Database (LiveGraph: Section 4.4)

#### All-in-one
```bash
# Taking about 8 hours
# It will execute SF30 and SF100 SNB Interactive benchmarks 
# 	with LiveGraph (TriCache) and LiveGraph (mmap).
# Logs are placed in results_livegraph_cache 
# 	and results_livegraph_mmap
$TRICACHE_ROOT/scripts/run_all.sh livegraph 
```
#### One-by-one
```bash
# LiveGraph (TriCache)
# Taking about 4 hours
# Logs are placed in results_livegraph_cache
$TRICACHE_ROOT/scripts/run_all.sh livegraph livegraph_cache

# LiveGraph (mmap)
# Taking about 4 hours
# Logs are placed in results_livegraph_mmap
$TRICACHE_ROOT/scripts/run_all.sh livegraph livegraph_mmap
```

### Micro-benchmarks (Section 4.5)

#### All-in-one (Except FastMap)
```bash
# Taking about 11 hours
# Logs are placed in results_microbenchmark_cache 
# 	and results_microbenchmark_mmap
$TRICACHE_ROOT/scripts/run_all.sh microbenchmark 
```

#### One-by-one
```bash
# TriCache
# Taking about 6 hours
# Logs are placed in results_microbenchmark_cache 
$TRICACHE_ROOT/scripts/run_all.sh microbenchmark microbenchmark_cache

# mmap
# Taking about 5 hours
# Logs are placed in results_microbenchmark_mmap
$TRICACHE_ROOT/scripts/run_all.sh microbenchmark microbenchmark_mmap
```

#### FastMap Experiments
FastMap experiments run on **an old kernel** and require **rebooting 20 times** because it might **crash the kernel**. The scripts will **automatically reboot the server**. These experiments are **very tricky** and take about **9 hours**. We recommend that you **be prepared for unexpected cases**, such as **losing the server** and **requiring cold resets**.


```bash
# Preparing disks for FastMap
$TRICACHE_ROOT/scripts/prepare_fastmap.sh

# Reboot to 4.14 kernel
$TRICACHE_ROOT/scripts/kexec.sh

# Wait rebooting, and ssh back again
# Check the kernel version is 4.14
uname -a
```

Go to another server to continue, such as the jumper. 

```bash
# Download the driver script from 
wget https://raw.githubusercontent.com/thu-pacman/TriCache/osdi-ae\
/scripts/run_microbenchmark_fastmap_driver.sh

# Run the driver script
# Taking 9 hours
# Logs are placed in results_microbenchmark_fastmap
./run_microbenchmark_fastmap_driver.sh HOST RESULTS_DIR

# ssh back again 
# Check the kernel version is 5.10
uname -a
```

### Performance Breakdown (Section 4.6)

#### All-in-one
```bash
# Taking about 9 hours
# We disable Shared-Cache-Only for GNU Terasort
# 	because it cannot finish within 5 hours, 
# 	and such low performance is already meaningless.
# Logs are placed in results_breakdown
$TRICACHE_ROOT/scripts/run_all.sh breakdown 
```

## Plot Figures
Make an empty directory for storing figures, such as `$HOME/figures`, and cd into it.

```bash
mkdir -p $HOME/figures
cd $HOME/figures
```
### "Quick & Small" Experiments
```bash
# After running "quick & small" experiments
$TRICACHE_ROOT/scripts/plot_all.sh small RESULTS_PATH
```

### All Experiments

```bash
# After running experiments (with/without long-running experiments)
$TRICACHE_ROOT/scripts/plot_all.sh RESULTS_PATH
```
### Graph Processing (Section 4.1 and Figure 8)

```bash
# After running graph processing experiments
#	(with/without long-running experiments)
$TRICACHE_ROOT/scripts/plot_all.sh graph_processing RESULTS_PATH
```
### Key-Value Stores (Section 4.2 and Figure 9)

```bash
# After running key-value experiments
$TRICACHE_ROOT/scripts/plot_all.sh rocksdb RESULTS_PATH
```
### Big-Data Analytics (Section 4.3 and Figure 10)

```bash
# After running big-data analytics experiments
#	(with/without long-running experiments)
$TRICACHE_ROOT/scripts/plot_all.sh terasort RESULTS_PATH
```
### Graph Database (Section 4.4 and Figure 11)

```bash
# After running graph database experiments
$TRICACHE_ROOT/scripts/plot_all.sh livegraph RESULTS_PATH
```
### Micro-benchmarks (Section 4.5 and Figure 12)

```bash
# After running micro-benchmarks experiments
#	(with/without FastMap experiments)
$TRICACHE_ROOT/scripts/plot_all.sh microbenchmark RESULTS_PATH
```
### Performance Breakdown (Section 4.6 and Table 1)

```bash
# After running performance breakdown experiments
$TRICACHE_ROOT/scripts/plot_all.sh breakdown RESULTS_PATH
```
