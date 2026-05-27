#!/usr/bin/env bash

LOGFILE="scripts/hardFunc7Workers.log"
WORKERS=7
TIMEOUT=600000
GREEN="\033[32m"
RED="\033[31m"
RESET="\033[0m"

echo > "$LOGFILE"
echo "Running hard func 7 workers test"

./build/examples/integral_master --workers "$WORKERS" --timeout "$TIMEOUT" >> "$LOGFILE" 2>&1 &
master_pid=$!

sleep 0.2
start=$(date +%s%N)

for ((i = 0; i < WORKERS; ++i)); do
    ./build/examples/integral_worker --hardFunc --threads 1 --cores 1 --first-core 0 --timeout "$TIMEOUT" >> "$LOGFILE" 2>&1 &
    worker_pids[$i]=$!
done

worker_status=0
for ((i = 0; i < WORKERS; ++i)); do
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
    echo -e "${RED}FAIL hard func 7 workers test${RESET}"
    exit 1
fi

result=$(awk '/Integral:/ {print $2}' "$LOGFILE")
awk -v value="$result" 'BEGIN { diff = value - 2.0; if (diff < 0) diff = -diff; exit(diff < 0.05 ? 0 : 1) }'
if [ "$?" -ne 0 ]; then
    echo -e "${RED}FAIL hard func 7 workers test: bad result $result${RESET}"
    exit 1
fi

echo "result: $result"
echo "time: ${elapsed_ms} ms"
echo -e "${GREEN}PASS hard func 7 workers test${RESET}"
