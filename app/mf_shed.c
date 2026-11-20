#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cbricks.h"
#include "mf_shed.h"
#include "mf_font.h"
#include "mf_json.h"

#define MAX_DEFS 64
#define MAX_NODES 256
#define MAX_WIRES 1024
#define NODE_W 190
#define HEAD_H 26
#define ROW_H 24

typedef struct { char name[24], type[8]; double dv[4]; int dn, raw; } port;
typedef struct { char key[32], title[40], group[16], kinds[4]; port in[8], out[8]; int nin, nout; } def;
typedef struct { int id; char type[32], t[8]; double x, y; const def *d; double v[8][4]; int vn[8]; int sel; } node;   /* vn[i] = numbers the file gives input i (0: the table default) */
typedef struct { int from, to; char out[24], in[24]; } wire;

static def defs[MAX_DEFS];
static int ndefs = 0;
static node nodes[MAX_NODES];
static wire wires[MAX_WIRES];
static int nnodes = 0, nwires = 0;
static char note[700] = "";
static char path[512] = "", kind_text[16] = "surface";
static unsigned long saved_hash = 0;
static int have_saved = 0, sel_id = 0, drag = -1, drag_moved = 0, panning = 0;
static double drag_dx = 0, drag_dy = 0;
static int marquee = 0;   /* shift-drag on empty space: a selection box, from (mq_x, mq_y) in world units */
static double mq_x = 0, mq_y = 0;
static char open_request[560] = "";   /* a new file the workspace should switch to */
static int wd_on = 0, wd_src = 0, wd_changed = 0;   /* a wire being dragged: its source node id and output name */
static char wd_out[24];
static int pal_open = 0, pal_len = 0, pal_scroll = 0;   /* the add-node palette (right click) */
static double pal_sx = 0, pal_sy = 0, pal_wx = 0, pal_wy = 0;
static char pal_search[32];
static int edit_node = -1, edit_in = -1, edit_len = 0;
static char edit_buf[64];
static int have_graph = 0, fitted = 0, fit_frames = 0;   /* fit_frames: keeps fitting while the window settles at start */
static double pan_x = 0, pan_y = 0, zoom = 1;   /* screen = (world - pan) * zoom + panel origin */
static int px = 0, py = 0, pw = 100, ph = 100;

static cb_rgba cb_rgb(int r, int g, int b) { cb_rgba c; c.r = (unsigned char)r; c.g = (unsigned char)g; c.b = (unsigned char)b; c.a = 255; return c; }

static const def *find_def(const char *key) {
    int i;
    for (i = 0; i < ndefs; i++) if (strcmp(defs[i].key, key) == 0) return &defs[i];
    return NULL;
}

/* The C-Vert / C-Bricks look (same values as mf_app.c's panels): near-black panels, 1 px frame, light-blue accent, rounded corners. */
#define T_BG    ((cb_rgba){22, 24, 29, 255})
#define T_DOT   ((cb_rgba){44, 48, 58, 255})
#define T_PANEL ((cb_rgba){27, 29, 35, 255})
#define T_FRAME ((cb_rgba){58, 62, 74, 255})
#define T_HEAD  ((cb_rgba){38, 42, 52, 255})
#define T_FIELD ((cb_rgba){45, 50, 62, 255})
#define T_FIELDH ((cb_rgba){60, 67, 84, 255})
#define T_ACC   ((cb_rgba){112, 186, 250, 255})
#define T_ACCBG ((cb_rgba){36, 64, 92, 255})
#define T_INK   ((cb_rgba){226, 229, 236, 255})
#define T_DIM   ((cb_rgba){138, 145, 160, 255})
#define T_WARN  ((cb_rgba){255, 166, 77, 255})
static void rrect(int x, int y, int w, int h, int r, cb_rgba c) {   /* a rounded rectangle */
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
static void framed(int x, int y, int w, int h, int r, cb_rgba fill, cb_rgba edge) { rrect(x, y, w, h, r, edge); rrect(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : r, fill); }
static void ui_text(const char *t, int x, int y, int size, cb_rgba c) {   /* the smooth UI font when it loaded */
    if (mf_font_ok()) mf_font_draw(t, x, y - 1, size, c.r, c.g, c.b, c.a);
    else cb_draw_text(t, x, y, size, c);
}
static int ui_width(const char *t, int size) { return mf_font_ok() ? mf_font_width(t, size) : (int)strlen(t) * (size > 14 ? 8 : 7); }

static void copy(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src ? src : ""); }

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    char *t;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    t = (char *)malloc((size_t)(n > 0 ? n : 0) + 1);
    if (t) { n = (long)fread(t, 1, (size_t)(n > 0 ? n : 0), f); t[n] = 0; }
    fclose(f);
    return t;
}

void mf_shed_init(const char *dir) {
    char path[600];
    char *text = NULL;
    mf_jv *root;
    const mf_jv *tab;
    int i, k;
    const char *tries[4] = { "assets/shader_nodes.json", "shader_nodes.json", "%s/assets/shader_nodes.json", "%s/../../assets/shader_nodes.json" };
    for (i = 0; i < 4 && !text; i++) {
        snprintf(path, sizeof path, tries[i], dir);
        text = slurp(path);
    }
    if (!text) { snprintf(note, sizeof note, "shader_nodes.json not found: the canvas has no node table"); return; }
    root = mf_json_parse(text);
    free(text);
    tab = mf_json_get(root, "nodes");
    if (!tab || tab->t != 4) { snprintf(note, sizeof note, "shader_nodes.json is not a node table"); mf_json_free(root); return; }
    for (i = 0; i < tab->count && ndefs < MAX_DEFS; i++) {
        def *d = &defs[ndefs++];
        const mf_jv *n = &tab->kids[i];
        memset(d, 0, sizeof *d);
        copy(d->key, sizeof d->key, tab->keys[i]);
        copy(d->title, sizeof d->title, mf_json_str(mf_json_get(n, "title"), tab->keys[i]));
        copy(d->group, sizeof d->group, mf_json_str(mf_json_get(n, "group"), ""));
        copy(d->kinds, sizeof d->kinds, mf_json_str(mf_json_get(n, "kinds"), "sp"));
        for (k = 0; k < 2; k++) {
            const mf_jv *list = mf_json_get(n, k ? "out" : "in");
            int j, cnt = list && list->t == 3 ? list->count : 0;
            for (j = 0; j < cnt && j < 8; j++) {
                port *p = k ? &d->out[j] : &d->in[j];
                copy(p->name, sizeof p->name, mf_json_str(mf_json_at(mf_json_at(list, j), 0), "?"));
                copy(p->type, sizeof p->type, mf_json_str(mf_json_at(mf_json_at(list, j), 1), "float"));
                if (!k) {
                    const mf_jv *dv = mf_json_at(mf_json_at(list, j), 2);
                    int q;
                    if (dv && dv->t == 3) { p->dn = dv->count > 4 ? 4 : dv->count; for (q = 0; q < p->dn; q++) p->dv[q] = mf_json_num(mf_json_at(dv, q), 0); }
                    else p->raw = 1;
                }
            }
            if (k) d->nout = cnt > 8 ? 8 : cnt; else d->nin = cnt > 8 ? 8 : cnt;
        }
    }
    mf_json_free(root);
}

