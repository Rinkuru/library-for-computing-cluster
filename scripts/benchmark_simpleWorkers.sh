#!/usr/bin/env bash

LOGFILE="scripts/simpelTest.log"

echo > $LOGFILE

./build/examples/integral_master --workers 1 >> $LOGFILE 2>&1 &

master_pid=$!

start=$(date +%s%N)

./build/examples/integral_worker --threads 1 --cores 1 --first-core "$i" >> $LOGFILE 2>&1 &
worker_pid=$!

wait "$worker_pid"
wait "$master_pid"

finish=$(date +%s%N)
elapsed_ms=$(((finish - start) / 1000000))

#здесь нужна какая-то проверка на то что тест проёден успешно, а точнее сравнение с числом пи


echo "time: ${elapsed_ms} ms" >> $LOGFILE
echo >> $LOGFILE
