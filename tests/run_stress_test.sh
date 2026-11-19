#!/usr/bin/env sh
# Stress test: a UV sphere subdivided to ~32k vertices / 32k faces in the real program (virtual display), through Mesh mode, orbit, grab,
# undo and extrude. Checks it survives and that the CPU side of a frame stays inside a budget (the display's software GL says nothing
# about the real GPU; --prof counts only the program's own work, so the budget is about the glue, not rasterising).   tests/run_stress_test.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export HERE
L="$HERE/.build/stress.log"
fail() { echo "FAIL: $*"; echo "--- log ---"; tail -30 "$L" 2>/dev/null; exit 1; }
command -v xdotool >/dev/null || { echo "(xdotool missing) skipped"; exit 0; }
[ -x "$HERE/.build/native/meshforge" ] || fail "no built program: run tools/build.sh first"
cd "$HERE"; mkdir -p tests/reports .build
cc -std=c99 -O2 -Wall -Wextra -pedantic -Werror -I"$HERE/core" "$HERE"/core/*.c "$HERE/tests/make_big.c" -lm -o "$HERE/.build/make_big"
BIG="$HERE/.build/stress_big.obj"; export BIG
"$HERE/.build/make_big" 3 "$BIG" || fail "could not write the big mesh"
LP_NUM_THREADS=2 nice -n 10 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" sh "$HERE/tests/stress_driver.sh" || fail "the driver failed"
grep -q 'V31746' "$L"                                   || fail "the big mesh didn't load"
grep -q 'prof (ms per frame' "$L"                       || fail "no --prof line (the program didn't exit cleanly)"
grep -q 'extrude' "$L"                                  || fail "the extrude never ran on the big mesh"
python3 - "$L" <<'PY' || fail "a frame's CPU time went over budget"
import re, sys
m = re.search(r"update ([\d.]+) \(visibility ([\d.]+), render rebuild ([\d.]+)\)\s+draw3d ([\d.]+) \(surface ([\d.]+)\)\s+hud ([\d.]+)", open(sys.argv[1]).read())
upd, vis, ren, d3, surf, hud = map(float, m.groups())
edges = d3 - surf          # the edges' own draw calls (the surface is software-rasterised here)
print("  per frame: update %.1f ms (visibility %.1f, render rebuild %.1f), edges %.1f ms, hud %.1f ms" % (upd, vis, ren, edges, hud))
sys.exit(0 if upd < 40 and vis < 8 and edges < 40 and hud < 40 else 1)
PY
[ -f stress.png ] && mv stress.png "$HERE/tests/reports/stress.png"
echo "stress test: ok"