static int node_rows(const node *n) { int r = n->d ? (n->d->nin > n->d->nout ? n->d->nin : n->d->nout) : 1; return r < 1 ? 1 : r; }
static double node_h(const node *n) { return HEAD_H + node_rows(n) * ROW_H + 6; }

static void fit(void) {
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9, zx, zy;
    int i;
    if (!nnodes) return;
    for (i = 0; i < nnodes; i++) {
        if (nodes[i].x < x0) x0 = nodes[i].x;
        if (nodes[i].y < y0) y0 = nodes[i].y;
        if (nodes[i].x + NODE_W > x1) x1 = nodes[i].x + NODE_W;
        if (nodes[i].y + node_h(&nodes[i]) > y1) y1 = nodes[i].y + node_h(&nodes[i]);
    }
    zx = (pw - 60.0) / (x1 - x0);
    zy = (ph - 60.0) / (y1 - y0);
    zoom = zx < zy ? zx : zy;
    if (zoom > 1.2) zoom = 1.2;
    if (zoom < 0.15) zoom = 0.15;
    pan_x = (x0 + x1) / 2 - pw / 2.0 / zoom;
    pan_y = (y0 + y1) / 2 - ph / 2.0 / zoom;
}

static unsigned long hash_of(const char *t) {
    unsigned long h = 5381;
    for (; *t; t++) h = h * 33 + (unsigned char)*t;
    return h;
}

/* Undo / redo: a snapshot of the graph is kept for the state after each saved edit; undo goes back one. */
typedef struct { int nn, nw; node *n; wire *w; char kind[16]; } snap;
#define HIST 60
static snap undo_s[HIST], redo_s[HIST], last_s;
static int undo_n = 0, redo_n = 0, have_last = 0;
static int nsel(void);
static void clear_sel(void);

static void snap_free(snap *s) { free(s->n); free(s->w); s->n = NULL; s->w = NULL; s->nn = s->nw = 0; }
static snap snap_take(void) {
    snap s;
    s.nn = nnodes; s.nw = nwires;
    s.n = (node *)malloc(sizeof(node) * (size_t)(nnodes ? nnodes : 1));
    s.w = (wire *)malloc(sizeof(wire) * (size_t)(nwires ? nwires : 1));
    if (s.n) memcpy(s.n, nodes, sizeof(node) * (size_t)nnodes);
    if (s.w) memcpy(s.w, wires, sizeof(wire) * (size_t)nwires);
    memcpy(s.kind, kind_text, sizeof s.kind);
    return s;
}
static void snap_apply(const snap *s) {
    int i;
    nnodes = s->nn; nwires = s->nw;
    if (s->n) memcpy(nodes, s->n, sizeof(node) * (size_t)nnodes);
    if (s->w) memcpy(wires, s->w, sizeof(wire) * (size_t)nwires);
    memcpy(kind_text, s->kind, sizeof kind_text);
    for (i = 0; i < nnodes; i++) nodes[i].d = find_def(nodes[i].type);   /* the table does not change, but the pointer is cheap to redo */
    edit_node = -1; drag = -1; sel_id = 0;
}
static void push(snap *stack, int *n, snap s) {
    if (*n >= HIST) { snap_free(&stack[0]); memmove(&stack[0], &stack[1], sizeof(snap) * (HIST - 1)); (*n)--; }
    stack[(*n)++] = s;
}
static void hist_reset(void) {
    while (undo_n > 0) snap_free(&undo_s[--undo_n]);
    while (redo_n > 0) snap_free(&redo_s[--redo_n]);
    if (have_last) snap_free(&last_s);
    last_s = snap_take();
    have_last = 1;
}

void mf_shed_load(const char *text) {
    mf_jv *root;
    const mf_jv *ns, *ws_;
    int i;
    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') text++;
    if (have_saved && have_graph && hash_of(text) == saved_hash) return;   /* our own save coming back: the canvas already shows it */
    if (*text != '{') {   /* a GLSL file: there is no graph to show */
        nnodes = nwires = 0;
        have_graph = 0;
        snprintf(note, sizeof note, "this is a GLSL file: it has no node graph");
        return;
    }
    root = mf_json_parse(text);
    if (!root || !mf_json_get(root, "nodes")) {
        snprintf(note, sizeof note, "the file is not a shader graph (kept the last good one on the canvas)");
        mf_json_free(root);
        return;
    }
    ns = mf_json_get(root, "nodes");
    ws_ = mf_json_get(root, "wires");
    copy(kind_text, sizeof kind_text, mf_json_str(mf_json_get(root, "kind"), "surface"));
    edit_node = -1; drag = -1;
    nnodes = nwires = 0;
    for (i = 0; ns && ns->t == 3 && i < ns->count && nnodes < MAX_NODES; i++) {
        const mf_jv *n = &ns->kids[i];
        node *o = &nodes[nnodes++];
        const mf_jv *pos = mf_json_get(n, "pos");
        o->id = (int)mf_json_num(mf_json_get(n, "id"), 0);
        copy(o->type, sizeof o->type, mf_json_str(mf_json_get(n, "type"), ""));
        copy(o->t, sizeof o->t, mf_json_str(mf_json_get(n, "t"), ""));
        o->x = mf_json_num(mf_json_at(pos, 0), 0);
        o->y = mf_json_num(mf_json_at(pos, 1), 0);
        o->d = find_def(o->type);
        memset(o->vn, 0, sizeof o->vn);
        o->sel = 0;
        if (o->d) {
            int q;
            for (q = 0; q < o->d->nin; q++) {
                const mf_jv *vals = mf_json_get(mf_json_get(n, "values"), o->d->in[q].name);
                if (vals && vals->t == 3 && vals->count > 0) {
                    int c;
                    o->vn[q] = vals->count > 4 ? 4 : vals->count;
                    for (c = 0; c < o->vn[q]; c++) o->v[q][c] = mf_json_num(mf_json_at(vals, c), 0);
                }
            }
        }
    }
    for (i = 0; ws_ && ws_->t == 3 && i < ws_->count && nwires < MAX_WIRES; i++) {
        const mf_jv *w = &ws_->kids[i];
        wire *o = &wires[nwires++];
        o->from = (int)mf_json_num(mf_json_get(w, "from"), 0);
        o->to = (int)mf_json_num(mf_json_get(w, "to"), 0);
        copy(o->out, sizeof o->out, mf_json_str(mf_json_get(w, "out"), ""));
        copy(o->in, sizeof o->in, mf_json_str(mf_json_get(w, "in"), ""));
    }
    mf_json_free(root);
    have_graph = 1;
    sel_id = 0;
    hist_reset();
    note[0] = 0;
    if (!fitted) { fitted = 2; }   /* fit on the first frame that knows the panel's size */
}

static const node *by_id(int id) {
    int i;
    for (i = 0; i < nnodes; i++) if (nodes[i].id == id) return &nodes[i];
    return NULL;
}

static double sx(double wx) { return px + (wx - pan_x) * zoom; }
static double sy(double wy) { return py + (wy - pan_y) * zoom; }

