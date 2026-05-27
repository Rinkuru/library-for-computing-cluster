#!/usr/bin/env bash

LOGFILE="scripts/speedupWorkers.log"
TIMEOUT=600000
GREEN="\033[32m"
RED="\033[31m"
RESET="\033[0m"

run_case() {
    workers=$1
    case_log="scripts/speedup_${workers}_workers.log"

    echo > "$case_log"
    echo "workers: $workers"

    ./build/examples/integral_master --workers "$workers" --timeout "$TIMEOUT" --end >> "$case_log" 2>&1 &
    master_pid=$!

    sleep 0.2
    start=$(date +%s%N)

    for ((i = 0; i < workers; ++i)); do
        ./build/examples/integral_worker --method --threads 2 --cores 1 --first-core "$i" --timeout "$TIMEOUT" >> "$case_log" 2>&1 &
        worker_pids[$i]=$!
    done

    worker_status=0
    for ((i = 0; i < workers; ++i)); do
        wait "${worker_pids[$i]}"
        if [ "$?" -ne 0 ]; then
            worker_status=1
        fi
    done

    wait "$master_pid"
    master_status=$?

    finish=$(date +%s%N)
    elapsed_ms=$(((finish - start) / 1000000))

    if [ "$worker_status" -ne 0 ] || [ "$master_status" -ne 0 ]; then
        echo -e "${RED}FAIL speedup test${RESET}"
        exit 1
    fi

    result=$(awk '/Integral:/ {print $2}' "$case_log")
    if [ -z "$result" ]; then
        echo -e "${RED}FAIL speedup test: result was not found${RESET}"
        exit 1
    fi

    echo "workers: $workers, result: $result, time: ${elapsed_ms} ms" >> "$LOGFILE"
    RUN_TIME=$elapsed_ms
    RUN_RESULT=$result
}

echo > "$LOGFILE"
echo "Running speedup test"

run_case 1
time_1=$RUN_TIME
result_1=$RUN_RESULT
run_case 2
time_2=$RUN_TIME
result_2=$RUN_RESULT
run_case 4
time_4=$RUN_TIME
result_4=$RUN_RESULT

cat "$LOGFILE"

awk -v a="$result_1" -v b="$result_2" 'BEGIN { diff = a - b; if (diff < 0) diff = -diff; base = a; if (base < 0) base = -base; tolerance = base * 0.01; if (tolerance < 0.01) tolerance = 0.01; exit(diff <= tolerance ? 0 : 1) }'
if [ "$?" -ne 0 ]; then
    echo -e "${RED}FAIL speedup test: results for 1 and 2 workers are different${RESET}"
    exit 1
fi

awk -v a="$result_1" -v b="$result_4" 'BEGIN { diff = a - b; if (diff < 0) diff = -diff; base = a; if (base < 0) base = -base; tolerance = base * 0.01; if (tolerance < 0.01) tolerance = 0.01; exit(diff <= tolerance ? 0 : 1) }'
if [ "$?" -ne 0 ]; then
    echo -e "${RED}FAIL speedup test: results for 1 and 4 workers are different${RESET}"
    exit 1
fi

if [ "$time_4" -ge "$time_1" ]; then
    echo -e "${RED}FAIL speedup test: 4 workers were not faster than 1 worker${RESET}"
    exit 1
fi

echo "time 1 worker : ${time_1} ms"
echo "time 2 workers: ${time_2} ms"
echo "time 4 workers: ${time_4} ms"
echo -e "${GREEN}PASS speedup test${RESET}"
