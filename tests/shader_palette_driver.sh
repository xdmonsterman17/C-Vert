#!/usr/bin/env sh
# Runs INSIDE a virtual X display (tests/run_shader_palette_test.sh starts it). Window 960x540: the workspace tabs sit at the top left
# (Modeling x 8..92, Shader x 92..176, y 3..27, then Import model x 184..296); the graph editor is the bottom 45% (from y 297) with the
# palette sidebar at its left (x 0..200): its first list row is at y 377.
set -e
LOG="$HERE/tests/reports/shader_palette.log"; F="$HERE/tests/reports/shader_palette.cbshader"
cp tests/data/checker.cbshader "$F"
./.build/native/meshforge --cd "$PWD" --no-maximize --shader "$F" --debug-nodes 1 --quit-after 120 > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
i=0; while ! grep -q '\[shed\] zoom' "$LOG" 2>/dev/null; do i=$((i+1)); [ $i -gt 300 ] && { echo "never laid out"; cat "$LOG"; exit 1; }; sleep 0.1; done
WID=""; i=0; while [ -z "$WID" ]; do WID=$(xdotool search --name C-Vert 2>/dev/null | head -1); i=$((i+1)); [ $i -gt 50 ] && { echo "no window"; exit 1; }; sleep 0.1; done
xdotool windowfocus "$WID" 2>/dev/null || true
sleep 0.5
click() { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.3; xdotool mousedown "$3"; sleep 0.25; xdotool mouseup "$3"; sleep 0.4; }
click 50 15 1            # Modeling tab: the workspace closes
click 130 15 1           # Shader tab: it opens again on the same file
sleep 1
click 240 15 1           # Import model: the path box opens with assets/ in it
xdotool type --delay 80 house.obj; sleep 0.4
xdotool keydown Return; sleep 0.2; xdotool keyup Return; sleep 1
click 130 389 1          # the first node of the list (Input > Base color): added at the canvas middle
# drag the second one onto the canvas
xdotool mousemove --window "$WID" 130 413; sleep 0.3; xdotool mousedown 1; sleep 0.3
xdotool mousemove --window "$WID" 400 440; sleep 0.2; xdotool mousemove --window "$WID" 600 470; sleep 0.3; xdotool mouseup 1; sleep 0.8
sleep 1
