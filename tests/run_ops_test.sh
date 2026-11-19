#!/usr/bin/env sh
# Real mouse / keys for the milestone 3 operators (inset, delete, flip, merge, bevel, loop cut) on a private X display.
#   tests/run_ops_test.sh      (needs a built program, xvfb-run, xdotool)
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export HERE
L="$HERE/.build/ops.log"
fail() { echo "FAIL: $*"; echo "--- log ---"; cat "$L" 2>/dev/null; exit 1; }
command -v xdotool >/dev/null || { echo "(xdotool missing) skipped"; exit 0; }
[ -x "$HERE/.build/native/meshforge" ] || fail "no built program: run tools/build.sh first"
cd "$HERE"; mkdir -p tests/reports
LP_NUM_THREADS=2 nice -n 10 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" sh "$HERE/tests/ops_driver.sh" || fail "the driver failed"
grep -q 'inset confirmed' "$L"                          || fail "I + mouse + click didn't confirm an inset"
grep -q 'deleted selected faces' "$L"                   || fail "X didn't delete the selected face"
grep -q 'flipped 1 face(s)' "$L"                        || fail "Alt+N didn't flip the selected face"
grep -q 'merged: 7 vertex(es) removed' "$L"             || fail "M didn't merge the eight vertices into one"
grep -q 'no vertices within 0.001' "$L"                 || fail "Alt+M should have found nothing to merge"
grep -q 'bevel confirmed' "$L"                          || fail "Ctrl+B + mouse + click didn't confirm a bevel"
grep -q 'bevel: 3 segment' "$L"                         || fail "the mouse wheel during a bevel didn't set 3 segments"
grep -q 'hid 1 element' "$L"                           || fail "H didn't hide the selected face"
grep -q 'revealed 1 hidden element' "$L"               || fail "Alt+H didn't show it again"
grep -q 'loop cut: 4 face(s) added' "$L"                || fail "Ctrl+R on an edge didn't cut a ring of 4 faces"
grep 'picked face .* X-ray' "$L" | python3 -c "import re,sys; m=re.search(r'picked face (\d+) \(the ray alone: (\d+)\)', sys.stdin.read()); sys.exit(0 if m and m.group(1)!=m.group(2) else 1)" || fail "X-ray face pick didn't reach a face behind another"
[ "$(grep -c 'undo (' "$L")" -ge 5 ]                    || fail "expected five undos"
"$HERE/tools/objinfo.sh" .build/ops_out.obj | tee .build/ops_objinfo.log
grep -q 'V12 E20 F10 euler 2 boundary 0 nonmanifold 0' .build/ops_objinfo.log || fail "the saved mesh isn't a cube with one loop cut"
[ -f ops.png ] && mv ops.png "$HERE/tests/reports/ops.png"
echo "ops test: ok"
