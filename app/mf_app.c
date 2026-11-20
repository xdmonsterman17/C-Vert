/* C-Vert app glue. See mf_app.h. Keys (also drawn in the HUD), Blender's where there is one:
     Tab            switch Object mode <-> Mesh mode
     F4             the Shader workspace (a .cbshader beside a live preview; --shader FILE opens it at start; F4 again goes back)
     right-hold     look around (mouse) and fly: W A S D forward / left / back / right, E up, Q down (Shift: faster)
     middle-drag / arrows   orbit        wheel   zoom      F / Home   frame the mesh
     Ctrl+Z   undo      Ctrl+Q   quit (Esc is only the transforms' cancel)      Ctrl+S / F5   save to --save FILE (default out.obj)
     G R S   grab / rotate / scale (modal: mouse, X Y Z axis, typed number, Enter / left click ok, Esc / right click cancel)
       In Object mode they change the selected objects' own transforms (click an object, drag a box, A, or its row in
       the hierarchy panel top right; click empty space / Alt+A deselects; X / Delete removes); in Mesh mode, the selection.
     Left click + drag: a selection box (both modes).   Toolbar left (or W): Box / Move / Rotate / Scale tools; the last
       three show a gizmo on the selection: drag an axis (arrow, ring, square) to transform along it.
     Mesh mode only:  1 2 3 vertex / edge / face select     left click select (Shift: add / remove)
                      A select all    Alt+A select none     E extrude (then move the mouse: it follows it; click ok)
                      I inset   X / Delete delete   Alt+N flip normals   M / Alt+M merge centre / by distance
                      Ctrl+R loop cut (Ctrl+Shift+R: 3)   Ctrl+B bevel (cube-like edges)
     Shift+A, or "+ Add" in the hierarchy panel: the Add menu (Plane Cube Circle UV Sphere Cylinder Cone Torus);
       in Object mode it makes a new object, in Mesh mode it adds the shape into the object being edited;
       a settings panel bottom left changes it live
   Flags: --shader FILE   start in the Shader workspace on that .cbshader / .glsl
          --obj FILE   load an OBJ        --save FILE   where Ctrl+S / F5 writes
          --demo       scripted run: select the top face, extrude, move, save, quit
          --shot NAME.png  with --demo / --quit-after: a screenshot before quitting
          --no-maximize    stay 960 x 540 (the window starts maximized, except with --demo / --quit-after)
          --quit-after SECONDS   close by itself (tests that drive the real mouse and keys) */
#include "cbricks.h"
#include "mf_mesh.h"
#include "mf_app.h"
#include "mf_font.h"
#include "mf_win.h"
#include "mf_shader.h"
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define UNDO_MAX 32
#define MAX_OBJ 24
#define PI_F 3.14159265f
/* raylib key numbers cbricks.h doesn't name */
#define KEY_HOME 268
#define KEY_DELETE 261
#define KEY_KP_1 321
#define KEY_KP_3 323
#define KEY_KP_7 327
#define KEY_KP_DECIMAL 330   /* GLFW / raylib codes: cbricks.h has no numpad names */
#define KEY_RIGHT_SHIFT 344
#define KEY_LEFT_ALT 342
#define KEY_RIGHT_CONTROL 345
#define KEY_RIGHT_ALT 346
static const float SNAPS[] = { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f };
#define NSNAP 5

enum { MODE_OBJECT = 0, MODE_MESH = 1 };
enum { PRIM_PLANE, PRIM_CUBE, PRIM_CIRCLE, PRIM_SPHERE, PRIM_CYLINDER, PRIM_CONE, PRIM_TORUS, PRIM_N };
static const char *PRIM_NAME[PRIM_N] = { "Plane", "Cube", "Circle", "UV Sphere", "Cylinder", "Cone", "Torus" };
/* one row of the settings panel: kind 0 float, 1 whole number, 2 on / off */
typedef struct { const char *label; float v, lo, hi, step; int kind; } prm;
#define MAX_PRM 10
enum { TOOL_SELECT, TOOL_MOVE, TOOL_ROTATE, TOOL_SCALE, TOOL_N };
static const char *TOOL_NAME[TOOL_N] = { "Box", "Move", "Rotate", "Scale" };
/* one object of the scene: its mesh in its own (local) space, and where it sits */
/* t is the object's WORLD transform; parent (-1 none) is only a hierarchy link: transforming an object also carries its
   descendants (affected()), and parenting keeps the world transform (Blender's "keep transform"). */
#define MAX_MOD 4
typedef struct { mf_mesh *mesh; mf_trs t; char name[48]; int sel; int parent; mf_modifier mod[MAX_MOD]; int nmod; int fold; int shut; int smooth; } sobj;   /* smooth: shaded with vertex normals (View tab); fold: its children are hidden in the Scene tree; shut: bit i set = modifier i's panel is collapsed */
/* an undo step: the whole scene */
typedef struct { int n, cur; sobj o[MAX_OBJ]; } ssnap;

static struct {
    /* The scene. A.mesh is what the Mesh-mode code edits: in Object mode the current object's own mesh (local space),
       in Mesh mode a WORLD-space copy of it (A.baked = 1) so picking, drawing and transforms need no matrices; it is
       written back (inverse transform) by edit_commit() whenever the scene is read as a whole. */
    sobj o[MAX_OBJ];
    mf_mesh *ev[MAX_OBJ];       /* each object's mesh with its modifier stack applied (NULL: it has none, or it is being edited) */
    int nobj, cur;              /* cur: the active object (-1 none) */
    mf_mesh *mesh;
    int baked;
    ssnap *undo[UNDO_MAX];
    int nundo;
    ssnap *redo[UNDO_MAX];     /* undone steps, Ctrl+Shift+Z brings them back; any new edit clears them */
    int nredo;
    int changed;                /* edits since the last save (the quit asks) */
    int auto_changed;           /* edits since the last autosave */
    double auto_t;              /* cb_game_time of the last autosave check */
    double qconf_t;             /* Ctrl+Q asked once: the time, 0 none */
    int fd_on, fd_obj, fd_mod, fd_kind, fd_x0, fd_pushed; float fd_v0;   /* a modifier's number field being dragged */
    cb_mesh *render;
    int dirty, rtris;           /* the render mesh needs rebuilding; triangles in it */
    float yaw, pitch, dist, fov;
    mf_vec3 target, eye;
    int mx, my;                 /* the mouse last frame (for dragging / looking) */
    int frame;                  /* frames since init (the demo script) */
    int mode;                   /* MODE_OBJECT / MODE_MESH */
    int gz_log;                 /* print the gizmo handles' screen positions once (the input test aims with them) */
    int tool;                   /* TOOL_SELECT / MOVE / ROTATE / SCALE */
    int lp, boxing, px0, py0;   /* left button down in the viewport; it became a box drag; where it went down */
    int gdrag;                  /* the running transform is a gizmo drag: it ends when the button is released */
    int xobj;                   /* the running transform is on objects (Object mode), not vertices */
    mf_trs *xtrs;               /* the objects' transforms when it started */
    int font_tried;
    double last_t;              /* cb_game_time at the last frame (for the avg fps line at exit) */
    int max_tried, max_ok, frame_no;   /* start-maximized: tries made, done, frame counter */
    mf_selmode selmode;         /* vertex / edge / face (Mesh mode) */
    unsigned char *mask;        /* per vertex: selected (this frame), for drawing */
    int mask_cap;
    int was_rmb;                /* the right button was held (flying) last frame */
    int captured;               /* the mouse is hidden and locked to the centre (while the right button flies the camera) */
    int rmb_ok;                 /* 0 after a right click cancelled a transform, until the button is released */
    /* a modal transform (G R S) */
    int xf;                     /* 0 none, else 'G' 'R' 'S' */
    int axis;                   /* 0 free, 1 X, 2 Y, 3 Z, 4 the extrude's normal (A.nrm) */
    int extr;                   /* the running grab is an extrude: cancelling takes the new geometry away too */
    int presnap;                /* the snapshot for undo was already taken (extrude) */
    mf_vec3 nrm;
    /* the Add menu and its settings panel */
    int menu_open, menu_x, menu_y, menu_kind;   /* menu_kind 0: the Add menu (primitives), 1: Add Modifier */
    int nrows;                  /* rows showing in the Scene tree (folded children are not) */
    int addon, addkind;         /* the settings panel is showing for this primitive */
    int addobj, addidx;         /* ... and it made a whole new object (Object mode): this one */
    mf_mesh *addbase;           /* the mesh as it was before the primitive: every change rebuilds from it */
    prm prm[MAX_PRM];
    int nprm;                   /* the primitive's own rows, then Location X Y Z */
    int drag; int drag_x0; float drag_v0;
    int eat;                    /* the left click this frame went to the interface */
    int xray, tab, snap_i;      /* see vertices through the mesh; the tab of the lower right panel; the Ctrl-snap step (SNAPS) */
    unsigned char *vis;         /* per vertex: in view (not behind a face), when X-ray is off */
    int vis_cap, vis_ok, vis_valid; mf_vec3 vis_eye;
    mf_vis *vtree;              /* the visibility tree, built once per mesh edit and refitted while vertices move */
    int vt_topo, vt_stale;      /* faces changed (rebuild) / vertices moved (refit) since the tree was made */
    int bev_seg; float bev_prof;   /* the bevel's segments (wheel while it runs) and profile (P / Shift+P), kept for the next one */
    int vact, vpos;             /* vact: a visibility pass is half done (time-sliced over frames); vpos is the next vertex */
    unsigned char *want; int want_cap;   /* vertices on screen: only those get a ray */
    mf_vec3 *wv; float *sxy; int wcap;  /* drawing scratch: each vertex in the world and on the screen (x, y, in front) */
    unsigned char *front; int fcap;     /* drawing scratch: per face, looks at the eye */
    int ord[MAX_OBJ], depth[MAX_OBJ];   /* the hierarchy panel's rows: object index and tree depth */
    int renaming, rn_i, rn_fresh; char rn[48];    /* rename (F2 / double click) */
    int imp; char imp_buf[240];                   /* the Shader workspace's "Import model" path box (typed, Enter loads) */
    int rdrag, rdx, rdy;        /* a row of the hierarchy is being dragged (object + 1), where it went down */
    double click_t; int click_row;
    mf_vec3 *xorig;             /* positions when it started */
    unsigned char *xmark;       /* the vertices it moves */
    mf_vec3 pivot, r0o, r0d;    /* its centre; the mouse ray when it started */
    float psx, psy, m0x, m0y;   /* the centre on screen; the mouse when it started */
    char num[24];               /* typed number */
    mf_mesh *morig; int mop_n, mop_reg;   /* mop_reg: the inset treats the selected faces as one region (I again, while it runs) */    /* inset / bevel: the mesh before the operator (redone from it every frame), and what the last try gave */
    char typed[32]; int ntyped; /* characters typed this frame (cb_char_typed, drained every frame so none go stale) */
    int ready;
    char status[200];
    const char *save_path;
    const char *shot;           /* a screenshot to take at the end of this frame (the demo, --quit-after) */
    int quit_armed;             /* --quit-after: the screenshot was asked for; the next frame closes */
} A;

/* The HUD's status line; also printed, so tests (and a terminal) can follow what the editor did. */
static void status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(A.status, sizeof A.status, fmt, ap);
    va_end(ap);
    printf("[meshforge] %s\n", A.status);
}

/* --prof: where a frame's CPU time goes (submitting draw calls counts, the GPU's own work does not). Printed at exit. */
enum { PR_UPDATE, PR_VIS, PR_BUILD, PR_DRAW3D, PR_HUD, PR_SURF, PR_N };
static double PR_T[PR_N], PR_T0[PR_N];
static int PR_ON = -1;
static void pr_begin(int k) { if (PR_ON < 0) PR_ON = cb_flag("prof") ? 1 : 0; if (PR_ON) PR_T0[k] = mf_win_clock(); }
static void pr_end(int k) { if (PR_ON > 0) PR_T[k] += mf_win_clock() - PR_T0[k]; }

/* ---- the scene: Mesh-mode editing copy, undo (whole-scene snapshots, see DECISIONS.md D4) ---- */

static mf_vec3 wpt(int i, mf_vec3 p) {   /* object i's vertex p in the world */
    return (i == A.cur && A.baked) ? p : mf_trs_point(&A.o[i].t, p);
}
/* the mesh in the object's own space as it is SHOWN (modifiers applied), and the one picking / drawing use */
static const mf_mesh *lmesh(int i) { return A.ev[i] ? A.ev[i] : A.o[i].mesh; }
static const mf_mesh *omesh(int i) { return (i == A.cur && A.baked) ? A.mesh : lmesh(i); }

/* writes the Mesh-mode world copy back into the object (local space) */
static void edit_commit(void) {
    mf_mesh *c;
    if (!A.baked || A.cur < 0) return;
    c = mf_clone(A.mesh);
    if (!c) { status("out of memory"); return; }
    mf_mesh_to_local(c, &A.o[A.cur].t);
    mf_free(A.o[A.cur].mesh);
    A.o[A.cur].mesh = c;
}

static int edit_enter(void) {
    mf_mesh *c;
    if (A.cur < 0 || A.baked) return A.baked;
    c = mf_clone(A.o[A.cur].mesh);
    if (!c) { status("out of memory"); return 0; }
    mf_mesh_to_world(c, &A.o[A.cur].t);
    A.mesh = c; A.baked = 1;
    return 1;
}

static void edit_leave(void) {
    edit_commit();
    if (A.baked) { mf_free(A.mesh); A.baked = 0; }
    A.mesh = A.cur >= 0 ? A.o[A.cur].mesh : NULL;
}

static void set_cur(int i) {   /* Object mode only */
    A.cur = i;
    A.mesh = i >= 0 ? A.o[i].mesh : NULL;
}

static ssnap *snap_take(void) {
    ssnap *sn = (ssnap *)calloc(1, sizeof *sn);
    int i;
    if (!sn) return NULL;
    sn->n = A.nobj; sn->cur = A.cur;
    for (i = 0; i < A.nobj; i++) {
        sn->o[i] = A.o[i];
        sn->o[i].mesh = mf_clone(A.o[i].mesh);
        if (!sn->o[i].mesh) { while (i--) mf_free(sn->o[i].mesh); free(sn); return NULL; }
    }
    return sn;
}

static void snap_free(ssnap *sn) {
    int i;
    for (i = 0; i < sn->n; i++) mf_free(sn->o[i].mesh);
    free(sn);
}

static void undo_push(void) {
    ssnap *sn;
    edit_commit();
    sn = snap_take();
    if (!sn) return;
    if (A.nundo == UNDO_MAX) {   /* drop the oldest */
        int i;
        snap_free(A.undo[0]);
        for (i = 1; i < UNDO_MAX; i++) A.undo[i - 1] = A.undo[i];
        A.nundo--;
    }
    A.undo[A.nundo++] = sn;
    while (A.nredo > 0) snap_free(A.redo[--A.nredo]);   /* a new edit ends the redo line */
    A.changed = A.auto_changed = 1;
}

static void undo_drop(void) { if (A.nundo > 0) snap_free(A.undo[--A.nundo]); }

static void close_add_panel(void) {
    if (A.addbase) mf_free(A.addbase);
    A.addbase = NULL; A.addon = 0; A.addobj = 0; A.drag = -1;
}

static void snap_restore(ssnap *sn) {
    int i;
    if (A.baked) { mf_free(A.mesh); A.baked = 0; }
    for (i = 0; i < A.nobj; i++) mf_free(A.o[i].mesh);
    A.nobj = sn->n; A.cur = sn->cur;
    for (i = 0; i < sn->n; i++) { A.o[i] = sn->o[i]; A.o[i].mesh->topo_ok = 0; }
    free(sn);
    A.mesh = A.cur >= 0 ? A.o[A.cur].mesh : NULL;
    if (A.cur < 0) A.mode = MODE_OBJECT;
    if (A.mode == MODE_MESH && !edit_enter()) A.mode = MODE_OBJECT;
    A.dirty = 1;
    A.changed = A.auto_changed = 1;
}

static void snap_stack_push(ssnap **stack, int *n, ssnap *sn) {   /* a bounded stack: the oldest falls off */
    if (*n == UNDO_MAX) {
        int i;
        snap_free(stack[0]);
        for (i = 1; i < UNDO_MAX; i++) stack[i - 1] = stack[i];
        (*n)--;
    }
    stack[(*n)++] = sn;
}

/* takes the newest undo step back WITHOUT making it redoable (used to revert a failed operation / a cancelled transform) */
static void undo_pop(void) {
    close_add_panel();
    if (A.nundo == 0) { status("nothing to undo"); return; }
    snap_restore(A.undo[--A.nundo]);
    status("undo (%d left)", A.nundo);
}

/* Ctrl+Z: the step goes to the redo stack */
static void undo_user(void) {
    ssnap *cur;
    close_add_panel();
    if (A.nundo == 0) { status("nothing to undo"); return; }
    edit_commit();
    cur = snap_take();
    snap_restore(A.undo[--A.nundo]);
    if (cur) snap_stack_push(A.redo, &A.nredo, cur);
    status("undo (%d left, %d to redo)", A.nundo, A.nredo);
}

static void redo_user(void) {
    ssnap *cur;
    close_add_panel();
    if (A.nredo == 0) { status("nothing to redo"); return; }
    edit_commit();
    cur = snap_take();
    snap_restore(A.redo[--A.nredo]);
    if (cur) snap_stack_push(A.undo, &A.nundo, cur);
    status("redo (%d left)", A.nredo);
}

static void unique_name(const char *base, char *out, size_t n) {
    int k = 0, i, clash;
    do {
        if (k == 0) snprintf(out, n, "%s", base); else snprintf(out, n, "%s.%03d", base, k);
        clash = 0;
        for (i = 0; i < A.nobj; i++) if (strcmp(A.o[i].name, out) == 0) clash = 1;
        k++;
    } while (clash);
}

/* ---- camera ---- */
/* ---- camera ---- */

/* selonly: frame the selection (Mesh mode: the selected vertices; Object mode: the selected objects); everything when it is empty */
static void frame_mesh(int selonly) {
    mf_vec3 lo = mf_v3(0, 0, 0), hi = mf_v3(0, 0, 0), ext;
    float r;
    int i, v, first = 1;
    for (i = 0; i < A.nobj; i++) {
        const mf_mesh *m = omesh(i);
        if (selonly && A.mode == MODE_OBJECT && !A.o[i].sel) continue;
        if (selonly && A.mode == MODE_MESH && i != A.cur) continue;
        for (v = 0; v < m->nv; v++) {
            mf_vec3 p;
            if (selonly && A.mode == MODE_MESH && !mf_vert_selected(m, v)) continue;
            p = wpt(i, m->v[v]);
            if (first) { lo = hi = p; first = 0; }
            if (p.x < lo.x) lo.x = p.x;
            if (p.y < lo.y) lo.y = p.y;
            if (p.z < lo.z) lo.z = p.z;
            if (p.x > hi.x) hi.x = p.x;
            if (p.y > hi.y) hi.y = p.y;
            if (p.z > hi.z) hi.z = p.z;
        }
    }
    if (first && selonly) { frame_mesh(0); return; }
    A.target = mf_mul(mf_add(lo, hi), 0.5f);
    ext = mf_sub(hi, lo);
    r = sqrtf(mf_dot(ext, ext)) * 0.5f;
    if (r < 0.1f) r = 0.1f;
    A.dist = r / sinf(A.fov * 0.5f * PI_F / 180.0f) * 1.15f;
}

static void place_camera(void) {
    float cp = cosf(A.pitch);
    A.eye = mf_add(A.target, mf_v3(A.dist * cp * sinf(A.yaw), A.dist * sinf(A.pitch), A.dist * cp * cosf(A.yaw)));
    if (mf_shader_active()) {   /* the Shader workspace: the graph editor covers the bottom, so slide the view to put the target in the top strip */
        float shift = mf_shader_shift() * A.dist * tanf(A.fov * 0.5f * PI_F / 180.0f);   /* the camera slides down its own up vector: the target rises into the top strip */
        float ux = -sinf(A.pitch) * sinf(A.yaw), uy = cosf(A.pitch), uz = -sinf(A.pitch) * cosf(A.yaw);
        cb_camera(cb_v3(A.eye.x - ux * shift, A.eye.y - uy * shift, A.eye.z - uz * shift), cb_v3(A.target.x - ux * shift, A.target.y - uy * shift, A.target.z - uz * shift), A.fov, false);
        return;
    }
    cb_camera(cb_v3(A.eye.x, A.eye.y, A.eye.z), cb_v3(A.target.x, A.target.y, A.target.z), A.fov, false);
}

/* ---- rendering the mesh with C-Bricks' mesh builder ---- */

static Vector3 to_cb(mf_vec3 p) { return cb_v3(p.x, p.y, p.z); }

/* smooth shading: a normal per vertex, the faces' normals weighted by the angle they have at that vertex. nv entries (caller frees), or NULL. */
static mf_vec3 *vert_normals(const mf_mesh *m) {
    mf_vec3 *nr = (mf_vec3 *)calloc((size_t)(m->nv > 0 ? m->nv : 1), sizeof(mf_vec3));
    int f, k;
    if (!nr) return NULL;
    for (f = 0; f < m->nf; f++) {
        const int *vi = mf_face_verts(m, f);
        int n = mf_face_size(m, f);
        mf_vec3 fn = mf_face_normal(m, f);
        for (k = 0; k < n; k++) {
            mf_vec3 p = m->v[vi[k]], a = mf_sub(m->v[vi[(k + n - 1) % n]], p), b = mf_sub(m->v[vi[(k + 1) % n]], p);
            float la = sqrtf(mf_dot(a, a)), lb = sqrtf(mf_dot(b, b)), c, ang;
            if (la < 1e-9f || lb < 1e-9f) continue;
            c = mf_dot(a, b) / (la * lb);
            ang = acosf(c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c));
            nr[vi[k]] = mf_add(nr[vi[k]], mf_mul(fn, ang));
        }
    }
    return nr;
}

