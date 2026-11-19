/* C-Vert core: an editable polygon mesh. Plain C99, standard library only: it knows nothing
   about C-Bricks, raylib or any graphics backend, so it builds and tests with a bare `cc`.

   Representation (decision D1, DECISIONS.md): faces are stored as index loops (a compact
   "corner list"); half-edge adjacency is DERIVED from it by mf_build(). Corner c of the list IS
   the half-edge going from vertex fidx[c] to the next corner of the same face. This keeps
   editing simple (append faces, rewrite a loop in place) while still giving twins and
   boundaries for tools that need them. */
#ifndef MF_MESH_H
#define MF_MESH_H

#include <stddef.h>

typedef struct mf_vec3 { float x, y, z; } mf_vec3;

typedef struct mf_mesh {
    mf_vec3 *v; int nv, cv;          /* vertex positions */
    unsigned char *vsel;             /* per vertex: selected (0 / 1) */
    unsigned char *vhid;             /* per vertex: hidden (H in Mesh mode). A hidden vertex has only hidden faces */
    int *fstart; int nf, cf;         /* face f owns corners fstart[f] .. fstart[f+1]-1 (nf + 1 entries) */
    int *fidx; int nc, cc;           /* the corner list: vertex index per corner */
    unsigned char *fsel;             /* per face: selected (0 / 1) */
    unsigned char *fhid;             /* per face: hidden. Hidden things are never selected, picked, drawn (Mesh mode) or acted on */
    /* derived by mf_build(); valid while topo_ok */
    int *twin;                       /* per corner: the opposite corner, -1 on a boundary */
    int *cface;                      /* per corner: the face it belongs to */
    int topo_ok;
    int ne;                          /* unique edges */
    int nboundary;                   /* edges with a single face */
    int nnonmanifold;                /* edges used by 3+ faces, or twice in the same direction */
} mf_mesh;

typedef struct mf_stats { int verts, edges, faces, boundary, nonmanifold, euler; } mf_stats;

