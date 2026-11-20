# C-Vert (placeholder name)

A small **Blender-inspired polygon-mesh editor for a Raspberry Pi 4**, written as a [C-Bricks](../../Godot/C-Bricks)
project: a thin brick graph for the window and frame loop, and plain C99 for everything that matters. Blender 4
needs OpenGL 4.3; the Pi 4's GPU stops at 3.1, so this takes a different road: C-Bricks exports portable C that
runs on raylib here.

**Status: rough draft (milestone 2 started).** It loads an OBJ (or makes a cube), flies / orbits, has Object and Mesh
modes, selects vertices / edges / faces, grabs / rotates / scales, extrudes, undoes and saves. It is a viewer-editor skeleton, not a modeller yet: see `PROGRESS.md`.

## Run it
```
tools/build.sh                                     # headless C-Bricks build (needs the C-Bricks checkout)
.build/native/meshforge --cd project --obj assets/house.obj
```
`./launch.sh` just runs the built program (no editor, no rebuild; `--build` rebuilds first; other arguments pass through).
Or open this folder in the C-Bricks editor (`cbricks`) and press Run. Run arguments go in Project Settings, e.g.
`--obj assets/house.obj`. No `--obj`: a cube.

## Controls (Blender's keys)
A scene of objects, each with its own position, rotation and scale. Two modes, **Tab** switches. Object mode: click an object (or drag a box, or A, or its row in the Scene tree (the outliner, top right: click an arrow to fold the children away)) to select it, then G / R / S (or the gizmo) change its transform; X / Delete removes it. Mesh mode (Tab on the active object) edits that object's vertices, edges and faces. The look is Blender's layout in the C-Bricks editor's colours (dark panels, rounded corners, light-blue accent). The text is a smooth TrueType font (`assets/ui_font.ttf`: replace the file to restyle).
| Input | Does |
|---|---|
| Tab | Object mode <-> Mesh mode |
| hold right button | look around with the mouse (the pointer hides and locks to the middle of the window while it is held, and comes back when you let go); **W A S D** fly forward / left / back / right, **E** up, **Q** down (Shift: faster); **Tab** still switches Object / Mesh mode while you hold it |
| middle-drag or arrow keys, wheel | orbit, zoom; **Shift + middle-drag** pans (the scene follows the mouse) |
| **Numpad 1 / 3 / 7** (Ctrl: the opposite side), **Numpad .** | front / right / top view (back / left / bottom with Ctrl); frame the selection (Mesh mode: the selected vertices, Object mode: the selected objects; everything when nothing is selected) |
| F or Home | frame the mesh |
| **G / R / S** | grab / rotate / scale (Object mode: the selected objects' transforms; Mesh mode: the selected vertices). Move the mouse; **X / Y / Z** constrains to an axis (again: free); type a number (`-` flips sign, Backspace) for an exact amount (metres, degrees, factor); **Enter or left click** confirms, **Esc or right click** cancels |
| Mesh mode: 1 / 2 / 3 | vertex / edge / face select (the selection converts the way Blender's does) |
| Mesh mode: left click | select (Shift: add / remove; empty space clears) |
| Mesh mode: A / Alt+A | select all / none |
| Mesh mode: **Ctrl+I** | invert the selection (faces in face mode, else vertices) |
| Mesh mode: **Alt+click** (vertex / edge mode) | select the edge loop through the edge under the mouse: it runs straight through every vertex where four quads meet and stops at poles, boundaries and triangles (Shift: add). Some window managers grab Alt+click to move the window: turn that off or use their other modifier |
| Mesh mode: E | extrude: the selection (faces; in vertex / edge mode the face its corners cover, else the edges) is copied and a grab follows the mouse along the normal; click / Enter ok, Esc cancels |
| Mesh mode: **I** | inset the selected faces, each on its own (move the mouse sideways or type the amount; click / Enter ok, Esc cancels; Ctrl snaps). **Press I again while it runs** to treat the faces as one region: the rim of the whole patch moves in, the shared edges stay |
| Mesh mode: **X** / **Delete** | delete what is selected, by select mode: vertices (and the faces touching them), edges (the faces using them) or faces (then the vertices only they used) |
| Mesh mode: **Alt+N** | flip the normals of the selected faces (all faces when nothing is selected) |
| Mesh mode: **M** / **Alt+M** | merge the selected vertices at their centre / merge by distance (0.001; the selection, or everything when nothing is selected) |
| Mesh mode: **Ctrl+R** (Ctrl+Shift+R: 3 cuts) | loop cut: the ring of quads through the edge under the mouse gets a new edge loop (selected, so G moves it). Refused when the ring runs into a triangle or n-gon. In Mesh mode Ctrl+R is this, not a snapped rotate: press R, then hold Ctrl |
| Mesh mode: **Ctrl+B** | bevel the selected edges (both ends selected; any valence, edges may share vertices, edges on a boundary are skipped): move the mouse sideways or type the width (measured inside each neighbouring face). **Mouse wheel while it runs: segments (1-16)**, **P / Shift+P: profile** (0.5 is a circular arc, lower flatter, 1.0 squarer). Where several beveled edges meet, the corner is filled in |
| Mesh mode: **H** / **Alt+H** | hide the selection (vertex / edge mode: the vertices and every face that touches one; face mode: the faces, and the vertices left with no visible face) / show everything again and select it. Hidden parts are not drawn, picked, selected (A, Ctrl+I, box) or touched by merge / flip / loop cut; they stay in the mesh, in undo and in saved OBJs. Object mode shows everything |
| Shift+A, or "+ Add" (top right) | Add menu: Plane, Cube, Circle, UV Sphere, Cylinder, Cone, Torus; a settings panel bottom left edits it live. **Object mode: a new object** in the Scene panel (Location sets its position). **Mesh mode: the shape goes inside the object being edited** |
| Left click + drag | a selection box (Object mode: objects with a vertex inside; Mesh mode: vertices, or face centres in face mode). Shift adds |
| Tool bar (left), or **W** | Box / Move / Rotate / Scale. The last three show a **gizmo** on the selection (world axes): drag an arrow, ring or square to transform along that axis (the centre square: free move / uniform scale); let go to confirm. Ctrl+Z undoes it |
| Mesh mode: **L** (Shift+L) | select (deselect) everything connected to the vertex / face under the mouse |
| **Alt+Z**, or the X-ray button (view bar, top centre) | X-ray (Mesh mode): the whole wireframe shows through the mesh, hidden edges too; vertices can be picked / boxed through it; in face mode every face has a dot and a click picks the nearest dot (through the mesh, like a box select); edge mode picks edges through it. Off (default): hidden things are hidden and cannot be picked |
| **Ctrl** held during G / R / S / E or a gizmo drag, or already held when you press G / R / E (Ctrl+S saves, so for scale press S first, then hold Ctrl) | snap: moves in steps of the snap size (View tab, default 0.25), turns 15 degrees, scales 0.1 (not with a typed number) |
| Object mode: **Shift+D** | duplicate the selected objects (own meshes, copies keep parent links) and grab them |
| **F2**, or double-click a hierarchy row | rename the active object (the old name is selected: typing replaces it; Enter ok, Esc cancel) |
| Object mode: **Ctrl+P** / **Alt+P** | parent the selected objects to the active one / clear their parent. Or drag a row onto another (child) or onto "Scene" (no parent). World transform is kept |
| Tabs under the Scene panel | **Item** (world location / rotation / scale, parent, selection median), **Mesh** (counts, size, surface), **Mods** (modifier stack, see below), **View** (X-ray, snap step, **Shading**: Flat / Smooth for the active object: smooth uses vertex normals, angle-weighted) |
| Mods tab | **Add Modifier** (menu: Mirror, Subdivision, Array; Object mode, up to 4, applied top to bottom). Each modifier is a panel: click its header to fold it; the **eye** switches it off / on; **Apply** bakes it and the ones above it into the mesh; **x** removes it. Fields: Mirror and Array have **Axis** X / Y / Z buttons, Subdivision **Levels** (1-3), Array **Count** (1-16) and **Offset** (0-5 sizes of the mesh along the axis; 1.0 = side by side): click an arrow to step, or **press in the middle of a field and drag sideways** (one undo step). Subdivide is Catmull-Clark; Mirror reflects through the object's local origin. In Mesh mode the cage is shown without them. Saved OBJs include them |
| Ctrl+Z, Ctrl+Shift+Z (or Ctrl+Y) | undo (32 deep), redo (a new edit clears the redo steps) |
| Ctrl+S or F5 | save to `--save FILE` (default `out.obj`) |
| Ctrl+Q | quit (Esc no longer quits: it cancels a transform). With unsaved edits the first press only asks: press it again within five seconds to quit without saving |
| (automatic) | **autosave**: every minute, when something changed, a copy goes to `<save file>.bak` (`--autosave SECONDS` changes the interval); the save file itself is only written by Ctrl+S / F5 |

Big meshes (stress-tested at 32k vertices / 32k faces, see PROGRESS): hidden-vertex tests (a bounding-volume tree, time-sliced over frames) only see the mesh being edited (not other objects); on a dense mesh the drawing skips edges smaller than 2 pixels, off the window or on faces turned away (closed meshes), and the vertex dots thin out to the selected ones when they are packed closer than 4 pixels (zoom in and they return).
Limits: in edge mode an edge counts as
selected when both its ends are; loop cut is limited to rings of quads (a hidden face, triangle or n-gon ends it), inset has no depth, bevel has no per-edge width or "vertex only" mode; no edge loops on faces, the Item tab is read-only, closing the window with its X button does not ask about unsaved changes (only Ctrl+Q does).

Frame-rate tools: `tests/run_bench.sh` (core timings, including a 32k-face stress section: every operator once), `tests/run_stress_test.sh` (the real program on a 32k-vertex sphere through Mesh mode, orbit, grab, undo, extrude, with a CPU-per-frame budget), the program's own `--prof` flag (prints where a frame's CPU time went on exit),  `tests/run_path_compare.sh` (plain vs the editor's `--debug FILE` Run path).
Multi-client load test: `tests/multi_client.sh N SECONDS` (real screen; `--virtual` for a private display); the program logs its average fps on exit.
Flags: `--obj FILE` `--save FILE` `--demo [--shot NAME.png]` (a scripted run, used by the tests)
F11 toggles borderless fullscreen (the window manager ignores the maximize request); `--no-maximize` (the window starts maximized, except with `--demo` / `--quit-after`; windowed is slow under Xwayland on the Pi) `--quit-after SECONDS`; C-Bricks' own: `--help`, `--trace`, `--debug FILE`, `--time`, `-v`.

## Shader workspace (F4)
`F4`, the **Shader** tab at the top left, or `--shader FILE` switches to a workspace for C-Bricks shaders: a live preview on top (a sphere on a floor; `M` or the **Preview** button shows your model instead; **Import model** takes an OBJ path, Enter loads it into the scene) and below it the node graph (a palette of nodes on its left: click or drag a node to add it; drag the canvas to pan, wheel to zoom, `F` to fit). The **Modeling** tab goes back. The preview reloads the file every
time it changes on disk, so a shader saved by C-Bricks' editor shows up within a quarter of a second. On the canvas, drag a node's header to move it, click an unwired input's box to type its numbers (commas or spaces; Enter confirms, Esc
cancels). Every finished edit is saved to the file at once, so the preview follows. **Right-click** opens the add-node palette (type to search, Enter adds the first match); **X** / Delete removes the selected node; drag from an output port
to an input port to wire (a new wire replaces the input's old one; loops are refused); drag a wired input's port to pick its wire up, and drop it on empty
space to delete it; click the type badge on a generic node to cycle float / vec2 / vec3 / vec4. Shift+click or shift+drag a box selects several nodes; **Ctrl+Z / Ctrl+Y** undo and redo, **Ctrl+C / X / V / A** copy, cut, paste (at the mouse) and select all; the **New surface / New post** buttons (Ctrl+N / Ctrl+Shift+N) start a fresh `untitledN.cbshader` beside the open file and switch to it. `F4` again goes back to modelling.

## Layout
```
.            the workspace root IS the C-Bricks project (open this folder in the editor)
  project.cbproj, main.cbricks   the project file and the shell: Open window, start-up check, Game loop, Draw 3D, five Call-C-function bricks
  core/          the mesh library: plain C99, no C-Bricks, no graphics (mf_mesh, mf_ops, mf_geom, mf_obj)
  app/           the glue: camera, input, picking, undo, drawing through cb_*  (mf_app.c)
  assets/        sample OBJs
  tests/         unit tests, end-to-end and real-input tests          (tests/run_all.sh)
  tools/         headless build, editor layout, graph generator, objinfo
  *.md           README, BUGS (known issues), DEBUG (the tests)
```
`.cbignore` keeps `tests/` and `tools/` (their own `main()`) out of the program; `.cbnoexport` keeps them, the
docs and the shell scripts out of exports.

## How the mesh works
Faces are index loops in one corner array; **half-edge adjacency is derived** from it (`mf_build`: a corner is a
half-edge, `twin` pairs opposite ones, edge counts give boundary / non-manifold / Euler characteristic). Editing a
mesh is simple, tools still get adjacency. Extrude duplicates the selected region's vertices, moves them along
the region's normals and joins old and new with side quads around the region's rim. `mf_check` and the unit tests
keep every operation closed and manifold.

## Tests
`tests/run_all.sh` runs: 917 core checks (strict flags + UBSan), `objinfo` on the sample meshes, a strict C-Bricks
build, the scripted demo (stats, saved OBJ, screenshot pixels) and four real mouse-and-keyboard tests on a virtual
display (`run_input_test.sh`, `run_ops_test.sh` for the milestone 3 operators, `run_ui_test.sh` for the outliner and modifier panels, `run_polish_test.sh` for redo, dragged fields, smooth shading, region inset, edge loops, numpad views, pan, autosave, quit). `tests/run_bench.sh` times the core's heavy calls (CPU only). Windows never open on your screen.

## Launching
`./edit.sh` is the quick launcher: it runs `cbricks .` (opens C-Vert in the C-Bricks editor); `--run` starts the built program on the real screen, `--build` rebuilds first.

## License
C-Vert is source-available, not open source (`LICENSE`), the same terms as C-Bricks: you can read the code and run the program for any purpose, and **everything you make with it (meshes, OBJ files, shaders) is yours** to use, ship and sell; you may not copy, modify or redistribute the source or the program itself. Third-party parts: `NOTICE.md`. A hobby project: use it, break it, tell me what's wrong.