static cb_rgba type_color(const char *t) {
    if (strcmp(t, "float") == 0) return cb_rgb(150, 190, 255);
    if (strcmp(t, "vec2") == 0) return cb_rgb(120, 230, 170);
    if (strcmp(t, "vec3") == 0) return cb_rgb(255, 210, 90);
    if (strcmp(t, "vec4") == 0) return cb_rgb(255, 130, 130);
    return cb_rgb(200, 200, 200);
}

/* The type a port really has on this node: "gen" ports follow the node's t (float when it has none). */
static const char *port_type(const node *n, const port *p) { return strcmp(p->type, "gen") == 0 ? (n->t[0] ? n->t : "float") : p->type; }

static int port_index(const node *n, const char *name, int out) {
    int i, cnt = n->d ? (out ? n->d->nout : n->d->nin) : 0;
    for (i = 0; i < cnt; i++) if (strcmp(out ? n->d->out[i].name : n->d->in[i].name, name) == 0) return i;
    return -1;
}

static void port_pos(const node *n, int out, int idx, double *x, double *y) {
    *x = sx(n->x + (out ? NODE_W : 0));
    *y = sy(n->y + HEAD_H + idx * ROW_H + ROW_H / 2.0);
}

static void text(const char *s, double x, double y, double size, cb_rgba c) {
    int sz = (int)(size * zoom + 0.5);
    if (sz >= 8) ui_text(s, (int)x, (int)y, sz, c);
}

static void draw_wire(double x0, double y0, double x1, double y1, cb_rgba c) {
    double dx = fabs(x1 - x0) * 0.5 + 20 * zoom, px_ = x0, py_ = y0;
    int i;
    for (i = 1; i <= 24; i++) {
        double t = i / 24.0, u = 1 - t;
        double bx = u * u * u * x0 + 3 * u * u * t * (x0 + dx) + 3 * u * t * t * (x1 - dx) + t * t * t * x1;
        double by = u * u * u * y0 + 3 * u * u * t * y0 + 3 * u * t * t * y1 + t * t * t * y1;
        cb_draw_line((int)px_, (int)py_, (int)bx, (int)by, c);
        cb_draw_line((int)px_, (int)py_ + 1, (int)bx, (int)by + 1, c);
        px_ = bx; py_ = by;
    }
}

static int ncomp(const char *t) { return strcmp(t, "vec2") == 0 ? 2 : strcmp(t, "vec3") == 0 ? 3 : strcmp(t, "vec4") == 0 ? 4 : 1; }

static int is_wired(const node *n, const char *in) {
    int i;
    for (i = 0; i < nwires; i++) if (wires[i].to == n->id && strcmp(wires[i].in, in) == 0) return 1;
    return 0;
}

/* The numbers of an unwired input: the file's, else the table's default (raw GLSL defaults have none and can't be edited). */
static int value_of(const node *n, int i, double out[4]) {
    const port *p = &n->d->in[i];
    int c, cnt;
    if (n->vn[i] > 0) { cnt = n->vn[i]; for (c = 0; c < cnt; c++) out[c] = n->v[i][c]; return cnt; }
    if (p->raw || p->dn == 0) return 0;
    for (c = 0; c < p->dn; c++) out[c] = p->dv[c];
    return p->dn;
}

static void value_text(const node *n, int i, char *buf, size_t sz) {
    double v[4];
    int c, cnt = value_of(n, i, v);
    size_t used = 0;
    buf[0] = 0;
    if (!cnt) { snprintf(buf, sz, "(code)"); return; }
    for (c = 0; c < cnt && used + 12 < sz; c++) used += (size_t)snprintf(buf + used, sz - used, c ? ", %.4g" : "%.4g", v[c]);
}

/* The editable value box of input i: world-space x offset, width. Row i, between the input's name and the output names. */
#define FIELD_X 52
#define FIELD_W 86

static int node_at(double mx, double my) {
    int i;
    for (i = nnodes - 1; i >= 0; i--) {
        double x = sx(nodes[i].x), y = sy(nodes[i].y);
        if (mx >= x && mx < x + NODE_W * zoom && my >= y && my < y + node_h(&nodes[i]) * zoom) return i;
    }
    return -1;
}

static int field_at(const node *n, double mx, double my) {
    int i;
    if (!n->d) return -1;
    for (i = 0; i < n->d->nin; i++) {
        double x = sx(n->x + FIELD_X), y = sy(n->y + HEAD_H + i * ROW_H + 3), w = FIELD_W * zoom, h = (ROW_H - 6) * zoom;
        double v[4];
        if (is_wired(n, n->d->in[i].name) || !value_of(n, i, v)) continue;
        if (mx >= x && mx < x + w && my >= y && my < y + h) return i;
    }
    return -1;
}

static void put(char **buf, size_t *cap, size_t *len, const char *fmt, ...) {
    va_list ap;
    char tmp[256];
    size_t n;
    va_start(ap, fmt);
    n = (size_t)vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n >= sizeof tmp) n = sizeof tmp - 1;
    if (*len + n + 1 > *cap) { *cap = (*len + n + 1) * 2; *buf = (char *)realloc(*buf, *cap); }
    memcpy(*buf + *len, tmp, n);
    *len += n;
    (*buf)[*len] = 0;
}

/* The whole graph as .cbshader text (kind and version kept; a node's own order, values only where the file set them). */
static char *graph_text(void) {
    char *b = (char *)malloc(1024);
    size_t cap = 1024, len = 0;
    int i, q, c;
    if (!b) return NULL;
    b[0] = 0;
    put(&b, &cap, &len, "{\n\t\"cbshader\": 1,\n\t\"kind\": \"%s\",\n\t\"nodes\": [", kind_text);
    for (i = 0; i < nnodes; i++) {
        const node *n = &nodes[i];
        int any = 0;
        put(&b, &cap, &len, "%s\n\t\t{\"id\": %d, \"type\": \"%s\"", i ? "," : "", n->id, n->type);
        if (n->t[0]) put(&b, &cap, &len, ", \"t\": \"%s\"", n->t);
        put(&b, &cap, &len, ", \"pos\": [%.6g, %.6g]", n->x, n->y);
        if (n->d) {
            for (q = 0; q < n->d->nin; q++) {
                if (n->vn[q] <= 0) continue;
                put(&b, &cap, &len, "%s\"%s\": [", any ? ", " : ", \"values\": {", n->d->in[q].name);
                any = 1;
                for (c = 0; c < n->vn[q]; c++) put(&b, &cap, &len, c ? ", %.6g" : "%.6g", n->v[q][c]);
                put(&b, &cap, &len, "]");
            }
            if (any) put(&b, &cap, &len, "}");
        }
        put(&b, &cap, &len, "}");
    }
    put(&b, &cap, &len, "\n\t],\n\t\"wires\": [");
    for (i = 0; i < nwires; i++)
        put(&b, &cap, &len, "%s\n\t\t{\"from\": %d, \"out\": \"%s\", \"to\": %d, \"in\": \"%s\"}", i ? "," : "", wires[i].from, wires[i].out, wires[i].to, wires[i].in);
    put(&b, &cap, &len, "\n\t]\n}\n");
    return b;
}

