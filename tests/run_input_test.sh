#!/usr/bin/env sh
# Drives the built program with real (synthetic) mouse and key events on a private X display and checks
# what it logged: Tab / 3 / click select, E extrudes, Ctrl+Z undoes, G grabs, right-hold + W flies, Ctrl+S saves, a middle-drag orbits.
# Needs xvfb-run + xdotool and a built program (tests/run_app_test.sh or tools/build.sh).
#   tests/run_input_test.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export HERE
fail() { echo "FAIL: $*"; echo "--- log ---"; cat "$HERE/.build/input.log" 2>/dev/null; exit 1; }
command -v xdotool >/dev/null || { echo "(xdotool missing: sudo apt install xdotool) skipped"; exit 0; }
[ -x "$HERE/.build/native/meshforge" ] || fail "no built program: run tools/build.sh first"
mkdir -p "$HERE/tests/reports"
cd "$HERE"
LP_NUM_THREADS=2 nice -n 10 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" sh "$HERE/tests/input_driver.sh" || fail "the driver failed"
L="$HERE/.build/input.log"
grep -q 'ready: new cube  V8 E12 F6 euler 2' "$L"      || fail "didn't start on a cube"
grep -q 'Mesh mode' "$L"                                || fail "Tab didn't enter Mesh mode"
grep -q 'face select mode' "$L"                         || fail "3 didn't pick face select"
grep -q '1 face(s) selected' "$L"                       || fail "the click didn't select a face"
[ "$(grep -c 'face(s) selected' "$L")" -eq 1 ]          || fail "the click in Object mode selected faces"
grep -q 'select the object first' "$L"                  || fail "G in Object mode with nothing selected didn't refuse"
[ "$(grep -c 'object "Cube" selected' "$L")" -eq 2 ]    || fail "expected the object selected twice (click, click again)"
grep -q 'object deselected' "$L"                        || fail "a click on empty space didn't deselect the object"
grep -q 'extrude: move the mouse (along the normal)' "$L" || fail "E didn't start a modal extrude along the normal"
grep -q 'undo (0 left, 1 to redo)' "$L"                            || fail "Ctrl+Z didn't undo"
[ "$(grep -c 'extrude confirmed' "$L")" -eq 2 ]         || fail "expected two confirmed extrudes (before and after the undo)"
grep -q 'extrude cancelled' "$L"                        || fail "vertex mode: E then Esc didn't cancel the extrude"
grep -q 'added Cylinder' "$L"                           || fail "Shift+A, Cylinder didn't add a cylinder"
grep -q 'saved .build/input_out.obj' "$L"               || fail "Ctrl+S didn't save"
grep -q 'grab confirmed' "$L"                           || fail "G + click didn't confirm a grab"
grep -q 'grab cancelled' "$L"                           || fail "G + Esc didn't cancel"
grep -q 'selection cleared' "$L"                        || fail "Alt+A didn't clear the selection"
grep -q 'added object Cone' "$L"                        || fail "Object mode: Shift+A, Cone didn't make a new object"
grep -q 'gizmo Move' "$L" && grep -q 'gizmo Rotate' "$L" && grep -q 'gizmo Scale' "$L" || fail "the tool bar didn't show the Move / Rotate / Scale gizmos"
[ "$(grep -c 'object "Cone" at' "$L")" -eq 3 ]          || fail "expected three confirmed gizmo drags on the Cone (move, rotate, scale)"
python3 - "$L" <<'PY' || fail "a gizmo drag didn't do what its handle says (see the 'object \"Cone\" at' lines)"
import re, sys
rows = [list(map(float, re.findall(r'-?\d+\.\d+', l))) for l in open(sys.argv[1]) if 'object "Cone" at' in l]
mv, rot, scl = rows[0], rows[1], rows[2]
assert abs(mv[0]) > 0.05 and abs(mv[1]) < 1e-3 and abs(mv[2]) < 1e-3, ("move along X only", mv)
assert 10 < abs(rot[4]) < 80 and abs(rot[3]) < 1e-2 and abs(rot[5]) < 1e-2, ("rotate about Y only", rot)
assert scl[8] > 1.5 and abs(scl[6] - 1) < 1e-3 and abs(scl[7] - 1) < 1e-3, ("scale along Z only", scl)
PY
grep -q 'box: 2 object(s) selected' "$L"                 || fail "a box drag in Object mode didn't select both objects"
grep -q 'box select' "$L"                                || fail "a box drag in Mesh mode didn't select"
grep -q 'added Cube into Cone' "$L"                      || fail "Mesh mode: Shift+A, Cube didn't add the shape into the active object"
python3 - "$L" <<'PY' || fail "select linked (L) didn't select one island of the Cone + Cube mesh"
import re, sys
m = [l for l in open(sys.argv[1]) if l.startswith('[meshforge] linked:')]
assert m, "no 'linked:' line: L did nothing"
n, tot = map(int, re.findall(r'(\d+) of (\d+)', m[0])[0])
assert 0 < n < tot, ("one island, not everything", n, tot)
PY
[ "$(grep -c 'X-ray on' "$L")" -eq 1 ] && [ "$(grep -c 'X-ray off' "$L")" -eq 1 ] || fail "Alt+Z / the X-ray button didn't toggle on then off"
grep -q 'duplicated 2 object' "$L"                       || fail "Shift+D didn't duplicate the two selected objects"
grep -q 'renamed to "Wheel"' "$L"                        || fail "F2 + typing + Enter didn't rename"
grep -q 'parented to "Wheel"' "$L"                       || fail "Ctrl+P didn't parent the other copy to Wheel"
for t in 'View and snapping tab' 'Modifiers tab' 'Mesh data tab' 'Transform tab' 'modifier Mirror added' 'modifier Subdivision added' 'modifier Array added' 'Subdivision: 2' 'applied 1 modifier(s)'; do grep -q "$t" "$L" || fail "the panel tab '$t' didn't open"; done
python3 - "$L" <<'PY' || fail "snapping or parenting moved things wrongly (see the 'object \"...\" at' lines)"
import re, sys
rows = {}
for l in open(sys.argv[1]):
    m = re.match(r'\[meshforge\] object "([^"]+)" at (-?\d+\.\d+) (-?\d+\.\d+) (-?\d+\.\d+)', l)
    if m: rows.setdefault(m.group(1), []).append(tuple(map(float, m.group(2, 3, 4))))
