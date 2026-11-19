#!/usr/bin/env sh
# Runs INSIDE a virtual X display (tests/run_shader_graph_test.sh starts it): with the real mouse and keyboard deletes a node, adds one
# from the right-click palette, wires it, picks a wire up and drops it on empty space, and changes a node's type. The file is checked after.
set -e
LOG="$HERE/tests/reports/shader_graph.log"; F="$HERE/tests/reports/shader_graph.cbshader"
cp tests/data/checker.cbshader "$F"
./.build/native/meshforge --cd "$PWD" --no-maximize --shader "$F" --debug-nodes 1 --quit-after 200 > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
i=0; while ! grep -q '\[shed\] zoom' "$LOG" 2>/dev/null; do i=$((i+1)); [ $i -gt 300 ] && { echo "never laid out"; cat "$LOG"; exit 1; }; sleep 0.1; done
WID=""; i=0; while [ -z "$WID" ]; do WID=$(xdotool search --name C-Vert 2>/dev/null | head -1); i=$((i+1)); [ $i -gt 50 ] && { echo "no window"; exit 1; }; sleep 0.1; done
xdotool windowfocus "$WID" 2>/dev/null || true
sleep 0.5
hold() { xdotool keydown "$1"; sleep 0.2; xdotool keyup "$1"; sleep 0.2; }
pos() { grep "\[shed\] $1" "$LOG" | head -1 | awk '{ print $(NF-1), $NF }'; }
click() { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.3; xdotool mousedown "$3"; sleep 0.25; xdotool mouseup "$3"; sleep 0.4; }
drag() { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.3; xdotool mousedown 1; sleep 0.3
  xdotool mousemove --window "$WID" $((($1 + $3) / 2)) $((($2 + $4) / 2)); sleep 0.2
  xdotool mousemove --window "$WID" "$3" "$4"; sleep 0.3; xdotool mouseup 1; sleep 0.6; }
Z=$(grep '\[shed\] zoom' "$LOG" | awk '{ print $3 }')
# 1. delete Base color (id 4): select it, press X
set -- $(pos "node 4 head"); BX=$1; BY=$2
click $BX $BY 1
hold x; sleep 0.5
# 2. add a Value node by right clicking where it was and typing "value", Enter
click $BX $BY 3
for k in v a l u e; do hold $k; done
hold Return; sleep 0.6
# 3. wire the new node's output (id 6) to Mix's input a
OUTX=$(awk "BEGIN { printf \"%d\", $BX + 190 * $Z }"); OUTY=$(awk "BEGIN { printf \"%d\", $BY + 38 * $Z }")
set -- $(pos "port 5 in a"); drag $OUTX $OUTY $1 $2
# 4. pick up Mix's t wire and drop it on empty space: it is deleted
set -- $(pos "port 5 in t"); TX=$1; TY=$2
drag $TX $TY $((TX + 20)) $((TY + 160))
# 5. click the new node's type badge: float -> vec2
click $(awk "BEGIN { printf \"%d\", $BX + 170 * $Z }") $(awk "BEGIN { printf \"%d\", $BY + 10 * $Z }") 1
sleep 0.5