static void write_file(void) {
    char tmp[560];
    char *text;
    FILE *f;
    if (!have_graph || !*path) return;
    text = graph_text();
    if (!text) return;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) { snprintf(note, sizeof note, "could not save %s", path); free(text); return; }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    if (rename(tmp, path) != 0) { snprintf(note, sizeof note, "could not save %s", path); remove(tmp); free(text); return; }
    saved_hash = hash_of(text);
    have_saved = 1;
    note[0] = 0;
    free(text);
}

/* Written whenever an edit is finished (a drag ends, a value is confirmed): the preview reloads from the file itself. */
static void save(void) {
    if (!have_graph || !*path) return;
    write_file();
    if (have_last) push(undo_s, &undo_n, last_s);
    while (redo_n > 0) snap_free(&redo_s[--redo_n]);
    last_s = snap_take();
    have_last = 1;
}

static void undo(void) {
    if (undo_n == 0 || !have_last) return;
    push(redo_s, &redo_n, last_s);
    last_s = undo_s[--undo_n];
    snap_apply(&last_s);
    write_file();
}

static void redo(void) {
    if (redo_n == 0 || !have_last) return;
    push(undo_s, &undo_n, last_s);
    last_s = redo_s[--redo_n];
    snap_apply(&last_s);
    write_file();
}

void mf_shed_set_path(const char *p) {
    snprintf(path, sizeof path, "%s", p ? p : "");
    have_saved = 0; have_graph = 0; fitted = 0; sel_id = 0; nnodes = nwires = 0;
    hist_reset();
}
const char *mf_shed_take_open(void) { static char out[560]; snprintf(out, sizeof out, "%s", open_request); open_request[0] = 0; return out; }
int mf_shed_editing(void) { return edit_node >= 0 || pal_open; }
static char pending[32];
static int npending = 0;
void mf_shed_type(const char *chars, int n) { int i; for (i = 0; i < n && npending < (int)sizeof pending; i++) pending[npending++] = chars[i]; }

static void start_edit(int ni, int in) {
    edit_node = ni; edit_in = in;
    value_text(&nodes[ni], in, edit_buf, sizeof edit_buf);
    edit_len = (int)strlen(edit_buf);
    printf("[shed] editing %s.%s\n", nodes[ni].type, nodes[ni].d->in[in].name);
}

/* Confirm: the typed numbers (commas or spaces between, at most the port's components) become the file's values. */
static void commit_edit(int keep) {
    node *n;
    double v[4];
    int cnt = 0;
    char *p, *end;
    if (edit_node < 0) return;
    n = &nodes[edit_node];
    printf("[shed] %s \"%s\"\n", keep ? "confirm" : "cancel", edit_buf);
    if (keep) {
        p = edit_buf;
        while (cnt < 4) {
            double d;
            while (*p == ' ' || *p == ',') p++;
            if (!*p) break;
            d = strtod(p, &end);
            if (end == p) break;
            v[cnt++] = d;
            p = end;
        }
        if (cnt > ncomp(port_type(n, &n->d->in[edit_in]))) cnt = ncomp(port_type(n, &n->d->in[edit_in]));
        if (cnt > 0) {
            int c;
            n->vn[edit_in] = cnt;
            for (c = 0; c < cnt; c++) n->v[edit_in][c] = v[c];
            save();
        }
    }
    edit_node = -1;
}


static int is_gen(const def *d) {
    int i;
    for (i = 0; i < d->nin; i++) if (strcmp(d->in[i].type, "gen") == 0) return 1;
    for (i = 0; i < d->nout; i++) if (strcmp(d->out[i].type, "gen") == 0) return 1;
    return 0;
}

/* The port under the cursor: node index; *out = 1 for an output, *idx its index. */
static int port_at(double mx, double my, int *out, int *idx) {
    int i, k, q;
    double r = 7 * zoom < 8 ? 8 : 7 * zoom;
    for (i = nnodes - 1; i >= 0; i--) {
        const node *n = &nodes[i];
        if (!n->d) continue;
        for (k = 0; k < 2; k++) {
            int cnt = k ? n->d->nout : n->d->nin;
            for (q = 0; q < cnt; q++) {
                double ax, ay;
                port_pos(n, k, q, &ax, &ay);
                if (fabs(mx - ax) <= r && fabs(my - ay) <= r) { *out = k; *idx = q; return i; }
            }
        }
    }
    return -1;
}

/* Would a wire from node `from` into node `to` close a loop? (does `to` already feed `from`?) */
static int reaches(int cur, int target, int depth) {
    int i;
    if (cur == target) return 1;
    if (depth > 64) return 1;
    for (i = 0; i < nwires; i++) if (wires[i].from == cur && reaches(wires[i].to, target, depth + 1)) return 1;
    return 0;
}

static void remove_wire(int i) { memmove(&wires[i], &wires[i + 1], (size_t)(nwires - i - 1) * sizeof wires[0]); nwires--; }

static void remove_node(int id) {
    int i;
    for (i = nwires - 1; i >= 0; i--) if (wires[i].from == id || wires[i].to == id) remove_wire(i);
    for (i = 0; i < nnodes; i++) if (nodes[i].id == id) { memmove(&nodes[i], &nodes[i + 1], (size_t)(nnodes - i - 1) * sizeof nodes[0]); nnodes--; break; }
}

/* Wire from (src, out) into (to, in): the input holds one wire, so an older one is replaced. Returns 1 when added. */
static int connect(int src, const char *out, int to, const char *in) {
    int i;
    if (src == to || reaches(to, src, 0)) { snprintf(note, sizeof note, "that wire would make a loop"); return 0; }
    for (i = nwires - 1; i >= 0; i--) if (wires[i].to == to && strcmp(wires[i].in, in) == 0) remove_wire(i);
    if (nwires >= MAX_WIRES) return 0;
    wires[nwires].from = src; wires[nwires].to = to;
    copy(wires[nwires].out, sizeof wires[nwires].out, out);
    copy(wires[nwires].in, sizeof wires[nwires].in, in);
    nwires++;
    return 1;
}

static const char *TYPES[4] = { "float", "vec2", "vec3", "vec4" };

static void cycle_type(node *n) {
    int k = 0, q;
    while (k < 3 && strcmp(n->t[0] ? n->t : "float", TYPES[k]) != 0) k++;
    copy(n->t, sizeof n->t, TYPES[(k + 1) % 4]);
    for (q = 0; q < 8; q++) n->vn[q] = 0;   /* the old numbers had the old size: back to the defaults */
}

static const char *pal_kind(void) { return strcmp(kind_text, "post") == 0 ? "p" : "s"; }

static int has_type(const char *type) { int i; for (i = 0; i < nnodes; i++) if (strcmp(nodes[i].type, type) == 0) return 1; return 0; }

