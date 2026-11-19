#!/usr/bin/env sh
# Runs INSIDE a virtual X display (tests/run_polish_test.sh): Ctrl+Q quits at once when nothing changed, and asks first when something did.
set -e
LOG="$HERE/.build/quit.log"
start() {
    ./.build/native/meshforge --cd "$PWD" --quit-after 100 --save .build/quit_out.obj > "$LOG" 2>&1 &
    PID=$!
    i=0; while ! grep -q 'ready:' "$LOG" 2>/dev/null; do i=$((i+1)); [ $i -gt 200 ] && { echo "never became ready"; exit 1; }; sleep 0.1; done
    WID=""; i=0; while [ -z "$WID" ]; do WID=$(xdotool search --name C-Vert 2>/dev/null | head -1); i=$((i+1)); [ $i -gt 50 ] && { echo "no window found"; exit 1; }; sleep 0.1; done
    xdotool windowfocus "$WID" 2>/dev/null || true
    sleep 1.0
}
combo() { xdotool keydown "$1"; sleep 0.15; xdotool keydown "$2"; sleep 0.3; xdotool keyup "$2"; xdotool keyup "$1"; sleep 0.3; }
alive() { kill -0 "$PID" 2>/dev/null; }
gone_within() { n=0; while alive && [ $n -lt 40 ]; do n=$((n+1)); sleep 0.1; done; ! alive; }
trap 'kill $PID 2>/dev/null || true' EXIT
start
combo ctrl q                                    # nothing changed: it quits
gone_within || { echo "Ctrl+Q with no changes didn't quit"; exit 1; }
start
xdotool mousemove --window "$WID" 480 270; sleep 0.3; xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.4
xdotool keydown shift; sleep 0.15; xdotool keydown d; sleep 0.3; xdotool keyup d; xdotool keyup shift; sleep 0.3
xdotool keydown Return; sleep 0.3; xdotool keyup Return; sleep 0.4                    # Shift+D, Enter: an edit
combo ctrl q                                    # the first Ctrl+Q only asks
sleep 1.5
alive || { echo "Ctrl+Q with unsaved changes quit without asking"; exit 1; }
grep -q 'unsaved changes' "$LOG" || { echo "no 'unsaved changes' question"; exit 1; }
combo ctrl q                                    # the second one quits
gone_within || { echo "the second Ctrl+Q didn't quit"; exit 1; }
echo "quit test: ok"
