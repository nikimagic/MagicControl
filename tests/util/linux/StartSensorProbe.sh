#!/bin/sh
set -u
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
sh "$base/../run-linux.sh" "$@"
status=$?
if [ -f "$base/../../../CMakeLists.txt" ] && [ -f "$base/../../../src/core/CMakeLists.txt" ]; then
    printf '\nDefault reports: %s/../../../out/test_util/logs\n' "$base"
else
    printf '\nDefault reports: %s/../logs\n' "$base"
fi
if [ -t 0 ]; then
    printf 'Press Enter to close... '
    read -r ignored || :
fi
exit "$status"