static Vector3 to_cb_dir(int obj, mf_vec3 n) {   /* a normal into the world, unit length */
    float l;
    if (!(obj == A.cur && A.baked)) n = mf_trs_dir(&A.o[obj].t, n);
    l = sqrtf(mf_dot(n, n));
    if (l < 1e-9f) return cb_v3(0, 1, 0);
    return cb_v3(n.x / l, n.y / l, n.z / l);
}

static void rebuild_render(void) {
    int f, i, k, tris = 0;
    for (k = 0; k < MAX_OBJ; k++) {   /* evaluate the modifier stacks (the object being edited shows its cage instead) */
        int need = k < A.nobj && A.o[k].nmod > 0 && !(k == A.cur && A.baked);
        /* A transform in progress moves objects or the cage, never a modifier's input: keep the results (a level-3 Subdivision is tens of ms) */
        if (need && A.xf && A.ev[k]) continue;
        if (A.ev[k]) { mf_free(A.ev[k]); A.ev[k] = NULL; }
        if (need) {
            A.ev[k] = mf_mod_eval(A.o[k].mesh, A.o[k].mod, A.o[k].nmod);
            if (!A.ev[k]) status("modifiers on \"%s\" gave nothing (too big, or out of memory): shown without them", A.o[k].name);
        }
    }
    A.render = cb_mesh_begin(A.render);
    for (k = 0; k < A.nobj; k++) {
        const mf_mesh *m = omesh(k);
        int edit = k == A.cur && A.mode == MODE_MESH;
        mf_vec3 *vn = A.o[k].smooth ? vert_normals(m) : NULL;
        for (f = 0; f < m->nf; f++) {
            const int *vi = mf_face_verts(m, f);
            int n = mf_face_size(m, f);
            cb_rgba col;
            if (edit && m->fhid[f]) continue;   /* hidden in Mesh mode (Object mode shows everything) */
            col = (m->fsel[f] && edit) ? (cb_rgba){255, 150, 30, 255}
                        : (A.o[k].sel && A.mode == MODE_OBJECT ? (cb_rgba){236, 200, 160, 255} : (cb_rgba){190, 196, 208, 255});
            /* cb_mesh_triangle wants its corners the other way round (it swaps b and c itself), so a
               counter-clockwise face is handed over as (0, i+1, i): lit and culled the right way round */
            for (i = 1; i + 1 < n; i++, tris++) {
                if (vn) {
                    Vector2 z = {0, 0};
                    cb_mesh_triangle_nuv(A.render, to_cb(wpt(k, m->v[vi[0]])), to_cb(wpt(k, m->v[vi[i + 1]])), to_cb(wpt(k, m->v[vi[i]])),
                        to_cb_dir(k, vn[vi[0]]), to_cb_dir(k, vn[vi[i + 1]]), to_cb_dir(k, vn[vi[i]]), z, z, z, col);
                } else
                    cb_mesh_triangle(A.render, to_cb(wpt(k, m->v[vi[0]])), to_cb(wpt(k, m->v[vi[i + 1]])), to_cb(wpt(k, m->v[vi[i]])), col);
            }
        }
        free(vn);
    }
    if (tris > 0) cb_mesh_finish(A.render, false, true, 0, 0);
    A.rtris = tris;
    A.dirty = 0;
}

/* ---- picking and edits ---- */

static mf_vec3 mouse_ray(float x, float y);

/* ---- the hierarchy panel (top right) and the text helper ---- */
#define PANEL_W 230
#define PANEL_ROW 22
#define PANEL_TOP 32
static int panel_x(void) { return cb_screen_width() - PANEL_W - 8; }
static void build_tree(void);
static int panel_h(void) { build_tree(); return (A.nrows + 2) * PANEL_ROW + 8; }   /* Scene row, one row per visible object, the mode line */
static int in_panel(int x, int y) { return x >= panel_x() && x < panel_x() + PANEL_W && y >= PANEL_TOP && y < PANEL_TOP + panel_h(); }
/* the object whose row is at (x, y), or -1 */
static int is_anc(int a, int i) {   /* a is a strict ancestor of i */
    int n = 0;
    for (i = A.o[i].parent; i >= 0 && i < A.nobj && n < MAX_OBJ; i = A.o[i].parent, n++) if (i == a) return 1;
    return 0;
}
static int affected(int i) {   /* selected, or carried along by a selected ancestor */
    int p, n = 0;
    if (A.o[i].sel) return 1;
    for (p = A.o[i].parent; p >= 0 && p < A.nobj && n < MAX_OBJ; p = A.o[p].parent, n++) if (A.o[p].sel) return 1;
    return 0;
}
static void tree_add(int i, int d, int *n) {
    int j;
    A.ord[*n] = i; A.depth[*n] = d; (*n)++;
    if (A.o[i].fold) return;
    for (j = 0; j < A.nobj; j++) if (A.o[j].parent == i && *n < MAX_OBJ) tree_add(j, d + 1, n);
}
static void build_tree(void) {   /* the panel's rows: roots in order, each followed by its children */
    int i, n = 0;
    for (i = 0; i < A.nobj; i++) if (A.o[i].parent < 0 || A.o[i].parent >= A.nobj) tree_add(i, 0, &n);
    A.nrows = n;
}
static int has_kids(int i) {
    int j;
    for (j = 0; j < A.nobj; j++) if (A.o[j].parent == i) return 1;
    return 0;
}
static int row_ax(int depth) { return panel_x() + 8 + depth * 14; }   /* the fold arrow's left edge */
/* display row under (x, y): -1 the Scene header, 0.. an object row, -2 outside */
static int row_pos(int x, int y) {
    int p;
    if (!in_panel(x, y) || y < PANEL_TOP + 4) return -2;
    p = (y - PANEL_TOP - 4) / PANEL_ROW - 1;
    return p < A.nrows ? p : -2;
}
/* the object whose row is at (x, y), or -1 */
static int row_at(int x, int y) {
    int p = row_pos(x, y);
    build_tree();
    return p >= 0 ? A.ord[p] : -1;
}

/* ---- the tabbed panel under the hierarchy: Item (transform), Mesh (data), Mods (modifiers), View (X-ray, snapping) ---- */
enum { TAB_ITEM, TAB_MESH, TAB_MODS, TAB_VIEW, TAB_N };
static const char *TAB_SHORT[TAB_N] = { "Item", "Mesh", "Mods", "View" };
static const char *TAB_LONG[TAB_N] = { "Transform", "Mesh data", "Modifiers", "View and snapping" };
#define TABS_HEAD 26
#define TABS_BODY 172
#define TABS_ROW 22
static int tabs_y(void) {
    int y = PANEL_TOP + panel_h() + 8, lim = cb_screen_height() - 56 - (TABS_HEAD + TABS_BODY);
    return y < lim ? y : lim;
}
static int tabs_h(void) {   /* down to the status line: the modifier stack needs the room */
    int h = cb_screen_height() - 56 - tabs_y();
    return h > TABS_HEAD + TABS_BODY ? h : TABS_HEAD + TABS_BODY;
}
static int in_tabs(int x, int y) { return x >= panel_x() && x < panel_x() + PANEL_W && y >= tabs_y() && y < tabs_y() + tabs_h(); }
static int tab_at(int x, int y) {
    if (!in_tabs(x, y) || y >= tabs_y() + TABS_HEAD) return -1;
    return (x - panel_x()) * TAB_N / PANEL_W;
}
static int tab_row_y(int r) { return tabs_y() + TABS_HEAD + 30 + r * TABS_ROW; }

/* ---- the tool bar (left): Box select / Move / Rotate / Scale ---- */
#define TB_X 8
#define TB_Y 36
#define TB_W 58
#define TB_H 32
#define TB_GAP 4
static int tool_at(int x, int y) {
    int i;
    if (x < TB_X || x >= TB_X + TB_W || y < TB_Y) return -1;
    i = (y - TB_Y) / (TB_H + TB_GAP);
    if (i >= TOOL_N || (y - TB_Y) % (TB_H + TB_GAP) >= TB_H) return -1;
    return i;
}
/* the view bar (top centre, under the title bar): switches for how the scene is shown. Just X-ray so far. */
#define VB_Y 32
#define VB_H 28
#define VB_BW 76
#define VB_N 1
static int vbar_x(void) { return (cb_screen_width() - VB_N * VB_BW) / 2; }
static int xray_btn_at(int x, int y) { return x >= vbar_x() && x < vbar_x() + VB_BW && y >= VB_Y && y < VB_Y + VB_H; }
/* The workspace tabs (top left): Modeling and Shader, so the shader editor is one click away (F4 still toggles). */
#define WS_X 8
#define WS_Y 3
#define WS_H 24
#define WS_W 84
static int ws_tab_at(int x, int y) { return y >= WS_Y && y < WS_Y + WS_H && x >= WS_X && x < WS_X + 2 * WS_W ? (x - WS_X) / WS_W : -1; }
/* Shader workspace buttons, right of the tabs: Import model (an OBJ path is typed), and the preview's Sphere / Model switch. */
#define WB_X (WS_X + 2 * WS_W + 8)
#define WB_IMP_W 112
#define WB_PRE_W 124
static int ws_btn_at(int x, int y) {   /* 0 import, 1 preview switch, -1 none */
    if (y < WS_Y || y >= WS_Y + WS_H) return -1;
    if (x >= WB_X && x < WB_X + WB_IMP_W) return 0;
    if (x >= WB_X + WB_IMP_W + 4 && x < WB_X + WB_IMP_W + 4 + WB_PRE_W) return 1;
    return -1;
}
static int ws_ui_at(int x, int y) { return ws_tab_at(x, y) >= 0 || (mf_shader_active() && ws_btn_at(x, y) >= 0); }
static int in_toolbar(int x, int y) { return (x >= TB_X && x < TB_X + TB_W && y >= TB_Y && y < TB_Y + TOOL_N * (TB_H + TB_GAP)) || xray_btn_at(x, y) || ws_tab_at(x, y) >= 0; }

/* Text through the smooth TrueType font when it loaded (mf_font.c), else C-Bricks' built-in pixel font. */
static void txt(const char *s, int x, int y, int size, cb_rgba c) {
    if (mf_font_ok()) mf_font_draw(s, x, y - 1, size, c.r, c.g, c.b, c.a);
    else cb_draw_text(s, x, y, size, c);
}

/* ---- the Add menu and the settings panel for the primitive just added (like Blender's "Adjust Last Operation") ---- */
#define MENU_W 150
#define MENU_ROW 24
#define AP_W 262
#define AP_ROW 24
#define AP_BOX_X 112          /* the value box starts this far into the panel */
static int add_btn_hit(int x, int y) { return x >= panel_x() + PANEL_W - 66 && x < panel_x() + PANEL_W - 6 && y >= PANEL_TOP + 3 && y < PANEL_TOP + 3 + PANEL_ROW; }
static const char *MOD_TITLE[3] = { "Mirror", "Subdivision", "Array" };
static int menu_n(void) { return A.menu_kind ? 3 : PRIM_N; }
static const char *menu_name(int i) { return A.menu_kind ? MOD_TITLE[i] : PRIM_NAME[i]; }
static int menu_item_at(int x, int y) {
    int i;
    if (x < A.menu_x || x >= A.menu_x + MENU_W || y < A.menu_y + 4) return -1;
    i = (y - A.menu_y - 4) / MENU_ROW;
    return i >= 0 && i < menu_n() ? i : -1;
}
static void menu_open_at(int x, int y, int kind) {
    int w = cb_screen_width(), h = cb_screen_height();
    A.menu_kind = kind;
    if (x + MENU_W > w - 4) x = w - MENU_W - 4;
    if (y + menu_n() * MENU_ROW + 8 > h - 4) y = h - menu_n() * MENU_ROW - 12;
    A.menu_open = 1; A.menu_x = x; A.menu_y = y;
}
static int ap_h(void) { return AP_ROW * (A.nprm + 1) + 10; }
static int ap_y(void) { return cb_screen_height() - 56 - ap_h(); }
static int ap_row_y(int i) { return ap_y() + AP_ROW * (i + 1) + 6; }
static int in_add_panel(int x, int y) { return A.addon && x >= 8 && x < 8 + AP_W && y >= ap_y() && y < ap_y() + ap_h(); }

/* ---- the look: C-Bricks' editor theme (near-black panels with a thin frame, rounded corners, the light-blue accent, the brick
   categories' pastels) laid out the way Blender does it: an outliner with fold arrows and icons, collapsible modifier panels,
   number fields, segmented buttons. Everything is opaque, so the corners can be drawn as rectangles plus four circles. ---- */
#define C_PANEL  ((cb_rgba){27, 29, 35, 255})
#define C_FRAME  ((cb_rgba){58, 62, 74, 255})
#define C_HEAD   ((cb_rgba){38, 42, 52, 255})     /* section header bar */
#define C_FIELD  ((cb_rgba){45, 50, 62, 255})
#define C_FIELDH ((cb_rgba){60, 67, 84, 255})
#define C_ACC    ((cb_rgba){112, 186, 250, 255})  /* the editor's accent */
#define C_ACCBG  ((cb_rgba){36, 64, 92, 255})     /* selected, active */
#define C_ACCBG2 ((cb_rgba){31, 50, 70, 255})     /* selected */
#define C_INK    ((cb_rgba){226, 229, 236, 255})
#define C_DIM    ((cb_rgba){138, 145, 160, 255})
#define C_MODC   ((cb_rgba){217, 191, 255, 255})  /* the "mesh" brick colour: modifiers */
#define C_MODBG  ((cb_rgba){54, 47, 76, 255})
#define C_OBJ    ((cb_rgba){255, 166, 77, 255})   /* Blender's object orange */

static int tw_(const char *s, int size) { return mf_font_ok() ? mf_font_width(s, size) : (int)strlen(s) * (size > 14 ? 8 : 7); }
static void rr(int x, int y, int w, int h, int r, cb_rgba c) {   /* a rounded rectangle */
    if (w <= 0 || h <= 0) return;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r < 1) { cb_draw_rect(x, y, w, h, c); return; }
    cb_draw_rect(x + r, y, w - 2 * r, h, c);
    cb_draw_rect(x, y + r, w, h - 2 * r, c);
    cb_draw_circle(x + r, y + r, (double)r, c);
    cb_draw_circle(x + w - r, y + r, (double)r, c);
    cb_draw_circle(x + r, y + h - r, (double)r, c);
    cb_draw_circle(x + w - r, y + h - r, (double)r, c);
}
static void ui_box(int x, int y, int w, int h) { rr(x, y, w, h, 5, C_FRAME); rr(x + 1, y + 1, w - 2, h - 2, 4, C_PANEL); }
static void ico_arrow(int x, int y, int open, cb_rgba c) {   /* fold arrow, 8 x 8 */
    int i;
    for (i = 0; i < 4; i++) {
        if (open) cb_draw_rect(x + i, y + 2 + i, 8 - 2 * i, 1, c);
        else cb_draw_rect(x + 2 + i, y + i, 1, 8 - 2 * i, c);
    }
}
static void ico_object(int x, int y, cb_rgba c) {   /* the mesh-object triangle, 13 x 12 */
    cb_draw_line(x + 6, y, x, y + 11, c);
    cb_draw_line(x, y + 11, x + 12, y + 11, c);
    cb_draw_line(x + 12, y + 11, x + 6, y, c);
}
static void ico_wrench(int x, int y, cb_rgba c) {   /* 12 x 12 */
    cb_draw_line(x + 1, y + 11, x + 7, y + 5, c);
    cb_draw_line(x + 2, y + 11, x + 8, y + 5, c);
    cb_draw_circle(x + 8, y + 4, 3.5, c);
}
static void ico_eye(int x, int y, int on, cb_rgba c) {   /* 14 x 10 */
    cb_draw_line(x, y + 5, x + 7, y, c); cb_draw_line(x + 7, y, x + 14, y + 5, c);
    cb_draw_line(x, y + 5, x + 7, y + 10, c); cb_draw_line(x + 7, y + 10, x + 14, y + 5, c);
    if (on) cb_draw_circle(x + 7, y + 5, 2.5, c);
    else cb_draw_line(x + 2, y + 10, x + 12, y, c);
}

/* A tiny immediate-mode layer: a panel's code runs twice with the same layout, once to draw (w_draw = 1) and once on a click,
   where each widget just says "that was me": so what is drawn and what is hit cannot drift apart. Widgets outside
   [w_top, w_bot) are skipped (not drawn, not clickable). */
static int w_draw, w_mx, w_my, w_top, w_bot;
static int w_in(int x, int y, int w, int h) { return w_mx >= x && w_mx < x + w && w_my >= y && w_my < y + h; }
static int w_vis(int y, int h) { return y >= w_top && y + h <= w_bot; }
static int w_btn(int x, int y, int w, int h, const char *s, int on, cb_rgba ink) {
    if (!w_vis(y, h)) return 0;
    if (!w_draw) return w_in(x, y, w, h);
    rr(x, y, w, h, 3, on ? C_ACCBG : (w_in(x, y, w, h) ? C_FIELDH : C_FIELD));
    txt(s, x + (w - tw_(s, 14)) / 2, y + (h - 14) / 2 + 1, 14, on ? C_ACC : ink);
    return 0;
}
/* a number field with arrows at both ends: 0 not hit, 1 left arrow, 2 right arrow, 3 the middle */
static int w_field(int x, int y, int w, int h, const char *val) {
    int hot, ty;
    if (!w_vis(y, h)) return 0;
    if (!w_draw) return w_in(x, y, w, h) ? (w_mx < x + 24 ? 1 : (w_mx >= x + w - 24 ? 2 : 3)) : 0;
    hot = w_in(x, y, w, h);
    ty = y + (h - 14) / 2 + 1;
    rr(x, y, w, h, 3, hot ? C_FIELDH : C_FIELD);
    txt("<", x + 9, ty, 14, hot && w_mx < x + 24 ? C_ACC : C_DIM);
    txt(">", x + w - 16, ty, 14, hot && w_mx >= x + w - 24 ? C_ACC : C_DIM);
    txt(val, x + (w - tw_(val, 14)) / 2, ty, 14, C_INK);
    return 0;
}
static int w_eye(int x, int y, int w, int h, int on) {
    if (!w_vis(y, h)) return 0;
    if (!w_draw) return w_in(x, y, w, h);
    if (w_in(x, y, w, h)) rr(x, y, w, h, 3, C_FIELDH);
    ico_eye(x + (w - 14) / 2, y + (h - 10) / 2, on, on ? C_INK : C_DIM);
    return 0;
}