static int ci_has(const char *hay, const char *needle) {
    size_t n = strlen(needle), i, j;
    if (!n) return 1;
    for (i = 0; hay[i]; i++) {
        for (j = 0; j < n && hay[i + j]; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (j == n) return 1;
    }
    return 0;
}

#define PAL_W 230
#define PAL_ROW 20
typedef struct { int def; const char *group; } pal_entry;   /* def < 0: a group heading */
static pal_entry pal_e[MAX_DEFS * 2];
static int pal_n = 0;

static void pal_build(void) {
    int i, j;
    pal_n = 0;
    for (i = 0; i < ndefs; i++) {
        int seen = 0;
        for (j = 0; j < i; j++) if (strcmp(defs[j].group, defs[i].group) == 0) { seen = 1; break; }
        if (seen) continue;
        {
            int head = pal_n;
            pal_e[pal_n].def = -1; pal_e[pal_n].group = defs[i].group; pal_n++;
            for (j = i; j < ndefs; j++) {
                const def *d = &defs[j];
                if (strcmp(d->group, defs[i].group) != 0 || !strchr(d->kinds, pal_kind()[0])) continue;
                if (d->key[0] && strlen(d->key) > 4 && strcmp(d->key + strlen(d->key) - 4, "_out") == 0 && has_type(d->key)) continue;
                if (!ci_has(d->title, pal_search) && !ci_has(d->key, pal_search)) continue;
                pal_e[pal_n].def = j; pal_e[pal_n].group = d->group; pal_n++;
            }
            if (pal_n == head + 1) pal_n = head;   /* a heading with nothing under it */
        }
    }
}

static void add_node(int di, double wx, double wy) {
    node *n;
    int i, id = 1;
    if (nnodes >= MAX_NODES) return;
    for (i = 0; i < nnodes; i++) if (nodes[i].id >= id) id = nodes[i].id + 1;
    n = &nodes[nnodes++];
    memset(n, 0, sizeof *n);
    n->id = id; n->x = floor(wx + 0.5); n->y = floor(wy + 0.5);
    copy(n->type, sizeof n->type, defs[di].key);
    n->d = &defs[di];
    if (is_gen(n->d)) copy(n->t, sizeof n->t, "float");
    clear_sel();
    n->sel = 1;
    sel_id = id;
    save();
}

static int nsel(void) { int i, c = 0; for (i = 0; i < nnodes; i++) if (nodes[i].sel) c++; return c; }
static void clear_sel(void) { int i; for (i = 0; i < nnodes; i++) nodes[i].sel = 0; sel_id = 0; }

static int shift_held(void) { return cb_key_down(CB_KEY_LEFT_SHIFT) || cb_key_down(344); }
static int ctrl_held(void) { return cb_key_down(CB_KEY_LEFT_CONTROL) || cb_key_down(345); }

static void delete_selected(void) {
    int i;
    for (i = nnodes - 1; i >= 0; i--) if (nodes[i].sel) remove_node(nodes[i].id);
    sel_id = 0; drag = -1;
    save();
}

/* The clipboard: the selected nodes and the wires between them (kept inside the program, in the file's own terms). */
static node clip_n[MAX_NODES];
static wire clip_w[MAX_WIRES];
static int clip_nn = 0, clip_nw = 0;

static void copy_selected(void) {
    int i, j;
    clip_nn = clip_nw = 0;
    for (i = 0; i < nnodes; i++) if (nodes[i].sel) clip_n[clip_nn++] = nodes[i];
    for (i = 0; i < nwires; i++) {
        int a = 0, b = 0;
        for (j = 0; j < clip_nn; j++) { if (clip_n[j].id == wires[i].from) a = 1; if (clip_n[j].id == wires[i].to) b = 1; }
        if (a && b) clip_w[clip_nw++] = wires[i];
    }
}

static void paste(double wx, double wy) {
    int i, j, next = 1;
    double x0 = 1e9, y0 = 1e9;
    int map_old[MAX_NODES], map_new[MAX_NODES], nm = 0;
    if (!clip_nn) return;
    for (i = 0; i < nnodes; i++) if (nodes[i].id >= next) next = nodes[i].id + 1;
    for (i = 0; i < clip_nn; i++) { if (clip_n[i].x < x0) x0 = clip_n[i].x; if (clip_n[i].y < y0) y0 = clip_n[i].y; }
    clear_sel();
    for (i = 0; i < clip_nn && nnodes < MAX_NODES; i++) {
        node *n;
        size_t tl = strlen(clip_n[i].type);
        if (tl > 4 && strcmp(clip_n[i].type + tl - 4, "_out") == 0 && has_type(clip_n[i].type)) continue;   /* one output node only */
        n = &nodes[nnodes++];
        *n = clip_n[i];
        n->id = next++;
        n->x = floor(wx + (clip_n[i].x - x0) + 0.5);
        n->y = floor(wy + (clip_n[i].y - y0) + 0.5);
        n->sel = 1;
        map_old[nm] = clip_n[i].id; map_new[nm] = n->id; nm++;
    }
    for (i = 0; i < clip_nw && nwires < MAX_WIRES; i++) {
        int a = 0, b = 0;
        for (j = 0; j < nm; j++) { if (map_old[j] == clip_w[i].from) a = map_new[j]; if (map_old[j] == clip_w[i].to) b = map_new[j]; }
        if (a && b) { wires[nwires] = clip_w[i]; wires[nwires].from = a; wires[nwires].to = b; nwires++; }
    }
    if (nm) { sel_id = map_new[nm - 1]; save(); }
}

/* A new graph next to the open file: its own name (untitled.cbshader, untitled2...), just the output node. The workspace switches to it. */
static void new_file(int post) {
    char dir[512], file[560];
    char *slash;
    int k;
    FILE *f;
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash) slash[1] = 0; else dir[0] = 0;
    for (k = 1; k < 1000; k++) {
        if (k == 1) snprintf(file, sizeof file, "%suntitled.cbshader", dir); else snprintf(file, sizeof file, "%suntitled%d.cbshader", dir, k);
        f = fopen(file, "rb");
        if (!f) break;
        fclose(f);
    }
    f = fopen(file, "wb");
    if (!f) { snprintf(note, sizeof note, "could not create %s", file); return; }
    fprintf(f, "{\n\t\"cbshader\": 1,\n\t\"kind\": \"%s\",\n\t\"nodes\": [\n\t\t{\"id\": 1, \"type\": \"%s\", \"pos\": [300, 100]}\n\t],\n\t\"wires\": [\n\t]\n}\n", post ? "post" : "surface", post ? "post_out" : "surface_out");
    fclose(f);
    snprintf(open_request, sizeof open_request, "%s", file);
}

#define BTN_W 96
#define BTN_H 22
static int btn_hit(int which, double mx, double my) {   /* the two New buttons at the canvas' top right */
    double bx = px + pw - 10 - (2 - which) * (BTN_W + 6), by = py + 8;
    return mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H;
}

