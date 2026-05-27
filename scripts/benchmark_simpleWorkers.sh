#!/usr/bin/env bash

LOGFILE="scripts/simpleWorkers.log"
GREEN="\033[32m"
RED="\033[31m"
RESET="\033[0m"

echo > "$LOGFILE"
echo "Running simple pi test"

./build/examples/integral_master --workers 1 --timeout 60000 >> "$LOGFILE" 2>&1 &
master_pid=$!

sleep 0.2
start=$(date +%s%N)

./build/examples/integral_worker --threads 1 --cores 1 --first-core 0 --timeout 60000 >> "$LOGFILE" 2>&1 &
worker_pid=$!

wait "$worker_pid"
worker_status=$?

wait "$master_pid"
master_status=$?

finish=$(date +%s%N)
elapsed_ms=$(((finish - start) / 1000000))

if [ "$worker_status" -ne 0 ] || [ "$master_status" -ne 0 ]; then
    echo -e "${RED}FAIL simple pi test${RESET}"
    exit 1
fi

result=$(awk '/Integral:/ {print $2}' "$LOGFILE")
awk -v value="$result" 'BEGIN { diff = value - 3.1415926536; if (diff < 0) diff = -diff; exit(diff < 0.01 ? 0 : 1) }'
if [ "$?" -ne 0 ]; then
    echo -e "${RED}FAIL simple pi test: bad result $result${RESET}"
    exit 1
fi

echo "result: $result"
echo "time: ${elapsed_ms} ms"
echo -e "${GREEN}PASS simple pi test${RESET}"