/* ---- the modifier stack (Mods tab): one collapsible panel per modifier, applied top to bottom ---- */
static int mod_max_count(const mf_modifier *m) { return m->type == MF_MOD_SUBDIV ? 3 : 16; }
static void mod_add(int k) {
    sobj *o;
    mf_modifier *m;
    if (A.cur < 0 || A.cur >= A.nobj) { status("select an object first"); return; }
    if (A.mode != MODE_OBJECT) { status("modifiers: press Tab to leave Mesh mode first (the cage is shown while editing)"); return; }
    o = &A.o[A.cur];
    if (o->nmod >= MAX_MOD) { status("at most %d modifiers per object", MAX_MOD); return; }
    undo_push();
    m = &o->mod[o->nmod];
    m->type = k; m->axis = 0; m->n = k == MF_MOD_ARRAY ? 2 : 1; m->v = k == MF_MOD_ARRAY ? 1.2f : 1.0f; m->off = 0;
    o->shut &= ~(1 << o->nmod);
    o->nmod++;
    A.dirty = 1;
    status("modifier %s added to \"%s\" (%d on it)", MOD_TITLE[k], o->name, o->nmod);
}
static void mod_remove(sobj *o, int i) {
    int j, low = o->shut & ((1 << i) - 1), high = (o->shut >> (i + 1)) << i;
    undo_push();
    for (j = i; j + 1 < o->nmod; j++) o->mod[j] = o->mod[j + 1];
    o->nmod--;
    o->shut = low | high;
    A.dirty = 1;
    status("modifier removed from \"%s\" (%d left)", o->name, o->nmod);
}
static void mod_apply(sobj *o, int i) {   /* bake this one and everything above it into the mesh */
    mf_mesh *r;
    int j, n = i + 1;
    if (o->mod[i].off) { status("that modifier is switched off: switch it on to apply it"); return; }
    r = mf_mod_eval(o->mesh, o->mod, n);
    if (!r) { status("apply failed: the result is too big, or out of memory"); return; }
    undo_push();
    mf_free(o->mesh);
    o->mesh = r;
    if (A.cur >= 0 && !A.baked) A.mesh = A.o[A.cur].mesh;
    for (j = 0; j + n < o->nmod; j++) o->mod[j] = o->mod[j + n];
    o->nmod -= n;
    o->shut >>= n;
    A.dirty = 1;
    status("applied %d modifier(s) to \"%s\": %d vertices, %d faces now", n, o->name, r->nv, r->nf);
}
static int mods_walk(int draw) {
    sobj *o = A.cur >= 0 && A.cur < A.nobj ? &A.o[A.cur] : NULL;
    static const char *AX[3] = { "X", "Y", "Z" };
    int x0 = panel_x() + 8, W = PANEL_W - 16, y, i, can = A.mode == MODE_OBJECT;
    w_draw = draw; w_mx = cb_mouse_x(); w_my = cb_mouse_y();
    w_top = tabs_y() + TABS_HEAD; w_bot = tabs_y() + tabs_h() - 4;
    y = w_top + 8;
    if (!o) { if (draw) txt("No active object", x0 + 4, y + 2, 14, C_DIM); return 1; }
    if (w_btn(x0, y, W, 26, "Add Modifier", 0, C_INK)) {
        if (!can) status("modifiers: press Tab to leave Mesh mode first (the cage is shown while editing)");
        else menu_open_at(x0, y + 28, 1);
        return 1;
    }
    if (draw && w_vis(y, 26)) ico_arrow(x0 + W - 20, y + 9, 1, C_DIM);
    y += 34;
    for (i = 0; i < o->nmod; i++) {
        mf_modifier *m = &o->mod[i];
        int open = !((o->shut >> i) & 1), nr = m->type == MF_MOD_ARRAY ? 3 : 1, hh = 26;
        int total = hh + (open ? 10 + nr * 27 : 0);
        int xr = x0 + W - 24, xa = xr - 3 - 40, xe = xa - 3 - 20, by = y + 3;
        cb_rgba hc = m->off ? C_HEAD : C_MODBG;
        if (w_vis(y, total)) {
            if (draw) {
                rr(x0, y, W, total, 5, C_FRAME);
                rr(x0 + 1, y + 1, W - 2, total - 2, 4, C_PANEL);
                rr(x0 + 1, y + 1, W - 2, hh - 1, 4, hc);
                if (open) cb_draw_rect(x0 + 1, y + hh / 2, W - 2, hh / 2, hc);
                ico_arrow(x0 + 8, y + 9, open, C_DIM);
                ico_wrench(x0 + 22, y + 7, m->off ? C_DIM : C_MODC);
                txt(MOD_TITLE[m->type], x0 + 42, y + 5, 14, m->off ? C_DIM : C_INK);
            } else if (w_in(x0, y, xe - x0 - 2, hh)) { o->shut ^= 1 << i; return 1; }
            if (w_eye(xe, by, 20, 20, !m->off)) {
                undo_push(); m->off = !m->off; A.dirty = 1;
                status("%s %s", MOD_TITLE[m->type], m->off ? "switched off" : "switched on");
                return 1;
            }
            if (w_btn(xa, by, 40, 20, "Apply", 0, C_INK)) {
                if (!can) status("modifiers: press Tab to leave Mesh mode first");
                else mod_apply(o, i);
                return 1;
            }
            if (w_btn(xr, by, 20, 20, "x", 0, C_INK)) {
                if (!can) status("modifiers: press Tab to leave Mesh mode first");
                else mod_remove(o, i);
                return 1;
            }
            if (open) {
                int ry = y + hh + 6, lx = x0 + 10, bx = x0 + 10 + 56, bw = W - 20 - 56, r = 0, d;
                if (m->type != MF_MOD_SUBDIV) {   /* Axis: X | Y | Z */
                    int a, sw = (bw - 4) / 3;
                    if (draw) txt("Axis", lx, ry + 5, 14, C_DIM);
                    for (a = 0; a < 3; a++)
                        if (w_btn(bx + a * (sw + 2), ry, sw, 24, AX[a], m->axis % 3 == a, C_INK)) {
                            if (!can) status("modifiers: press Tab to leave Mesh mode first");
                            else if (m->axis != a) { undo_push(); m->axis = a; A.dirty = 1; status("%s axis %s", MOD_TITLE[m->type], AX[a]); }
                            return 1;
                        }
                    r++;
                }
                if (m->type != MF_MOD_MIRROR) {   /* Levels / Count */
                    int yy = ry + r * 27;
                    if (draw && w_vis(yy, 24)) txt(m->type == MF_MOD_SUBDIV ? "Levels" : "Count", lx, yy + 5, 14, C_DIM);
                    d = w_field(bx, yy, bw, 24, cb_format("%d", m->n));
                    if (d == 3 && can) { A.fd_on = 1; A.fd_obj = A.cur; A.fd_mod = i; A.fd_kind = 0; A.fd_x0 = cb_mouse_x(); A.fd_v0 = (float)m->n; A.fd_pushed = 0; return 1; }
                    if (d) {
                        int nn = m->n + (d == 1 ? -1 : (d == 2 ? 1 : 0));
                        if (!can) status("modifiers: press Tab to leave Mesh mode first");
                        else if (nn >= 1 && nn <= mod_max_count(m) && nn != m->n) { undo_push(); m->n = nn; A.dirty = 1; status("%s: %d", MOD_TITLE[m->type], nn); }
                        return 1;
                    }
                    r++;
                }
                if (m->type == MF_MOD_ARRAY) {   /* Relative offset, in sizes of the mesh along the axis */
                    int yy = ry + r * 27;
                    if (draw && w_vis(yy, 24)) txt("Offset", lx, yy + 5, 14, C_DIM);
                    d = w_field(bx, yy, bw, 24, cb_format("%.1f", (double)m->v));
                    if (d == 3 && can) { A.fd_on = 1; A.fd_obj = A.cur; A.fd_mod = i; A.fd_kind = 1; A.fd_x0 = cb_mouse_x(); A.fd_v0 = m->v; A.fd_pushed = 0; return 1; }
                    if (d) {
                        float nv = (float)((int)(m->v * 10.0f + 0.5f)) / 10.0f + (d == 1 ? -0.1f : (d == 2 ? 0.1f : 0.0f));
                        if (!can) status("modifiers: press Tab to leave Mesh mode first");
                        else if (nv >= 0.0f && nv <= 5.0f && d != 3) { undo_push(); m->v = nv; A.dirty = 1; status("Array offset %.1f", (double)nv); }
                        return 1;
                    }
                }
            }
        }
        y += total + 6;
    }
    if (draw && w_vis(y, 16)) txt(!o->nmod ? "No modifiers yet." : (can ? "Applied top to bottom." : "Tab: Object mode to edit the stack."), x0 + 4, y + 2, 13, C_DIM);
    return 1;
}

static void prm_set(int i, const char *label, float v, float lo, float hi, float step, int kind) {
    A.prm[i].label = label; A.prm[i].v = v; A.prm[i].lo = lo; A.prm[i].hi = hi; A.prm[i].step = step; A.prm[i].kind = kind;
}

static void prm_defaults(int kind) {
    int n = 0;
    switch (kind) {
    case PRIM_PLANE:    prm_set(n++, "Size", 2.0f, 0.01f, 100.0f, 0.1f, 0); break;
    case PRIM_CUBE:     prm_set(n++, "Size", 2.0f, 0.01f, 100.0f, 0.1f, 0); break;
    case PRIM_CIRCLE:   prm_set(n++, "Vertices", 32, 3, 128, 1, 1); prm_set(n++, "Radius", 1.0f, 0.01f, 100.0f, 0.1f, 0); break;
    case PRIM_SPHERE:   prm_set(n++, "Segments", 32, 3, 64, 1, 1); prm_set(n++, "Rings", 16, 2, 32, 1, 1);
                        prm_set(n++, "Radius", 1.0f, 0.01f, 100.0f, 0.1f, 0); break;
    case PRIM_CYLINDER: prm_set(n++, "Vertices", 32, 3, 128, 1, 1); prm_set(n++, "Radius", 1.0f, 0.01f, 100.0f, 0.1f, 0);
                        prm_set(n++, "Depth", 2.0f, 0.01f, 100.0f, 0.1f, 0); prm_set(n++, "Caps", 1, 0, 1, 1, 2); break;
    case PRIM_CONE:     prm_set(n++, "Vertices", 32, 3, 128, 1, 1); prm_set(n++, "Radius 1", 1.0f, 0.0f, 100.0f, 0.1f, 0);
                        prm_set(n++, "Radius 2", 0.0f, 0.0f, 100.0f, 0.1f, 0); prm_set(n++, "Depth", 2.0f, 0.01f, 100.0f, 0.1f, 0);
                        prm_set(n++, "Base fill", 1, 0, 1, 1, 2); break;
    default:            prm_set(n++, "Major segs", 32, 3, 96, 1, 1); prm_set(n++, "Minor segs", 12, 3, 48, 1, 1);
                        prm_set(n++, "Major radius", 1.0f, 0.01f, 100.0f, 0.1f, 0); prm_set(n++, "Minor radius", 0.25f, 0.01f, 50.0f, 0.05f, 0); break;
    }
    prm_set(n++, "Location X", 0.0f, -100.0f, 100.0f, 0.1f, 0);
    prm_set(n++, "Location Y", 0.0f, -100.0f, 100.0f, 0.1f, 0);
    prm_set(n++, "Location Z", 0.0f, -100.0f, 100.0f, 0.1f, 0);
    A.nprm = n;
}

/* Appends the primitive with the panel's current values to m (at the origin: the caller places it). 0 ok. */
static int build_prim(mf_mesh *m) {
    const prm *p = A.prm;
    switch (A.addkind) {
    case PRIM_PLANE:    return mf_make_plane(m, p[0].v);
    case PRIM_CUBE:     return mf_make_cube(m, p[0].v);
    case PRIM_CIRCLE:   return mf_make_circle(m, (int)p[0].v, p[1].v);
    case PRIM_SPHERE:   return mf_make_uvsphere(m, (int)p[0].v, (int)p[1].v, p[2].v);
    case PRIM_CYLINDER: return mf_make_cylinder(m, (int)p[0].v, p[1].v, p[2].v, p[3].v > 0.5f);
    case PRIM_CONE:     return mf_make_cone(m, (int)p[0].v, p[1].v, p[2].v, p[3].v, p[4].v > 0.5f);
    default:            return mf_make_torus(m, (int)p[0].v, (int)p[1].v, p[2].v, p[3].v);
    }
}

/* Rebuild from the panel's values. Object mode: the new object's mesh and position. Mesh mode: the saved base
   (the object's mesh) plus the primitive, with the new piece selected. */
static void add_regen(void) {
    const prm *p = A.prm;
    int n = A.nprm - 3, first, first_f, r, v, f;
    mf_vec3 loc = mf_v3(p[n].v, p[n + 1].v, p[n + 2].v);
    mf_mesh *c, *m;
    if (A.addobj) {
        m = mf_new();
        if (!m) { status("out of memory"); return; }
        r = build_prim(m);
        if (r != 0) status("%s: those settings make nothing (a cone needs a radius)", PRIM_NAME[A.addkind]);
        mf_free(A.o[A.addidx].mesh);
        A.o[A.addidx].mesh = m;
        A.o[A.addidx].t.pos = loc;
        A.mesh = m;
        A.dirty = 1;
        return;
    }
    c = mf_clone(A.addbase);
    if (!c) { status("out of memory"); return; }
    mf_free(A.mesh);
    A.mesh = m = c;
    m->topo_ok = 0;
    first = m->nv; first_f = m->nf;
    r = build_prim(m);
    if (r != 0) status("%s: those settings make nothing (a cone needs a radius)", PRIM_NAME[A.addkind]);
    mf_move_verts_from(m, first, loc);
    mf_sel_clear(m);
    for (v = first; v < m->nv; v++) if (A.selmode != MF_SEL_FACE) mf_sel_vert(m, v, 1);
    for (f = first_f; f < m->nf; f++) if (A.selmode == MF_SEL_FACE) mf_sel_face(m, f, 1);
    A.dirty = 1;
}

static void prm_change(int i, float v) {
    prm *p = &A.prm[i];
    if (p->kind == 1) v = floorf(v + 0.5f);
    if (v < p->lo) v = p->lo;
    if (v > p->hi) v = p->hi;
    if (v == p->v) return;
    p->v = v;
    if (A.addon) add_regen();
}

/* Object mode: a whole new object in the hierarchy. Mesh mode: the shape goes into the object being edited. */
static void add_primitive(int kind) {
    close_add_panel();
    if (A.mode == MODE_OBJECT || A.cur < 0) {
        int i;
        if (A.nobj >= MAX_OBJ) { status("the scene is full (%d objects)", MAX_OBJ); return; }
        undo_push();   /* Ctrl+Z takes the whole addition away */
        i = A.nobj++;
        memset(&A.o[i], 0, sizeof A.o[i]);
        A.o[i].t = mf_trs_identity();
        A.o[i].parent = -1;
        A.o[i].mesh = mf_new();
        if (!A.o[i].mesh) { A.nobj--; undo_drop(); status("out of memory"); return; }
        A.nobj--;   /* (the new slot must not count as a clash with its own name) */
        unique_name(PRIM_NAME[kind], A.o[i].name, sizeof A.o[i].name);
        A.nobj++;
        { int j; for (j = 0; j < A.nobj; j++) A.o[j].sel = j == i; }
        set_cur(i);
        A.addon = 1; A.addobj = 1; A.addidx = i; A.addkind = kind;
        prm_defaults(kind);
        A.mode = MODE_OBJECT;
        add_regen();
        status("added object %s (own transform, in the hierarchy); change it in the panel bottom left", A.o[i].name);
        return;
    }
    undo_push();   /* Ctrl+Z takes the whole addition away */
    A.addbase = mf_clone(A.mesh);
    if (!A.addbase) { status("out of memory"); return; }
    A.addon = 1; A.addkind = kind;
    prm_defaults(kind);
    {   /* it appears at the object's origin (the panel's Location is in the world) */
        int n = A.nprm - 3;
        mf_vec3 pos = A.o[A.cur].t.pos;
        A.prm[n].v = pos.x; A.prm[n + 1].v = pos.y; A.prm[n + 2].v = pos.z;
    }
    add_regen();
    status("added %s into %s (selected: G moves it); change it in the panel bottom left", PRIM_NAME[kind], A.o[A.cur].name);
}

/* The interface's part of a frame (before the viewport commands). Returns 1 when it took the left click. */
static int shift_down(void);
static int alt_down(void);

/* ---- rename (F2 / double click on a row) and parenting ---- */
static void rename_start(int i) {
    if (i < 0 || i >= A.nobj) return;
    A.renaming = 1; A.rn_i = i; A.rn_fresh = 1;   /* the old name is selected: the first key replaces it */
    snprintf(A.rn, sizeof A.rn, "%s", A.o[i].name);
    status("rename: type, Enter ok, Esc cancel");
}
static void rename_end(int ok) {
    A.renaming = 0;
    if (ok && A.rn[0] && A.rn_i < A.nobj && strcmp(A.rn, A.o[A.rn_i].name) != 0) {
        char tmp[48];
        undo_push();
        A.o[A.rn_i].name[0] = 0;   /* (so it does not clash with itself) */
        unique_name(A.rn, tmp, sizeof tmp);
        snprintf(A.o[A.rn_i].name, sizeof A.o[A.rn_i].name, "%s", tmp);
        status("renamed to \"%s\"", tmp);
    } else status("rename %s", ok ? "unchanged" : "cancelled");
}
/* Import model (Shader workspace): an OBJ becomes a new object in the scene and the preview switches to the model. */
static void import_obj(const char *path) {
    mf_mesh *m;
    char err[200];
    int i, j;
    err[0] = 0;
    if (A.mode == MODE_MESH) { status("leave Mesh mode (Tab) before importing"); return; }
    if (A.nobj >= MAX_OBJ) { status("the scene is full (%d objects)", MAX_OBJ); return; }
    m = mf_new();
    if (!m) { status("out of memory"); return; }
    if (mf_obj_read(m, path, err, sizeof err) != 0 || m->nf == 0) {
        status("import failed: %s", m->nf == 0 && err[0] == 0 ? "no faces" : err);
        printf("[meshforge] import %s failed\n", path);
        mf_free(m);
        return;
    }
    undo_push();
    i = A.nobj;
    memset(&A.o[i], 0, sizeof A.o[i]);
    A.o[i].t = mf_trs_identity();
    A.o[i].parent = -1;
    A.o[i].mesh = m;
    {   /* the name: the file's, without folders and extension */
        const char *b = strrchr(path, '/');
        char nm[48], *dot;
        snprintf(nm, sizeof nm, "%s", b ? b + 1 : path);
        dot = strrchr(nm, '.');
        if (dot && dot != nm) *dot = 0;
        unique_name(nm, A.o[i].name, sizeof A.o[i].name);
    }
    A.nobj++;
    for (j = 0; j < A.nobj; j++) A.o[j].sel = j == i;
    set_cur(i);
    A.dirty = 1;
    frame_mesh(0);
    mf_shader_set_model(1);
    status("imported %s as \"%s\"", path, A.o[i].name);
    printf("[meshforge] imported %s as %s\n", path, A.o[i].name);
}
static void import_update(void) {
    size_t n = strlen(A.imp_buf);
    int i;
    if (cb_key_pressed(CB_KEY_ESCAPE)) { A.imp = 0; return; }
    if (cb_key_pressed(CB_KEY_ENTER)) { A.imp = 0; if (n) import_obj(A.imp_buf); return; }
    for (i = 0; i < A.ntyped; i++) {
        int c = (unsigned char)A.typed[i];
        if (c >= 32 && c < 127 && n + 1 < sizeof A.imp_buf) { A.imp_buf[n++] = (char)c; A.imp_buf[n] = 0; }
    }
    if (cb_key_pressed(CB_KEY_BACKSPACE) && n > 0) A.imp_buf[n - 1] = 0;
}

static void rename_update(void) {
    size_t n = strlen(A.rn);
    int i;
    if (cb_key_pressed(CB_KEY_ESCAPE)) { rename_end(0); return; }
    if (cb_key_pressed(CB_KEY_ENTER) || cb_mouse_pressed(CB_MOUSE_LEFT) || cb_mouse_pressed(CB_MOUSE_RIGHT)) { rename_end(1); return; }
    if (A.rn_fresh && (A.ntyped || cb_key_pressed(CB_KEY_BACKSPACE))) {   /* the first key replaces the old name */
        A.rn[0] = 0; n = 0; A.rn_fresh = 0;
        if (cb_key_pressed(CB_KEY_BACKSPACE)) return;
    }
    for (i = 0; i < A.ntyped; i++) {   /* any printable ASCII (the font only has Latin-1; stay in ASCII for file-safe names) */
        int c = (unsigned char)A.typed[i];
        if (c >= 32 && c < 127 && n + 1 < sizeof A.rn) { A.rn[n++] = (char)c; A.rn[n] = 0; }
    }
    if (cb_key_pressed(CB_KEY_BACKSPACE) && n > 0) A.rn[n - 1] = 0;
}

/* Makes object i a child of j (j = -1: no parent). Its world transform stays as it is. 1 when it changed. */
static int set_parent(int i, int j, int quiet) {
    if (i < 0 || i >= A.nobj || j >= A.nobj || i == j) return 0;
    if (j >= 0 && is_anc(i, j)) { status("\"%s\" is inside \"%s\": it cannot become its parent", A.o[j].name, A.o[i].name); return 0; }
    if (A.o[i].parent == j) return 0;
    A.o[i].parent = j;
    if (!quiet) status(j >= 0 ? "\"%s\" is now a child of \"%s\"" : "\"%s\" has no parent now", A.o[i].name, j >= 0 ? A.o[j].name : "");
    return 1;
}

static int ui_update(void) {
    int mx = cb_mouse_x(), my = cb_mouse_y();
    int press = cb_mouse_pressed(CB_MOUSE_LEFT), down = cb_mouse_down(CB_MOUSE_LEFT);
    if (A.fd_on) {   /* dragging a modifier's number field sideways (the centre of the field; the arrows step) */
        sobj *o = &A.o[A.fd_obj];
        if (!down || A.fd_obj >= A.nobj || A.fd_mod >= o->nmod) { A.fd_on = 0; return 1; }
        {   mf_modifier *m = &o->mod[A.fd_mod];
            if (A.fd_kind == 0) {
                int nn = (int)A.fd_v0 + (mx - A.fd_x0) / 14;
                if (nn < 1) nn = 1;
                if (nn > mod_max_count(m)) nn = mod_max_count(m);
                if (nn != m->n) { if (!A.fd_pushed) { undo_push(); A.fd_pushed = 1; } m->n = nn; A.dirty = 1; status("%s: %d", MOD_TITLE[m->type], nn); }
            } else {
                float nv = A.fd_v0 + (float)((mx - A.fd_x0) / 6) * 0.1f;
                nv = (float)((int)(nv * 10.0f + 0.5f)) / 10.0f;
                if (nv < 0.0f) nv = 0.0f;
                if (nv > 5.0f) nv = 5.0f;
                if (nv != m->v) { if (!A.fd_pushed) { undo_push(); A.fd_pushed = 1; } m->v = nv; A.dirty = 1; status("Array offset %.1f", (double)nv); }
            }
        }
        return 1;
    }
    if (A.drag >= 0) {   /* dragging a value sideways, like Blender's number fields */
        if (!down) A.drag = -1;
        else {
            const prm *p = &A.prm[A.drag];
            prm_change(A.drag, A.drag_v0 + (float)(mx - A.drag_x0) * (p->kind == 1 ? 1.0f / 6.0f : p->step * 0.2f));
        }
        return 1;
    }
    if (A.rdrag) {   /* a hierarchy row dragged onto another makes it that one's child; onto "Scene": no parent */
        if (!down) {
            int i = A.rdrag - 1, j = row_at(mx, my), p = row_pos(mx, my);
            A.rdrag = 0;
            if (abs(mx - A.rdx) > 4 || abs(my - A.rdy) > 4) {
                if (j >= 0 && j != i) { undo_push(); if (!set_parent(i, j, 0)) undo_drop(); }
                else if (p == -1) { undo_push(); if (!set_parent(i, -1, 0)) undo_drop(); }
            }
        }
        return 1;
    }
    if (A.menu_open) {
        if (cb_key_pressed(CB_KEY_ESCAPE)) { A.menu_open = 0; return 1; }
        if (press) {
            int it = menu_item_at(mx, my);
            A.menu_open = 0;
            if (it >= 0) { if (A.menu_kind) mod_add(it); else add_primitive(it); }
            return 1;
        }
        return 0;
    }
    if (!press) return 0;
    if (tool_at(mx, my) >= 0) { A.tool = tool_at(mx, my); A.gz_log = 1; status("%s tool", TOOL_NAME[A.tool]); return 1; }
    if (xray_btn_at(mx, my)) { A.xray = !A.xray; A.dirty = 1; status("X-ray %s", A.xray ? "on: vertices show through the mesh" : "off: hidden vertices stay hidden"); return 1; }
    if (in_toolbar(mx, my)) return 1;
    if (in_tabs(mx, my)) {
        int t = tab_at(mx, my);
        if (t >= 0) { A.tab = t; status("%s tab", TAB_LONG[t]); return 1; }
        if (A.tab == TAB_MODS) return mods_walk(0);
        if (A.tab == TAB_VIEW) {
            int r0 = tab_row_y(0), r1 = tab_row_y(1), bx = panel_x() + 96, bw = PANEL_W - 96 - 10;
            if (my >= r0 && my < r0 + TABS_ROW && mx >= bx) { A.xray = !A.xray; A.dirty = 1; status("X-ray %s", A.xray ? "on" : "off"); }
            else if (my >= r1 && my < r1 + TABS_ROW && mx >= bx) {
                if (mx < bx + 26 && A.snap_i > 0) A.snap_i--;
                else if (mx >= bx + bw - 26 && A.snap_i < NSNAP - 1) A.snap_i++;
                status("snap step %.2f (hold Ctrl while moving)", (double)SNAPS[A.snap_i]);
            }
            else if (my >= tab_row_y(2) && my < tab_row_y(2) + TABS_ROW && mx >= bx) {   /* Shading: flat / smooth, per object */
                if (A.cur < 0) status("no active object");
                else { undo_push(); A.o[A.cur].smooth = !A.o[A.cur].smooth; A.dirty = 1; status("\"%s\": %s shading", A.o[A.cur].name, A.o[A.cur].smooth ? "smooth" : "flat"); }
            }
        }
        return 1;
    }
    if (in_panel(mx, my) && !add_btn_hit(mx, my)) {   /* a row of the hierarchy selects that object (Object mode) */
        int i = row_at(mx, my), rp = row_pos(mx, my);
        if (i >= 0 && rp >= 0 && has_kids(i) && mx >= row_ax(A.depth[rp]) - 3 && mx < row_ax(A.depth[rp]) + 12) {   /* the fold arrow */
            A.o[i].fold = !A.o[i].fold;
            status("\"%s\": children %s", A.o[i].name, A.o[i].fold ? "folded" : "shown");
            return 1;
        }
        if (i >= 0 && A.mode == MODE_OBJECT && !A.xf) {
            close_add_panel();
            if (i == A.click_row && cb_game_time() - A.click_t < 0.4) { A.click_row = -1; rename_start(i); return 1; }
            A.click_row = i; A.click_t = cb_game_time();
            A.rdrag = i + 1; A.rdx = mx; A.rdy = my;
            if (shift_down()) { A.o[i].sel = !A.o[i].sel; if (A.o[i].sel) set_cur(i); A.dirty = 1; }
            else { int j; for (j = 0; j < A.nobj; j++) A.o[j].sel = j == i; set_cur(i); A.dirty = 1; status("object \"%s\" selected", A.o[i].name); }
        }
        return 1;
    }
    if (add_btn_hit(mx, my)) { menu_open_at(panel_x() + PANEL_W - MENU_W - 6, PANEL_TOP + 4 + PANEL_ROW, 0); return 1; }
    if (in_add_panel(mx, my)) {
        int i = (my - ap_row_y(0)) / AP_ROW, bx = 8 + AP_BOX_X, bw = AP_W - AP_BOX_X - 8;
        if (my >= ap_row_y(0) && i >= 0 && i < A.nprm && mx >= bx && mx < bx + bw) {
            prm *p = &A.prm[i];
            float stp = p->kind == 0 ? p->step : 1.0f;
            if (p->kind == 2) prm_change(i, p->v > 0.5f ? 0.0f : 1.0f);
            else if (mx < bx + 24) prm_change(i, p->v - stp);
            else if (mx >= bx + bw - 24) prm_change(i, p->v + stp);
            else { A.drag = i; A.drag_x0 = mx; A.drag_v0 = p->v; }
        }
        return 1;
    }
    return 0;
}

