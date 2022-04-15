echo $$ | sudo tee /sys/fs/cgroup/limit/cgroup.procs
rm /mnt/raid/temp/livegraph_$1g -rf
sudo cp /mnt/data/TriCache/livegraph/livegraph_$1g /mnt/raid/temp -r
sudo chown -R $(id -u):$(id -g) /mnt/raid/temp/livegraph_$1g
