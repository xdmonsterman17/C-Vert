#!/usr/bin/env sh
# Runs INSIDE a virtual X display (tests/run_polish_test.sh): redo, a dragged modifier field, smooth shading, region inset,
# Alt+click edge loop, Ctrl+I, numpad views / frame selection, Shift+middle-drag pan, and the autosave backup.
# Layout numbers (window 960 x 540) come from app/mf_app.c like the UI driver's.
set -e
LOG="$HERE/.build/polish.log"
rm -f .build/polish_out.obj .build/polish_out.obj.bak
./.build/native/meshforge --cd "$PWD" --quit-after 70 --shot polish.png --save .build/polish_out.obj --autosave 3 > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
i=0; while ! grep -q 'ready:' "$LOG" 2>/dev/null; do i=$((i+1)); [ $i -gt 200 ] && { echo "never became ready"; cat "$LOG"; exit 1; }; sleep 0.1; done
WID=""; i=0; while [ -z "$WID" ]; do WID=$(xdotool search --name C-Vert 2>/dev/null | head -1); i=$((i+1)); [ $i -gt 50 ] && { echo "no window found"; exit 1; }; sleep 0.1; done
xdotool windowfocus "$WID" 2>/dev/null || true
sleep 1.0
hold_key() { xdotool keydown "$1"; sleep 0.3; xdotool keyup "$1"; sleep 0.3; }
combo()    { xdotool keydown "$1"; sleep 0.15; xdotool keydown "$2"; sleep 0.3; xdotool keyup "$2"; xdotool keyup "$1"; sleep 0.3; }
combo3()   { xdotool keydown "$1"; sleep 0.15; xdotool keydown "$2"; sleep 0.15; xdotool keydown "$3"; sleep 0.3; xdotool keyup "$3"; xdotool keyup "$2"; xdotool keyup "$1"; sleep 0.3; }
click()    { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.3; xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.4; }
at() { python3 - "$@" <<'PY'
import math, sys
pts = [tuple(map(float, a.split(","))) for a in sys.argv[1:]]
p = tuple(sum(q[i] for q in pts) / len(pts) for i in range(3))
yaw, pitch, fov, w, h = 0.6, 0.45, 50.0, 960, 540
dist = math.sqrt(3) / 2 / math.sin(math.radians(fov / 2)) * 1.15
eye = (dist * math.cos(pitch) * math.sin(yaw), dist * math.sin(pitch), dist * math.cos(pitch) * math.cos(yaw))
def norm(v): l = math.sqrt(sum(c * c for c in v)); return tuple(c / l for c in v)
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def dot(a, b): return sum(x * y for x, y in zip(a, b))
f = norm(tuple(-c for c in eye)); r = norm(cross(f, (0, 1, 0))); u = cross(r, f)
rel = tuple(p[i] - eye[i] for i in range(3)); z = dot(rel, f); half = math.tan(math.radians(fov / 2))
print(int((dot(rel, r) / (z * half * w / h) + 1) * 0.5 * w), int((1 - dot(rel, u) / (z * half)) * 0.5 * h))
PY
}

click 480 270                  # select the cube
click 866 126                  # Mods tab (one object: the tabs start at y 114)
click 830 161                  # Add Modifier
click 780 240                  # Array
# drag the Count field sideways: +4 steps
xdotool mousemove --window "$WID" 880 253; sleep 0.3; xdotool mousedown 1; sleep 0.3
xdotool mousemove --window "$WID" 900 253; sleep 0.2; xdotool mousemove --window "$WID" 920 253; sleep 0.2; xdotool mousemove --window "$WID" 936 253; sleep 0.3; xdotool mouseup 1; sleep 0.4
combo ctrl z                   # undo the drag (one step) ...
combo3 ctrl shift z            # ... and redo it
click 923 126                  # View tab
click 880 225                  # Shading: smooth
hold_key Tab                   # Mesh mode (the Array shows as its cage)
hold_key 3                     # face select
click 480 270                  # a face
hold_key i; hold_key i         # inset, then I again: the faces as one region
xdotool mousemove --window "$WID" 480 270; sleep 0.2; xdotool mousemove --window "$WID" 500 270; sleep 0.2; xdotool mousemove --window "$WID" 520 270; sleep 0.3
xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.4
hold_key 1                     # vertex mode
click $(at 0.5,-0.5,0.5)       # a vertex ...
xdotool mousemove --window "$WID" $(at 0,-0.5,0.5); sleep 0.3
xdotool keydown alt; sleep 0.15; xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.3; xdotool keyup alt; sleep 0.4   # Alt+click an edge: its loop
combo ctrl i                   # invert the selection
hold_key KP_7                  # top view
hold_key KP_1                  # front view
hold_key KP_3                  # right view
hold_key KP_Decimal            # frame the selection
xdotool keydown shift; sleep 0.15; xdotool mousemove --window "$WID" 400 250; sleep 0.2; xdotool mousedown 2; sleep 0.2
xdotool mousemove --window "$WID" 440 270; sleep 0.2; xdotool mousemove --window "$WID" 480 290; sleep 0.3; xdotool mouseup 2; xdotool keyup shift; sleep 0.4   # Shift + middle-drag: pan
sleep 4                        # the autosave (every 3 s here) writes the .bak
wait $PID || true
