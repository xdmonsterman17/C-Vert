#!/usr/bin/env sh
# Runs INSIDE a virtual X display (tests/run_input_test.sh starts it): launches the program and drives
# it with real mouse and keyboard events, so the input code (picking, keys, orbit) is exercised, not
# just the scripted demo. Each press is HELD for a few frames: raylib reads input once per frame.
# Expects: HERE (the workspace), run from the workspace root.
set -e
LOG="$HERE/.build/input.log"
rm -f "$LOG" input.png .build/input_out.obj
./.build/native/meshforge --cd "$PWD" --quit-after 160 --shot input.png --save .build/input_out.obj > "$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT   # a failed step must not leave the program running
# wait for the program to say it's ready (raylib compile is done; this is just the window opening)
i=0; while ! grep -q 'ready:' "$LOG" 2>/dev/null; do i=$((i+1)); [ $i -gt 200 ] && { echo "never became ready"; cat "$LOG"; exit 1; }; sleep 0.1; done
WID=""; i=0; while [ -z "$WID" ]; do WID=$(xdotool search --name C-Vert 2>/dev/null | head -1); i=$((i+1)); [ $i -gt 50 ] && { echo "no window found"; exit 1; }; sleep 0.1; done
xdotool windowfocus "$WID" 2>/dev/null || true
sleep 1.0
hold_key() { xdotool keydown "$1"; sleep 0.3; xdotool keyup "$1"; sleep 0.3; }
click()    { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.3; xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.3; }

combo()    { xdotool keydown "$1"; sleep 0.15; xdotool keydown "$2"; sleep 0.3; xdotool keyup "$2"; xdotool keyup "$1"; sleep 0.3; }

hold_key g               # Object mode, nothing selected: refuses
click 480 270            # Object mode: a click on the cube selects the object
click 60 480             # a click on empty space deselects it
click 480 270            # select again (it stays selected into Mesh mode)
hold_key Tab             # Mesh mode
hold_key 3               # face select
click 480 270            # the middle of the view: a cube face
hold_key e               # extrude it: the face follows the mouse until a click
click 520 220            # (the move, then the click confirms)
combo ctrl z             # undo
hold_key e               # and again
click 520 220
xdotool mousemove --window "$WID" 480 270; sleep 0.2
xdotool mousedown 2; sleep 0.3                       # middle-drag: orbit
for d in 20 40 60 80 100 120; do xdotool mousemove --window "$WID" $((480 + d)) 270; sleep 0.1; done
xdotool mouseup 2; sleep 0.3
hold_key g                                           # grab the selected face with the mouse
xdotool mousemove --window "$WID" 500 260; sleep 0.2
xdotool mousemove --window "$WID" 560 240; sleep 0.3
xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.3   # left click confirms
hold_key g; hold_key Escape                          # a grab, cancelled
xdotool mousemove --window "$WID" 480 270; sleep 0.2
xdotool mousedown 3; sleep 0.3                       # right-hold: fly forward
xdotool keydown w; sleep 0.6; xdotool keyup w
hold_key Tab; hold_key Tab                           # Tab while flying: Object mode, and back to Mesh mode
xdotool mouseup 3; sleep 0.3
combo ctrl s             # save
hold_key 1               # vertex mode
hold_key a               # all vertices
hold_key e               # extrude them (the faces they cover) ...
hold_key Escape          # ... and cancel: everything goes back
combo alt a              # select none
xdotool mousemove --window "$WID" 200 300; sleep 0.3
combo shift a            # the Add menu opens at the mouse
click 240 412            # its 5th item: Cylinder (menu top + 4 + 4 rows of 24 + half a row)

# ---- objects, tools, gizmo, box select ----
drag()     { xdotool mousemove --window "$WID" "$1" "$2"; sleep 0.3; xdotool mousedown 1; sleep 0.3
             for k in 1 2 3 4 5 6; do xdotool mousemove --window "$WID" $(python3 -c "print(int($1+($3-$1)*$k/6), int($2+($4-$2)*$k/6))"); sleep 0.1; done
             sleep 0.2; xdotool mouseup 1; sleep 0.4; }
