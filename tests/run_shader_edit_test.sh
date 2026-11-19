#!/usr/bin/env sh
# The shader canvas with the real mouse and keyboard (drag a node, type a value, Esc cancels), file checked.   tests/run_shader_edit_test.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export HERE
L="$HERE/tests/reports/shader_edit.log"; F="$HERE/tests/reports/shader_edit.cbshader"
fail() { echo "FAIL: $*"; echo "--- log ---"; cat "$L" 2>/dev/null; exit 1; }
command -v xdotool >/dev/null || { echo "(xdotool missing) skipped"; exit 0; }
[ -x "$HERE/.build/native/meshforge" ] || fail "no built program: run tools/build.sh first"
cd "$HERE"; mkdir -p tests/reports
export CBRICKS_CONFIG="$HERE/tests/reports/shader_config"
LP_NUM_THREADS=2 nice -n 10 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" sh "$HERE/tests/shader_edit_driver.sh" || fail "the driver failed"
python3 - "$F" <<'PY' || fail "the saved file is wrong (see $F)"
import json, sys
g = json.load(open(sys.argv[1]))
n = {x["id"]: x for x in g["nodes"]}
assert len(g["nodes"]) == 5 and len(g["wires"]) == 4, "nodes or wires lost"
assert g["kind"] == "surface" and g["cbshader"] == 1
assert n[5]["pos"][0] > 400 and n[5]["pos"][1] > 100, ("the Mix node did not move", n[5]["pos"])
assert n[3]["values"]["scale"] == [9], ("scale", n[3]["values"])
assert n[5]["values"]["b"] == [1.0, 0.8, 0.1] and n[5]["t"] == "vec3", "other values were lost"
PY
loads=$(grep -o 'shader loaded ([0-9]*)' "$L" | tail -1 | grep -o '[0-9]*')
[ "${loads:-0}" -ge 3 ] || fail "the preview did not reload after the saves ($loads loads)"
echo "shader edit test: ok ($loads loads)"
