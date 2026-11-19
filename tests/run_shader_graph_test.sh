#!/usr/bin/env sh
# The shader canvas adds, wires and deletes nodes with the real mouse and keyboard, file checked.   tests/run_shader_graph_test.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export HERE
L="$HERE/tests/reports/shader_graph.log"; F="$HERE/tests/reports/shader_graph.cbshader"
fail() { echo "FAIL: $*"; echo "--- log ---"; cat "$L" 2>/dev/null; exit 1; }
command -v xdotool >/dev/null || { echo "(xdotool missing) skipped"; exit 0; }
[ -x "$HERE/.build/native/meshforge" ] || fail "no built program: run tools/build.sh first"
cd "$HERE"; mkdir -p tests/reports
export CBRICKS_CONFIG="$HERE/tests/reports/shader_config"
LP_NUM_THREADS=2 nice -n 10 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" sh "$HERE/tests/shader_graph_driver.sh" || fail "the driver failed"
python3 - "$F" <<'PY' || fail "the saved file is wrong (see $F)"
import json, sys
g = json.load(open(sys.argv[1]))
n = {x["id"]: x for x in g["nodes"]}
assert sorted(n) == [1, 2, 3, 5, 6], ("nodes", sorted(n))
assert 4 not in n, "Base color was not deleted"
assert n[6]["type"] == "value", n[6]
assert n[6]["t"] == "vec2", ("type badge did not cycle", n[6])
w = {(x["from"], x["out"], x["to"], x["in"]) for x in g["wires"]}
assert w == {(2, "uv", 3, "uv"), (6, "out", 5, "a"), (5, "out", 1, "color")}, ("wires", w)
PY
loads=$(grep -o 'shader loaded ([0-9]*)' "$L" | tail -1 | grep -o '[0-9]*')
[ "${loads:-0}" -ge 4 ] || fail "the preview did not reload after the saves ($loads loads)"
echo "shader graph test: ok ($loads loads)"
