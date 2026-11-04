#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ ! -x "$base/linux/SensorProbe" ]; then
    chmod u+x "$base/linux/SensorProbe"
fi
exec "$base/linux/SensorProbe" "$@"
