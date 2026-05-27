#!/usr/bin/env bash
GREEN="\033[32m"
RESET="\033[0m"

TITLE=${1}
INFO_FILE="scripts/coverage.info"
FILTERED_INFO="scripts/coverage.filtered.info"

lcov --capture --directory build-coverage --output-file "$INFO_FILE" --quiet
lcov --extract "$INFO_FILE" "$PWD/src/*" "$PWD/examples/*" --output-file "$FILTERED_INFO" --quiet

echo "$TITLE:"
echo -e "${GREEN}lines: "
lcov --summary "$FILTERED_INFO" 2>&1 | awk '/lines/ {print $2; exit}'
echo "functions: "
lcov --summary "$FILTERED_INFO" 2>&1 | awk '/functions/ {print $2; exit}'
echo -e "${RESET}"
