#!/usr/bin/env sh
# Real mouse / keys for redo, dragged modifier fields, smooth shading, region inset, edge loops, invert, numpad views, pan,
# autosave and the quit confirmation, on a private X display.   tests/run_polish_test.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export HERE
L="$HERE/.build/polish.log"
fail() { echo "FAIL: $*"; echo "--- log ---"; cat "$L" 2>/dev/null; exit 1; }
command -v xdotool >/dev/null || { echo "(xdotool missing) skipped"; exit 0; }
[ -x "$HERE/.build/native/meshforge" ] || fail "no built program: run tools/build.sh first"
cd "$HERE"; mkdir -p tests/reports
LP_NUM_THREADS=2 nice -n 10 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" sh "$HERE/tests/polish_driver.sh" || fail "the driver failed"
grep -q 'modifier Array added' "$L"                     || fail "the Array modifier wasn't added"
grep -q 'Array: [3-9]' "$L"                             || fail "dragging the Count field sideways changed nothing"
grep -q 'redo (0 left)' "$L"                            || fail "Ctrl+Shift+Z didn't redo"
grep -q 'smooth shading' "$L"                           || fail "the View tab's Shading button didn't switch to smooth"
grep -q 'the faces as one region' "$L"                  || fail "I again during an inset didn't switch to a region inset"
grep -q 'inset (region) confirmed' "$L"                           || fail "no region inset ran"
grep -q 'edge loop: 4 edge' "$L"                        || fail "Alt+click didn't select the 4-edge ring the region inset made"
grep -q 'selection inverted' "$L"                       || fail "Ctrl+I didn't invert"
for v in 'top view' 'front view' 'right view' 'framed the selection'; do grep -q "$v" "$L" || fail "numpad: no '$v'"; done
[ -s .build/polish_out.obj.bak ]                        || fail "no autosave backup next to the save file"
[ ! -e .build/polish_out.obj ]                          || fail "autosave must not touch the save file itself"
python3 - "$L" <<'PY' || fail "the pan didn't move the camera target"
import re, sys
t = re.findall(r"camera yaw [-\d.]+ pitch [-\d.]+ dist [-\d.]+ target ([-\d.]+) ([-\d.]+) ([-\d.]+)", open(sys.argv[1]).read())
sys.exit(0 if t and max(abs(float(x)) for x in t[-1]) > 0.05 else 1)
PY
[ -f polish.png ] && mv polish.png "$HERE/tests/reports/polish.png"
LP_NUM_THREADS=2 nice -n 10 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" sh "$HERE/tests/quit_driver.sh" || fail "the quit test failed"
echo "polish test: ok"