static void draw_node(const node *n) {
    double x = sx(n->x), y = sy(n->y), w = NODE_W * zoom, h = node_h(n) * zoom;
    int i;
    if (x > px + pw || y > py + ph || x + w < px || y + h < py) return;
    {
        int rad = (int)(5 * zoom + 0.5), hh = (int)(HEAD_H * zoom);
        framed((int)x, (int)y, (int)w, (int)h, rad, T_PANEL, n->sel ? T_ACC : T_FRAME);
        rrect((int)x + 1, (int)y + 1, (int)w - 2, hh, rad > 1 ? rad - 1 : rad, n->d ? (n->sel ? T_ACCBG : T_HEAD) : (cb_rgba){92, 40, 40, 255});
        cb_draw_rect((int)x + 1, (int)y + hh / 2, (int)w - 2, hh - hh / 2, n->d ? (n->sel ? T_ACCBG : T_HEAD) : (cb_rgba){92, 40, 40, 255});   /* square its lower edge */
        cb_draw_rect((int)x + 1, (int)y + hh, (int)w - 2, 1, n->sel ? T_ACC : T_FRAME);
    }
    text(n->d ? n->d->title : n->type, x + 8 * zoom, y + 5 * zoom, 15, T_INK);
    if (n->d && is_gen(n->d)) {   /* click it to change the node's type */
        const char *tt = n->t[0] ? n->t : "float";
        rrect((int)(x + w - 44 * zoom), (int)(y + 4 * zoom), (int)(38 * zoom), (int)((HEAD_H - 8) * zoom), 3, T_FIELD);
        text(tt, x + w - 40 * zoom, y + 6 * zoom, 12, type_color(tt));
    }
    if (!n->d) { text("unknown node type", x + 8 * zoom, y + (HEAD_H + 4) * zoom, 13, T_WARN); return; }
    for (i = 0; i < n->d->nin; i++) {
        double ax, ay;
        port_pos(n, 0, i, &ax, &ay);
        cb_draw_circle((int)ax, (int)ay, 6 * zoom, T_PANEL);
        cb_draw_circle((int)ax, (int)ay, 4.5 * zoom, type_color(port_type(n, &n->d->in[i])));
        text(n->d->in[i].name, ax + 10 * zoom, ay - 7 * zoom, 13, T_DIM);
        {
            double fv[4];
            if (!is_wired(n, n->d->in[i].name) && value_of(n, i, fv)) {
                double fx = sx(n->x + FIELD_X), fy = sy(n->y + HEAD_H + i * ROW_H + 3), fw = FIELD_W * zoom, fh = (ROW_H - 6) * zoom;
                char vt[80];
                int editing = edit_node >= 0 && &nodes[edit_node] == n && edit_in == i;
                if (editing) snprintf(vt, sizeof vt, "%s_", edit_buf); else value_text(n, i, vt, sizeof vt);
                if (editing) framed((int)fx, (int)fy, (int)fw, (int)fh, 3, T_BG, T_ACC); else rrect((int)fx, (int)fy, (int)fw, (int)fh, 3, T_FIELD);
                text(vt, fx + 4 * zoom, fy + 2 * zoom, 13, n->vn[i] > 0 ? T_INK : T_DIM);
            }
        }
    }
    for (i = 0; i < n->d->nout; i++) {
        double ax, ay;
        int tw;
        port_pos(n, 1, i, &ax, &ay);
        cb_draw_circle((int)ax, (int)ay, 6 * zoom, T_PANEL);
        cb_draw_circle((int)ax, (int)ay, 4.5 * zoom, type_color(port_type(n, &n->d->out[i])));
        tw = ui_width(n->d->out[i].name, (int)(13 * zoom + 0.5));
        text(n->d->out[i].name, ax - 10 * zoom - tw, ay - 7 * zoom, 13, T_DIM);
    }
}

