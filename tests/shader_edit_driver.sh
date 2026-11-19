#!/usr/bin/env sh
# Runs INSIDE a virtual X display (tests/run_shader_edit_test.sh starts it): opens a copy of the checker graph, then with the real
# mouse and keyboard drags the Mix node, types a new Checker scale, clicks a field and cancels with Esc. The file is checked afterwards.
set -e
LOG="$HERE/tests/reports/shader_edit.log"; F="$HERE/tests/reports/shader_edit.cbshader"
cp tests/data/checker.cbshader "$F"
./.build/native/meshforge --cd "$PWD" --no-maximize --shader "$F" --debug-nodes 1 --quit-after 150 > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
i=0; while ! grep -q '\[shed\] zoom' "$LOG" 2>/dev/null; do i=$((i+1)); [ $i -gt 300 ] && { echo "never laid out"; cat "$LOG"; exit 1; }; sleep 0.1; done
WID=""; i=0; while [ -z "$WID" ]; do WID=$(xdotool search --name C-Vert 2>/dev/null | head -1); i=$((i+1)); [ $i -gt 50 ] && { echo "no window"; exit 1; }; sleep 0.1; done
xdotool windowfocus "$WID" 2>/dev/null || true
sleep 0.5
hold() { xdotool keydown "$1"; sleep 0.2; xdotool keyup "$1"; sleep 0.2; }   # raylib reads keys once per frame: hold each press
pos() { grep "\[shed\] $1" "$LOG" | head -1 | awk '{ print $(NF-1), $NF }'; }
# drag the Mix node (id 5) by its header
set -- $(pos "node 5 head"); X=$1; Y=$2
xdotool mousemove --window "$WID" $X $Y; sleep 0.3
xdotool mousedown 1; sleep 0.3
for d in 10 20 30 40 50; do xdotool mousemove --window "$WID" $((X + d)) $((Y + d)); sleep 0.1; done
xdotool mouseup 1; sleep 0.6
# type a new scale into Checker's (id 3) field: click, erase, "9", Enter
set -- $(pos "field 3 scale"); X=$1; Y=$2
xdotool mousemove --window "$WID" $X $Y; sleep 0.3
xdotool mousedown 1; sleep 0.2; xdotool mouseup 1; sleep 0.4
for k in 1 2 3 4 5 6 7 8; do hold BackSpace; done
hold 9
hold Return; sleep 0.6
# a field edit that is cancelled changes nothing: Mix's b (id 5), "7", Esc
set -- $(pos "field 5 b"); X=$(($1 + 50)); Y=$(($2 + 50))   # the node moved by the drag
xdotool mousemove --window "$WID" $X $Y; sleep 0.3
xdotool mousedown 1; sleep 0.2; xdotool mouseup 1; sleep 0.4
hold 7
hold Escape; sleep 0.6
