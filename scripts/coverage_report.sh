#!/usr/bin/env bash

TITLE=${2}
INFO_FILE="scripts/coverage.info"
FILTERED_INFO="scripts/coverage.filtered.info"

lcov --capture --directory build-coverage --output-file "$INFO_FILE" --quiet
lcov --extract "$INFO_FILE" "$PWD/src/*" "$PWD/examples/*" --output-file "$FILTERED_INFO" --quiet

percent=$(lcov --summary "$FILTERED_INFO" 2>&1 | awk '/lines/ {print $2; exit}')

if [ -z "$percent" ]; then
    echo "coverage: unknown"
    exit 1
fi

echo "$TITLE: $percent"
