#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ ! -x "$base/linux/SensorProbe" ]; then
    chmod u+x "$base/linux/SensorProbe"
fi
if [ -f "$base/../../CMakeLists.txt" ] && [ -f "$base/../../src/core/CMakeLists.txt" ]; then
    exec "$base/linux/SensorProbe" --output "$base/../../out/test_util/logs" "$@"
fi
exec "$base/linux/SensorProbe" "$@"
