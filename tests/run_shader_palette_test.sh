#!/usr/bin/env sh
# The workspace tabs (Modeling / Shader) and the node palette sidebar, with the real mouse: switch to Modeling and back, click a
# palette entry (adds a node at the canvas middle), drag another onto the canvas (adds at the drop point), import an OBJ. File checked.   tests/run_shader_palette_test.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export HERE
L="$HERE/tests/reports/shader_palette.log"; F="$HERE/tests/reports/shader_palette.cbshader"
fail() { echo "FAIL: $*"; echo "--- log ---"; cat "$L" 2>/dev/null; exit 1; }
command -v xdotool >/dev/null || { echo "(xdotool missing) skipped"; exit 0; }
[ -x "$HERE/.build/native/meshforge" ] || fail "no built program: run tools/build.sh first"
cd "$HERE"; mkdir -p tests/reports
export CBRICKS_CONFIG="$HERE/tests/reports/shader_config"
LP_NUM_THREADS=2 nice -n 10 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" sh "$HERE/tests/shader_palette_driver.sh" || fail "the driver failed"
python3 - "$F" <<'PY' || fail "the saved file is wrong (see $F)"
import json, sys
g = json.load(open(sys.argv[1]))
types = sorted(x["type"] for x in g["nodes"])
base = sorted(x["type"] for x in json.load(open("tests/data/checker.cbshader"))["nodes"])
assert len(types) == len(base) + 2, ("expected two new nodes", types, base)
extra = list(types)
for t in base: extra.remove(t)
assert len(extra) == 2, extra
PY
grep -q 'workspace Modeling' "$L" && grep -q 'workspace Shader' "$L" || fail "the tabs did not switch both ways"
grep -q "imported assets/house.obj as house" "$L" || fail "the model was not imported"
echo "shader palette test: ok"
