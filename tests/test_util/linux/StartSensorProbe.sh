#!/bin/sh
set -u
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
sh "$base/../run-linux.sh" "$@"
status=$?
printf '\nReports: %s/../logs\n' "$base"
if [ -t 0 ]; then
    printf 'Press Enter to close... '
    read -r ignored || :
fi
exit "$status"
