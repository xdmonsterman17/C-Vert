#!/usr/bin/env sh
# End-to-end check of the C-Bricks project: strict build, then the scripted --demo run on a virtual
# display, then assertions on what it printed, the OBJ it saved and the screenshot it took.
# Slow part: raylib compiles once per computer (cached by C-Bricks afterwards).
#   tests/run_app_test.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
CBRICKS_DIR="${CBRICKS_DIR:-$HOME/Documents/Projects/Godot/C-Bricks}"
OUT="$HERE/.build"
fail() { echo "FAIL: $*"; exit 1; }

echo "== strict build (-std=c99 -Wall -Wextra -pedantic) =="
"$HERE/tools/build.sh" strict | tee "$HERE/tests/reports/build.log"
grep -q "^built:" "$HERE/tests/reports/build.log" || fail "no program was built"
grep -qiE "warning|error" "$HERE/tests/reports/build.log" && fail "the strict build printed warnings (above)"

echo "== scripted demo on a house.obj =="
cd "$HERE"
rm -f demo.png "$OUT/demo_out.obj"
"$CBRICKS_DIR/tests/offscreen.sh" timeout 120 ./.build/native/meshforge --cd "$PWD" --demo \
    --obj assets/house.obj --save .build/demo_out.obj --shot demo.png > "$OUT/demo.log" 2>&1 || fail "the demo exited with an error: $(tail -3 "$OUT/demo.log")"
cat "$OUT/demo.log"
grep -q 'ready: assets/house.obj  V10 E17 F9 euler 2' "$OUT/demo.log" || fail "start-up stats differ"
grep -q '\[demo\] V14 E25 F13 euler 2 boundary 0 nonmanifold 0' "$OUT/demo.log" || fail "stats after the extrude differ"
grep -q 'saved .build/demo_out.obj' "$OUT/demo.log" || fail "the demo didn't save"

echo "== the saved OBJ is a closed manifold =="
"$HERE/tools/objinfo.sh" "$OUT/demo_out.obj" | tee "$OUT/objinfo.log"
grep -q 'V14 E25 F13 euler 2 boundary 0 nonmanifold 0' "$OUT/objinfo.log" || fail "the saved mesh differs"

echo "== the screenshot isn't blank =="
[ -f demo.png ] || fail "no screenshot"
SIZE=$(wc -c < demo.png)
[ "$SIZE" -gt 15000 ] || fail "screenshot is only $SIZE bytes (blank?)"
python3 "$HERE/tests/png_stats.py" demo.png | tee "$OUT/png.log"
ORANGE=$(sed -n 's/.*orange pixels \([0-9]*\).*/\1/p' "$OUT/png.log")
COLOURS=$(sed -n 's/.*colours \([0-9]*\).*/\1/p' "$OUT/png.log")
[ "$ORANGE" -gt 3000 ] || fail "the selected face isn't orange in the screenshot ($ORANGE orange pixels)"
[ "$COLOURS" -gt 5 ] || fail "the screenshot has only $COLOURS colours"
mv demo.png "$HERE/tests/reports/demo.png" 2>/dev/null || true
echo "app test: ok"
