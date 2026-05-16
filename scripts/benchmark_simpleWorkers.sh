#!/usr/bin/env bash

MASTER="./build/examples/integral_master"
WORKER="./build/examples/integral_worker"
LOGFILE="scripts/test.log"

echo >> $LOGFILE
for workers in 1 2 4; do
    echo "workers: $workers"

    echo "workers: $workers" >> $LOGFILE
    "$MASTER" --workers "$workers" >> $LOGFILE 2>&1 &
    master_pid=$!

    start=$(date +%s%N)

    worker_pids=()
    for ((i = 0; i < workers; ++i)); do
        "$WORKER" --threads 1 --cores 1 --first-core "$i" >> $LOGFILE 2>&1 &
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

done
