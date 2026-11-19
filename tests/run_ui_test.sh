#!/usr/bin/env sh
# Real mouse for the outliner and the modifier panels (fold arrow, drag to parent, Add Modifier menu, axis / count / offset
# fields, the eye) on a private X display.   tests/run_ui_test.sh   (needs a built program, xvfb-run, xdotool)
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export HERE
L="$HERE/.build/ui.log"
fail() { echo "FAIL: $*"; echo "--- log ---"; cat "$L" 2>/dev/null; exit 1; }
command -v xdotool >/dev/null || { echo "(xdotool missing) skipped"; exit 0; }
[ -x "$HERE/.build/native/meshforge" ] || fail "no built program: run tools/build.sh first"
cd "$HERE"; mkdir -p tests/reports
LP_NUM_THREADS=2 nice -n 10 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" sh "$HERE/tests/ui_driver.sh" || fail "the driver failed"
grep -q 'is now a child of' "$L"                   || fail "dragging a row onto another didn't parent it"
grep -q 'modifier Array added'  "$L"               || fail "Add Modifier > Array didn't add one"
grep -q 'modifier Subdivision added' "$L"          || fail "Add Modifier > Subdivision didn't add one"
grep -q 'Array axis Y' "$L"                        || fail "the axis buttons didn't switch to Y"
grep -q 'Array: 3' "$L"                            || fail "the Count field's arrow didn't raise it to 3"
grep -q 'Array offset 1.3' "$L"                    || fail "the Offset field's arrow didn't raise it to 1.3"
grep -q 'Subdivision switched off' "$L"            || fail "the eye didn't switch the modifier off"
grep -q 'Subdivision switched on' "$L"             || fail "the eye didn't switch it back on"
grep -q 'children folded' "$L"                     || fail "the fold arrow didn't fold the children"
[ -f ui.png ] && mv ui.png "$HERE/tests/reports/ui.png"
echo "ui test: ok"