/* ---- vector helpers (inline: no link dependency) ---- */
static inline mf_vec3 mf_v3(float x, float y, float z) { mf_vec3 r; r.x = x; r.y = y; r.z = z; return r; }
static inline mf_vec3 mf_add(mf_vec3 a, mf_vec3 b) { return mf_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline mf_vec3 mf_sub(mf_vec3 a, mf_vec3 b) { return mf_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline mf_vec3 mf_mul(mf_vec3 a, float k) { return mf_v3(a.x * k, a.y * k, a.z * k); }
static inline float mf_dot(mf_vec3 a, mf_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline mf_vec3 mf_cross(mf_vec3 a, mf_vec3 b) { return mf_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

/* ---- lifetime ---- */
mf_mesh *mf_new(void);
void mf_free(mf_mesh *m);
mf_mesh *mf_clone(const mf_mesh *m);          /* a deep copy (selection included): the undo snapshot */
void mf_clear(mf_mesh *m);                     /* empties it, keeps its memory */

/* ---- building ---- */
int mf_add_vert(mf_mesh *m, float x, float y, float z);   /* its index, or -1 (no memory) */
int mf_add_face(mf_mesh *m, const int *idx, int n);       /* its index, or -1 (n < 3, bad index, no memory) */

/* ---- reading ---- */
int mf_face_size(const mf_mesh *m, int f);
const int *mf_face_verts(const mf_mesh *m, int f);
int mf_corner_next(const mf_mesh *m, int c);   /* the next corner of the same face (wraps) */
mf_vec3 mf_face_normal(const mf_mesh *m, int f);   /* unit (Newell's method); zero for a degenerate face */
mf_vec3 mf_face_center(const mf_mesh *m, int f);

/* ---- topology ---- */
int mf_build(mf_mesh *m);                      /* fills twin / cface / counts; 0 ok, -1 no memory */
mf_stats mf_stats_get(mf_mesh *m);             /* builds first when stale */
/* Index checks and degenerate faces (a vertex twice in one face). 0 ok, else -1 and a message. */
int mf_check(const mf_mesh *m, char *msg, size_t msgn);

/* ---- hide (mf_edit.c): H hides the selection, Alt+H shows everything again ---- */
/* Hides what is selected: in vertex / edge mode the selected vertices and every face that touches one; in face mode the selected faces,
   and the vertices that have no visible face left. The selection is cleared. Returns how many faces + vertices became hidden. */
int mf_hide_selected(mf_mesh *m, int faces_mode);
/* Shows everything and selects what came back (vertices, or faces in face mode). Returns how many faces + vertices were revealed. */
int mf_reveal_all(mf_mesh *m, int faces_mode);
int mf_hidden_count(const mf_mesh *m, int *verts, int *faces);   /* how many of each are hidden; returns their sum */

/* ---- selection ---- */
void mf_sel_clear(mf_mesh *m);
void mf_sel_face(mf_mesh *m, int f, int on);
int mf_sel_face_count(const mf_mesh *m);
int mf_sel_vert_count(const mf_mesh *m);       /* vertices selected directly or by a selected face */
int mf_vert_selected(const mf_mesh *m, int v);

/* ---- select modes, masks, conversion, screen picking, transforms (mf_edit.c) ----
   The selection is stored as vsel (vertices) + fsel (faces). Vertex mode and edge mode use vsel only
   (an edge is "selected" when both its ends are: picking an edge selects its two ends); face mode uses fsel. */
typedef enum { MF_SEL_VERT = 1, MF_SEL_EDGE = 2, MF_SEL_FACE = 3 } mf_selmode;
/* mark[v] = 1 for every vertex selected directly or by a selected face (mark has nv entries). Returns the count. */
int mf_sel_mask(const mf_mesh *m, unsigned char *mark);
void mf_sel_vert(mf_mesh *m, int v, int on);
void mf_sel_all(mf_mesh *m, int on);           /* every vertex and face selected / not */
/* Blender's rules when the select mode changes: to vertex / edge, selected faces become selected vertices;
   to face, a face stays selected only if all its vertices are (vertices alone are then cleared). */
void mf_sel_convert(mf_mesh *m, mf_selmode to);
mf_vec3 mf_sel_center(const mf_mesh *m, const unsigned char *mark);   /* mean of the marked vertices; 0 if none */
/* Projects a world point to pixel (*sx, *sy) of a w x h view (same camera as mf_camera_ray). 1 when in front of the eye. */
int mf_project(mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, mf_vec3 p, float *sx, float *sy);
/* Nearest vertex to pixel (px, py) within radius pixels, or -1. X-ray: hidden vertices can be picked. */
int mf_pick_vert_screen(const mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float px, float py, float radius);
/* Nearest edge within radius pixels as the corner c it starts at (from fidx[c] to the next corner), or -1. Builds topology. */
int mf_pick_edge_screen(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float px, float py, float radius);
typedef enum { MF_XF_MOVE, MF_XF_ROTATE, MF_XF_SCALE } mf_xfkind;
/* Sets every marked vertex from its ORIGINAL position orig[v] (so a modal transform can be redone each frame):
   MOVE: orig + a.  ROTATE: about the unit axis a through pivot by `angle` radians (right-handed).
   SCALE: pivot + (orig - pivot) * a, per axis (a = (1,1,1) leaves it alone). */
void mf_xform(mf_mesh *m, const mf_vec3 *orig, const unsigned char *mark, mf_xfkind kind, mf_vec3 pivot, mf_vec3 a, float angle);

/* ---- primitives and edits (mf_ops.c) ---- */
int mf_make_cube(mf_mesh *m, float size);      /* appends an axis-aligned cube centred on the origin; 0 ok */
void mf_translate_selection(mf_mesh *m, mf_vec3 by);
/* Region extrude: duplicates the vertices of the selected faces, moves them `dist` along the
   region's normals and joins old and new with side quads (only around the region's border).
   Returns the number of side faces added, 0 when nothing is selected, -1 on no memory. */
int mf_extrude_selected(mf_mesh *m, float dist);

/* Extrude for vertex / edge selections (Blender's E on edges): every selected edge (both ends in vsel) gets a
   side quad from itself to a copy of its ends; the copies are the vertices from the old nv up (not moved: the
   caller grabs them). Returns the quads added, 0 when no edge is selected, -1 on no memory. */
int mf_extrude_edges(mf_mesh *m);
/* Selects (fsel) every face all of whose vertices are selected (vsel). Returns how many faces are selected after. */
int mf_sel_faces_from_verts(mf_mesh *m);

/* ---- milestone 3 operators (mf_tools.c). Each rebuilds / edits the mesh in place and returns a count (see each), -1 no memory,
   -2 refused (the situation is not supported: nothing was changed). Selection comes from vsel / fsel. ---- */
int mf_remove_unused(mf_mesh *m);                       /* drops vertices no face uses; how many */
int mf_delete_verts(mf_mesh *m);                        /* selected vertices and every face touching them; vertices + faces removed */
int mf_delete_faces(mf_mesh *m);                        /* selected faces (then the vertices only they used) */
int mf_delete_edges(mf_mesh *m);                        /* faces using a selected edge (both ends in vsel) */
int mf_flip_faces(mf_mesh *m, int all);                 /* reverses the selected faces (all = 1: every face); how many */
int mf_merge_center(mf_mesh *m);                        /* selected vertices into one at their mean; faces that collapse go; vertices removed */
int mf_merge_by_distance(mf_mesh *m, float dist, int only_selected);   /* vertices within dist of each other join; vertices removed */
/* Inset the selected faces, each on its own: a ring of quads around a smaller copy (which stays selected). Returns quads added. */
int mf_inset_faces(mf_mesh *m, float amount);
/* Inset the selected faces as ONE region: the faces keep their shape, a ring of quads is added around the whole patch and
   the rim moves `amount` inward (mitred at the corners). The inner copy stays selected. Returns quads added; 0 nothing selected;
   -2 when the region has no rim (all of a closed mesh); -1 no memory. */
int mf_inset_region(mf_mesh *m, float amount);
/* Loop cut: the ring of quads through the edge that starts at `corner`, split by `cuts` (1..16) new edge loops (new vertices are
   selected, the rest not). Returns faces added; 0 for nothing to cut; -2 when the ring ends in a face that is not a quad. */
int mf_loop_cut(mf_mesh *m, int corner, int cuts);
/* Bevel the selected edges (both ends in vsel; an edge needs two faces: boundary edges are skipped). Any valence, edges may share vertices.
   `width` is the distance from the edge measured inside each neighbouring face; `segments` (1..16) is how many quads wide the bevel is;
   `profile` (0.05..1, 0.5 = a circular arc, smaller = flatter / concave, 1 = squarer) shapes the curve when segments > 1.
   The new faces (the strips, and the filled corners) are selected, the vertex selection is cleared.
   Returns the edges bevelled; 0 when no selected edge can be (nothing changed); -2 for a non-manifold mesh; -1 no memory. */
int mf_bevel_edges_ex(mf_mesh *m, float width, int segments, float profile);
int mf_bevel_edges(mf_mesh *m, float width);   /* the same with one segment */

/* ---- primitives (mf_prim.c): appended around the origin, Y up, normals out; each returns 0 ok / -1 no memory ---- */
int mf_make_plane(mf_mesh *m, float size);
int mf_make_circle(mf_mesh *m, int n, float radius);                                  /* an n-gon facing +Y */
int mf_make_cylinder(mf_mesh *m, int n, float radius, float depth, int caps);
int mf_make_cone(mf_mesh *m, int n, float r_bottom, float r_top, float depth, int caps);   /* r_top 0 = a point */
int mf_make_uvsphere(mf_mesh *m, int n, int rings, float radius);
int mf_make_torus(mf_mesh *m, int major_n, int minor_n, float major_r, float minor_r);
void mf_move_verts_from(mf_mesh *m, int first, mf_vec3 by);                       /* offsets vertices first .. nv-1 */

/* ---- object transforms (mf_trs.c): a position, an Euler rotation and a scale per object ---- */
/* rot is in radians, applied X then Y then Z (Blender's default XYZ Euler: R = Rz * Ry * Rx). A local point p becomes
   R * (scl * p) + pos in the world. */
typedef struct mf_trs { mf_vec3 pos, rot, scl; } mf_trs;
mf_trs mf_trs_identity(void);
mf_vec3 mf_trs_point(const mf_trs *t, mf_vec3 p);      /* local -> world */
mf_vec3 mf_trs_unpoint(const mf_trs *t, mf_vec3 p);    /* world -> local */
mf_vec3 mf_trs_dir(const mf_trs *t, mf_vec3 d);        /* a direction, local -> world (no translation) */
mf_vec3 mf_trs_undir(const mf_trs *t, mf_vec3 d);      /* a direction, world -> local */
/* Rotates the object about the world axis (unit) through pivot by angle radians (right-handed): its position
   swings round the pivot and its orientation is composed with the turn. */
void mf_trs_rotate_world(mf_trs *t, mf_vec3 pivot, mf_vec3 axis, float angle);
/* Scales the object about a pivot: its position moves away from / toward it and its scale grows by a (per axis). */
void mf_trs_scale_about(mf_trs *t, mf_vec3 pivot, mf_vec3 a);
void mf_mesh_to_world(mf_mesh *m, const mf_trs *t);    /* every vertex local -> world (in place) */
void mf_mesh_to_local(mf_mesh *m, const mf_trs *t);    /* every vertex world -> local (in place) */
/* Box select (screen pixels, any corner order) through the camera of mf_project. X-ray, like the other picks.
   Vertices: vsel becomes the vertices inside (add keeps what was selected); returns how many are inside.
   Faces: fsel becomes the faces whose centre is inside; returns how many. */
int mf_box_select_verts(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float x0, float y0, float x1, float y1, int add);
int mf_box_select_faces(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float x0, float y0, float x1, float y1, int add);
/* 1 when any vertex of the mesh (placed by t, which may be NULL) lies inside the box. */
int mf_mesh_in_box(const mf_mesh *m, const mf_trs *t, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float x0, float y0, float x1, float y1);

/* Visibility-aware picks: vis[v] = 0 vertices are skipped (an edge needs one visible end, a face one visible vertex). NULL = all. */
int mf_pick_vert_screen_vis(const mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float px, float py, float radius, const unsigned char *vis);
int mf_pick_edge_screen_vis(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float px, float py, float radius, const unsigned char *vis);
int mf_box_select_verts_vis(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float x0, float y0, float x1, float y1, int add, const unsigned char *vis);
int mf_box_select_faces_vis(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float x0, float y0, float x1, float y1, int add, const unsigned char *vis);
/* Select linked (L): every vertex connected to `seed` through faces (faces = 0: sets vsel; faces = 1: sets fsel on the
   island's faces). on = 0 deselects. Returns the island's vertex count, -1 for a bad seed / no memory. */
int mf_select_linked(mf_mesh *m, int seed, int faces, int on);
/* Edge loop (Alt+click): from the edge that starts at `corner`, walks straight through every vertex where four quads meet, both ways,
   and selects (vsel) the vertices on the way; it stops at a boundary, a pole or a non-quad. `add` = 0 clears the selection first.
   Returns the number of edges in the loop (a closed loop counts each once); -1 for a bad corner / no memory. */
int mf_select_edge_loop(mf_mesh *m, int corner, int add);
/* Inverts the selection: faces when `faces` is set (fsel), else vertices (vsel). Returns how many are selected after. */
int mf_sel_invert(mf_mesh *m, int faces);
/* vis[v] = 1 when vertex v is not hidden behind a face as seen from eye (nv entries). Returns how many are visible. O(V*F). */
int mf_visible_verts(const mf_mesh *m, mf_vec3 eye, unsigned char *vis);
/* The same test in pieces, for a mesh that is tested again and again: build the tree once (NULL: no faces / no memory: everything is
   visible), refit it after vertices moved (returns 0 when the faces changed: build a new one), then test vertices from..to-1 of m whenever the
   eye moves (returns how many are visible, or -1 when m no longer has the faces the tree was built for). want (or NULL) lists the vertices worth a ray: the others come out as visible. */
typedef struct mf_vis mf_vis;
mf_vis *mf_vis_new(const mf_mesh *m);
void mf_vis_free(mf_vis *vs);
int mf_vis_refit(mf_vis *vs, const mf_mesh *m);
int mf_vis_run(mf_vis *vs, const mf_mesh *m, mf_vec3 eye, unsigned char *vis, const unsigned char *want, int from, int to);

/* ---- picking (mf_geom.c) ---- */
/* Nearest face hit by a ray (both sides), or -1; *t gets the distance along dir (dir unit). */
int mf_pick_face(const mf_mesh *m, mf_vec3 origin, mf_vec3 dir, float *t);   /* hidden faces are not hit */
int mf_pick_face_any(const mf_mesh *m, mf_vec3 origin, mf_vec3 dir, float *t); /* the same, hidden faces included (Object mode) */
float mf_face_ray_hit(const mf_mesh *m, int f, mf_vec3 origin, mf_vec3 dir);   /* nearest hit distance on face f, or -1 */
/* A perspective camera's ray through pixel (px, py) of a w x h view: eye, target, vertical fov in degrees. */
mf_vec3 mf_camera_ray(mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float px, float py);
void mf_bounds(const mf_mesh *m, mf_vec3 *lo, mf_vec3 *hi);   /* an empty mesh gives 0, 0 */
/* Where a ray meets the plane through p0 with normal n: 1 and *hit, or 0 (parallel, or behind the origin). */
int mf_ray_plane(mf_vec3 o, mf_vec3 d, mf_vec3 p0, mf_vec3 n, mf_vec3 *hit);
/* The point of the line p0 + t*axis (axis unit) closest to the ray: its t. 0 when ray and line are parallel. */
int mf_ray_axis_param(mf_vec3 o, mf_vec3 d, mf_vec3 p0, mf_vec3 axis, float *t);

/* ---- OBJ (mf_obj.c) ---- */
/* Each returns 0 on success, else -1 with a message (line numbers for parse errors).
   Reads v and f (v, v/vt, v//vn, v/vt/vn; negative indices; polygons); everything else is skipped. */
int mf_obj_parse(mf_mesh *m, const char *text, char *err, size_t errn);
int mf_obj_read(mf_mesh *m, const char *path, char *err, size_t errn);
int mf_obj_write(const mf_mesh *m, const char *path, char *err, size_t errn);

/* ---- modifiers (mf_mod.c): a stack of these turns a mesh into a new one; the input is never changed ---- */
enum { MF_MOD_MIRROR, MF_MOD_SUBDIV, MF_MOD_ARRAY };
/* axis 0 / 1 / 2 = X / Y / Z. Mirror: reflect through the plane axis = 0 (local space). Subdivide: n levels of Catmull-Clark
   (1..4). Array: n copies (1..64), each v * (the mesh's size along axis) further on (v 1.0 = side by side).
   off != 0: the modifier is switched off (skipped, like Blender's viewport eye); set it to 0 for a live one. */
typedef struct mf_modifier { int type, axis, n; float v; int off; } mf_modifier;
/* A new mesh (selection cleared), or NULL: no memory or the result would pass 120 000 faces. */
mf_mesh *mf_mod_eval(const mf_mesh *in, const mf_modifier *mods, int nmods);

#endif
