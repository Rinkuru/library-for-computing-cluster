#!/usr/bin/env bash

LOGFILE="scripts/simpelTest.log"
workers="8";

echo > $LOGFILE

./build/examples/integral_master --workers "$workers" >> $LOGFILE 2>&1 &

master_pid=$!
worker_pids=()

start=$(date +%s%N)

for ((i = 0; i < workers; ++i)); do
    ./build/examples/integral_worker --threads 1 --cores 1 --first-core "$i" >> $LOGFILE 2>&1 &
    worker_pids[$i]=$!
done

for ((i = 0; i < workers; ++i)); do
    wait "${worker_pids[$i]}"
done
wait "$master_pid"

finish=$(date +%s%N)
elapsed_ms=$(((finish - start) / 1000000))

echo "time: ${elapsed_ms} ms" >> $LOGFILE
echo >> $LOGFILE
