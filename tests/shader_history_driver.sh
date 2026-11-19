#!/usr/bin/env sh
# Runs INSIDE a virtual X display (tests/run_shader_history_test.sh starts it): select two nodes with shift, copy, paste, delete, undo, redo,
# then the "New post" button makes and opens a new file. The files are checked after.
set -e
LOG="$HERE/tests/reports/shader_history.log"; F="$HERE/tests/reports/shader_history.cbshader"
rm -f "$HERE/tests/reports/untitled.cbshader"
cp tests/data/checker.cbshader "$F"
./.build/native/meshforge --cd "$PWD" --no-maximize --shader "$F" --debug-nodes 1 --quit-after 220 > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
i=0; while ! grep -q '\[shed\] zoom' "$LOG" 2>/dev/null; do i=$((i+1)); [ $i -gt 300 ] && { echo "never laid out"; cat "$LOG"; exit 1; }; sleep 0.1; done
WID=""; i=0; while [ -z "$WID" ]; do WID=$(xdotool search --name C-Vert 2>/dev/null | head -1); i=$((i+1)); [ $i -gt 50 ] && { echo "no window"; exit 1; }; sleep 0.1; done
xdotool windowfocus "$WID" 2>/dev/null || true
sleep 0.5
hold() { xdotool keydown "$1"; sleep 0.2; xdotool keyup "$1"; sleep 0.2; }
ctrl() { xdotool keydown Control_L; sleep 0.15; hold "$1"; xdotool keyup Control_L; sleep 0.4; }
pos() { grep "\[shed\] $1" "$LOG" | head -1 | awk '{ print $(NF-1), $NF }'; }
click() { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.3; xdotool mousedown 1; sleep 0.25; xdotool mouseup 1; sleep 0.4; }
set -- $(pos "node 4 head"); BX=$1; BY=$2
set -- $(pos "node 5 head"); MX=$1; MY=$2
click $BX $BY
xdotool keydown Shift_L; sleep 0.2; click $MX $MY; xdotool keyup Shift_L; sleep 0.4
ctrl c
xdotool mousemove --window "$WID" $((BX - 100)) $((BY - 100)); sleep 0.4
ctrl v          # nodes 6 and 7, with the wire between them
hold x; sleep 0.5        # both pasted nodes are selected: gone
ctrl z; sleep 0.3        # back
ctrl z; sleep 0.3        # the paste undone
ctrl y; sleep 0.5        # redone
# the New post button
set -- $(pos "button post"); click $1 $2
sleep 1.0
