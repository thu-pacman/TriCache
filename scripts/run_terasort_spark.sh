#!/bin/bash
$TRICACHE_ROOT/scripts/cgroups.sh

trap "kill 0" SIGINT
(while true; do pgrep java | xargs -I {} sudo bash -c "echo {} > /sys/fs/cgroup/limit/cgroup.procs 2>/dev/null"; sleep 0.1; done) &
echo max | sudo tee /sys/fs/cgroup/limit/memory.max

mkdir -p results_terasort_spark

sudo rsync -avhP /mnt/data/TriCache/terasort/terasort-150G /mnt/ssd0/shm
sudo rsync -avhP /mnt/data/TriCache/terasort/terasort-400G /mnt/ssd0/shm

function start_spark
{
    pushd $TRICACHE_ROOT/ae-projects/terasort_spark/spark-confs
    ln -sf spark-env-$1.sh spark-env.sh
    popd
    $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/sbin/stop-all.sh
    sleep 5
    killall -q -9 java
    $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/sbin/start-master.sh -h localhost
    $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/sbin/start-slaves.sh
}

function stop_spark
{
    $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/sbin/stop-all.sh
}

echo 495G | sudo tee /sys/fs/cgroup/limit/memory.max
start_spark 16
stdbuf -oL $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 25G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-150G /mnt/ssd0/shm/terasort-150G-out 4096 4096 3 2>&1 | tee results_terasort_spark/terasort_spark_150G_512G.txt
start_spark 16
stdbuf -oL $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 25G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-400G /mnt/ssd0/shm/terasort-400G-out 4096 4096 3 2>&1 | tee results_terasort_spark/terasort_spark_400G_512G.txt

echo 256G | sudo tee /sys/fs/cgroup/limit/memory.max
start_spark 16
stdbuf -oL $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 10G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-150G /mnt/ssd0/shm/terasort-150G-out 4096 4096 3 2>&1 | tee results_terasort_spark/terasort_spark_150G_256G.txt
start_spark 16
stdbuf -oL $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 10G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-400G /mnt/ssd0/shm/terasort-400G-out 4096 4096 3 2>&1 | tee results_terasort_spark/terasort_spark_400G_256G.txt

echo 128G | sudo tee /sys/fs/cgroup/limit/memory.max
start_spark 16
stdbuf -oL $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 5G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-150G /mnt/ssd0/shm/terasort-150G-out 4096 4096 1 2>&1 | tee results_terasort_spark/terasort_spark_150G_128G.txt
start_spark 16
stdbuf -oL $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 5G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-400G /mnt/ssd0/shm/terasort-400G-out 4096 4096 1 2>&1 | tee results_terasort_spark/terasort_spark_400G_128G.txt

echo 64G | sudo tee /sys/fs/cgroup/limit/memory.max
start_spark 8
stdbuf -oL $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 4G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-150G /mnt/ssd0/shm/terasort-150G-out 4096 4096 1 2>&1 | tee results_terasort_spark/terasort_spark_150G_64G.txt
start_spark 8
stdbuf -oL $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 4G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-400G /mnt/ssd0/shm/terasort-400G-out 4096 4096 1 2>&1 | tee results_terasort_spark/terasort_spark_400G_64G.txt

echo 32G | sudo tee /sys/fs/cgroup/limit/memory.max
start_spark 4
stdbuf -oL $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 4G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-150G /mnt/ssd0/shm/terasort-150G-out 4096 4096 1 2>&1 | tee results_terasort_spark/terasort_spark_150G_32G.txt
start_spark 4
stdbuf -oL $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 4G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-400G /mnt/ssd0/shm/terasort-400G-out 4096 4096 1 2>&1 | tee results_terasort_spark/terasort_spark_400G_32G.txt

# OOM
# echo 16G | sudo tee /sys/fs/cgroup/limit/memory.max
# $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 1G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-150G /mnt/ssd0/shm/terasort-150G-out 4096 4096 1 2>&1 | tee results_terasort_spark/terasort_spark_150G_32G.txt
# $TRICACHE_ROOT/ae-projects/terasort_spark/spark-3.2.0-bin-hadoop3.2/bin/spark-submit --master spark://localhost:7077 --class com.github.ehiggs.spark.terasort.TeraSortComp --executor-memory 1G --driver-memory 16G $TRICACHE_ROOT/ae-projects/terasort_spark/spark-terasort/target/spark-terasort-1.2-SNAPSHOT-jar-with-dependencies.jar /mnt/ssd0/shm/terasort-400G /mnt/ssd0/shm/terasort-400G-out 4096 4096 1 2>&1 | tee results_terasort_spark/terasort_spark_400G_32G.txt

stop_spark

kill -9 $(jobs -p)
