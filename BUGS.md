# Bugs

Found, fixed and suspected bugs in C-Vert (the core, the glue, the tests). Same format as C-Bricks' BUGS.md.
- **Open:** seen happening, not fixed yet.
- **Fixed:** what went wrong, why, the fix, and what guards against it coming back.
- **Potential:** risks we know about but haven't seen go wrong. Check these when touching the area.

When a bug is found: add it to Open (what you did, what happened, what you expected). When it's fixed: move it
to Fixed with cause, fix and guard. Prefer a test that reproduces it *before* fixing.

---

## Open
(none)

---

## Fixed

- **X-ray did nothing for edge / face mode** (user report, 2026-11-18). Cause: X-ray only lifted the vertex-visibility mask; hidden edges and faces were never drawn (depth-tested lines), and face picking was a ray, which stops at the nearest face. Fix: in X-ray the Mesh-mode wireframe, selected edges / face outlines and one dot per face are drawn in 2D over the mesh (`draw_xray_overlay`), and faces are picked by nearest dot (18 px). Guard: `run_ops_test.sh` clicks the dot of the back face and requires the picked face to differ from what the ray alone gives. Screenshot looked at.
- **2026-11-18: `mf_inset_faces` used a dangling corner pointer** (found by the new inset test, which reported 0 quads for "inset all faces"). Cause: `vs = m->fidx + ...` was kept across `mf_add_face`, which reallocates the corner list. Fix: re-read the pointer before each use. Guard: `test_tools` (inset all faces, 24 quads).
- **2026-11-18: bevel took the wrong neighbour vertex on one side** (`q2` was the corner's own vertex, so the offset was 0 and a quarter of the bevel collapsed; volume off by a third). Fix: two corners on. Guard: the bevel volume check (a 0.1 x 0.1 prism cut away); mutation of the strip winding is caught by the manifold check too.
- **2026-11-18: merging every vertex removed them all** (the merged vertex was "unused" so it was swept away). Fix: a vertex others merged into is kept even when loose. Guard: the merge-all check (7 removed, 1 vertex left).
- **2026-11-12: lines drawn on the mesh's own edges dashed (z-fighting).** Seen in `tests/reports/input.png`:
  edges of faces seen at a slant broke into dashes. Cause: `cb_draw_line3d` has no depth bias, so a line lying
  exactly in a surface's plane ties with it in the depth buffer. Fix: `line_pt()` in `mf_app.c` pulls each endpoint
  toward the eye by 0.4 % of the camera distance (selection outline 0.7 %). Guard: none automatic (visual); look at
  the screenshot. The ground grid still dashes where it passes *through* the cube (real geometry: the cube straddles y=0).
- **2026-11-12: `cb_mesh_triangle` lit the mesh from the wrong side.** Cause: the public function swaps its 2nd and
  3rd corner (`cb_mesh_add(m, a, c, b, ...)`), so it wants corners clockwise from the front; I passed
  counter-clockwise ones, giving inverted normals (a selected face rendered dark grey, not orange). Hidden by
  "Both sides". Fix: pass `(0, i+1, i)` (comment in `rebuild_render`). Guard: `tests/run_app_test.sh` counts orange
  pixels in the demo screenshot (35 130 now; fails under 3 000). Also logged in EDITOR-NOTES.md.
- **2026-11-12: the demo screenshot was blank.** Cause: `cb_screenshot` ran at the start of the frame (inside
  `mf_app_update`), after Clear but before anything was drawn. Fix: the demo only *asks* for it
  (`A.shot`); `mf_app_draw_hud`, the last draw call of the frame, takes it. Guard: the same pixel check (a blank
  shot has 1 colour). raylib also saves into the working directory by base name, ignoring the path's folders.
- **2026-11-12: `assets/house.obj` was not a valid closed mesh** (found by `tools/objinfo.sh`: nonmanifold 4). Cause:
  one roof quad (`f 3 7 10 9`) wound backwards, so its edges ran the same way as its neighbours'. Fix: `f 7 3 9 10`
  (traced every shared edge's direction). Guard: `tests/run_all.sh` runs `objinfo` on every `project/assets/*.obj`
  (exit 1 on boundary or non-manifold edges) and the app test expects V10 E17 F9 euler 2.
- **2026-11-12: my first real-input test expected `undo (1 left)`.** The expectation was wrong, not the code: after one
  extrude and one undo, zero snapshots remain. Test fixed to `undo (0 left)`.
- **2026-11-12: `tools/objinfo.sh` broke relative paths** (it `cd`'d into the workspace). Fix: no `cd`; absolute source paths.

---

## Potential
- Shader canvas rewrites the whole `.cbshader` on every edit: top-level or node keys it does not know are dropped (C-Bricks' own files only use the ones it keeps).
- Shader canvas: undo is a whole-graph snapshot per saved edit; an edit in progress (a value being typed) is not part of it. Another program rewriting the file clears the history.
- Shader canvas: new files are named untitledN by the program; a file other than a .cbshader (GLSL) has no graph, so New works but starts from a graph, not from the GLSL.
- Shader canvas: only 14 palette rows show at a time (wheel scrolls); very small windows may clip the box.
- **Shader workspace (2026-11-19):** a post shader processes the whole frame, so the node canvas and the status text are tinted by it too. On the model (`M`) a surface shader sees UVs of zero (C-Vert has no UV data yet), so UV patterns look flat there; the sphere has UVs. Only run on the virtual display so far.
- **X-ray off is O(V*F) per camera move** (2026-11-18, `tests/run_bench.sh`): 190 ms on a 2048-face mesh in Mesh mode (12 ms at 512 faces). Needs a spatial structure. Also: Alt+click is a window-move gesture in many window managers; smooth shading uses rotation only for normals (non-uniform scale slightly off); closing the window with its X button skips the unsaved-changes question.
- **Milestone 3 operators (2026-11-18):** bevel is one segment and only cube-like edges (two faces, ends on valence-3 vertices, no shared vertices); inset is per face (adjacent selected faces do not share their rim); loop cut is quads only, uniform positions (no slide), and Ctrl+R in Mesh mode replaced the snapped-rotate shortcut; delete in edge mode removes whole faces (no dissolve); merge by distance is O(V^2); the operators work on the world-space edit copy, so a non-uniformly scaled object is bevelled in world units. Inset / bevel redo from a cloned mesh every frame (a clone per mouse move: fine for hundreds of faces).
- **Frame rate drops when the window is not maximized** (user report, 2026-11-15; fine maximized). Cause unknown, not reproducible on llvmpipe. Needs a measurement on the real display at both sizes (frames per second printed, and the number of faces / modifiers at the time).
- **Modifiers**: the stack is re-evaluated on every redraw of a dirty scene (so a Subdivide L3 on a big mesh will make transforms slow); clicking the Mods tab does nothing in Mesh mode; picking in Object mode uses the evaluated mesh, Mesh mode the cage; Mirror does not merge near-plane vertices or clip, Array copies stay separate shells; the Mesh tab counts the cage, not the result.
- **Hierarchy panel is not scrollable**: with many objects the tabbed panel is pushed down and clamps to the screen bottom, overlapping the rows. 24 objects max.
- **X-ray off: visibility is per vertex and only against the edited mesh**; a face counts as visible if any vertex is; a vertex peeking through a gap is hidden while 0.5 % of the distance is the tolerance.
- **Rename**: no cursor movement, no capitals beyond Shift+letter, no characters outside letters / digits / space - _ .
- **Parented scaling** inherits the no-shear limit: a scaled parent with a rotated child shears in Blender, here the child just rotates and scales in place.
- **Extruding every face of a closed mesh leaves the original vertices unused** (no rim, so no side walls; all
  vertices are duplicated and the faces move onto the copies). Pinned by `test_extrude_everything_and_nothing`
  (`nv == 16`, `nonmanifold == 0`). Blender would make an inner shell; ours has 8 orphan vertices. Add a "remove
  unused vertices" pass before saving, or make it do what Blender does.
- **Extrude moves along the *summed* face normals per vertex**, which is exact for flat regions but shrinks or
  skews on bent regions (no miter correction). Check when extruding wrapped regions.
- **Picking triangulates faces as a fan from corner 0**: right for convex faces, wrong for concave ones (a ray may
  hit "the hole" or miss the face). Fix with ear clipping when concave faces can exist (OBJ import allows them).
- **`mf_check` only catches repeated vertices and bad indices** (also zero-area faces are not flagged).
- **OBJ reader:** a vertex line longer than 1023 characters is silently truncated; `usemtl` / `mtllib` / `vt` /
  `vn` / groups are skipped (normals and UVs are dropped on load, and not written back).
- **Undo snapshots copy the whole scene** (D4, every object's mesh): fine at hundreds of faces; measure at thousands.
- **Mesh mode edits a world-space copy of the active object** and writes it back through the inverse transform (on Tab out and before each
  undo snapshot): a float round trip each time, and an object with a scale axis of 0 (clamped to 1e-6) would blow its local mesh up.
- **Scaling a rotated object** scales its local axes about the median point (no shear, unlike Blender's global-axis scale of a rotated
  object); the gizmo is world-axes only. A rotation of exactly Y = +-90 degrees (gimbal lock) puts all of the rest into X: same
  orientation, different numbers.
- **Object picking tests each object's own faces with a fan triangulation** (as the other picks) and uses the nearest hit in the world.
- **Edge drawing is one `cb_draw_line3d` per edge per frame** and the render mesh is rebuilt on every change: fine
  small, will not be at tens of thousands of faces.
- **Never run on the real GPU.** Everything was tested on Mesa llvmpipe in a virtual display. V3D (GL 2.1 shaders,
  no 3.3) might differ in shading or speed. First thing for the user to try (DECISIONS Open 2).
- **Interactive paths covered only by one scripted run**: the real-input test covers click, E, Z, F5, C and a middle
  drag. Not covered: Shift-click add/remove, I J K L U O moves (each frame!), W / S / wheel zoom, F frame, arrow
  orbit. Add them when touching that code.
- **AddressSanitizer unusable on this kernel** (only UBSan runs): out-of-bounds reads in the core would show
  as wrong results or crashes, not as ASan reports. Valgrind is not installed. Consider building the core tests on
  another machine / in a container with ASan before the core grows.
- **A C-Bricks update can break us**: the build driver depends on `ProjectBuild.generate / write`,
  `Toolchains.build_raylib / compile`, the layout tool on private editor members (EDITOR-NOTES.md).
- Vertex / edge picking is x-ray (hidden ones can be picked); a diagonal can light when two edges' ends are selected; Object mode has no object transform of its own (G / R / S move every vertex); extrude is face-mode only. Added 2026-11-13.

- (Potential, 2026-11-17, user report) Frame rate drops when the window is NOT maximized, fine maximized. Real session is Wayland/labwc + Xwayland. See DECISIONS 'Real-display report'.
