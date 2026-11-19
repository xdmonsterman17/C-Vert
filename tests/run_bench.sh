#!/usr/bin/env sh
# CPU timings of the core's heavier calls (see bench_core.c).   tests/run_bench.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HERE/.build"
cc -std=c99 -O2 -Wall -Wextra -pedantic -Werror -I"$HERE/core" "$HERE"/core/*.c "$HERE/tests/bench_core.c" -lm -o "$HERE/.build/bench_core"
"$HERE/.build/bench_core"
