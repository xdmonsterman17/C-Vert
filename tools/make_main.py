#!/usr/bin/env python3
"""Writes main.cbricks: C-Vert's brick graph.

  python3 tools/make_main.py

The graph is only the SHELL (window, frame loop, a start-up check); the program is the C in
core and app. It follows C-Bricks' own generator pattern (tests/make_*.py there).
Edit the graph in the C-Bricks editor if you like: this script then overwrites it, so either
change this script or stop using it (say so in DECISIONS.md). Bricks are placed in plain columns;
open it in the editor and press Arrange for the editor's own layout.

    When program starts
      -> Open window
      -> Call C: mf_app_init -> failed
      -> If failed != 0: Print + Exit program 1
      -> Game loop
           Frame: Clear window -> mf_app_update -> Draw 3D { mf_app_draw3d } -> mf_app_draw_hud
           Done:  mf_app_shutdown
"""
import json, os

nodes = []
nid = [0]


def node(t, fields=None, args=None, outs=None, links=None, note=None, at=(0, 0)):
    nid[0] += 1
    n = {"id": nid[0], "type": t, "pos": [float(at[0]), float(at[1])], "size": [0.0, 0.0],
         "fields": fields or {}, "args": args or {},
         "outs": {k: {"var": v, "use": False} for k, v in (outs or {}).items()},
         "links": links or {}}
    if note:
        n["note"] = note
    nodes.append(n)
    return nid[0]


def link(i, group, ids):
    next(n for n in nodes if n["id"] == i)["links"][group] = ids


I = lambda x: {"type": "int", "value": str(x)}
S = lambda x: {"type": "string", "value": x}
V = lambda v: {"var": v}


def call_c(name, header, at, returns="void", outs=None, note=None):
    return node("call_c", {"name": name, "returns": returns, "args": [], "header": header},
                outs=outs, links={"next": []}, note=note, at=at)


X = 280.0   # column spacing
Y = 140.0   # row spacing

start = node("start", links={"out": []}, at=(32, 32), note="main(): the shell. The program is C: core + app.")
win = node("window", {"esc_closes": False}, {"width": I(960), "height": I(540), "title": S("C-Vert"), "fps": I(60)},
           links={"next": []}, at=(32 + X, 32))
init = call_c("mf_app_init", "mf_app.h", (32 + 2 * X, 32), "int", {"result": "failed"},
              note="Loads --obj FILE (else a cube). Gives back 1 when it can't.")
check = node("if", {"operator": "!="}, {"a": V("failed"), "b": I(0)}, links={"true": [], "false": [], "next": []},
             at=(32 + 3 * X, 32))
say = node("print", {"newline": True}, {"value": S("C-Vert: couldn't start (the reason is printed above)")},
           links={"next": []}, at=(32 + 3 * X, 32 + Y))
quit_ = node("quit", args={"code": I(1)}, at=(32 + 3 * X, 32 + 2 * Y))
loop = node("game_loop", links={"frame": [], "done": []}, at=(32 + 4 * X, 32))
clear = node("clear", args={"color": S("#23252e")}, links={"next": []}, at=(32, 32 + 4 * Y))
update = call_c("mf_app_update", "mf_app.h", (32 + X, 32 + 4 * Y), note="Input, camera, picking, edits (and the --demo script).")
d3 = node("draw3d", links={"draw": [], "next": []}, at=(32 + 2 * X, 32 + 4 * Y))
draw = call_c("mf_app_draw3d", "mf_app.h", (32 + 3 * X, 32 + 5 * Y), note="Grid, solid mesh, edges, selection outline.")
hud = call_c("mf_app_draw_hud", "mf_app.h", (32 + 3 * X, 32 + 4 * Y), note="Stats line and key help (2D, on top).")
down = call_c("mf_app_shutdown", "mf_app.h", (32 + 4 * X, 32 + Y), note="Game loop's Done: frees the mesh and undo history.")

link(start, "out", [win])
link(win, "next", [init])
link(init, "next", [check])
link(check, "true", [say])
link(check, "next", [loop])
link(say, "next", [quit_])
link(loop, "frame", [clear])
link(loop, "done", [down])
link(clear, "next", [update])
link(update, "next", [d3])
link(d3, "draw", [draw])
link(d3, "next", [hud])

frames = [{"id": 50, "title": "Frame: runs every frame until the window closes", "color": "#3a5a7a", "fit": True,
           "nodes": [clear, update, d3, draw, hud], "pos": [0.0, 0.0], "size": [0.0, 0.0]}]

graph = {"format": "c-bricks", "version": 4, "target": "native", "next_id": 100, "nodes": nodes, "frames": frames}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "main.cbricks")
with open(out, "w") as f:
    json.dump(graph, f, indent="\t", sort_keys=True)
print("wrote", os.path.normpath(out), "(%d bricks)" % len(nodes))
