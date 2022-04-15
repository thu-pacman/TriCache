echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs

sudo rsync -avhP --delete /mnt/data/TriCache/livegraph/livegraph_$1g /mnt/data/TriCache/temp
sudo chown -R $(id -u):$(id -g) /mnt/data/TriCache/temp/livegraph_$1g
