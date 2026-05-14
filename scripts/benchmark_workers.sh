#!/usr/bin/env bash

MASTER="./build/examples/integral_master"
WORKER="./build/examples/integral_worker"

if [ ! -x "$MASTER" ] || [ ! -x "$WORKER" ]; then
    echo "Run this script from project directory after build"
    echo "Example: cd project && cmake --build build && ./scripts/benchmark_workers.sh"
    exit 1
fi

for workers in 1 2 4; do
    echo "workers: $workers"

    rm -f "master-$workers.log" worker-"$workers"-*.log

    echo "workers: $workers" > "scripts/test.log"
    "$MASTER" --workers "$workers" --timeout 6000000 --end > "scripts/test.log" 2>&1 &
    master_pid=$!

    sleep 1

    start=$(date +%s%N)

    worker_pids=()
    for ((i = 0; i < workers; ++i)); do
        "$WORKER" --method --threads 1 --cores 1 --first-core "$i" --timeout 6000000 > "scripts/test.log" 2>&1 &
        worker_pids[$i]=$!
    done

    for ((i = 0; i < workers; ++i)); do
        wait "${worker_pids[$i]}"
    done

    wait "$master_pid"

    finish=$(date +%s%N)
    elapsed_ms=$(((finish - start) / 1000000))

    grep "Integral:" "master-$workers.log"
    echo "time: ${elapsed_ms} ms" > "scripts/test.log"
    echo

    sleep 1
done