static void draw_btn(int x, int y, int w, int h, const char *label, int hot) {
    rr(x, y, w, h, 3, hot ? C_FIELDH : C_FIELD);
    txt(label, x + (w - tw_(label, 14)) / 2, y + (h - 14) / 2 + 1, 14, hot ? C_ACC : C_INK);
}

static void draw_ui(void) {
    int mx = cb_mouse_x(), my = cb_mouse_y(), i;
    draw_btn(panel_x() + PANEL_W - 66, PANEL_TOP + 5, 58, PANEL_ROW - 2, "+ Add", add_btn_hit(mx, my) || A.menu_open);
    if (A.addon) {   /* "Adjust Last Operation": the settings of the primitive just added */
        int x = 8, y = ap_y(), bx = x + AP_BOX_X, bw = AP_W - AP_BOX_X - 8;
        ui_box(x, y, AP_W, ap_h());
        rr(x + 1, y + 1, AP_W - 2, AP_ROW + 2, 4, C_HEAD);
        cb_draw_rect(x + 1, y + AP_ROW / 2, AP_W - 2, AP_ROW / 2 + 3, C_HEAD);
        txt(cb_format("Add %s", PRIM_NAME[A.addkind]), x + 10, y + 6, 15, C_INK);
        for (i = 0; i < A.nprm; i++) {
            const prm *p = &A.prm[i];
            int ry = ap_row_y(i);
            const char *val = p->kind == 1 ? cb_format("%d", (int)p->v) : (p->kind == 2 ? (p->v > 0.5f ? "On" : "Off") : cb_format("%.2f", (double)p->v));
            int hot = mx >= bx && mx < bx + bw && my >= ry && my < ry + AP_ROW;
            txt(p->label, x + 10, ry + 4, 14, C_DIM);
            rr(bx, ry + 1, bw, AP_ROW - 2, 3, p->kind == 2 && p->v > 0.5f ? C_ACCBG : (hot || A.drag == i ? C_FIELDH : C_FIELD));
            if (p->kind != 2) {
                txt("<", bx + 8, ry + 4, 14, C_DIM);
                txt(">", bx + bw - 16, ry + 4, 14, C_DIM);
            }
            txt(val, bx + (bw - tw_(val, 14)) / 2, ry + 4, 14, p->kind == 2 && p->v > 0.5f ? C_ACC : C_INK);
        }
    }
    if (A.menu_open) {
        ui_box(A.menu_x, A.menu_y, MENU_W, menu_n() * MENU_ROW + 8);
        for (i = 0; i < menu_n(); i++) {
            int ry = A.menu_y + 4 + i * MENU_ROW;
            if (menu_item_at(mx, my) == i) rr(A.menu_x + 3, ry, MENU_W - 6, MENU_ROW, 3, C_ACCBG);
            if (A.menu_kind) ico_wrench(A.menu_x + 12, ry + 6, C_MODC);
            txt(menu_name(i), A.menu_x + (A.menu_kind ? 32 : 14), ry + 4, 14, menu_item_at(mx, my) == i ? C_ACC : C_INK);
        }
    }
}

static void select_object(int i, int on) {
    A.o[i].sel = on;
    if (on) set_cur(i);
    A.dirty = 1;
    status(on ? "object \"%s\" selected" : "object \"%s\" unselected", A.o[i].name);
}

static int any_selected(void) {
    int i;
    for (i = 0; i < A.nobj; i++) if (A.o[i].sel) return 1;
    return 0;
}

static void deselect_all(void) {
    int i, was = any_selected();
    for (i = 0; i < A.nobj; i++) A.o[i].sel = 0;
    A.dirty = 1;
    if (was) status("object deselected");
}

/* The nearest object under pixel (x, y): each ray is carried into the object's own space, the hit point back out. */
static int object_at(float x, float y) {
    mf_vec3 d = mouse_ray(x, y);
    int i, best = -1;
    float best_d = 1e30f;
    for (i = 0; i < A.nobj; i++) {
        const sobj *o = &A.o[i];
        mf_vec3 ol = mf_trs_unpoint(&o->t, A.eye), dl = mf_trs_undir(&o->t, d), hit, w;
        float len = sqrtf(mf_dot(dl, dl)), t, dist;
        if (len < 1e-9f) continue;
        dl = mf_mul(dl, 1.0f / len);
        if (mf_pick_face_any(lmesh(i), ol, dl, &t) < 0) continue;
        hit = mf_add(ol, mf_mul(dl, t));
        w = mf_sub(mf_trs_point(&o->t, hit), A.eye);
        dist = sqrtf(mf_dot(w, w));
        if (dist < best_d) { best_d = dist; best = i; }
    }
    return best;
}

/* Object mode click: the object under the mouse (Shift toggles it), or nothing. */
static void pick_object(int shift) {
    int i = object_at((float)cb_mouse_x(), (float)cb_mouse_y()), j;
    if (i >= 0) {
        if (shift) select_object(i, !A.o[i].sel);
        else { for (j = 0; j < A.nobj; j++) A.o[j].sel = 0; select_object(i, 1); }
    } else if (!shift) deselect_all();
}

/* Object mode: delete the selected objects (X / Delete). */
static void delete_selected(void) {
    int i, k = 0, n = 0, cur_name_keep = -1, map[MAX_OBJ], np[MAX_OBJ];
    if (!any_selected()) { status("select an object first (click it)"); return; }
    close_add_panel();
    undo_push();
    for (i = 0; i < A.nobj; i++) map[i] = A.o[i].sel ? -1 : k++;
    for (i = 0; i < A.nobj; i++) {   /* children of a deleted object go to its parent (they keep their world transform) */
        int pp = A.o[i].parent, guard = 0;
        while (pp >= 0 && pp < A.nobj && A.o[pp].sel && guard++ < MAX_OBJ) pp = A.o[pp].parent;
        np[i] = (pp >= 0 && pp < A.nobj) ? map[pp] : -1;
    }
    k = 0;
    for (i = 0; i < A.nobj; i++) {
        if (A.o[i].sel) { mf_free(A.o[i].mesh); n++; continue; }
        if (i == A.cur) cur_name_keep = k;
        A.o[k] = A.o[i];
        A.o[k].parent = np[i];
        k++;
    }
    A.nobj = k;
    set_cur(cur_name_keep >= 0 ? cur_name_keep : (k > 0 ? k - 1 : -1));
    A.dirty = 1;
    status("deleted %d object(s)", n);
}

static mf_vec3 mouse_ray(float x, float y) {
    return mf_camera_ray(A.eye, A.target, A.fov, cb_screen_width(), cb_screen_height(), x, y);
}

static const char *selmode_name(void) {
    return A.selmode == MF_SEL_VERT ? "vertex" : (A.selmode == MF_SEL_EDGE ? "edge" : "face");
}

static void report_selection(void) {
    if (A.selmode == MF_SEL_FACE) status("%d face(s) selected", mf_sel_face_count(A.mesh));
    else status("%d vertex(es) selected (%s mode)", mf_sel_vert_count(A.mesh), selmode_name());
}

/* A face's centre on screen (the dot X-ray mode shows and picks by). 0 when it is behind the camera. */
static int face_dot(const mf_mesh *m, int f, float *sx, float *sy) {
    const int *vi = mf_face_verts(m, f);
    int n = mf_face_size(m, f), i;
    mf_vec3 c = mf_v3(0, 0, 0);
    if (n < 1) return 0;
    for (i = 0; i < n; i++) c = mf_add(c, m->v[vi[i]]);
    c = mf_mul(c, 1.0f / (float)n);
    return mf_project(A.eye, A.target, A.fov, cb_screen_width(), cb_screen_height(), c, sx, sy);
}

/* Vertices hidden behind faces (when X-ray is off): recomputed when the camera, the mesh or the mode changed. */
static const unsigned char *vis_mask(void) {
    if (A.xray || A.mode != MODE_MESH || !A.vis_ok || !A.mesh || A.vis_cap < A.mesh->nv) return NULL;
    return A.vis;
}
/* A pass tests every vertex that is on screen, 3 ms of CPU a frame at most: a big mesh (30k vertices, ~60 ms of rays) takes a few frames to
   catch up with the camera (the old answer stays in the mask meanwhile) instead of stalling every frame. Small meshes finish at once. */
