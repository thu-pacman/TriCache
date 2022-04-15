for i in $(seq 0 15)
do
    sudo rsync -avhP /mnt/data/TriCache/flashgraph/ssd$i/ /mnt/ssd$i/
    sudo chmod -R a+rw /mnt/ssd$i/*
done
sudo $TRICACHE_ROOT/scripts/clearcache.sh