void mf_shed_frame(int x, int y, int w, int h) {
    int i, over;
    px = x; py = y; pw = w; ph = h;
    over = cb_mouse_x() >= x && cb_mouse_x() < x + w && cb_mouse_y() >= y && cb_mouse_y() < y + h;
    if (fitted == 2) { fitted = 1; fit_frames = 15; }
    if (fit_frames > 0) {   /* the window may still be resizing for a few frames: fit again until it is settled or the user takes over */
        fit();
        if (--fit_frames == 0 && *cb_option("debug-nodes")) {   /* the tests click by these screen positions */
            for (i = 0; i < nnodes; i++) {
                int q;
                printf("[shed] node %d head %d %d\n", nodes[i].id, (int)(sx(nodes[i].x) + 20), (int)(sy(nodes[i].y) + 8 * zoom));
                for (q = 0; nodes[i].d && q < nodes[i].d->nin; q++) {
                    double fv[4];
                    if (!is_wired(&nodes[i], nodes[i].d->in[q].name) && value_of(&nodes[i], q, fv))
                        printf("[shed] field %d %s %d %d\n", nodes[i].id, nodes[i].d->in[q].name, (int)(sx(nodes[i].x + FIELD_X + 20)), (int)(sy(nodes[i].y + HEAD_H + q * ROW_H + ROW_H / 2.0)));
                }
            }
            for (i = 0; i < nnodes; i++) {
                int q, k;
                for (k = 0; k < 2 && nodes[i].d; k++)
                    for (q = 0; q < (k ? nodes[i].d->nout : nodes[i].d->nin); q++) {
                        double ax, ay;
                        port_pos(&nodes[i], k, q, &ax, &ay);
                        printf("[shed] port %d %s %s %d %d\n", nodes[i].id, k ? "out" : "in", k ? nodes[i].d->out[q].name : nodes[i].d->in[q].name, (int)ax, (int)ay);
                    }
            }
            printf("[shed] button surface %d %d\n", px + pw - 10 - 2 * (BTN_W + 6) + BTN_W / 2, py + 8 + BTN_H / 2);
            printf("[shed] button post %d %d\n", px + pw - 10 - (BTN_W + 6) + BTN_W / 2, py + 8 + BTN_H / 2);
            printf("[shed] zoom %g\n", zoom);
            fflush(stdout);
        }
    }
    if (over) {
        double wheel = cb_mouse_wheel();
        if (wheel != 0 || cb_mouse_pressed(0) || cb_mouse_pressed(2)) fit_frames = 0;
        if (wheel != 0) {   /* zoom about the cursor */
            double mx = cb_mouse_x(), my = cb_mouse_y();
            double wx = (mx - px) / zoom + pan_x, wy = (my - py) / zoom + pan_y;
            zoom *= wheel > 0 ? 1.12 : 1 / 1.12;
            if (zoom < 0.15) zoom = 0.15;
            if (zoom > 3) zoom = 3;
            pan_x = wx - (mx - px) / zoom;
            pan_y = wy - (my - py) / zoom;
        }
        if (pal_open && (cb_mouse_pressed(0) || cb_mouse_pressed(1))) {   /* the palette is modal: a click picks an entry or closes it */
            double mx = cb_mouse_x(), my = cb_mouse_y();
            double bh = (pal_n < 14 ? pal_n : 14) * PAL_ROW + 30;
            if (cb_mouse_pressed(0) && mx >= pal_sx && mx < pal_sx + PAL_W && my >= pal_sy + 26 && my < pal_sy + bh) {
                int row = (int)((my - pal_sy - 26) / PAL_ROW) + pal_scroll;
                if (row >= 0 && row < pal_n && pal_e[row].def >= 0) { add_node(pal_e[row].def, pal_wx, pal_wy); pal_open = 0; }
            } else if (!(mx >= pal_sx && mx < pal_sx + PAL_W && my >= pal_sy && my < pal_sy + bh)) pal_open = 0;
        } else if (cb_mouse_pressed(1) && edit_node < 0) {
            pal_open = 1; pal_len = 0; pal_search[0] = 0; pal_scroll = 0;
            pal_sx = cb_mouse_x(); pal_sy = cb_mouse_y();
            pal_wx = (pal_sx - px) / zoom + pan_x; pal_wy = (pal_sy - py) / zoom + pan_y;
            if (pal_sx + PAL_W > px + pw) pal_sx = px + pw - PAL_W;
            if (pal_sy + 14 * PAL_ROW + 30 > py + ph) pal_sy = py + ph - (14 * PAL_ROW + 30);
            if (pal_sy < py) pal_sy = py;
            pal_build();
        } else if (cb_mouse_pressed(0) && edit_node < 0 && (btn_hit(0, cb_mouse_x(), cb_mouse_y()) || btn_hit(1, cb_mouse_x(), cb_mouse_y()))) {
            new_file(btn_hit(1, cb_mouse_x(), cb_mouse_y()));
        } else if (cb_mouse_pressed(0)) {
            double mx = cb_mouse_x(), my = cb_mouse_y();
            int po, pi_, pn = port_at(mx, my, &po, &pi_);
            int ni = node_at(mx, my), fi = ni >= 0 ? field_at(&nodes[ni], mx, my) : -1;
            if (edit_node >= 0 && !(ni == edit_node && fi == edit_in)) commit_edit(1);
            if (pn >= 0 && (po || is_wired(&nodes[pn], nodes[pn].d->in[pi_].name))) {   /* start a wire (a wired input's wire is picked up) */
                int w;
                if (!nodes[pn].sel) { clear_sel(); nodes[pn].sel = 1; }
                sel_id = nodes[pn].id;
                wd_on = 1; wd_changed = 0;
                if (po) { wd_src = nodes[pn].id; copy(wd_out, sizeof wd_out, nodes[pn].d->out[pi_].name); }
                else {
                    for (w = 0; w < nwires; w++) if (wires[w].to == nodes[pn].id && strcmp(wires[w].in, nodes[pn].d->in[pi_].name) == 0) break;
                    if (w < nwires) { wd_src = wires[w].from; copy(wd_out, sizeof wd_out, wires[w].out); remove_wire(w); wd_changed = 1; }
                    else wd_on = 0;
                }
            } else if (ni >= 0) {
                if (shift_held()) { nodes[ni].sel = !nodes[ni].sel; sel_id = nodes[ni].sel ? nodes[ni].id : 0; }
                else if (!nodes[ni].sel) { clear_sel(); nodes[ni].sel = 1; sel_id = nodes[ni].id; }
                else sel_id = nodes[ni].id;
                if (shift_held()) { /* shift only changes the selection */ }
                else if (nodes[ni].d && is_gen(nodes[ni].d) && my < sy(nodes[ni].y) + HEAD_H * zoom && mx > sx(nodes[ni].x) + (NODE_W - 44) * zoom) { cycle_type(&nodes[ni]); save(); }
                else if (fi >= 0) { if (edit_node < 0) start_edit(ni, fi); }
                else { drag = ni; drag_moved = 0; drag_dx = (mx - px) / zoom + pan_x - nodes[ni].x; drag_dy = (my - py) / zoom + pan_y - nodes[ni].y; }
            } else if (shift_held()) { marquee = 1; mq_x = (mx - px) / zoom + pan_x; mq_y = (my - py) / zoom + pan_y; }
            else { clear_sel(); panning = 1; }
        }
        if (cb_mouse_down(0) && drag >= 0) {
            double mx = cb_mouse_x(), my = cb_mouse_y();
            double nx = (mx - px) / zoom + pan_x - drag_dx, ny = (my - py) / zoom + pan_y - drag_dy;
            if (floor(nx + 0.5) != nodes[drag].x || floor(ny + 0.5) != nodes[drag].y) {
                double ddx = floor(nx + 0.5) - nodes[drag].x, ddy = floor(ny + 0.5) - nodes[drag].y;
                int q;
                for (q = 0; q < nnodes; q++) if (nodes[q].sel || q == drag) { nodes[q].x += ddx; nodes[q].y += ddy; }
                drag_moved = 1;
            }
        } else if ((cb_mouse_down(0) && panning) || cb_mouse_down(2)) {
            Vector2 d = cb_mouse_motion();
            pan_x -= d.x / zoom;
            pan_y -= d.y / zoom;
        }
        if (pal_open && wheel != 0) { pal_scroll -= wheel > 0 ? 3 : -3; }
        if (edit_node < 0 && !pal_open && cb_key_pressed(CB_KEY_F)) { fit(); fit_frames = 0; }
        if (edit_node < 0 && !pal_open) {
            if (ctrl_held()) {
                if (cb_key_pressed(CB_KEY_Z)) { if (shift_held()) redo(); else undo(); }
                else if (cb_key_pressed(CB_KEY_Y)) redo();
                else if (cb_key_pressed(CB_KEY_C)) copy_selected();
                else if (cb_key_pressed(CB_KEY_X)) { copy_selected(); if (nsel()) delete_selected(); }
                else if (cb_key_pressed(CB_KEY_V)) paste((cb_mouse_x() - px) / zoom + pan_x, (cb_mouse_y() - py) / zoom + pan_y);
                else if (cb_key_pressed(CB_KEY_A)) { int q; for (q = 0; q < nnodes; q++) nodes[q].sel = 1; }
                else if (cb_key_pressed(CB_KEY_N)) new_file(shift_held());
            } else if (nsel() && (cb_key_pressed(261) || cb_key_pressed(CB_KEY_X))) delete_selected();   /* Delete or X: the selected nodes and their wires */
        }
    }
    if (!cb_mouse_down(0)) {
        if (wd_on) {
            int po, pi_, pn = port_at(cb_mouse_x(), cb_mouse_y(), &po, &pi_);
            if (pn >= 0 && !po && nodes[pn].d && connect(wd_src, wd_out, nodes[pn].id, nodes[pn].d->in[pi_].name)) wd_changed = 1;
            if (wd_changed) save();
            wd_on = 0; wd_changed = 0;
        }
        if (marquee) {   /* every node the box touches joins the selection */
            double x1 = (cb_mouse_x() - px) / zoom + pan_x, y1 = (cb_mouse_y() - py) / zoom + pan_y;
            double ax = mq_x < x1 ? mq_x : x1, bx = mq_x < x1 ? x1 : mq_x, ay = mq_y < y1 ? mq_y : y1, by = mq_y < y1 ? y1 : mq_y;
            int q;
            for (q = 0; q < nnodes; q++)
                if (nodes[q].x < bx && nodes[q].x + NODE_W > ax && nodes[q].y < by && nodes[q].y + node_h(&nodes[q]) > ay) nodes[q].sel = 1;
            marquee = 0;
        }
        if (drag >= 0 && drag_moved) save();
        drag = -1; drag_moved = 0; panning = 0;
    }
    if (edit_node < 0 && !pal_open) npending = 0;
    if (pal_open) {   /* typing filters the palette, Enter adds the first match, Esc closes */
        int ch;
        for (ch = 0; ch < npending; ch++)
            if (npending && pending[ch] >= 32 && pending[ch] < 127 && pal_len < (int)sizeof pal_search - 1) { pal_search[pal_len++] = pending[ch]; pal_search[pal_len] = 0; pal_scroll = 0; }
        npending = 0;
        if (cb_key_pressed(CB_KEY_BACKSPACE) && pal_len > 0) { pal_search[--pal_len] = 0; pal_scroll = 0; }
        pal_build();
        if (pal_scroll > pal_n - 14) pal_scroll = pal_n - 14;
        if (pal_scroll < 0) pal_scroll = 0;
        if (cb_key_pressed(CB_KEY_ENTER)) {
            int r;
            for (r = 0; r < pal_n; r++) if (pal_e[r].def >= 0) { add_node(pal_e[r].def, pal_wx, pal_wy); break; }
            pal_open = 0;
        } else if (cb_key_pressed(CB_KEY_ESCAPE)) pal_open = 0;
    }
    if (edit_node >= 0) {
        int ch;
        for (ch = 0; ch < npending; ch++) {
            int c = pending[ch];
            char buf1 = (char)c;
            if (((c >= '0' && c <= '9') || c == '.' || c == '-' || c == ',' || c == ' ' || c == 'e') && edit_len < (int)sizeof edit_buf - 2) {
                edit_buf[edit_len++] = buf1;
                edit_buf[edit_len] = 0;
            }
        }
        npending = 0;
        if (cb_key_pressed(CB_KEY_BACKSPACE) && edit_len > 0) edit_buf[--edit_len] = 0;
        if (cb_key_pressed(CB_KEY_ENTER)) commit_edit(1);
        else if (cb_key_pressed(CB_KEY_ESCAPE)) commit_edit(0);
    }
    cb_clip(x, y, w, h);
    cb_draw_rect(x, y, w, h, T_BG);
    {   /* a dotted grid every 40 world units, so panning is visible */
        double step = 40 * zoom, gx, gy;
        if (step >= 12) {
            for (gx = fmod(px - pan_x * zoom, step) + px - step; gx < px + pw; gx += step)
                for (gy = fmod(py - pan_y * zoom, step) + py - step; gy < py + ph; gy += step)
                    cb_draw_rect((int)gx, (int)gy, 2, 2, T_DOT);
        }
    }
    for (i = 0; i < nwires; i++) {
        const node *a = by_id(wires[i].from), *b = by_id(wires[i].to);
        double x0, y0, x1, y1;
        int oi, ii;
        if (!a || !b || !a->d || !b->d) continue;
        oi = port_index(a, wires[i].out, 1);
        ii = port_index(b, wires[i].in, 0);
        if (oi < 0 || ii < 0) continue;
        port_pos(a, 1, oi, &x0, &y0);
        port_pos(b, 0, ii, &x1, &y1);
        draw_wire(x0, y0, x1, y1, type_color(port_type(a, &a->d->out[oi])));
    }
    for (i = 0; i < nnodes; i++) draw_node(&nodes[i]);
    if (wd_on) {   /* the wire being dragged */
        const node *a = by_id(wd_src);
        int oi = a && a->d ? port_index(a, wd_out, 1) : -1;
        if (oi >= 0) {
            double x0, y0;
            port_pos(a, 1, oi, &x0, &y0);
            draw_wire(x0, y0, cb_mouse_x(), cb_mouse_y(), cb_rgb(255, 255, 255));
        }
    }
    if (marquee) {
        double ax = sx(mq_x), ay = sy(mq_y), bx = cb_mouse_x(), by = cb_mouse_y();
        cb_draw_rect_lines((int)(ax < bx ? ax : bx), (int)(ay < by ? ay : by), (int)fabs(bx - ax), (int)fabs(by - ay), T_ACC);
    }
    for (i = 0; i < 2; i++) {
        double bx = px + pw - 10 - (2 - i) * (BTN_W + 6), by = py + 8;
        framed((int)bx, (int)by, BTN_W, BTN_H, 4, btn_hit(i, cb_mouse_x(), cb_mouse_y()) ? T_FIELDH : T_FIELD, T_FRAME);
        {
            const char *lbl = i ? "New post" : "New surface";
            ui_text(lbl, (int)bx + (BTN_W - ui_width(lbl, 14)) / 2, (int)by + 4, 14, T_INK);
        }
    }
    if (pal_open) {
        int r, rows = pal_n < 14 ? pal_n : 14;
        double bh = rows * PAL_ROW + 30;
        char head[64];
        framed((int)pal_sx, (int)pal_sy, PAL_W, (int)bh, 5, T_PANEL, T_ACC);
        rrect((int)pal_sx + 4, (int)pal_sy + 3, PAL_W - 8, 20, 3, T_FIELD);
        snprintf(head, sizeof head, "add: %s_", pal_search);
        ui_text(head, (int)pal_sx + 10, (int)pal_sy + 5, 14, T_INK);
        for (r = 0; r < rows && r + pal_scroll < pal_n; r++) {
            const pal_entry *e = &pal_e[r + pal_scroll];
            int ry = (int)pal_sy + 26 + r * PAL_ROW;
            if (e->def < 0) ui_text(e->group, (int)pal_sx + 8, ry + 3, 13, T_ACC);
            else {
                int hot = cb_mouse_x() >= pal_sx && cb_mouse_x() < pal_sx + PAL_W && cb_mouse_y() >= ry && cb_mouse_y() < ry + PAL_ROW;
                if (hot) rrect((int)pal_sx + 3, ry, PAL_W - 6, PAL_ROW, 3, T_ACCBG);
                ui_text(defs[e->def].title, (int)pal_sx + 20, ry + 3, 14, T_INK);
            }
        }
        if (!pal_n) ui_text("nothing matches", (int)pal_sx + 8, (int)pal_sy + 30, 14, T_WARN);
    }
    if (!have_graph || note[0]) {
        ui_text(note[0] ? note : "no shader graph yet", x + 14, y + 14, 15, T_WARN);
    }
    cb_draw_rect(x, y + h - 30, w, 30, T_PANEL);
    cb_draw_rect(x, y + h - 30, w, 1, T_FRAME);
    ui_text(edit_node >= 0 ? "typing: Enter confirms, Esc cancels" : pal_open ? "type to search  Enter: add first  click: add  Esc: close" : "right click: add   drag port: wire   shift: select   X: delete   Ctrl+Z/Y   Ctrl+C/V   F: fit", x + 10, y + h - 24, 14, T_DIM);
    cb_clip_end();
}

void mf_shed_chip(const char *t, int x, int y, int size, int accent) {   /* a rounded label in the C-Vert panel style (the preview's status lines) */
    int w = ui_width(t, size) + 16;
    framed(x, y, w, size + 10, 5, T_PANEL, T_FRAME);
    ui_text(t, x + 8, y + 5, size, accent ? T_ACC : T_INK);
}

int mf_shed_node_count(void) { return nnodes; }
int mf_shed_wire_count(void) { return nwires; }
const char *mf_shed_note(void) { return note; }