#define VIS_BUDGET_MS 3.0
#define DENSE_EDGES 6000   /* above this many edges the drawing skips the ones too small or off the window to see */
static void vis_stop(void) {
    A.vis_ok = 0; A.vis_valid = 0; A.vact = 0;
    if (A.vtree) { mf_vis_free(A.vtree); A.vtree = NULL; }
}
static void vis_update(void) {
    mf_mesh *m = A.mesh;
    int w = cb_screen_width(), h = cb_screen_height(), v;
    clock_t t0;
    if (A.xray || A.mode != MODE_MESH || !m || m->nv == 0) { vis_stop(); return; }
    if (A.dirty) { if (A.xf) A.vt_stale = 1; else A.vt_topo = 1; }
    if (A.vact && (A.vt_topo || m->nv > A.vis_cap || m->nv > A.want_cap)) A.vact = 0;   /* the faces changed mid-pass: start over */
    if (!A.vact) {   /* no pass running: start one when something changed */
        int moved = !(A.vis_eye.x == A.eye.x && A.vis_eye.y == A.eye.y && A.vis_eye.z == A.eye.z);
        if (A.vis_valid && !moved && !A.vt_topo && !A.vt_stale && m->nv <= A.vis_cap) return;
        if (m->nv > A.vis_cap) {
            unsigned char *nv = (unsigned char *)realloc(A.vis, (size_t)m->nv);
            if (!nv) { A.vis_ok = 0; return; }
            A.vis = nv; A.vis_cap = m->nv; A.vis_valid = 0; A.vis_ok = 0;
            memset(A.vis, 1, (size_t)m->nv);
        }
        if (A.vtree && !A.vt_topo && A.vt_stale && !mf_vis_refit(A.vtree, m)) A.vt_topo = 1;
        if (A.vtree && A.vt_topo) { mf_vis_free(A.vtree); A.vtree = NULL; }
        if (!A.vtree) {
            A.vtree = mf_vis_new(m);
            if (!A.vtree) { memset(A.vis, 1, (size_t)m->nv); A.vis_ok = 1; A.vis_valid = 1; A.vis_eye = A.eye; A.vt_topo = A.vt_stale = 0; return; }
        }
        A.vt_topo = A.vt_stale = 0;
        if (m->nv > A.want_cap) {
            unsigned char *nw = (unsigned char *)realloc(A.want, (size_t)m->nv);
            if (!nw) { vis_stop(); return; }
            A.want = nw; A.want_cap = m->nv;
        }
        for (v = 0; v < m->nv; v++) {   /* a ray only for vertices that can be seen at all: in front of the camera, near the window */
            float sx, sy;
            A.want[v] = mf_project(A.eye, A.target, A.fov, w, h, m->v[v], &sx, &sy) && sx > -30 && sy > -30 && sx < w + 30 && sy < h + 30;
        }
        A.vis_eye = A.eye;   /* the pass is for this eye; a later move starts the next pass */
        A.vpos = 0; A.vact = 1;
    }
    t0 = clock();
    while (A.vpos < m->nv) {
        int to = A.vpos + 512 < m->nv ? A.vpos + 512 : m->nv;
        if (mf_vis_run(A.vtree, m, A.vis_eye, A.vis, A.want, A.vpos, to) < 0) {   /* the faces changed mid-pass: start over next frame */
            A.vt_topo = 1; A.vact = 0; return;
        }
        A.vpos = to;
        if ((double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC > VIS_BUDGET_MS) break;
    }
    if (A.vpos >= m->nv) { A.vact = 0; A.vis_ok = 1; A.vis_valid = 1; }
}

/* L: select (Shift+L: deselect) everything connected to the vertex (or face) under the mouse. */
static void select_linked(int on) {
    mf_mesh *m = A.mesh;
    int w = cb_screen_width(), h = cb_screen_height(), seed = -1, n, faces = A.selmode == MF_SEL_FACE;
    float px = (float)cb_mouse_x(), py = (float)cb_mouse_y();
    if (!m || m->nv == 0) return;
    if (!faces) seed = mf_pick_vert_screen_vis(m, A.eye, A.target, A.fov, w, h, px, py, 60.0f, vis_mask());
    if (seed < 0) {   /* no vertex near: the face under the mouse */
        int f = mf_pick_face(m, A.eye, mouse_ray(px, py), NULL);
        if (f >= 0) seed = mf_face_verts(m, f)[0];
    }
    if (seed < 0) { status("L: point the mouse at the mesh you want to select"); return; }
    n = mf_select_linked(m, seed, faces, on);
    A.dirty = 1;
    status("linked: %d of %d vertex(es) %s", n, m->nv, on ? "selected" : "deselected");
    if (faces) printf("[meshforge] linked %d face(s) selected\n", mf_sel_face_count(m));
}

static void pick_at_mouse(int add) {
    mf_mesh *m = A.mesh;
    int w = cb_screen_width(), h = cb_screen_height();
    float px = (float)cb_mouse_x(), py = (float)cb_mouse_y();
    if (alt_down() && A.selmode != MF_SEL_FACE) {   /* Alt+click: the edge loop through the edge under the mouse */
        int c = mf_pick_edge_screen_vis(m, A.eye, A.target, A.fov, w, h, px, py, 10.0f, vis_mask());
        if (c >= 0) {
            int n = mf_select_edge_loop(m, c, add);
            status("edge loop: %d edge(s)", n);
        } else if (!add) mf_sel_clear(m);
        A.dirty = 1;
        report_selection();
        return;
    }
    if (A.selmode == MF_SEL_FACE) {
        mf_vec3 d = mouse_ray(px, py);
        int f = -1, ray_f = mf_pick_face(m, A.eye, d, NULL);
        if (A.xray) {   /* X-ray: faces are picked by their dots, through the mesh (the ray would stop at the nearest face) */
            float best = 18.0f * 18.0f;
            int g;
            for (g = 0; g < m->nf; g++) {
                float sx, sy, dd;
                if (m->fhid[g] || !face_dot(m, g, &sx, &sy)) continue;
                dd = (sx - px) * (sx - px) + (sy - py) * (sy - py);
                if (dd < best) { best = dd; f = g; }
            }
        } else f = ray_f;
        printf("[meshforge] picked face %d (the ray alone: %d)%s\n", f, ray_f, A.xray ? " X-ray" : "");
        if (!add) mf_sel_clear(m);
        if (f >= 0) mf_sel_face(m, f, add ? !m->fsel[f] : 1);
    } else if (A.selmode == MF_SEL_VERT) {
        int v = mf_pick_vert_screen_vis(m, A.eye, A.target, A.fov, w, h, px, py, 14.0f, vis_mask());
        if (!add) mf_sel_clear(m);
        if (v >= 0) mf_sel_vert(m, v, add ? !m->vsel[v] : 1);
    } else {
        int c = mf_pick_edge_screen_vis(m, A.eye, A.target, A.fov, w, h, px, py, 10.0f, vis_mask());
        if (!add) mf_sel_clear(m);
        if (c >= 0) {
            int a = m->fidx[c], b = m->fidx[mf_corner_next(m, c)];
            int on = add ? !(m->vsel[a] && m->vsel[b]) : 1;
            mf_sel_vert(m, a, on); mf_sel_vert(m, b, on);
        }
    }
    A.dirty = 1;
    report_selection();
}

static void do_extrude(float dist) {
    int n;
    if (mf_sel_face_count(A.mesh) == 0) { status("select a face first (click one)"); return; }
    undo_push();
    n = mf_extrude_selected(A.mesh, dist);
    if (n < 0) { undo_pop(); status("extrude failed (out of memory)"); return; }
    A.dirty = 1;
    status("extruded %+.2f: %d side face(s) added", (double)dist, n);
}

/* Where the modal move of an extrude goes: along the average normal of the faces being extruded. */
static void xf_start(int kind);

static void strip_suffix(char *name) {   /* "Cube.003" -> "Cube" */
    size_t n = strlen(name);
    if (n > 4 && name[n - 4] == '.' && name[n - 3] >= '0' && name[n - 3] <= '9' && name[n - 2] >= '0' && name[n - 2] <= '9' && name[n - 1] >= '0' && name[n - 1] <= '9')
        name[n - 4] = 0;
}

/* Shift+D (Object mode): copies the selected objects (own meshes), keeps the parent links between copies, selects the
   copies and starts a grab on them. Ctrl+Z removes the copies. */
static void duplicate_selected(void) {
    int map[MAX_OBJ], i, base = A.nobj, n = 0, first = -1, old_cur = A.cur;
    if (A.mode != MODE_OBJECT) { status("Shift+D duplicates objects: press Tab to leave Mesh mode first"); return; }
    for (i = 0; i < base; i++) n += A.o[i].sel;
    if (!n) { status("select an object first (click it), then Shift+D"); return; }
    if (base + n > MAX_OBJ) { status("the scene is full (%d objects)", MAX_OBJ); return; }
    close_add_panel();
    undo_push();
    for (i = 0; i < base; i++) map[i] = -1;
    for (i = 0; i < base; i++) {
        char nm[48];
        int k;
        if (!A.o[i].sel) continue;
        k = A.nobj;
        A.o[k] = A.o[i];
        A.o[k].mesh = mf_clone(A.o[i].mesh);
        if (!A.o[k].mesh) { undo_pop(); status("out of memory: nothing duplicated"); return; }
        snprintf(nm, sizeof nm, "%s", A.o[i].name);
        strip_suffix(nm);
        unique_name(nm, A.o[k].name, sizeof A.o[k].name);   /* (slot k is not counted yet) */
        map[i] = k;
        A.nobj++;
        if (first < 0) first = k;
    }
    for (i = base; i < A.nobj; i++) {
        int p = A.o[i].parent;
        if (p >= 0 && p < base && map[p] >= 0) A.o[i].parent = map[p];
    }
    for (i = 0; i < base; i++) A.o[i].sel = 0;
    for (i = base; i < A.nobj; i++) A.o[i].sel = 1;
    set_cur(old_cur >= 0 && old_cur < base && map[old_cur] >= 0 ? map[old_cur] : first);
    A.dirty = 1;
    printf("[meshforge] duplicated %d object(s)\n", n);
    xf_start('G');
}

/* E in Mesh mode, Blender style: duplicate and join (nothing moves yet), then a grab that follows the mouse along
   the normal; click / Enter confirms, Esc / right click puts everything back. Vertex and edge selections: the faces
   they cover are extruded, else the selected edges are (free grab). */
static void extrude_start(void) {
    mf_mesh *m = A.mesh;
    int old_nv, f, n, v, faces = 0;
    mf_vec3 ns = mf_v3(0, 0, 0);
    float len;
    close_add_panel();
    if (A.selmode == MF_SEL_FACE ? mf_sel_face_count(m) == 0 : mf_sel_vert_count(m) == 0) {
        status(A.selmode == MF_SEL_FACE ? "select a face first (click one)" : "select vertices or edges first (click)");
        return;
    }
    undo_push();
    old_nv = m->nv;
    if (A.selmode != MF_SEL_FACE) mf_sel_faces_from_verts(m);
    for (f = 0; f < m->nf; f++) if (m->fsel[f]) { ns = mf_add(ns, mf_face_normal(m, f)); faces++; }
    if (faces) n = mf_extrude_selected(m, 0.0f);
    else n = mf_extrude_edges(m);
    if (n < 0 || m->nv == old_nv) {   /* (extruding a closed mesh's every face adds no side faces but does add vertices) */
        undo_drop();   /* nothing happened: no snapshot */
        if (n < 0) status("extrude failed (out of memory)");
        else status("nothing to extrude: select two joined vertices (an edge) or all the corners of a face");
        return;
    }
    if (A.selmode != MF_SEL_FACE) {   /* the copies are the selection now */
        memset(m->vsel, 0, (size_t)m->nv);
        memset(m->fsel, 0, (size_t)m->nf);
        for (v = old_nv; v < m->nv; v++) m->vsel[v] = 1;
    }
    A.dirty = 1;
    A.presnap = 1;
    xf_start('G');
    if (!A.xf) return;
    A.extr = 1;
    len = sqrtf(mf_dot(ns, ns));
    if (faces && len > 1e-6f) { A.nrm = mf_mul(ns, 1.0f / len); A.axis = 4; }
    status("extrude: move the mouse%s, type a distance, X Y Z: axis, Enter / click ok, Esc / right click cancel", A.axis == 4 ? " (along the normal)" : "");
}

/* Saves every object, placed by its transform, as one OBJ. */
/* path NULL: the save file (and the quit no longer asks); else a backup there (autosave) */
static void do_save_to(const char *path) {
    char err[160];
    const char *to = path ? path : A.save_path;
    mf_mesh *all = mf_new();
    int i, v, f, ok = all != NULL;
    for (i = 0; ok && i < A.nobj; i++) {
        const mf_mesh *m = omesh(i);
        int base = all->nv;
        for (v = 0; ok && v < m->nv; v++) { mf_vec3 p = wpt(i, m->v[v]); ok = mf_add_vert(all, p.x, p.y, p.z) >= 0; }
        for (f = 0; ok && f < m->nf; f++) {
            int n = mf_face_size(m, f), idx[256], k;
            const int *vi = mf_face_verts(m, f);
            if (n > 256) continue;
            for (k = 0; k < n; k++) idx[k] = vi[k] + base;
            ok = mf_add_face(all, idx, n) >= 0;
        }
    }
    if (!ok) { mf_free(all); status("save failed: out of memory"); return; }
    if (mf_obj_write(all, to, err, sizeof err) == 0) {
        if (path) status("autosaved to %s", to);
        else { status("saved %s (%d object(s))", to, A.nobj); A.changed = 0; A.qconf_t = 0; }
    } else status("save failed: %s", err);
    mf_free(all);
}
static void do_save(void) { do_save_to(NULL); }

/* every minute (--autosave SECONDS), when something changed since the last time: a copy next to the save file, so a crash costs little */
static void autosave_tick(void) {
    const char *o = cb_option("autosave");
    double every = o[0] ? cb_to_float(o) : 60.0, now = cb_game_time();
    if (every < 1.0) every = 1.0;
    if (now - A.auto_t < every) return;
    A.auto_t = now;
    if (!A.auto_changed || A.xf || A.renaming) return;
    {   char bak[600];
        snprintf(bak, sizeof bak, "%s.bak", A.save_path);
        A.auto_changed = 0;
        do_save_to(bak);
    }
}

/* Ctrl+Q: with unsaved edits the first press only asks; a second one within five seconds quits */
static void quit_request(void) {
    double now = cb_game_time();
    if (!A.changed || (A.qconf_t > 0 && now - A.qconf_t < 5.0)) { cb_close_window(); return; }
    A.qconf_t = now;
    status("unsaved changes: Ctrl+S saves, Ctrl+Q again (within 5 s) quits without saving");
}

/* ---- modal transforms: G R S (mouse, X Y Z, typed number; Enter / left click ok, Esc / right click cancel) ---- */

static int ctrl_down(void)  { return cb_key_down(CB_KEY_LEFT_CONTROL) || cb_key_down(KEY_RIGHT_CONTROL); }
static int alt_down(void)   { return cb_key_down(KEY_LEFT_ALT) || cb_key_down(KEY_RIGHT_ALT); }
static int shift_down(void) { return cb_key_down(CB_KEY_LEFT_SHIFT) || cb_key_down(KEY_RIGHT_SHIFT); }

static float snapf(float v, float step) { return floorf(v / step + 0.5f) * step; }
static const char *xf_name(void) { return A.extr ? "extrude" : A.xf == 'I' ? "inset" : A.xf == 'B' ? "bevel" : A.xf == 'G' ? "grab" : (A.xf == 'R' ? "rotate" : "scale"); }
static mf_vec3 axis_vec(int axis) { return axis == 4 ? A.nrm : axis == 1 ? mf_v3(1, 0, 0) : (axis == 2 ? mf_v3(0, 1, 0) : mf_v3(0, 0, 1)); }

static mf_vec3 axis_vec_world(int axis) { return axis == 1 ? mf_v3(1, 0, 0) : (axis == 2 ? mf_v3(0, 1, 0) : mf_v3(0, 0, 1)); }

static mf_vec3 view_forward(void) {
    mf_vec3 f = mf_sub(A.target, A.eye);
    float len = sqrtf(mf_dot(f, f));
    return len > 1e-9f ? mf_mul(f, 1.0f / len) : mf_v3(0, 0, -1);
}

static void xf_free(void) {
    free(A.xorig); free(A.xmark); free(A.xtrs);
    mf_free(A.morig); A.morig = NULL; A.mop_n = 0;
    A.xorig = NULL; A.xmark = NULL; A.xtrs = NULL;
    A.xf = 0; A.axis = 0; A.num[0] = 0; A.extr = 0; A.presnap = 0; A.gdrag = 0; A.xobj = 0;
}

static void xf_start(int kind) {
    mf_mesh *m = A.mesh;
    int v, count = 0;
    float sx, sy;
    if (A.mode == MODE_OBJECT) {   /* the selected objects' own transforms */
        mf_vec3 sum = mf_v3(0, 0, 0);
        if (!any_selected()) { status("select the object first: click it, drag a box, or press A"); return; }
        A.xtrs = (mf_trs *)malloc((size_t)A.nobj * sizeof(mf_trs));
        if (!A.xtrs) { xf_free(); status("out of memory"); return; }
        for (v = 0; v < A.nobj; v++) {
            A.xtrs[v] = A.o[v].t;
            if (A.o[v].sel) { sum = mf_add(sum, A.o[v].t.pos); count++; }
        }
        A.pivot = mf_mul(sum, 1.0f / (float)count);
        A.xobj = 1;
    } else {
        if (!m || m->nv == 0) return;
        A.xorig = (mf_vec3 *)malloc((size_t)m->nv * sizeof(mf_vec3));
        A.xmark = (unsigned char *)malloc((size_t)m->nv);
        if (!A.xorig || !A.xmark) { xf_free(); status("out of memory"); return; }
        count = mf_sel_mask(m, A.xmark);
        if (count == 0) { xf_free(); status("select something first (Mesh mode, click)"); return; }
        for (v = 0; v < m->nv; v++) A.xorig[v] = m->v[v];
        A.pivot = mf_sel_center(m, A.xmark);
    }
    if (!mf_project(A.eye, A.target, A.fov, cb_screen_width(), cb_screen_height(), A.pivot, &sx, &sy)) {
        sx = cb_screen_width() * 0.5f; sy = cb_screen_height() * 0.5f;
    }
    A.psx = sx; A.psy = sy;
    A.m0x = (float)cb_mouse_x(); A.m0y = (float)cb_mouse_y();
    A.r0o = A.eye; A.r0d = mouse_ray(A.m0x, A.m0y);
    A.xf = kind; A.axis = 0; A.num[0] = 0;
    if (!A.presnap) undo_push();   /* cancelling drops it again */
    status("%s %d %s: move the mouse, X Y Z axis, type a number, Enter / click ok, Esc / right click cancel", xf_name(), count, A.xobj ? "object(s)" : "vertex(es)");
}

/* The three modal operations on whatever the transform is working on: objects' transforms or vertices. */
static void do_move(mf_vec3 a) {
    int i;
    if (!A.xobj) { mf_xform(A.mesh, A.xorig, A.xmark, MF_XF_MOVE, A.pivot, a, 0.0f); return; }
    for (i = 0; i < A.nobj; i++) if (affected(i)) { A.o[i].t = A.xtrs[i]; A.o[i].t.pos = mf_add(A.xtrs[i].pos, a); }
}
static void do_rotate(mf_vec3 u, float ang) {
    int i;
    if (!A.xobj) { mf_xform(A.mesh, A.xorig, A.xmark, MF_XF_ROTATE, A.pivot, u, ang); return; }
    for (i = 0; i < A.nobj; i++) if (affected(i)) { A.o[i].t = A.xtrs[i]; mf_trs_rotate_world(&A.o[i].t, A.pivot, u, ang); }
}
static void do_scale(mf_vec3 a) {
    int i;
    if (!A.xobj) { mf_xform(A.mesh, A.xorig, A.xmark, MF_XF_SCALE, A.pivot, a, 0.0f); return; }
    for (i = 0; i < A.nobj; i++) if (affected(i)) { A.o[i].t = A.xtrs[i]; mf_trs_scale_about(&A.o[i].t, A.pivot, a); }
}

/* Inset (I) and bevel (Ctrl+B): the amount follows the mouse sideways from where it started (or a typed number); the mesh is put
   back to the saved copy and the operator redone every frame, like the transforms. */
static void xf_end(int ok);
static void mop_apply(void) {
    float mx = (float)cb_mouse_x(), amt, per_px;
    int typed = A.num[0] != 0 && strcmp(A.num, "-") != 0, n;
    mf_mesh *c = A.morig ? mf_clone(A.morig) : NULL;
    const char *nm = A.xf == 'I' ? (A.mop_reg ? "inset region" : "inset") : "bevel";
    if (!c) { A.mop_n = 0; status("%s: out of memory", nm); return; }
    {   mf_mesh t = *A.mesh; *A.mesh = *c; *c = t; mf_free(c); }   /* the mesh object stays; its contents come back */
    per_px = 2.0f * A.dist * tanf(A.fov * 0.5f * PI_F / 180.0f) / (float)cb_screen_height();
    amt = typed ? (float)atof(A.num) : (mx - A.m0x) * per_px;
    if (amt < 0.0f) amt = 0.0f;
    if (ctrl_down() && !typed) amt = snapf(amt, SNAPS[A.snap_i]);
    n = amt > 1e-6f ? (A.xf == 'I' ? (A.mop_reg ? mf_inset_region(A.mesh, amt) : mf_inset_faces(A.mesh, amt)) : mf_bevel_edges_ex(A.mesh, amt, A.bev_seg, A.bev_prof)) : 0;
    A.mop_n = n;
    A.dirty = 1;
    if (n == -2 && A.xf == 'I') status("inset region: the faces have no rim (the whole closed mesh): nothing to inset around");
    else if (n == -2) status("bevel: this mesh is non-manifold (an edge with three faces, or faces wound against each other): fix that first");
    else if (n < 0) status("%s: out of memory", nm);
    else if (A.xf == 'B') snprintf(A.status, sizeof A.status, "bevel %.3f   %d segment(s), profile %.1f   (wheel: segments, P / Shift+P: profile)%s%s", (double)amt, A.bev_seg, (double)A.bev_prof, A.num[0] ? "   [typed " : "", A.num[0] ? A.num : "");
    else snprintf(A.status, sizeof A.status, "%s %.3f%s%s", nm, (double)amt, A.num[0] ? "   [typed " : "", A.num[0] ? A.num : "");
    if (n >= 0 && A.num[0]) { size_t k = strlen(A.status); snprintf(A.status + k, sizeof A.status - k, "]"); }
}

static void mop_start(int kind) {
    mf_mesh *m = A.mesh;
    close_add_panel();
    if (!m) return;
    if (kind == 'I') {
        if (A.selmode != MF_SEL_FACE) mf_sel_faces_from_verts(m);
        if (mf_sel_face_count(m) == 0) { status("inset: select a face (or all the corners of one) first"); return; }
    } else if (mf_sel_vert_count(m) < 2) { status("bevel: select the two ends of an edge first (vertex or edge mode)"); return; }
    xf_start(kind);
    if (!A.xf) return;
    A.mop_reg = 0;
    if (A.bev_seg < 1) A.bev_seg = 1;
    if (A.bev_prof < 0.1f) A.bev_prof = 0.5f;
    A.morig = mf_clone(m);
    if (!A.morig) { xf_end(0); status("out of memory"); return; }
    if (kind == 'I') status("inset: move the mouse sideways (or type an amount), Enter / click ok, Esc / right click cancel");
    else status("bevel: move the mouse sideways (or type a width), mouse wheel: segments, P / Shift+P: profile, Enter / click ok, Esc / right click cancel");
}

static void xf_apply(void) {
    if (A.xf == 'I' || A.xf == 'B') { mop_apply(); return; }
    mf_vec3 fwd = view_forward(), a = mf_v3(0, 0, 0);
    float mx = (float)cb_mouse_x(), my = (float)cb_mouse_y(), value = 0.0f, ang = 0.0f;
    int typed = A.num[0] != 0 && strcmp(A.num, "-") != 0;
    int snap = ctrl_down() && !typed;
    float step = SNAPS[A.snap_i];
    const char *ax = A.axis == 1 ? " X" : (A.axis == 2 ? " Y" : (A.axis == 3 ? " Z" : (A.axis == 4 ? " normal" : "")));
    if (typed) value = (float)atof(A.num);
    if (A.xf == 'G') {
        if (typed) {
            a = mf_mul(axis_vec(A.axis ? A.axis : 1), value);
        } else if (A.axis) {
            mf_vec3 u = axis_vec(A.axis), d1 = mouse_ray(mx, my);
            float t0, t1;
            if (mf_ray_axis_param(A.r0o, A.r0d, A.pivot, u, &t0) && mf_ray_axis_param(A.eye, d1, A.pivot, u, &t1))
                a = mf_mul(u, t1 - t0);
        } else {
            mf_vec3 h0, h1, d1 = mouse_ray(mx, my);
            if (mf_ray_plane(A.r0o, A.r0d, A.pivot, fwd, &h0) && mf_ray_plane(A.eye, d1, A.pivot, fwd, &h1))
                a = mf_sub(h1, h0);
        }
        if (snap) {   /* Ctrl: move in steps of the snap size (along the axis, or per world axis when free) */
            if (A.axis) { mf_vec3 u = axis_vec(A.axis); a = mf_mul(u, snapf(mf_dot(a, u), step)); }
            else a = mf_v3(snapf(a.x, step), snapf(a.y, step), snapf(a.z, step));
        }
        do_move(a);
        snprintf(A.status, sizeof A.status, "%s%s: %+.3f %+.3f %+.3f", xf_name(), ax, (double)a.x, (double)a.y, (double)a.z);
    } else if (A.xf == 'R') {
        mf_vec3 u = A.axis ? axis_vec(A.axis) : mf_mul(fwd, -1.0f);
        if (typed) ang = value * PI_F / 180.0f;
        else ang = atan2f(-(my - A.psy), mx - A.psx) - atan2f(-(A.m0y - A.psy), A.m0x - A.psx);
        if (!typed && A.axis && mf_dot(u, fwd) > 0.0f) ang = -ang;   /* the axis points away from us: the screen turn reverses */
        if (snap) ang = snapf(ang, 15.0f * PI_F / 180.0f);
        do_rotate(u, ang);
        snprintf(A.status, sizeof A.status, "rotate%s: %+.1f degrees", ax, (double)(ang * 180.0f / PI_F));
    } else {
        float f = value;
        if (!typed) {
            float d0 = hypotf(A.m0x - A.psx, A.m0y - A.psy), d1 = hypotf(mx - A.psx, my - A.psy);
            if (d0 < 8.0f) d0 = 8.0f;
            f = d1 / d0;
        }
        if (snap) f = snapf(f, 0.1f);
        a = mf_v3(1, 1, 1);
        if (!A.axis) a = mf_v3(f, f, f);
        else if (A.axis == 1) a.x = f;
        else if (A.axis == 2) a.y = f;
        else a.z = f;
        do_scale(a);
        snprintf(A.status, sizeof A.status, "scale%s: x%.3f", ax, (double)f);
    }
    if (snap) { size_t n = strlen(A.status); snprintf(A.status + n, sizeof A.status - n, "   [snap]"); }
    if (A.num[0]) { size_t n = strlen(A.status); snprintf(A.status + n, sizeof A.status - n, "   [typed %s]", A.num); }
    A.dirty = 1;
}

static void xf_end(int ok) {
    const char *name = xf_name();
    int region = ok && A.xf == 'I' && A.mop_reg;
    if (ok && A.xobj) {   /* (the input test reads these) */
        int i;
        for (i = 0; i < A.nobj; i++)
            if (affected(i))
                printf("[meshforge] object \"%s\" at %.3f %.3f %.3f rot %.1f %.1f %.1f scale %.3f %.3f %.3f\n", A.o[i].name,
                       (double)A.o[i].t.pos.x, (double)A.o[i].t.pos.y, (double)A.o[i].t.pos.z,
                       (double)(A.o[i].t.rot.x * 180.0f / PI_F), (double)(A.o[i].t.rot.y * 180.0f / PI_F), (double)(A.o[i].t.rot.z * 180.0f / PI_F),
                       (double)A.o[i].t.scl.x, (double)A.o[i].t.scl.y, (double)A.o[i].t.scl.z);
    }
    if (ok && A.morig && A.mop_n <= 0) {   /* the operator gave nothing (amount 0, or refused): drop it like a cancel */
        xf_free();
        undo_pop();
        status("%s: nothing changed", name);
        return;
    }
    if (!ok && (A.extr || A.morig)) {   /* the operation changed the topology: go back to the snapshot */
        xf_free();
        undo_pop();
        status("%s cancelled", name);
        return;
    }
    if (!ok) {   /* put every vertex back and drop the snapshot taken at the start */
        do_move(mf_v3(0, 0, 0));   /* a move by nothing puts everything back (objects: their saved transforms) */
        undo_drop();
        A.dirty = 1;
    }
    if (ok && A.xf == 'B') printf("[meshforge] bevel: %d segment(s), profile %.1f\n", A.bev_seg, (double)A.bev_prof);   /* (the ops test reads this) */
    xf_free();
    status("%s%s %s", name, region ? " (region)" : "", ok ? "confirmed" : "cancelled");
}

/* one frame of a running transform */
static void xf_update(void) {
    if (cb_key_pressed(CB_KEY_ESCAPE)) { xf_end(0); return; }
    if (cb_mouse_pressed(CB_MOUSE_RIGHT)) { A.rmb_ok = 0; xf_end(0); return; }
    /* a gizmo drag ends when the button is released, anything else (G R S, E) on the next click */
    if (cb_key_pressed(CB_KEY_ENTER) || (A.gdrag ? !cb_mouse_down(CB_MOUSE_LEFT) : cb_mouse_pressed(CB_MOUSE_LEFT))) { xf_apply(); xf_end(1); return; }
    if (cb_key_pressed(CB_KEY_X)) A.axis = A.axis == 1 ? 0 : 1;
    if (A.xf == 'B') {   /* wheel: segments; P / Shift+P: profile (0.5 is a circular arc) */
        int wh = cb_mouse_wheel();
        if (wh != 0) { A.bev_seg += wh > 0 ? 1 : -1; if (A.bev_seg < 1) A.bev_seg = 1; if (A.bev_seg > 16) A.bev_seg = 16; }
        if (cb_key_pressed(CB_KEY_P)) {
            A.bev_prof += shift_down() ? -0.1f : 0.1f;
            if (A.bev_prof < 0.1f) A.bev_prof = 0.1f;
            if (A.bev_prof > 1.0f) A.bev_prof = 1.0f;
        }
    }
    if (A.xf == 'I' && cb_key_pressed(CB_KEY_I)) { A.mop_reg = !A.mop_reg; status("inset: %s", A.mop_reg ? "the faces as one region" : "each face on its own"); }
    if (cb_key_pressed(CB_KEY_Y)) A.axis = A.axis == 2 ? 0 : 2;
    if (cb_key_pressed(CB_KEY_Z)) A.axis = A.axis == 3 ? 0 : 3;
    {   /* typed number: digits, a point, a minus (toggles the sign), backspace */
        size_t n = strlen(A.num);
        int i;
        for (i = 0; i < A.ntyped; i++) {
            char c = A.typed[i];
            n = strlen(A.num);
            if (c >= '0' && c <= '9' && n + 1 < sizeof A.num) { A.num[n++] = c; A.num[n] = 0; }
            else if (c == '.' && n + 1 < sizeof A.num && !strchr(A.num, '.')) { A.num[n++] = '.'; A.num[n] = 0; }
            else if (c == '-') {
                if (A.num[0] == '-') memmove(A.num, A.num + 1, n);
                else if (n + 1 < sizeof A.num) { memmove(A.num + 1, A.num, n + 1); A.num[0] = '-'; }
            }
        }
        n = strlen(A.num);
        if (cb_key_pressed(CB_KEY_BACKSPACE) && n > 0) A.num[n - 1] = 0;
    }
    xf_apply();
}

/* ---- the transform gizmo: drawn over the view in 2D (always visible), in world axes. Move: arrows and a centre
   square (free move); Rotate: three rings; Scale: lines with square ends and a centre square (uniform). Dragging a handle
   runs the same modal transform as G R S, constrained to that axis, and ends when the button is released. ---- */
#define GZ_RING 48
typedef struct { int axis, np; float x[GZ_RING + 1], y[GZ_RING + 1]; } gz_h;
static struct { int ok; gz_h h[3]; float cx, cy; mf_vec3 pivot; } GZ;

static int gizmo_pivot(mf_vec3 *p) {
    int i, v, n = 0;
    if (A.mode == MODE_OBJECT) {
        mf_vec3 sum = mf_v3(0, 0, 0);
        for (i = 0; i < A.nobj; i++) if (A.o[i].sel) { sum = mf_add(sum, A.o[i].t.pos); n++; }
        if (!n) return 0;
        *p = mf_mul(sum, 1.0f / (float)n);
        return 1;
    }
    if (!A.mesh || A.mask_cap < A.mesh->nv) return 0;
    for (v = 0; v < A.mesh->nv; v++) n += A.mask[v] != 0;
    if (!n) return 0;
    *p = mf_sel_center(A.mesh, A.mask);
    return 1;
}

/* Lays the handles out on the screen for this frame (the drawing and the hit test share it). */
static void gizmo_build(void) {
    int a, k, w = cb_screen_width(), h = cb_screen_height();
    float gs, d;
    mf_vec3 e;
    GZ.ok = 0;
    if (!A.ready || A.tool == TOOL_SELECT || !gizmo_pivot(&GZ.pivot)) return;
    if (!mf_project(A.eye, A.target, A.fov, w, h, GZ.pivot, &GZ.cx, &GZ.cy)) return;
    e = mf_sub(A.eye, GZ.pivot);
    d = sqrtf(mf_dot(e, e));
    gs = d * 0.16f;
    for (a = 1; a <= 3; a++) {
        gz_h *g = &GZ.h[a - 1];
        mf_vec3 u = axis_vec_world(a), e1 = axis_vec_world(a % 3 + 1), e2 = axis_vec_world((a + 1) % 3 + 1);
        g->axis = a; g->np = 0;
        if (A.tool == TOOL_ROTATE) {
            for (k = 0; k <= GZ_RING; k++) {
                float t = 2.0f * PI_F * (float)k / (float)GZ_RING;
                mf_vec3 p = mf_add(GZ.pivot, mf_mul(mf_add(mf_mul(e1, cosf(t)), mf_mul(e2, sinf(t))), gs * 0.85f));
                if (!mf_project(A.eye, A.target, A.fov, w, h, p, &g->x[k], &g->y[k])) { g->np = 0; break; }
                g->np = k + 1;
            }
        } else {
            g->x[0] = GZ.cx; g->y[0] = GZ.cy;
            if (mf_project(A.eye, A.target, A.fov, w, h, mf_add(GZ.pivot, mf_mul(u, gs)), &g->x[1], &g->y[1])) g->np = 2;
        }
    }
    GZ.ok = 1;
}

static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float abx = bx - ax, aby = by - ay, l2 = abx * abx + aby * aby, u = l2 > 1e-9f ? ((px - ax) * abx + (py - ay) * aby) / l2 : 0.0f, dx, dy;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    dx = ax + abx * u - px; dy = ay + aby * u - py;
    return sqrtf(dx * dx + dy * dy);
}