# the copy of the Cube: first line is the Ctrl-snapped grab (all multiples of the 0.25 step, and it did move)
cube = rows["Cube.001"]
assert any(abs(c) > 0.1 for c in cube[0]), ("the snapped grab moved nothing", cube)
assert all(abs(c / 0.25 - round(c / 0.25)) < 1e-2 for c in cube[0]), ("not on the 0.25 grid", cube[0])
# G with Ctrl held first: Wheel moved again, by a nonzero multiple of the step on every axis
wh = rows["Wheel"]
assert len(wh) >= 2, ("no Ctrl-first grab was logged", wh)
d = [wh[1][i] - wh[0][i] for i in range(3)]
assert any(abs(c) > 0.1 for c in d), ("Ctrl then G moved nothing", d)
assert all(abs(c / 0.25 - round(c / 0.25)) < 1e-2 for c in d), ("not in 0.25 steps", d)
# G X 1 on Wheel alone: the child (Cube.001) came along by exactly 1 on X
assert len(cube) >= 2 and abs(cube[1][0] - cube[0][0] - 1.0) < 2e-3 and abs(cube[1][1] - cube[0][1]) < 1e-3, ("the child didn't follow its parent", cube)
PY
[ "$(grep -c 'Tab while flying' "$L")" -eq 2 ]      || fail "Tab didn't register twice while the right button was held"
TGT=$(sed -n 's/.*flew: target \(.*\)/\1/p' "$L" | tail -1)
python3 -c "import sys; x,y,z=map(float,'$TGT'.split()); sys.exit(0 if (x*x+y*y+z*z)**0.5 > 0.5 else 1)" || fail "right-hold + W didn't fly (target $TGT)"
YAW=$(sed -n 's/.*camera yaw \(-\{0,1\}[0-9.]*\).*/\1/p' "$L" | tail -1)
python3 -c "import sys; sys.exit(0 if float('$YAW') < 0.0 else 1)" || fail "the middle-drag didn't orbit (yaw $YAW, started at 0.60)"
"$HERE/tools/objinfo.sh" .build/input_out.obj | tee .build/input_objinfo.log
grep -q 'V12 E20 F10 euler 2 boundary 0 nonmanifold 0' .build/input_objinfo.log || fail "the saved mesh isn't an extruded cube"
[ -f input.png ] || fail "no screenshot"
mv input.png "$HERE/tests/reports/input.png"
echo "input test: ok (yaw $YAW, target $TGT)"
