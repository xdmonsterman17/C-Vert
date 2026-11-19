#!/usr/bin/env sh
# Multi-select, copy / paste, delete, undo / redo and New file with the real mouse and keyboard, files checked.   tests/run_shader_history_test.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export HERE
L="$HERE/tests/reports/shader_history.log"; F="$HERE/tests/reports/shader_history.cbshader"
fail() { echo "FAIL: $*"; echo "--- log ---"; cat "$L" 2>/dev/null; exit 1; }
command -v xdotool >/dev/null || { echo "(xdotool missing) skipped"; exit 0; }
[ -x "$HERE/.build/native/meshforge" ] || fail "no built program: run tools/build.sh first"
cd "$HERE"; mkdir -p tests/reports
export CBRICKS_CONFIG="$HERE/tests/reports/shader_config"
LP_NUM_THREADS=2 nice -n 10 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" sh "$HERE/tests/shader_history_driver.sh" || fail "the driver failed"
python3 - "$F" "$HERE/tests/reports/untitled.cbshader" <<'PY' || fail "a saved file is wrong (see tests/reports)"
import json, sys
g = json.load(open(sys.argv[1]))
n = {x["id"]: x for x in g["nodes"]}
assert sorted(n) == [1, 2, 3, 4, 5, 6, 7], ("nodes", sorted(n))
assert n[6]["type"] == "base" and n[7]["type"] == "mix" and n[7]["t"] == "vec3", (n[6], n[7])
assert n[7]["values"]["b"] == [1.0, 0.8, 0.1], n[7]
assert n[6]["pos"][1] < n[4]["pos"][1], ("pasted at the mouse, above", n[6]["pos"])
w = {(x["from"], x["out"], x["to"], x["in"]) for x in g["wires"]}
assert (6, "color", 7, "a") in w and len(w) == 5, ("wires", w)
u = json.load(open(sys.argv[2]))
assert u["kind"] == "post" and [x["type"] for x in u["nodes"]] == ["post_out"] and u["wires"] == [], u
PY
loads=$(grep -o 'shader loaded ([0-9]*)' "$L" | tail -1 | grep -o '[0-9]*')
[ "${loads:-0}" -ge 6 ] || fail "the preview did not reload after the saves ($loads loads)"
echo "shader history test: ok ($loads loads)"