/* The handle under the mouse: 1 2 3 an axis, 0 the centre, -1 none. */
static int gizmo_hit(int mx, int my) {
    int a, k, best = -1;
    float best_d = 9.0f;
    if (!GZ.ok) return -1;
    if (A.tool != TOOL_ROTATE && hypotf((float)mx - GZ.cx, (float)my - GZ.cy) < 9.0f) return 0;
    for (a = 0; a < 3; a++) {
        const gz_h *g = &GZ.h[a];
        for (k = 0; k + 1 < g->np; k++) {
            float d = seg_dist((float)mx, (float)my, g->x[k], g->y[k], g->x[k + 1], g->y[k + 1]);
            if (d < best_d) { best_d = d; best = g->axis; }
        }
    }
    return best;
}

/* A press on a handle starts the transform for the tool, on that axis. 1 when it did. */
static int gizmo_press(int mx, int my) {
    int hit;
    gizmo_build();
    hit = gizmo_hit(mx, my);
    if (hit < 0) return 0;
    xf_start(A.tool == TOOL_MOVE ? 'G' : (A.tool == TOOL_ROTATE ? 'R' : 'S'));
    if (!A.xf) return 1;
    A.axis = hit; A.gdrag = 1;
    return 1;
}

static void thick_line(int x1, int y1, int x2, int y2, cb_rgba c) {
    cb_draw_line(x1, y1, x2, y2, c);
    cb_draw_line(x1 + 1, y1, x2 + 1, y2, c);
    cb_draw_line(x1, y1 + 1, x2, y2 + 1, c);
}

static void draw_gizmo(void) {
    int a, k, hot;
    cb_rgba col[3] = { {230, 70, 70, 255}, {90, 210, 90, 255}, {80, 130, 240, 255} }, yel = {255, 230, 90, 255};
    gizmo_build();
    if (!GZ.ok || (A.xf && !A.gdrag)) return;
    if (A.gz_log) {
        const gz_h *g = GZ.h;
        int i = A.tool == TOOL_ROTATE ? GZ_RING / 8 : 1;   /* a ring point away from where the rings cross */
        printf("[meshforge] gizmo %s c=%d,%d X=%d,%d Y=%d,%d Z=%d,%d\n", TOOL_NAME[A.tool], (int)GZ.cx, (int)GZ.cy,
               (int)g[0].x[i], (int)g[0].y[i], (int)g[1].x[i], (int)g[1].y[i], (int)g[2].x[i], (int)g[2].y[i]);
        A.gz_log = 0;
    }
    hot = A.gdrag ? A.axis : gizmo_hit(cb_mouse_x(), cb_mouse_y());
    for (a = 0; a < 3; a++) {
        const gz_h *g = &GZ.h[a];
        cb_rgba c = hot == g->axis ? yel : col[a];
        for (k = 0; k + 1 < g->np; k++) thick_line((int)g->x[k], (int)g->y[k], (int)g->x[k + 1], (int)g->y[k + 1], c);
        if (A.tool != TOOL_ROTATE && g->np == 2) {
            float dx = g->x[1] - g->x[0], dy = g->y[1] - g->y[0], len = sqrtf(dx * dx + dy * dy);
            int tx = (int)g->x[1], ty = (int)g->y[1];
            if (len < 1.0f) continue;
            dx /= len; dy /= len;
            if (A.tool == TOOL_MOVE) {   /* an arrow head */
                int bx = tx - (int)(dx * 14.0f), by = ty - (int)(dy * 14.0f), px = (int)(-dy * 6.0f), py = (int)(dx * 6.0f);
                thick_line(tx, ty, bx + px, by + py, c);
                thick_line(tx, ty, bx - px, by - py, c);
                thick_line(bx + px, by + py, bx - px, by - py, c);
            } else {                     /* a square end */
                cb_draw_rect(tx - 5, ty - 5, 11, 11, c);
            }
        }
    }
    if (A.tool != TOOL_ROTATE) cb_draw_rect((int)GZ.cx - 5, (int)GZ.cy - 5, 11, 11, hot == 0 ? yel : (cb_rgba){235, 238, 245, 255});
}

/* ---- the demo script (a deterministic run for tests: --demo [--shot FILE]) ---- */

static void demo_step(void) {
    int f, best = -1;
    float best_y = 0.0f;
    mf_stats s;
    switch (A.frame) {
    case 3:   /* select the face that points most upward */
        if (A.cur >= 0 && A.mode != MODE_MESH && edit_enter()) A.mode = MODE_MESH;
        A.selmode = MF_SEL_FACE;
        for (f = 0; f < A.mesh->nf; f++) {
            float y = mf_face_normal(A.mesh, f).y;
            if (best < 0 || y > best_y) { best = f; best_y = y; }
        }
        mf_sel_clear(A.mesh);
        mf_sel_face(A.mesh, best, 1);
        A.dirty = 1;
        printf("[demo] selected face %d\n", best);
        break;
    case 6:
        do_extrude(0.5f);
        break;
    case 9:
        undo_push();
        mf_translate_selection(A.mesh, mf_v3(0.4f, 0.0f, 0.0f));
        A.dirty = 1;
        s = mf_stats_get(A.mesh);
        printf("[demo] V%d E%d F%d euler %d boundary %d nonmanifold %d\n", s.verts, s.edges, s.faces, s.euler, s.boundary, s.nonmanifold);
        A.yaw = -0.8f; A.pitch = 0.55f;   /* from the left: the extruded slope faces this way */
        frame_mesh(0);
        break;
    case 12:
        do_save();
        break;
    case 15:   /* asked for here, taken at the END of the frame (mf_app_draw_hud), when everything is drawn */
        A.shot = cb_option("shot");
        break;
    case 18:
        cb_close_window();
        break;
    default:
        break;
    }
}

/* ---- the functions the bricks call ---- */

int mf_app_init(void) {
    const char *obj = cb_option("obj");
    const char *save = cb_option("save");
    char err[200];
    mf_stats s;
    mf_mesh *m;
    setvbuf(stdout, NULL, _IOLBF, 0);   /* the log is read while the program runs (the input test): one line at a time */
    m = mf_new();
    if (!m) { fprintf(stderr, "[meshforge] out of memory\n"); return 1; }
    A.save_path = save[0] ? save : "out.obj";
    if (obj[0]) {
        if (mf_obj_read(m, obj, err, sizeof err) != 0) { fprintf(stderr, "[meshforge] %s: %s\n", obj, err); return 1; }
        if (m->nf == 0) { fprintf(stderr, "[meshforge] %s has no faces\n", obj); return 1; }
    } else if (mf_make_cube(m, 2.0f) != 0) {
        fprintf(stderr, "[meshforge] out of memory\n");
        return 1;
    }
    A.nobj = 1; A.cur = 0;
    A.o[0].mesh = m; A.o[0].t = mf_trs_identity(); A.o[0].parent = -1;
    A.snap_i = 2;
    {   /* the object's name: the file name without folders and extension */
        const char *b = obj[0] ? strrchr(obj, '/') : NULL;
        char *dot;
        snprintf(A.o[0].name, sizeof A.o[0].name, "%s", obj[0] ? (b ? b + 1 : obj) : "Cube");
        dot = strrchr(A.o[0].name, '.');
        if (dot && dot != A.o[0].name) *dot = 0;
    }
    A.mesh = m;
    A.fov = 50.0f; A.yaw = 0.6f; A.pitch = 0.45f;
    A.mode = MODE_OBJECT; A.selmode = MF_SEL_VERT; A.rmb_ok = 1; A.drag = -1;
    frame_mesh(0);
    A.dirty = 1;
    A.ready = 1;
    A.mx = cb_mouse_x(); A.my = cb_mouse_y();
    cb_register_app("c-vert");   /* the C-Bricks editor finds this program through the app registry (its Shader Preview button) */
    if (cb_option("shader")[0]) mf_shader_open(cb_option("shader"));
    s = mf_stats_get(A.mesh);
    snprintf(A.status, sizeof A.status, "%s", obj[0] ? obj : "new cube");
    printf("[meshforge] ready: %s  V%d E%d F%d euler %d\n", A.status, s.verts, s.edges, s.faces, s.euler);
    return 0;
}

/* right button held: look around with the mouse, fly with W A S D (E up, Q down). The eye stays put while looking. */
static void fly(double dt) {
    mf_vec3 dir, right, move = mf_v3(0, 0, 0);
    float cp, speed = shift_down() ? 12.0f : 4.0f;
    Vector2 mo = cb_mouse_motion();   /* the mouse is captured: its position stays in the middle, only the motion counts */
    A.yaw -= mo.x * 0.006f;
    A.pitch += mo.y * 0.006f;
    if (A.pitch > 1.5f) A.pitch = 1.5f;
    if (A.pitch < -1.5f) A.pitch = -1.5f;
    cp = cosf(A.pitch);
    dir = mf_v3(cp * sinf(A.yaw), sinf(A.pitch), cp * cosf(A.yaw));   /* from the target to the eye */
    A.target = mf_sub(A.eye, mf_mul(dir, A.dist));                    /* the eye keeps its place */
    right = mf_v3(cosf(A.yaw), 0, -sinf(A.yaw));
    if (cb_key_down(CB_KEY_W)) move = mf_sub(move, dir);
    if (cb_key_down(CB_KEY_S)) move = mf_add(move, dir);
    if (cb_key_down(CB_KEY_D)) move = mf_add(move, right);
    if (cb_key_down(CB_KEY_A)) move = mf_sub(move, right);
    if (cb_key_down(CB_KEY_E)) move = mf_add(move, mf_v3(0, 1, 0));
    if (cb_key_down(CB_KEY_Q)) move = mf_sub(move, mf_v3(0, 1, 0));
    A.target = mf_add(A.target, mf_mul(move, speed * (float)dt));
}

static void refresh_mask(void) {
    if (!A.mesh) return;
    if (A.mesh->nv > A.mask_cap) {
        unsigned char *nm = (unsigned char *)realloc(A.mask, (size_t)A.mesh->nv);
        if (!nm) { A.mask_cap = 0; return; }
        A.mask = nm; A.mask_cap = A.mesh->nv;
    }
    if (A.mask_cap >= A.mesh->nv) mf_sel_mask(A.mesh, A.mask);
}

static void set_selmode(mf_selmode to) {
    if (to == A.selmode) return;
    mf_sel_convert(A.mesh, to);
    A.selmode = to;
    A.dirty = 1;
    status("%s select mode", selmode_name());
}

static int in_ui(int x, int y) { return in_panel(x, y) || in_toolbar(x, y) || in_tabs(x, y); }

/* Box select: Object mode picks the objects with any vertex inside, Mesh mode the vertices / faces (X-ray). */
static void box_select(int shift, int x0, int y0, int x1, int y1) {
    int w = cb_screen_width(), h = cb_screen_height(), i, n = 0;
    if (A.mode == MODE_OBJECT) {
        int first = -1;
        for (i = 0; i < A.nobj; i++) {
            int in = mf_mesh_in_box(lmesh(i), &A.o[i].t, A.eye, A.target, A.fov, w, h, (float)x0, (float)y0, (float)x1, (float)y1);
            if (!shift) A.o[i].sel = 0;
            if (in) { A.o[i].sel = 1; n++; if (first < 0) first = i; }
        }
        if (first >= 0 && !(A.cur >= 0 && A.o[A.cur].sel)) set_cur(first);
        A.dirty = 1;
        status("box: %d object(s) selected", n);
        return;
    }
    if (A.selmode == MF_SEL_FACE) mf_box_select_faces_vis(A.mesh, A.eye, A.target, A.fov, w, h, (float)x0, (float)y0, (float)x1, (float)y1, shift, vis_mask());
    else mf_box_select_verts_vis(A.mesh, A.eye, A.target, A.fov, w, h, (float)x0, (float)y0, (float)x1, (float)y1, shift, vis_mask());
    A.dirty = 1;
    printf("[meshforge] box select\n");
    report_selection();
}

/* The left button in the viewport: a click picks, a drag draws a box and selects what is inside. */
static void viewport_mouse(int shift) {
    int mx = cb_mouse_x(), my = cb_mouse_y();
    if (cb_mouse_pressed(CB_MOUSE_LEFT) && !A.eat && !in_ui(mx, my)) {
        close_add_panel();
        if (A.tool != TOOL_SELECT && gizmo_press(mx, my)) return;
        A.lp = 1; A.boxing = 0; A.px0 = mx; A.py0 = my;
        return;
    }
    if (!A.lp) return;
    if (cb_mouse_down(CB_MOUSE_LEFT)) {
        if (!A.boxing && (abs(mx - A.px0) > 4 || abs(my - A.py0) > 4)) A.boxing = 1;
        return;
    }
    A.lp = 0;
    if (A.boxing) { A.boxing = 0; box_select(shift, A.px0, A.py0, mx, my); }
    else if (A.mode == MODE_OBJECT) pick_object(shift);
    else pick_at_mouse(shift);
}

/* ---- the one-shot Mesh mode operators: delete (X / Delete), flip (Alt+N), merge (M, Alt+M), loop cut (Ctrl+R) ---- */
static void mesh_op_done(int n, const char *what, const char *none) {
    if (n < 0) { undo_pop(); status("%s: %s", what, n == -2 ? "not supported here (nothing changed)" : "out of memory"); return; }
    if (n == 0) { undo_drop(); status("%s", none); return; }
    A.dirty = 1;
}
static void mesh_delete(void) {
    mf_mesh *m = A.mesh;
    int n;
    const char *what = A.selmode == MF_SEL_VERT ? "vertices" : A.selmode == MF_SEL_EDGE ? "edges" : "faces";
    if (A.selmode == MF_SEL_FACE ? mf_sel_face_count(m) == 0 : mf_sel_vert_count(m) == 0) { status("delete: select something first"); return; }
    undo_push();
    n = A.selmode == MF_SEL_VERT ? mf_delete_verts(m) : A.selmode == MF_SEL_EDGE ? mf_delete_edges(m) : mf_delete_faces(m);
    mesh_op_done(n, "delete", "nothing to delete (edge mode needs two joined vertices)");
    if (n > 0) { mf_sel_clear(m); status("deleted selected %s (%d vertices and faces removed)", what, n); }
}
/* H hides the selection, Alt+H shows everything again (and selects what came back), like Blender's. Hidden parts are not drawn, picked,
   selected or touched by an operator while you edit; they stay in the mesh (and in saved OBJs). */
static void mesh_hide(int reveal) {
    int n, faces = A.selmode == MF_SEL_FACE;
    if (reveal) {
        if (mf_hidden_count(A.mesh, NULL, NULL) == 0) { status("nothing is hidden"); return; }
        undo_push();
        n = mf_reveal_all(A.mesh, faces);
        A.dirty = 1;
        status("revealed %d hidden element(s), selected", n);
        printf("[meshforge] revealed %d hidden element(s)\n", n);
        return;
    }
    if (faces ? mf_sel_face_count(A.mesh) == 0 : mf_sel_vert_count(A.mesh) == 0) { status("hide: select something first (Alt+H shows what is hidden)"); return; }
    undo_push();
    n = mf_hide_selected(A.mesh, faces);
    A.dirty = 1;
    status("hid %d element(s): Alt+H shows them again", n);
    printf("[meshforge] hid %d element(s)\n", n);
}

static void mesh_flip(void) {
    int n;
    undo_push();
    n = mf_flip_faces(A.mesh, mf_sel_face_count(A.mesh) == 0 && mf_sel_vert_count(A.mesh) == 0);
    mesh_op_done(n, "flip", "no faces to flip");
    if (n > 0) status("flipped %d face(s)", n);
}
static void mesh_merge(int by_distance) {
    int n;
    if (!by_distance && mf_sel_vert_count(A.mesh) < 2) { status("merge: select two or more vertices first"); return; }
    undo_push();
    n = by_distance ? mf_merge_by_distance(A.mesh, 0.001f, mf_sel_vert_count(A.mesh) > 0) : mf_merge_center(A.mesh);
    mesh_op_done(n, "merge", by_distance ? "no vertices within 0.001 of each other" : "nothing merged");
    if (n > 0) status("merged: %d vertex(es) removed", n);
}
static void mesh_loopcut(int cuts) {
    mf_mesh *m = A.mesh;
    int c, n;
    c = mf_pick_edge_screen_vis(m, A.eye, A.target, A.fov, cb_screen_width(), cb_screen_height(), (float)cb_mouse_x(), (float)cb_mouse_y(), 14.0f, vis_mask());
    if (c < 0) { status("loop cut: put the mouse on an edge of a quad ring (a cube's side, a grid...)"); return; }
    undo_push();
    mf_sel_clear(m);
    n = mf_loop_cut(m, c, cuts);
    if (n == -2) { undo_pop(); status("loop cut: the ring runs into a triangle, an n-gon or a hidden face: not supported"); return; }
    mesh_op_done(n, "loop cut", "nothing to cut there");
    if (n > 0) status("loop cut: %d face(s) added (the new loop is selected; G moves it)", n);
}

static void mode_toggle(void) {   /* Tab */
    close_add_panel();
    if (A.mode == MODE_OBJECT) {
        if (A.cur >= 0 && A.o[A.cur].sel && edit_enter()) { A.mode = MODE_MESH; status("Mesh mode (%s)", A.o[A.cur].name); }
        else status("select an object first (click it), then Tab edits its mesh");
    } else {
        edit_leave();
        A.mode = MODE_OBJECT;
        status("Object mode");
    }
    A.dirty = 1;
}

