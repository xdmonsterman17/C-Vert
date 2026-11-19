# Debugging and testing

Bugs themselves live in `BUGS.md`; this is the process and the tools.

## The short version
1. `tests/run_all.sh` (everything, about 1-3 minutes; `--core` for the 5-second part).
2. Read what failed: every script prints a one-line `FAIL:` and the log it checked.
3. Look at pictures: `tests/reports/demo.png` (scripted demo), `input.png` (real input), `layout_main.png`
   (the editor with the graph). Use the `Read` tool on them.
4. Found a bug? Add a test that shows it, add it to `BUGS.md` Open, fix, move it to Fixed with the guard.

## The layers
| Layer | What | Where |
|---|---|---|
| Core unit tests | 156 checks on topology, extrude (one face, a region, an open mesh, nothing, everything), selection, clone, OBJ forms / errors / round trip, non-manifold detection, picking, camera rays, bounds. Strict flags (`-std=c99 -Wall -Wextra -pedantic -Werror`) plus UBSan. | `tests/test_mesh.c`, `tests/run_core_tests.sh` |
| Sample meshes | `objinfo` on every `assets/*.obj`: closed manifold or exit 1 | `tools/objinfo.sh` |
| App end to end | strict C-Bricks build; scripted `--demo` on `house.obj`; asserts stdout stats, the saved OBJ (re-read with `objinfo`), screenshot size and orange-pixel count | `tests/run_app_test.sh` |
| Real input | the program on a private X display driven by `xdotool`: click selects, E extrudes, Z undoes, F5 saves, C clears, middle-drag orbits | `tests/run_input_test.sh`, `tests/input_driver.sh` |
| Shader workspace | `--shader` on a surface shader, a post shader and a broken one (virtual display, screenshots checked), the canvas reading the graph (log line `nodes 5 wires 4`), `assets/shader_nodes.json` equal to C-Bricks' copy, the app registry entry `c-vert`, hot reload 40 times (more than the runtime's 32 shader slots) | `tests/run_shader_test.sh` |
| Shader editor: edits | dragging a node, typing an unwired value, autosave and the preview's hot reload | `tests/run_shader_edit_test.sh` |
| Shader editor: graph | delete a node, add one from the right-click palette, wire it, drop a wire on empty space, type badge | `tests/run_shader_graph_test.sh` |
| Shader editor: history | shift-select, copy / paste, delete, undo / redo, New post button | `tests/run_shader_history_test.sh` |
| Editor opens it | the real C-Bricks editor offscreen opens the project (the workspace root), Arrange, screenshot (not part of `run_all`) | `tools/layout.sh` |

## Rules of thumb
- **Mutation-check a new test once**: break the code on purpose and watch the test fail. (Done for extrude:
  reversing the side-quad winding fails five checks, then restore.)
- A green strict build + a passing demo is **not** evidence the input code works: that is what the real-input
  test is for.
- Screenshots are taken at the *end* of the frame, and (since the C-Bricks update of 2026-11-12) `cb_screenshot` writes exactly the path given and returns a bool.
- Windows only on a virtual display (`xvfb-run`, or `$CBRICKS_DIR/tests/offscreen.sh`). Hold synthetic key and
  mouse presses for a few frames: raylib polls once per frame.
- Running by hand: `.build/native/meshforge --cd project [--obj FILE] [--save FILE] [--demo --shot NAME.png]
  [--quit-after SECONDS] [-v]`; `--help` lists C-Bricks' own flags (`--trace`, `--debug FILE`, `--time`...).
  The editor's debugger and Trace work on the shell graph (the frame structure), not inside the C.
- Dumping a mesh: `tools/objinfo.sh FILE.obj` prints V / E / F / Euler / boundary / non-manifold counts.
