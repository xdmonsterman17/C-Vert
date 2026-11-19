#!/usr/bin/env sh
# Does the editor's Run path (--debug FILE) cost frames? Runs the house scene for SECONDS, alternating plain and --debug,
# ROUNDS times, and prints the average fps of each. On the REAL screen by default (windows pop up: leave the mouse alone);
# --virtual uses a private display (llvmpipe: only shows the CPU cost of the debugger, not the GPU / editor load).
#   tests/run_path_compare.sh [SECONDS=15] [ROUNDS=3] [--virtual]
# To include the editor's own load: start the C-Bricks editor with this project open (leave it idle) and run this again.
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SECS=${1:-15}; ROUNDS=${2:-3}
if [ "$3" = "--virtual" ] && [ -z "$PC_INNER" ]; then PC_INNER=1 exec xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" "$0" "$SECS" "$ROUNDS" --virtual; fi
BIN="$HERE/.build/native/meshforge"; [ -x "$BIN" ] || { echo "no built program: run tools/build.sh first"; exit 1; }
CMD="$HERE/.build/path_compare_cmds"; : > "$CMD"
mkdir -p "$HERE/tests/reports"; OUT="$HERE/tests/reports/path_compare.txt"; : > "$OUT"
for r in $(seq 1 "$ROUNDS"); do
  "$BIN" --cd "$HERE" --no-maximize --quit-after "$SECS" --obj assets/house.obj > "$HERE/.build/pc_plain.log" 2>&1
  echo "round $r plain: $(grep -o 'avg fps [0-9.]*' "$HERE/.build/pc_plain.log")" | tee -a "$OUT"
  "$BIN" --cd "$HERE" --no-maximize --quit-after "$SECS" --obj assets/house.obj --debug "$CMD" > "$HERE/.build/pc_debug.log" 2>&1
  echo "round $r debug: $(grep -o 'avg fps [0-9.]*' "$HERE/.build/pc_debug.log")" | tee -a "$OUT"
done