static void commands(int shift) {
    int ctrl = ctrl_down(), alt = alt_down();
    if (cb_key_pressed(CB_KEY_TAB)) mode_toggle();
    if (cb_key_pressed(CB_KEY_Z) && ctrl) { if (shift) redo_user(); else undo_user(); }
    if (cb_key_pressed(CB_KEY_Y) && ctrl) redo_user();
    if (cb_key_pressed(CB_KEY_Q) && ctrl) quit_request();   /* Escape is the transforms' cancel key, so it no longer quits */
    if (cb_key_pressed(300)) mf_window_borderless_toggle();   /* F11 (cbricks.h has no CB_KEY_F11) */
    if (cb_key_pressed(CB_KEY_F5) || (cb_key_pressed(CB_KEY_S) && ctrl)) do_save();
    if (ctrl && cb_key_pressed(CB_KEY_P) && A.mode == MODE_OBJECT && !A.xf) {   /* Ctrl+P: parent the selected objects to the active one */
        int i, n = 0;
        if (A.cur < 0 || !A.o[A.cur].sel) status("Ctrl+P: select the children, then the parent last (it becomes the active object)");
        else {
            undo_push();
            for (i = 0; i < A.nobj; i++) if (A.o[i].sel && i != A.cur) n += set_parent(i, A.cur, 1);
            if (n) status("%d object(s) parented to \"%s\"", n, A.o[A.cur].name); else { undo_drop(); status("nothing to parent (select the children, then the parent last)"); }
        }
    }
    if (ctrl && !A.xf && !A.renaming && !A.menu_open && !alt) {   /* Ctrl already held when the transform starts: it snaps from the first frame (S is Ctrl+S: save) */
        if (cb_key_pressed(CB_KEY_G)) { close_add_panel(); xf_start('G'); }
        else if (cb_key_pressed(CB_KEY_R) && A.mode == MODE_MESH) mesh_loopcut(shift ? 3 : 1);
        else if (cb_key_pressed(CB_KEY_R)) xf_start('R');
        else if (cb_key_pressed(CB_KEY_E) && A.mode == MODE_MESH) extrude_start();
        else if (cb_key_pressed(CB_KEY_B) && A.mode == MODE_MESH) mop_start('B');
    }
    if (ctrl && cb_key_pressed(CB_KEY_I) && A.mode == MODE_MESH && !A.xf) {   /* Ctrl+I: invert the selection */
        undo_push();
        status("selection inverted: %d %s selected", mf_sel_invert(A.mesh, A.selmode == MF_SEL_FACE), A.selmode == MF_SEL_FACE ? "face(s)" : "vertex(es)");
        A.dirty = 1;
    }
    if (ctrl) { if (!A.xf) viewport_mouse(shift); return; }   /* the keys below are plain keys; a Ctrl-click / Ctrl-drag (gizmo) still reaches the viewport */
    if (cb_key_pressed(CB_KEY_Z) && alt) { A.xray = !A.xray; A.dirty = 1; status("X-ray %s", A.xray ? "on: vertices show through the mesh" : "off: hidden vertices stay hidden"); }
    if (cb_key_pressed(CB_KEY_F2) && A.cur >= 0 && !A.xf) { close_add_panel(); rename_start(A.cur); return; }
    if (cb_key_pressed(CB_KEY_W)) { A.tool = (A.tool + 1) % TOOL_N; A.gz_log = 1; status("%s tool", TOOL_NAME[A.tool]); }
    if (cb_key_pressed(CB_KEY_G)) { close_add_panel(); xf_start('G'); }
    else if (cb_key_pressed(CB_KEY_R)) xf_start('R');
    else if (cb_key_pressed(CB_KEY_S)) xf_start('S');
    if (A.xf) return;
    if (cb_key_pressed(CB_KEY_A) && shift) { menu_open_at(cb_mouse_x(), cb_mouse_y(), 0); return; }
    viewport_mouse(shift);
    if (A.mode == MODE_OBJECT) {
        int i;
        if (cb_key_pressed(CB_KEY_A)) {
            if (alt) deselect_all();
            else { for (i = 0; i < A.nobj; i++) A.o[i].sel = 1; A.dirty = 1; status("all %d object(s) selected", A.nobj); }
        }
        if (cb_key_pressed(CB_KEY_X) || cb_key_pressed(KEY_DELETE)) delete_selected();
        if (cb_key_pressed(CB_KEY_D) && shift) duplicate_selected();
        if (cb_key_pressed(CB_KEY_P) && alt) {   /* Alt+P: clear the parent of the selected objects */
            int n = 0;
            undo_push();
            for (i = 0; i < A.nobj; i++) if (A.o[i].sel) n += set_parent(i, -1, 1);
            if (n) status("cleared the parent of %d object(s)", n); else { undo_drop(); status("the selected objects have no parent"); }
        }
        return;
    }
    if (cb_key_pressed(CB_KEY_ONE)) set_selmode(MF_SEL_VERT);
    if (cb_key_pressed(CB_KEY_TWO)) set_selmode(MF_SEL_EDGE);
    if (cb_key_pressed(CB_KEY_THREE)) set_selmode(MF_SEL_FACE);
    if (cb_key_pressed(CB_KEY_A)) {
        if (alt) { mf_sel_clear(A.mesh); A.dirty = 1; status("selection cleared"); }
        else { mf_sel_all(A.mesh, 1); A.dirty = 1; status("selected all"); }
    }
    if (cb_key_pressed(CB_KEY_E)) extrude_start();
    if (cb_key_pressed(CB_KEY_I)) mop_start('I');
    if (cb_key_pressed(CB_KEY_X) || cb_key_pressed(KEY_DELETE)) mesh_delete();
    if (cb_key_pressed(CB_KEY_H)) mesh_hide(alt);
    if (cb_key_pressed(CB_KEY_N) && alt) mesh_flip();
    if (cb_key_pressed(CB_KEY_M)) mesh_merge(alt);
    if (cb_key_pressed(CB_KEY_L)) select_linked(!shift);
}

void mf_app_update(void) {
    double dt;
    int shift, mx, my, rmb, sh, strip;
    if (!A.ready) return;
    pr_begin(PR_UPDATE);
    {   int c;
        A.ntyped = 0;
        while ((c = cb_char_typed()) != 0) if (A.ntyped < (int)sizeof A.typed) A.typed[A.ntyped++] = c < 128 ? (char)c : '?';
    }
    if (A.max_tried < 6 && !A.max_ok) {   /* windowed Xwayland is slow on the Pi (22-34 fps), maximized is 60: DECISIONS 2026-11-17 */
        /* the window manager may not honour the request on the first frames (it did not on the user's labwc): ask again later */
        static const int at[6] = {2, 20, 60, 120, 240, 480};
        if (A.frame_no++ == at[A.max_tried]) {
            A.max_tried++;
            if (cb_flag("demo") || cb_flag("no-maximize") || (cb_option("quit-after") && cb_option("quit-after")[0])) A.max_tried = 6;
            else {
                A.max_ok = mf_window_maximize();
                printf("[meshforge] maximize try %d: %s\n", A.max_tried, A.max_ok ? "maximized" : "not (yet)");
            }
        }
    }
    dt = cb_frame_time();
    if (dt <= 0.0 || dt > 0.25) dt = 1.0 / 60.0;
    shift = shift_down();
    mx = cb_mouse_x(); my = cb_mouse_y();
    if (!cb_mouse_down(CB_MOUSE_RIGHT)) A.rmb_ok = 1;
    if (!A.renaming && !A.xf && cb_mouse_pressed(CB_MOUSE_LEFT)) {   /* the workspace tabs */
        int t = ws_tab_at(cb_mouse_x(), cb_mouse_y());
        if (t == 1 && !mf_shader_active()) { mf_shader_open(NULL); printf("[meshforge] workspace Shader\n"); }
        else if (mf_shader_active() && ws_btn_at(cb_mouse_x(), cb_mouse_y()) == 0) { A.imp = !A.imp; if (A.imp && !A.imp_buf[0]) snprintf(A.imp_buf, sizeof A.imp_buf, "assets/"); }
        else if (mf_shader_active() && ws_btn_at(cb_mouse_x(), cb_mouse_y()) == 1) mf_shader_set_model(!mf_shader_model());
        else if (t == 0 && mf_shader_active()) { mf_shader_close(); printf("[meshforge] workspace Modeling\n"); }
    }
    if (!A.renaming && !A.xf && cb_key_pressed(CB_KEY_F4)) { if (mf_shader_active()) mf_shader_close(); else mf_shader_open(NULL); }   /* the Shader workspace */
    sh = mf_shader_active();
    strip = !sh || (my < mf_shader_split() && !ws_ui_at(mx, my));   /* in the Shader workspace the camera only answers over the top strip */
    rmb = cb_mouse_down(CB_MOUSE_RIGHT) && A.rmb_ok && !A.xf && !A.renaming && strip;

    if (A.was_rmb && !rmb) printf("[meshforge] flew: target %.2f %.2f %.2f\n", (double)A.target.x, (double)A.target.y, (double)A.target.z);
    A.was_rmb = rmb;
    if (rmb && !A.captured) { cb_capture_mouse(); A.captured = 1; }   /* hidden and locked to the centre while flying */
    if (!rmb && A.captured) { cb_cursor(0); A.captured = 0; }
    if (rmb && cb_key_pressed(CB_KEY_TAB)) { printf("[meshforge] Tab while flying\n"); mode_toggle(); }   /* Tab still switches Object / Mesh mode while flying */
    if (rmb) {
        fly(dt);
    } else if (!A.xf && strip) {   /* the camera is frozen during a transform: its maths use the camera it started with */
        if (cb_key_down(CB_KEY_LEFT)) A.yaw -= (float)(1.8 * dt);
        if (cb_key_down(CB_KEY_RIGHT)) A.yaw += (float)(1.8 * dt);
        if (cb_key_down(CB_KEY_UP)) A.pitch += (float)(1.2 * dt);
        if (cb_key_down(CB_KEY_DOWN)) A.pitch -= (float)(1.2 * dt);
        if (sh && cb_mouse_down(CB_MOUSE_LEFT)) {   /* the Shader workspace has no selection: left-drag turns the view */
            A.yaw -= (float)(mx - A.mx) * 0.01f;
            A.pitch += (float)(my - A.my) * 0.01f;
        }
        if (cb_mouse_down(CB_MOUSE_MIDDLE) && shift) {   /* Shift + middle-drag: pan (the scene follows the mouse) */
            float cp = cosf(A.pitch), k = 2.0f * A.dist * tanf(A.fov * 0.5f * PI_F / 180.0f) / (float)cb_screen_height();
            mf_vec3 d = mf_v3(cp * sinf(A.yaw), sinf(A.pitch), cp * cosf(A.yaw)), right = mf_v3(cosf(A.yaw), 0, -sinf(A.yaw));
            mf_vec3 up = mf_cross(d, right);
            A.target = mf_sub(A.target, mf_mul(right, (float)(mx - A.mx) * k));
            A.target = mf_add(A.target, mf_mul(up, (float)(my - A.my) * k));
        } else if (cb_mouse_down(CB_MOUSE_MIDDLE)) {
            A.yaw -= (float)(mx - A.mx) * 0.01f;
            A.pitch += (float)(my - A.my) * 0.01f;
        }
        if (!A.renaming) {   /* numpad: 1 front, 3 right, 7 top (Ctrl: the opposite side), . frame the selection */
            int ct = ctrl_down();
            if (cb_key_pressed(KEY_KP_1)) { A.yaw = ct ? PI_F : 0.0f; A.pitch = 0.0f; status(ct ? "back view" : "front view"); }
            if (cb_key_pressed(KEY_KP_3)) { A.yaw = ct ? -PI_F * 0.5f : PI_F * 0.5f; A.pitch = 0.0f; status(ct ? "left view" : "right view"); }
            if (cb_key_pressed(KEY_KP_7)) { A.yaw = 0.0f; A.pitch = ct ? -1.5f : 1.5f; status(ct ? "bottom view" : "top view"); }
            if (cb_key_pressed(KEY_KP_DECIMAL)) { frame_mesh(1); status("framed the selection"); }
        }
        if (A.pitch > 1.5f) A.pitch = 1.5f;
        if (A.pitch < -1.5f) A.pitch = -1.5f;
        if (A.xf != 'B') A.dist *= 1.0f - (float)cb_mouse_wheel() * 0.1f;   /* (while a bevel runs the wheel sets its segments) */
        if (A.dist < 0.2f) A.dist = 0.2f;
        if (cb_key_pressed(CB_KEY_F) || cb_key_pressed(KEY_HOME)) frame_mesh(0);
    }
    A.mx = mx; A.my = my;
    place_camera();   /* before picking: the click uses this frame's camera */

    A.eat = 0;
    if (A.renaming) rename_update();
    else if (A.xf) xf_update();
    else if (sh && A.imp) { A.eat = 1; import_update(); }
    else if (sh) { A.eat = 1; mf_shader_typed(A.typed, A.ntyped); mf_shader_update(); if (ctrl_down() && cb_key_pressed(CB_KEY_Q)) quit_request(); }
    else if (!rmb) { A.eat = ui_update(); commands(shift); }
    refresh_mask();

    A.frame++;
    A.last_t = cb_game_time();
    if (cb_flag("demo")) demo_step();
    else {
        const char *qa = cb_option("quit-after");   /* seconds since the window opened */
        if (qa[0] && cb_game_time() > cb_to_float(qa)) {
            if (!A.quit_armed) { A.shot = cb_option("shot"); A.quit_armed = 1; }
            else cb_close_window();
        }
    }
    autosave_tick();
    pr_begin(PR_VIS); vis_update(); pr_end(PR_VIS);
    pr_begin(PR_BUILD); if (A.dirty) rebuild_render(); pr_end(PR_BUILD);
    pr_end(PR_UPDATE);
}

void mf_app_draw3d(void) {
    int f, c, i, k;
    cb_rgba edge = {30, 32, 40, 255}, sel = {255, 190, 60, 255};
    if (!A.ready) return;
    pr_begin(PR_DRAW3D);
    if (mf_shader_active()) {   /* the Shader workspace draws only the preview (the sphere, or the model with the shader on it) */
        mf_shader_draw3d(A.target.x, A.target.y, A.target.z, A.dist * 0.18f, A.render, A.rtris);
        pr_end(PR_DRAW3D);
        return;
    }
    cb_draw_grid(20, 1.0);
    pr_begin(PR_SURF);
    if (A.render && A.rtris > 0) cb_draw_mesh(A.render, cb_v3(0, 0, 0), cb_v3(1, 1, 1), 0.0, (cb_rgba){255, 255, 255, 255});
    pr_end(PR_SURF);
    for (k = 0; k < A.nobj; k++) {   /* every object's edges, placed by its transform */
        mf_mesh *m = (mf_mesh *)omesh(k);
        int edit = k == A.cur && A.mode == MODE_MESH;
        int obj_hot = A.o[k].sel && A.mode == MODE_OBJECT;
        int dense, cull, sw = cb_screen_width(), sh = cb_screen_height();
        float minpx2;
        if (edit && A.xray) continue;   /* X-ray: the HUD draws this object's whole wireframe, hidden edges too */
        if (!m->topo_ok) mf_build(m);
        /* Every edge is a draw call, and at 30k faces that is the whole frame. So: each vertex goes to the world and the screen ONCE; on a dense
           mesh edges that are off the window, or shorter than 2 pixels (they only blur into a grey haze), are skipped; and on a closed mesh an
           edge between two faces that point away from the camera is hidden by the depth test anyway, so it is not sent. Selected edges always are. */
        dense = m->ne > DENSE_EDGES;
        minpx2 = m->ne > 3 * DENSE_EDGES ? 16.0f : 4.0f;   /* a very dense mesh: edges under 4 px are haze too (each one is a draw call) */
        cull = m->nboundary == 0 && m->nnonmanifold == 0 && m->nf > 0;
        if (m->nv > A.wcap) {
            mf_vec3 *nw = (mf_vec3 *)realloc(A.wv, (size_t)m->nv * sizeof *nw);
            float *ns = (float *)realloc(A.sxy, (size_t)m->nv * 3 * sizeof *ns);
            if (nw) A.wv = nw;
            if (ns) A.sxy = ns;
            if (!nw || !ns) { A.wcap = 0; continue; }
            A.wcap = m->nv;
        }
        for (i = 0; i < m->nv; i++) {
            float sx = 0, sy = 0;
            A.wv[i] = wpt(k, m->v[i]);
            A.sxy[3 * i + 2] = dense && mf_project(A.eye, A.target, A.fov, sw, sh, A.wv[i], &sx, &sy) ? 1.0f : 0.0f;
            A.sxy[3 * i] = sx; A.sxy[3 * i + 1] = sy;
        }
        if (cull) {   /* which faces look at the eye (Newell normal of the world-space corners) */
            if (m->nf > A.fcap) {
                unsigned char *nf = (unsigned char *)realloc(A.front, (size_t)m->nf);
                if (nf) { A.front = nf; A.fcap = m->nf; }
            }
            if (m->nf > A.fcap) cull = 0;
            else for (f = 0; f < m->nf; f++) {
                const int *vi = mf_face_verts(m, f);
                int n = mf_face_size(m, f);
                mf_vec3 nrm = mf_v3(0, 0, 0), p0 = A.wv[vi[0]];
                for (i = 0; i < n; i++) {
                    mf_vec3 a = A.wv[vi[i]], b = A.wv[vi[(i + 1) % n]];
                    nrm.x += (a.y - b.y) * (a.z + b.z); nrm.y += (a.z - b.z) * (a.x + b.x); nrm.z += (a.x - b.x) * (a.y + b.y);
                }
                A.front[f] = (!(edit && m->fhid[f]) && mf_dot(nrm, mf_sub(A.eye, p0)) > -1e-6f * (1.0f + fabsf(nrm.x) + fabsf(nrm.y) + fabsf(nrm.z))) ? 1 : 0;
            }
        }
        for (c = 0; c < m->nc; c++) {   /* each edge once: from the lower corner of a twin pair, or alone on a boundary */
            int t = m->twin[c], a = m->fidx[c], b = m->fidx[mf_corner_next(m, c)];
            int hot = edit && A.selmode == MF_SEL_EDGE && A.mask_cap >= m->nv && A.mask[a] && A.mask[b];
            if (t >= 0 && t < c) continue;
            if (edit && m->fhid[m->cface[c]] && (t < 0 || m->fhid[m->cface[t]])) continue;   /* hidden: both its faces are */
            if (!hot && !obj_hot) {
                if (cull && !A.front[m->cface[c]] && (t < 0 || !A.front[m->cface[t]])) continue;
                if (dense) {
                    const float *pa = &A.sxy[3 * a], *pb = &A.sxy[3 * b];
                    float dx = pa[0] - pb[0], dy = pa[1] - pb[1];
                    if (pa[2] > 0.5f && pb[2] > 0.5f) {
                        if (dx * dx + dy * dy < minpx2) continue;
                        if ((pa[0] < 0 && pb[0] < 0) || (pa[1] < 0 && pb[1] < 0) || (pa[0] > (float)sw && pb[0] > (float)sw) || (pa[1] > (float)sh && pb[1] > (float)sh)) continue;
                    }
                }
            }
            cb_draw_line3d_biased(to_cb(A.wv[a]), to_cb(A.wv[b]), hot || obj_hot ? (cb_rgba){255, 150, 30, 255} : edge);
        }
        if (edit && A.selmode == MF_SEL_FACE) {
            for (f = 0; f < m->nf; f++) {
                const int *vi;
                int n;
                if (!m->fsel[f] || m->fhid[f]) continue;
                vi = mf_face_verts(m, f);
                n = mf_face_size(m, f);
                for (i = 0; i < n; i++)   /* a little more bias than the edges: the outline sits on top of them */
                    cb_draw_line3d_biased(to_cb(m->v[vi[i]]), to_cb(m->v[vi[(i + 1) % n]]), sel);
            }
        }
    }
    if (A.xf && A.axis && !A.gdrag) {   /* the constraint axis through the pivot: X red, Y green, Z blue */
        mf_vec3 u = mf_mul(axis_vec(A.axis), 100.0f);
        cb_rgba col = A.axis == 1 ? (cb_rgba){230, 70, 70, 255} : (A.axis == 2 ? (cb_rgba){90, 210, 90, 255} : (cb_rgba){80, 130, 240, 255});
        if (A.axis == 4) col = (cb_rgba){255, 220, 90, 255};
        cb_draw_line3d_biased(to_cb(mf_sub(A.pivot, u)), to_cb(mf_add(A.pivot, u)), col);
    }
    pr_end(PR_DRAW3D);
}

