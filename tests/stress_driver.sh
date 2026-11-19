#!/usr/bin/env sh
# Runs INSIDE a virtual X display (tests/run_stress_test.sh): a 30k-vertex sphere through the editing paths that cost something per
# frame: Mesh mode with the visibility pass, orbiting, a select-all + grab (every frame the mesh moves), an extrude, undo, a click pick.
set -e
LOG="$HERE/.build/stress.log"
./.build/native/meshforge --cd "$PWD" --obj "$BIG" --prof --quit-after 45 --shot stress.png > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
i=0; while ! grep -q 'ready:' "$LOG" 2>/dev/null; do i=$((i+1)); [ $i -gt 300 ] && { echo "never became ready"; cat "$LOG"; exit 1; }; sleep 0.1; done
WID=""; i=0; while [ -z "$WID" ]; do WID=$(xdotool search --name C-Vert 2>/dev/null | head -1); i=$((i+1)); [ $i -gt 50 ] && { echo "no window found"; exit 1; }; sleep 0.1; done
xdotool windowfocus "$WID" 2>/dev/null || true
sleep 1.5
hold_key() { xdotool keydown "$1"; sleep 0.4; xdotool keyup "$1"; sleep 0.5; }
click()    { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.4; xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.6; }
click 480 270                        # select the object
hold_key Tab                         # Mesh mode: the visibility pass starts
sleep 2.0
xdotool keydown Left; sleep 2.0; xdotool keyup Left; sleep 1.0   # orbit: every frame a new eye
click 480 270                        # pick a vertex
hold_key a                           # select all
hold_key g                           # grab: the whole mesh moves each frame
xdotool mousemove --window "$WID" 500 270; sleep 0.5; xdotool mousemove --window "$WID" 540 280; sleep 0.8; xdotool mousemove --window "$WID" 580 290; sleep 0.8
xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.8       # confirm
xdotool keydown ctrl; sleep 0.15; xdotool keydown z; sleep 0.4; xdotool keyup z; xdotool keyup ctrl; sleep 1.0   # undo
hold_key 3                           # face select
click 480 270
hold_key e                           # extrude
xdotool mousemove --window "$WID" 490 265; sleep 0.8; xdotool mousemove --window "$WID" 500 260; sleep 0.8
xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 1.0
sleep 1.0
wait $PID || true