gizmo()    { grep '^\[meshforge\] gizmo' "$LOG" | tail -1; }
hold_key Tab             # back to Object mode (the mesh edit is written back to the object)
hold_key f               # frame everything
xdotool mousemove --window "$WID" 200 300; sleep 0.3
combo shift a            # Object mode: the Add menu makes a NEW OBJECT
click 240 436            # its 6th item: Cone
click 30 88              # tool bar: Move
sleep 0.5
G=$(gizmo)
echo "$G" | grep -q 'gizmo Move' || { echo "no Move gizmo logged"; exit 1; }
# drag the X arrow: from 60 % of the way to its tip, to 100 %
read AX AY BX BY <<EOF2
$(echo "$G" | python3 -c "
import re,sys
n=list(map(int,re.findall(r'-?\d+',sys.stdin.read().split('gizmo')[1])))
cx,cy,xx,xy=n[0],n[1],n[2],n[3]
print(int(cx+(xx-cx)*0.6),int(cy+(xy-cy)*0.6),int(cx+(xx-cx)*1.0),int(cy+(xy-cy)*1.0))")
EOF2
drag $AX $AY $BX $BY     # the object moves along X only
click 30 124             # tool bar: Rotate
sleep 0.5
G=$(gizmo)
read AX AY BX BY <<EOF2
$(echo "$G" | python3 -c "
import re,sys,math
n=list(map(int,re.findall(r'-?\d+',sys.stdin.read().split('gizmo')[1])))
cx,cy,yx,yy=n[0],n[1],n[4],n[5]
a=math.radians(40); dx,dy=yx-cx,yy-cy
print(yx,yy,int(cx+dx*math.cos(a)-dy*math.sin(a)),int(cy+dx*math.sin(a)+dy*math.cos(a)))")
EOF2
drag $AX $AY $BX $BY     # the Y ring: turns the object about Y
click 30 160             # tool bar: Scale
sleep 0.5
G=$(gizmo)
read AX AY BX BY <<EOF2
$(echo "$G" | python3 -c "
import re,sys
n=list(map(int,re.findall(r'-?\d+',sys.stdin.read().split('gizmo')[1])))
cx,cy,zx,zy=n[0],n[1],n[6],n[7]
print(int(cx+(zx-cx)*0.5),int(cy+(zy-cy)*0.5),int(cx+(zx-cx)*1.4),int(cy+(zy-cy)*1.4))")
EOF2
drag $AX $AY $BX $BY     # the Z scale handle
click 30 52              # tool bar: Box
drag 100 200 700 500     # a box in empty-ish space: selects the objects with a vertex inside
hold_key Tab             # Mesh mode of the active object
drag 100 200 700 500     # a box in Mesh mode (vertex select)
xdotool mousemove --window "$WID" 200 300; sleep 0.3
combo shift a            # Mesh mode: the Add menu puts the shape INSIDE this object
click 240 340            # its 2nd item: Cube

# ---- select linked, X-ray, duplicate, snap, rename, parenting, tabs ----
combo alt a              # Mesh mode of the Cone (+ the Cube inside it): select none
xdotool mousemove --window "$WID" 482 272; sleep 0.3   # (not 480 270: GLFW drops the first motion onto the spot it warped the pointer back to after the right-hold)
hold_key l               # L: everything connected to what is under the mouse
combo alt z              # X-ray on
click 480 46              # the view bar's X-ray button (top centre): off again
hold_key Tab             # Object mode (both objects are still selected)
combo shift d            # Shift+D: duplicate both, grab the copies
xdotool keydown ctrl; sleep 0.2
xdotool mousemove --window "$WID" 330 270; sleep 0.2
xdotool mousemove --window "$WID" 390 250; sleep 0.3
xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.3   # Ctrl held: the move snaps
xdotool keyup ctrl; sleep 0.3
hold_key F2              # rename the active object
combo shift w; hold_key h; hold_key e; hold_key e; hold_key l; hold_key Return
combo ctrl p             # the other copy becomes its child
click 850 113            # the hierarchy row of "Wheel" (third row): select only it
hold_key g; hold_key x; hold_key 1; hold_key Return   # G X 1 Enter: the child must come along
xdotool keydown ctrl; sleep 0.3                      # Ctrl held BEFORE the transform starts: it must snap from the first frame
xdotool keydown g; sleep 0.3; xdotool keyup g; sleep 0.2
xdotool mousemove --window "$WID" 400 300; sleep 0.2
xdotool mousemove --window "$WID" 460 270; sleep 0.3
xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.3
xdotool keyup ctrl; sleep 0.3
click 930 193            # View tab
click 800 193            # Mesh tab
click 740 193            # Item tab
click 860 193            # Mods tab
click 830 227            # Add Modifier (the button under the tab strip; the menu opens below it) ...
click 780 258            # ... Mirror (first item)
click 830 227
click 780 282            # ... Subdivision (second)
click 830 227
click 780 306            # ... Array (third)
click 925 361            # the Subdivision panel's Levels field, right arrow: level 2
click 897 261            # Apply on the Mirror panel's header
sleep 1
wait $PID || true