/* ---- the tabbed panel (under the hierarchy): read-only data for now, plus the View tab's switches ---- */
static void vbox(int x, int y, int w, const char *val, cb_rgba ink) {
    rr(x, y + 1, w, TABS_ROW - 3, 3, C_FIELD);
    txt(val, x + (w - tw_(val, 14)) / 2, y + 3, 14, ink);
}
static void row3(int r, const char *label, float a, float b, float c, const char *fmt, cb_rgba ink, cb_rgba dim) {
    int x = panel_x() + 10, y = tab_row_y(r), bw = 44, bx = x + 74;
    txt(label, x, y + 3, 14, dim);
    vbox(bx, y, bw, cb_format(fmt, (double)a), ink);
    vbox(bx + bw + 3, y, bw, cb_format(fmt, (double)b), ink);
    vbox(bx + 2 * (bw + 3), y, bw, cb_format(fmt, (double)c), ink);
}
static void row_text(int r, const char *label, const char *val, cb_rgba ink, cb_rgba dim) {
    int x = panel_x() + 10, y = tab_row_y(r);
    txt(label, x, y + 3, 14, dim);
    txt(val, x + 74, y + 3, 14, ink);
}
static void sect_head(int px, int y, const char *title) {   /* a Blender-style panel header bar */
    rr(px + 8, y, PANEL_W - 16, 22, 3, C_HEAD);
    ico_arrow(px + 16, y + 7, 1, C_DIM);
    txt(title, px + 32, y + 4, 14, C_INK);
}
static void draw_ws_tabs(void) {
    static const char *name[2] = { "Modeling", "Shader" };
    int i, mx = cb_mouse_x(), my = cb_mouse_y(), cur = mf_shader_active() ? 1 : 0;
    for (i = 0; i < 2; i++) {
        int x = WS_X + i * WS_W, on = i == cur, hot = ws_tab_at(mx, my) == i;
        rr(x, WS_Y, WS_W - 3, WS_H, 4, on ? C_ACC : C_FRAME);
        rr(x + 1, WS_Y + 1, WS_W - 5, WS_H - 2, 3, on ? C_ACCBG : (hot ? C_FIELDH : C_PANEL));
        txt(name[i], x + (WS_W - 3 - tw_(name[i], 14)) / 2, WS_Y + 5, 14, on ? C_ACC : C_INK);
    }
}
static void draw_ws_buttons(void) {   /* only in the Shader workspace */
    int mx = cb_mouse_x(), my = cb_mouse_y(), hot = ws_btn_at(mx, my), i;
    for (i = 0; i < 2; i++) {
        const char *lbl = i == 0 ? "Import model" : (mf_shader_model() ? "Preview: model" : "Preview: sphere");
        int x = WB_X + (i ? WB_IMP_W + 4 : 0), w = i ? WB_PRE_W : WB_IMP_W, on = i == 0 && A.imp;
        rr(x, WS_Y, w, WS_H, 4, on ? C_ACC : C_FRAME);
        rr(x + 1, WS_Y + 1, w - 2, WS_H - 2, 3, on ? C_ACCBG : (hot == i ? C_FIELDH : C_PANEL));
        txt(lbl, x + (w - tw_(lbl, 14)) / 2, WS_Y + 5, 14, on ? C_ACC : C_INK);
    }
    if (A.imp) {   /* the path box, across the foot of the preview */
        int bw = cb_screen_width() - 20, by = mf_shader_split() - 66;
        char line[280];
        rr(10, by, bw, 56, 5, C_ACC);
        rr(11, by + 1, bw - 2, 54, 4, C_PANEL);
        txt("Import an OBJ into the scene (it is previewed): type its path, Enter loads, Esc cancels", 20, by + 6, 13, C_DIM);
        snprintf(line, sizeof line, "%s_", A.imp_buf);
        rr(18, by + 26, bw - 16, 24, 3, C_FIELD);
        txt(line, 24, by + 31, 14, C_INK);
    }
}
static void draw_tabs(void) {
    int px = panel_x(), py = tabs_y(), t, mx = cb_mouse_x(), my = cb_mouse_y(), cw = PANEL_W / TAB_N;
    cb_rgba ink = C_INK, dim = C_DIM;
    const sobj *o = A.cur >= 0 && A.cur < A.nobj ? &A.o[A.cur] : NULL;
    ui_box(px, py, PANEL_W, tabs_h());
    for (t = 0; t < TAB_N; t++) {   /* text tabs with an accent underline, like the C-Bricks editor's own */
        int on = A.tab == t, tx = px + t * cw + (cw - tw_(TAB_SHORT[t], 14)) / 2;
        txt(TAB_SHORT[t], tx, py + 6, 14, on ? C_ACC : (tab_at(mx, my) == t ? ink : dim));
        if (on) cb_draw_rect(px + t * cw + 8, py + TABS_HEAD - 3, cw - 16, 2, C_ACC);
    }
    cb_draw_rect(px + 1, py + TABS_HEAD - 1, PANEL_W - 2, 1, C_FRAME);
    if (A.tab == TAB_MODS) { mods_walk(1); return; }
    sect_head(px, py + TABS_HEAD + 4, TAB_LONG[A.tab]);
    if (A.tab == TAB_ITEM) {
        if (!o) { txt("No active object", px + 10, tab_row_y(0) + 3, 14, dim); return; }
        row_text(0, "Object", o->name, ink, dim);
        row3(1, "Location", o->t.pos.x, o->t.pos.y, o->t.pos.z, "%.2f", ink, dim);
        row3(2, "Rotation", o->t.rot.x * 180.0f / PI_F, o->t.rot.y * 180.0f / PI_F, o->t.rot.z * 180.0f / PI_F, "%.1f", ink, dim);
        row3(3, "Scale", o->t.scl.x, o->t.scl.y, o->t.scl.z, "%.2f", ink, dim);
        row_text(4, "Parent", o->parent >= 0 && o->parent < A.nobj ? A.o[o->parent].name : "none", ink, dim);
        if (A.mode == MODE_MESH && A.mesh && A.mask_cap >= A.mesh->nv && mf_sel_vert_count(A.mesh) > 0) {
            mf_vec3 c = mf_sel_center(A.mesh, A.mask);
            row3(5, "Median", c.x, c.y, c.z, "%.2f", ink, dim);
        } else txt("world space (Mesh mode: median)", px + 10, tab_row_y(5) + 4, 12, dim);
    } else if (A.tab == TAB_MESH) {
        mf_stats st;
        int f, tris = 0;
        mf_vec3 lo, hi;
        if (!A.mesh) { txt("No active object", px + 10, tab_row_y(0) + 3, 14, dim); return; }
        st = mf_stats_get(A.mesh);
        for (f = 0; f < A.mesh->nf; f++) tris += mf_face_size(A.mesh, f) - 2;
        mf_bounds(A.mesh, &lo, &hi);
        row_text(0, "Vertices", cb_format("%d", st.verts), ink, dim);
        row_text(1, "Edges", cb_format("%d", st.edges), ink, dim);
        row_text(2, "Faces", cb_format("%d  (%d triangles)", st.faces, tris), ink, dim);
        row3(3, "Size", (hi.x - lo.x) * (A.baked || !o ? 1.0f : o->t.scl.x), (hi.y - lo.y) * (A.baked || !o ? 1.0f : o->t.scl.y), (hi.z - lo.z) * (A.baked || !o ? 1.0f : o->t.scl.z), "%.2f", ink, dim);
        row_text(4, "Surface", st.nonmanifold ? "NON-MANIFOLD" : (st.boundary ? "open" : "closed"), ink, dim);
        row_text(5, "Selected", A.mode == MODE_MESH ? cb_format("%d vert / %d face", mf_sel_vert_count(A.mesh), mf_sel_face_count(A.mesh)) : "(Mesh mode)", ink, dim);
    } else {
        int bx = px + 96, bw = PANEL_W - 96 - 10, r1 = tab_row_y(1);
        txt("X-ray", px + 10, tab_row_y(0) + 3, 14, dim);
        rr(bx, tab_row_y(0) + 1, bw, TABS_ROW - 3, 3, A.xray ? C_ACCBG : C_FIELD);
        txt(A.xray ? "On  (Alt+Z)" : "Off  (Alt+Z)", bx + 10, tab_row_y(0) + 3, 14, A.xray ? C_ACC : ink);
        txt("Snap step", px + 10, r1 + 3, 14, dim);
        rr(bx, r1 + 1, bw, TABS_ROW - 3, 3, C_FIELD);
        txt("<", bx + 8, r1 + 3, 14, dim);
        txt(">", bx + bw - 16, r1 + 3, 14, dim);
        {
            const char *v = cb_format("%.2f", (double)SNAPS[A.snap_i]);
            txt(v, bx + (bw - tw_(v, 14)) / 2, r1 + 3, 14, ink);
        }
        txt("Shading", px + 10, tab_row_y(2) + 3, 14, dim);
        rr(bx, tab_row_y(2) + 1, bw, TABS_ROW - 3, 3, o && o->smooth ? C_ACCBG : C_FIELD);
        txt(o && o->smooth ? "Smooth" : "Flat", bx + 10, tab_row_y(2) + 3, 14, o && o->smooth ? C_ACC : ink);
        txt("Hold Ctrl while moving, rotating", px + 10, tab_row_y(3) + 3, 13, dim);
        txt("or scaling to snap: moves in steps", px + 10, tab_row_y(4) + 3, 13, dim);
        txt("of the step, 15 deg turns, 0.1 scale", px + 10, tab_row_y(5) + 3, 13, dim);
    }
}

/* X-ray in Mesh mode: the whole wireframe over the shaded mesh (hidden edges too), selected edges / face outlines on top, and one
   dot per face in face mode. Drawn in 2D from the projected points, so nothing is depth-tested. */
static void draw_xray_overlay(void) {
    mf_mesh *m = A.mesh;
    int c, f, i, pass, w = cb_screen_width(), h = cb_screen_height();
    cb_rgba orange = {255, 150, 30, 255}, line = {44, 49, 64, 255};
    if (!A.xray || A.mode != MODE_MESH || !m || m->nv == 0) return;
    if (!m->topo_ok) mf_build(m);
    for (pass = 0; pass < 2; pass++) {   /* plain edges first, selected ones over them */
        for (c = 0; c < m->nc; c++) {
            int t = m->twin[c], a = m->fidx[c], b = m->fidx[mf_corner_next(m, c)], hot;
            float ax, ay, bx, by;
            if (t >= 0 && t < c) continue;
            if (m->fhid[m->cface[c]] && (t < 0 || m->fhid[m->cface[t]])) continue;
            hot = A.selmode == MF_SEL_EDGE && A.mask_cap >= m->nv && A.mask[a] && A.mask[b];
            if (hot != pass) continue;
            if (!mf_project(A.eye, A.target, A.fov, w, h, m->v[a], &ax, &ay) || !mf_project(A.eye, A.target, A.fov, w, h, m->v[b], &bx, &by)) continue;
            cb_draw_line((int)ax, (int)ay, (int)bx, (int)by, hot ? orange : line);
        }
    }
    if (A.selmode != MF_SEL_FACE) return;
    for (f = 0; f < m->nf; f++) {
        float sx, sy;
        if (m->fhid[f]) continue;
        if (m->fsel[f]) {   /* the selected face's outline, in the selection colour */
            const int *vi = mf_face_verts(m, f);
            int n = mf_face_size(m, f);
            for (i = 0; i < n; i++) {
                float ax, ay, bx, by;
                if (mf_project(A.eye, A.target, A.fov, w, h, m->v[vi[i]], &ax, &ay) && mf_project(A.eye, A.target, A.fov, w, h, m->v[vi[(i + 1) % n]], &bx, &by))
                    cb_draw_line((int)ax, (int)ay, (int)bx, (int)by, (cb_rgba){255, 190, 60, 255});
            }
        }
        if (!face_dot(m, f, &sx, &sy)) continue;
        if (m->fsel[f]) cb_draw_rect((int)sx - 3, (int)sy - 3, 7, 7, orange);
        else cb_draw_rect((int)sx - 2, (int)sy - 2, 5, 5, (cb_rgba){20, 22, 30, 255});
    }
}

static void take_shot(void) {
    if (A.shot && A.shot[0]) {   /* the last drawing call of the frame: the picture is complete */
        bool shot_ok = cb_screenshot(A.shot);
        printf("[meshforge] shot %s %s\n", A.shot, shot_ok ? "saved" : "FAILED");
        A.shot = NULL;
    }
}

void mf_app_draw_hud(void) {
    mf_stats s;
    mf_mesh *m;
    int v, w, h, px;
    cb_rgba ink = {235, 238, 245, 255}, dim = {150, 158, 175, 255};
    if (!A.ready) return;
    pr_begin(PR_HUD);
    if (!A.font_tried) { A.font_tried = 1; mf_font_load("assets/ui_font.ttf", 16); }
    m = A.mesh;
    w = cb_screen_width(); h = cb_screen_height();
    if (mf_shader_active()) { mf_shader_draw_hud(); draw_ws_tabs(); draw_ws_buttons(); take_shot(); pr_end(PR_HUD); return; }
    draw_xray_overlay();
    if (A.mode == MODE_MESH && m && A.selmode == MF_SEL_VERT && A.mask_cap >= m->nv) {   /* vertex dots, over everything */
        /* One projection per vertex. When the visible dots are packed closer than 4 pixels they only paint the mesh grey (and are 30k draw
           calls), so then the unselected ones are left out and the selected ones stay: zoom in and they come back. */
        int shown = 0, thin = 0, ok = A.wcap >= m->nv;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        if (ok) {
            for (v = 0; v < m->nv; v++) {
                float sx, sy;
                int in = !m->vhid[v] && (!vis_mask() || vis_mask()[v]) && mf_project(A.eye, A.target, A.fov, w, h, m->v[v], &sx, &sy) && sx > -4 && sy > -4 && sx < w + 4 && sy < h + 4;
                A.sxy[3 * v + 2] = in ? 1.0f : 0.0f;
                if (!in) continue;
                A.sxy[3 * v] = sx; A.sxy[3 * v + 1] = sy;
                shown++;
                if (sx < x0) x0 = sx;
                if (sx > x1) x1 = sx;
                if (sy < y0) y0 = sy;
                if (sy > y1) y1 = sy;
            }
            thin = shown > 3000 && (x1 - x0 + 1.0f) * (y1 - y0 + 1.0f) < 16.0f * (float)shown;
        }
        for (v = 0; v < m->nv; v++) {
            float sx, sy;
            if (ok) {
                if (A.sxy[3 * v + 2] < 0.5f) continue;
                if (thin && !A.mask[v]) continue;
                sx = A.sxy[3 * v]; sy = A.sxy[3 * v + 1];
            } else {
                if (m->vhid[v] || (vis_mask() && !vis_mask()[v])) continue;   /* hidden, or behind a face (X-ray off) */
                if (!mf_project(A.eye, A.target, A.fov, w, h, m->v[v], &sx, &sy)) continue;
            }
            if (A.mask[v]) cb_draw_rect((int)sx - 3, (int)sy - 3, 7, 7, (cb_rgba){255, 150, 30, 255});
            else cb_draw_rect((int)sx - 2, (int)sy - 2, 5, 5, (cb_rgba){20, 22, 30, 255});
        }
    }
    memset(&s, 0, sizeof s);
    if (m) s = mf_stats_get(m);
    cb_draw_rect(0, 0, w, 26, C_PANEL);
    cb_draw_rect(0, 26, w, 1, C_FRAME);
    if (A.mode == MODE_OBJECT) {
        int nsel = 0;
        for (v = 0; v < A.nobj; v++) nsel += A.o[v].sel;
        txt(cb_format("OBJECT MODE   %d object(s), %d selected   active: V %d   E %d   F %d   %s", A.nobj, nsel, s.verts, s.edges, s.faces,
                               s.nonmanifold ? "NON-MANIFOLD" : (s.boundary ? "open" : "closed")), 8, 5, 16, ink);
    } else
    {
        char hb[40];
        int hid = mf_hidden_count(m, NULL, NULL);
        hb[0] = 0;
        if (hid) snprintf(hb, sizeof hb, "   %d hidden (Alt+H)", hid);   /* (cb_format shares one buffer: not nested) */
        txt(cb_format("MESH MODE (%s)   V %d   E %d   F %d   selected %d vert / %d face   %s%s", selmode_name(),
                               s.verts, s.edges, s.faces, mf_sel_vert_count(m), mf_sel_face_count(m),
                               s.nonmanifold ? "NON-MANIFOLD" : (s.boundary ? "open" : "closed"), hb), 8, 5, 16, ink);
    }
    /* the outliner (Blender's name for it): Scene > one row per object, children under their parent, fold arrows, icons. */
    px = panel_x();
    ui_box(px, PANEL_TOP, PANEL_W, panel_h());
    rr(px + 1, PANEL_TOP + 1, PANEL_W - 2, PANEL_ROW + 2, 4, C_HEAD);
    cb_draw_rect(px + 1, PANEL_TOP + 12, PANEL_W - 2, PANEL_ROW - 8, C_HEAD);
    cb_draw_rect(px + 10, PANEL_TOP + 9, 12, 12, C_DIM);    /* the scene's icon: a box */
    cb_draw_rect(px + 12, PANEL_TOP + 11, 8, 8, C_HEAD);
    txt("Scene", px + 30, PANEL_TOP + 8, 15, ink);
    build_tree();
    {
        int hov = row_pos(cb_mouse_x(), cb_mouse_y());
        for (w = 0; w < A.nrows; w++) {   /* (w is free here: the screen width is not needed below) */
            int ox, ry = PANEL_TOP + 4 + (w + 1) * PANEL_ROW, sel, act;
            cb_rgba nameink;
            v = A.ord[w];
            sel = A.o[v].sel; act = v == A.cur;
            ox = row_ax(A.depth[w]);
            if (sel) rr(px + 3, ry, PANEL_W - 6, PANEL_ROW - 1, 3, act ? C_ACCBG : C_ACCBG2);
            else if (w == hov || act) rr(px + 3, ry, PANEL_W - 6, PANEL_ROW - 1, 3, C_FIELD);
            else if (w & 1) rr(px + 3, ry, PANEL_W - 6, PANEL_ROW - 1, 3, (cb_rgba){31, 33, 40, 255});
            if (act && sel) cb_draw_rect(px + 3, ry + 3, 2, PANEL_ROW - 7, C_ACC);
            if (A.depth[w] > 0) cb_draw_rect(ox - 6, ry + PANEL_ROW / 2, 6, 1, C_FRAME);
            if (has_kids(v)) ico_arrow(ox, ry + 7, !A.o[v].fold, dim);
            ico_object(ox + 14, ry + 5, sel ? C_OBJ : (cb_rgba){205, 135, 70, 255});
            if (A.renaming && A.rn_i == v) {
                cb_draw_rect(ox + 30, ry + 1, PANEL_W - (ox - px) - 36, PANEL_ROW - 3, (cb_rgba){12, 13, 18, 255});
                if (A.rn_fresh) cb_draw_rect(ox + 32, ry + 3, tw_(A.rn, 15) + 4, PANEL_ROW - 7, (cb_rgba){70, 90, 150, 255});
                txt(A.rn_fresh ? A.rn : cb_format("%s|", A.rn), ox + 34, ry + 4, 15, ink);
            } else {
                nameink = sel ? (cb_rgba){255, 255, 255, 255} : ink;
                txt(A.o[v].name, ox + 34, ry + 4, 15, nameink);
                if (A.o[v].nmod > 0) ico_wrench(px + PANEL_W - 24, ry + 5, C_MODC);
                if (act && A.mode == MODE_MESH) {
                    int ex = px + PANEL_W - (A.o[v].nmod > 0 ? 62 : 46);
                    rr(ex, ry + 3, 32, PANEL_ROW - 7, 7, C_ACCBG);
                    txt("Edit", ex + 5, ry + 4, 12, C_ACC);
                }
            }
            if (A.rdrag && cb_mouse_down(CB_MOUSE_LEFT) && row_pos(cb_mouse_x(), cb_mouse_y()) == w && A.rdrag - 1 != v)   /* the drop target */
                cb_draw_rect_lines(px + 3, ry, PANEL_W - 6, PANEL_ROW - 1, C_ACC);
        }
    }
    w = cb_screen_width();
    {   /* the mode, as a chip under the rows */
        const char *ml = A.mode == MODE_OBJECT ? "Object mode" : "Mesh mode";
        int cy = PANEL_TOP + 8 + (A.nrows + 1) * PANEL_ROW;
        rr(px + 8, cy - 2, tw_(ml, 13) + 30, 19, 9, C_FIELD);
        cb_draw_circle(px + 17, cy + 7, 3.0, A.mode == MODE_OBJECT ? C_OBJ : C_ACC);
        txt(ml, px + 26, cy, 13, ink);
    }
    txt(A.status, 8, h - 42, 16, ink);
    {   /* the tool bar */
        int mx = cb_mouse_x(), my = cb_mouse_y(), i;
        for (i = 0; i < TOOL_N; i++) {
            int y = TB_Y + i * (TB_H + TB_GAP), on = A.tool == i, hot = tool_at(mx, my) == i;
            rr(TB_X - 1, y - 1, TB_W + 2, TB_H + 2, 5, on ? C_ACC : C_FRAME);
            rr(TB_X, y, TB_W, TB_H, 4, on ? C_ACCBG : (hot ? C_FIELDH : C_PANEL));
            txt(TOOL_NAME[i], TB_X + (TB_W - tw_(TOOL_NAME[i], 14)) / 2, y + (TB_H - 14) / 2 + 1, 14, on ? C_ACC : ink);
        }
    }
    {   /* the view bar, top centre */
        int hot = xray_btn_at(cb_mouse_x(), cb_mouse_y()), bx = vbar_x();
        rr(bx - 1, VB_Y - 1, VB_BW + 2, VB_H + 2, 5, A.xray ? C_ACC : C_FRAME);
        rr(bx, VB_Y, VB_BW, VB_H, 4, A.xray ? C_ACCBG : (hot ? C_FIELDH : C_PANEL));
        txt("X-ray", bx + (VB_BW - tw_("X-ray", 14)) / 2, VB_Y + (VB_H - 14) / 2 + 1, 14, A.xray ? C_ACC : ink);
    }
    draw_tabs();
    draw_ws_tabs();
    draw_gizmo();
    if (A.lp && A.boxing) {   /* the selection box being dragged */
        int x0 = A.px0 < cb_mouse_x() ? A.px0 : cb_mouse_x(), y0 = A.py0 < cb_mouse_y() ? A.py0 : cb_mouse_y();
        int bw = abs(cb_mouse_x() - A.px0), bh = abs(cb_mouse_y() - A.py0);
        cb_draw_rect(x0, y0, bw, bh, (cb_rgba){255, 190, 90, 40});
        cb_draw_rect_lines(x0, y0, bw, bh, (cb_rgba){255, 200, 110, 255});
    }
    {   /* frame-rate readout, right end of the top bar (cb_fps = the window's measured rate) */
        char fb[64];
        snprintf(fb, sizeof fb, "%d fps  %.1f ms  %dx%d", cb_fps(), (double)cb_frame_time() * 1000.0, w, h);
        txt(fb, w - 190, 6, 14, dim);
    }
    draw_ui();
    if (A.xf) txt("move mouse   X Y Z: axis   type a number   Enter / click: ok   Esc / right click: cancel", 8, h - 20, 14, dim);
    else if (A.mode == MODE_OBJECT)
        txt("Tab: Mesh mode   click / drag box / A: select   Shift+A: add   X: delete   W: tool   G R S   hold right: look + WASD   Ctrl+Z   F: frame",
                     8, h - 20, 14, dim);
    else
        txt("Tab: Object   1 2 3: vert/edge/face   click / drag box   A / Alt+A   W: tool   G R S   E: extrude   Shift+A: add   Ctrl+Z",
                     8, h - 20, 14, dim);
    take_shot();
    pr_end(PR_HUD);
}

void mf_app_shutdown(void) {
    int i;
    if (A.ready && A.frame > 30 && A.last_t > 1.0) printf("[meshforge] avg fps %.1f (%d frames in %.1f s)\n", (double)A.frame / A.last_t, A.frame, A.last_t);   /* cb_game_time is 0 once the window is closed */
    if (A.ready) printf("[meshforge] camera yaw %.2f pitch %.2f dist %.2f target %.2f %.2f %.2f\n", (double)A.yaw, (double)A.pitch, (double)A.dist,
                        (double)A.target.x, (double)A.target.y, (double)A.target.z);
    if (PR_ON > 0 && A.frame > 0) printf("[meshforge] prof (ms per frame, CPU): update %.2f (visibility %.2f, render rebuild %.2f)  draw3d %.2f (surface %.2f)  hud %.2f\n",
        PR_T[PR_UPDATE] * 1000.0 / A.frame, PR_T[PR_VIS] * 1000.0 / A.frame, PR_T[PR_BUILD] * 1000.0 / A.frame, PR_T[PR_DRAW3D] * 1000.0 / A.frame, PR_T[PR_SURF] * 1000.0 / A.frame, PR_T[PR_HUD] * 1000.0 / A.frame);
    if (A.captured) { cb_cursor(0); A.captured = 0; }
    xf_free();
    close_add_panel();
    mf_shader_shutdown();
    mf_font_unload();
    free(A.mask); A.mask = NULL; A.mask_cap = 0;
    free(A.wv); A.wv = NULL; free(A.sxy); A.sxy = NULL; A.wcap = 0; free(A.front); A.front = NULL; A.fcap = 0;
    vis_stop(); free(A.want); A.want = NULL; A.want_cap = 0;
    free(A.vis); A.vis = NULL; A.vis_cap = 0;
    for (i = 0; i < A.nundo; i++) snap_free(A.undo[i]);
    A.nundo = 0;
    if (A.baked) { mf_free(A.mesh); A.baked = 0; }
    for (i = 0; i < A.nobj; i++) mf_free(A.o[i].mesh);
    A.nobj = 0;
    A.mesh = NULL;
    A.ready = 0;
}
