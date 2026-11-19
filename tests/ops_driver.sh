#!/usr/bin/env sh
# Runs INSIDE a virtual X display (tests/run_ops_test.sh): drives the milestone 3 operators with real mouse and keys on the
# default cube: inset (I), delete (X), flip (Alt+N), merge (M, Alt+M), bevel (Ctrl+B), loop cut (Ctrl+R), each undone, then a loop cut kept and saved.
set -e
LOG="$HERE/.build/ops.log"
rm -f .build/ops_out.obj
./.build/native/meshforge --cd "$PWD" --quit-after 110 --shot ops.png --save .build/ops_out.obj > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
i=0; while ! grep -q 'ready:' "$LOG" 2>/dev/null; do i=$((i+1)); [ $i -gt 200 ] && { echo "never became ready"; cat "$LOG"; exit 1; }; sleep 0.1; done
WID=""; i=0; while [ -z "$WID" ]; do WID=$(xdotool search --name C-Vert 2>/dev/null | head -1); i=$((i+1)); [ $i -gt 50 ] && { echo "no window found"; exit 1; }; sleep 0.1; done
xdotool windowfocus "$WID" 2>/dev/null || true
sleep 1.0
hold_key() { xdotool keydown "$1"; sleep 0.3; xdotool keyup "$1"; sleep 0.3; }
combo()    { xdotool keydown "$1"; sleep 0.15; xdotool keydown "$2"; sleep 0.3; xdotool keyup "$2"; xdotool keyup "$1"; sleep 0.3; }
click()    { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.3; xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.3; }
# screen position of a cube corner / edge midpoint, from the default camera (yaw 0.6, pitch 0.45, fov 50, 960 x 540, framed)
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
slide() { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.2; xdotool mousemove --window "$WID" $(($1 + 20)) "$2"; sleep 0.2; xdotool mousemove --window "$WID" $(($1 + 40)) "$2"; sleep 0.3; }

click 480 270            # select the cube object
hold_key Tab             # Mesh mode
hold_key 3               # face select
click 480 270            # a face
hold_key i               # inset: the amount follows the mouse sideways
slide 480 270
xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.4   # click confirms
combo ctrl z
hold_key x               # delete the selected face
combo ctrl z
combo alt n              # flip it
combo ctrl z
hold_key 1               # vertex mode
hold_key a               # all vertices
hold_key m               # merge at centre: the cube collapses to one vertex
combo ctrl z
combo alt m              # merge by distance: nothing is that close
combo alt a              # select none
click $(at 0.5,0.5,0.5)  # the two ends of one edge
xdotool keydown shift; sleep 0.2; click $(at 0.5,0.5,-0.5); xdotool keyup shift; sleep 0.3
xdotool mousemove --window "$WID" 480 270; sleep 0.2
xdotool keydown ctrl; sleep 0.15; xdotool keydown b; sleep 0.3; xdotool keyup b; xdotool keyup ctrl; sleep 0.3   # bevel
slide 480 270
xdotool click 4; sleep 0.3; xdotool click 4; sleep 0.3   # the wheel: two more segments (3), the shape follows
hold_key p               # P: the profile one step rounder / squarer
xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.4
combo ctrl z
combo alt a
hold_key 3                # face select, X-ray on: the dot of the face at the BACK of the cube picks that face, through the mesh
combo alt z
click $(at -0.5,0,0)
combo alt z
combo alt a
click 480 270            # a face
hold_key h               # H: hide it
combo alt h              # Alt+H: show it again (and select it)
combo alt a
xdotool mousemove --window "$WID" $(at 0.5,0.5,0.5 -0.5,0.5,0.5); sleep 0.4   # the top front edge
xdotool keydown ctrl; sleep 0.15; xdotool keydown r; sleep 0.3; xdotool keyup r; xdotool keyup ctrl; sleep 0.4   # loop cut
combo ctrl s             # save: a cube with one loop cut
sleep 1
wait $PID || true
