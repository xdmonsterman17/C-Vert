#!/usr/bin/env sh
# Runs INSIDE a virtual X display (tests/run_ui_test.sh): real clicks on the outliner and the modifier panels
# (window 960 x 540: the layout numbers below come from app/mf_app.c: panel_x 722, outliner rows 22 px, Mods tab under it).
set -e
LOG="$HERE/.build/ui.log"
./.build/native/meshforge --cd "$PWD" --quit-after 24 --shot ui.png > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
i=0; while ! grep -q 'ready:' "$LOG" 2>/dev/null; do i=$((i+1)); [ $i -gt 200 ] && { echo "never became ready"; cat "$LOG"; exit 1; }; sleep 0.1; done
WID=""; i=0; while [ -z "$WID" ]; do WID=$(xdotool search --name C-Vert 2>/dev/null | head -1); i=$((i+1)); [ $i -gt 50 ] && { echo "no window found"; exit 1; }; sleep 0.1; done
xdotool windowfocus "$WID" 2>/dev/null || true
sleep 1.0
click() { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.3; xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.4; }
hold_key() { xdotool keydown "$1"; sleep 0.3; xdotool keyup "$1"; sleep 0.3; }
click 480 270                  # select the cube object
xdotool keydown shift; sleep 0.15; xdotool keydown d; sleep 0.3; xdotool keyup d; xdotool keyup shift; sleep 0.3
hold_key Return                # Shift+D then Enter: a copy, left in place: two rows in the outliner
# drag the copy's row onto the first row: it becomes that one's child (a fold arrow appears)
xdotool mousemove --window "$WID" 800 91; sleep 0.3; xdotool mousedown 1; sleep 0.3
xdotool mousemove --window "$WID" 800 80; sleep 0.2; xdotool mousemove --window "$WID" 800 69; sleep 0.3; xdotool mouseup 1; sleep 0.5
click 866 148                  # the Mods tab (third of four)
click 830 183                  # Add Modifier
click 780 262                  # ... Array (third item)
click 830 183
click 780 238                  # Add Modifier again: Subdivision (second item)
click 864 248                  # the Array panel: axis Y
click 925 275                  # Count +
click 925 302                  # Offset +
click 856 340                  # the Subdivision panel's eye: off
click 856 340                  # ... on again
sleep 0.5
click 734 69                   # the fold arrow of the first row: hides its child
sleep 0.5
wait $PID || true
