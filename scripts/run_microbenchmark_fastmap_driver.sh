#!/bin/bash
for i in 1.0 0.9 0.8 0.7 0.6 0.5 0.4 0.3 0.2 0.1
do
    ssh -o ServerAliveInterval=90 $1 "source ~/.bashrc; cd $2; "'echo $PWD; bash $TRICACHE_ROOT/scripts/run_microbenchmark_fastmap.sh '"$i"
    sleep 30
    ssh -o ServerAliveInterval=90 $1 "source ~/.bashrc; cd $2; "'echo $PWD; bash $TRICACHE_ROOT/scripts/run_microbenchmark_fastmap.sh '"allpage_$i"
    sleep 30
done
sleep 30
ssh -o ServerAliveInterval=90 $1 "source ~/.bashrc; cd $2; "'echo $PWD; bash $TRICACHE_ROOT/scripts/run_microbenchmark_fastmap.sh '"finish"

