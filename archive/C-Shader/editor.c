#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cbricks.h"
#include "editor.h"
#include "json.h"

#define MAX_DEFS 64
#define MAX_NODES 256
#define MAX_WIRES 1024
#define NODE_W 170
#define HEAD_H 26
#define ROW_H 24

typedef struct { char name[24], type[8]; } port;
typedef struct { char key[32], title[40], group[16]; port in[8], out[8]; int nin, nout; } def;
typedef struct { int id; char type[32], t[8]; double x, y; const def *d; } node;
typedef struct { int from, to; char out[24], in[24]; } wire;

static def defs[MAX_DEFS];
static int ndefs = 0;
static node nodes[MAX_NODES];
static wire wires[MAX_WIRES];
static int nnodes = 0, nwires = 0;
static char note[200] = "";
static int have_graph = 0, fitted = 0;
static double pan_x = 0, pan_y = 0, zoom = 1;   /* screen = (world - pan) * zoom + panel origin */
static int px = 0, py = 0, pw = 100, ph = 100;

static cb_rgba cb_rgb(int r, int g, int b) { cb_rgba c; c.r = (unsigned char)r; c.g = (unsigned char)g; c.b = (unsigned char)b; c.a = 255; return c; }

static const def *find_def(const char *key) {
    int i;
    for (i = 0; i < ndefs; i++) if (strcmp(defs[i].key, key) == 0) return &defs[i];
    return NULL;
}

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

void editor_init(const char *dir) {
    char path[600];
    char *text = NULL;
    jv *root;
    const jv *tab;
    int i, k;
    const char *tries[3] = { "shader_nodes.json", "%s/shader_nodes.json", "%s/../../shader_nodes.json" };
    for (i = 0; i < 3 && !text; i++) {
        snprintf(path, sizeof path, tries[i], dir);
        text = slurp(path);
    }
    if (!text) { snprintf(note, sizeof note, "shader_nodes.json not found: the canvas has no node table"); return; }
    root = json_parse(text);
    free(text);
    tab = json_get(root, "nodes");
    if (!tab || tab->t != 4) { snprintf(note, sizeof note, "shader_nodes.json is not a node table"); json_free(root); return; }
    for (i = 0; i < tab->count && ndefs < MAX_DEFS; i++) {
        def *d = &defs[ndefs++];
        const jv *n = &tab->kids[i];
        memset(d, 0, sizeof *d);
        copy(d->key, sizeof d->key, tab->keys[i]);
        copy(d->title, sizeof d->title, json_str(json_get(n, "title"), tab->keys[i]));
        copy(d->group, sizeof d->group, json_str(json_get(n, "group"), ""));
        for (k = 0; k < 2; k++) {
            const jv *list = json_get(n, k ? "out" : "in");
            int j, cnt = list && list->t == 3 ? list->count : 0;
            for (j = 0; j < cnt && j < 8; j++) {
                port *p = k ? &d->out[j] : &d->in[j];
                copy(p->name, sizeof p->name, json_str(json_at(json_at(list, j), 0), "?"));
                copy(p->type, sizeof p->type, json_str(json_at(json_at(list, j), 1), "float"));
            }
            if (k) d->nout = cnt > 8 ? 8 : cnt; else d->nin = cnt > 8 ? 8 : cnt;
        }
    }
    json_free(root);
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

void editor_load(const char *text) {
    jv *root = json_parse(text);
    const jv *ns, *ws_;
    int i;
    if (!root || !json_get(root, "nodes")) {
        snprintf(note, sizeof note, "the file is not a shader graph (kept the last good one on the canvas)");
        json_free(root);
        return;
    }
    ns = json_get(root, "nodes");
    ws_ = json_get(root, "wires");
    nnodes = nwires = 0;
    for (i = 0; ns && ns->t == 3 && i < ns->count && nnodes < MAX_NODES; i++) {
        const jv *n = &ns->kids[i];
        node *o = &nodes[nnodes++];
        const jv *pos = json_get(n, "pos");
        o->id = (int)json_num(json_get(n, "id"), 0);
        copy(o->type, sizeof o->type, json_str(json_get(n, "type"), ""));
        copy(o->t, sizeof o->t, json_str(json_get(n, "t"), ""));
        o->x = json_num(json_at(pos, 0), 0);
        o->y = json_num(json_at(pos, 1), 0);
        o->d = find_def(o->type);
    }
    for (i = 0; ws_ && ws_->t == 3 && i < ws_->count && nwires < MAX_WIRES; i++) {
        const jv *w = &ws_->kids[i];
        wire *o = &wires[nwires++];
        o->from = (int)json_num(json_get(w, "from"), 0);
        o->to = (int)json_num(json_get(w, "to"), 0);
        copy(o->out, sizeof o->out, json_str(json_get(w, "out"), ""));
        copy(o->in, sizeof o->in, json_str(json_get(w, "in"), ""));
    }
    json_free(root);
    have_graph = 1;
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
    if (sz >= 8) cb_draw_text(s, (int)x, (int)y, sz, c);
}

static void draw_wire(double x0, double y0, double x1, double y1, cb_rgba c) {
    double dx = fabs(x1 - x0) * 0.5 + 20 * zoom, px_ = x0, py_ = y0;
    int i;
    for (i = 1; i <= 24; i++) {
        double t = i / 24.0, u = 1 - t;
        double bx = u * u * u * x0 + 3 * u * u * t * (x0 + dx) + 3 * u * t * t * (x1 - dx) + t * t * t * x1;
        double by = u * u * u * y0 + 3 * u * u * t * y0 + 3 * u * t * t * y1 + t * t * t * y1;
        cb_draw_line((int)px_, (int)py_, (int)bx, (int)by, c);
        px_ = bx; py_ = by;
    }
}

static void draw_node(const node *n) {
    double x = sx(n->x), y = sy(n->y), w = NODE_W * zoom, h = node_h(n) * zoom;
    int i;
    if (x > px + pw || y > py + ph || x + w < px || y + h < py) return;
    cb_draw_rect((int)x, (int)y, (int)w, (int)h, cb_rgb(46, 48, 56));
    cb_draw_rect((int)x, (int)y, (int)w, (int)(HEAD_H * zoom), n->d ? cb_rgb(70, 82, 120) : cb_rgb(130, 50, 50));
    cb_draw_rect_lines((int)x, (int)y, (int)w, (int)h, cb_rgb(20, 20, 24));
    text(n->d ? n->d->title : n->type, x + 8 * zoom, y + 5 * zoom, 15, cb_color("WHITE"));
    if (!n->d) { text("unknown node type", x + 8 * zoom, y + (HEAD_H + 4) * zoom, 13, cb_rgb(255, 150, 150)); return; }
    for (i = 0; i < n->d->nin; i++) {
        double ax, ay;
        port_pos(n, 0, i, &ax, &ay);
        cb_draw_circle((int)ax, (int)ay, 5 * zoom, type_color(port_type(n, &n->d->in[i])));
        text(n->d->in[i].name, ax + 10 * zoom, ay - 7 * zoom, 13, cb_rgb(210, 210, 220));
    }
    for (i = 0; i < n->d->nout; i++) {
        double ax, ay;
        int tw;
        port_pos(n, 1, i, &ax, &ay);
        cb_draw_circle((int)ax, (int)ay, 5 * zoom, type_color(port_type(n, &n->d->out[i])));
        tw = (int)((double)cb_text_width_font((cb_font){0}, n->d->out[i].name, (int)(13 * zoom + 0.5)));
        text(n->d->out[i].name, ax - 10 * zoom - tw, ay - 7 * zoom, 13, cb_rgb(210, 210, 220));
    }
}

void editor_frame(int x, int y, int w, int h) {
    int i, over;
    px = x; py = y; pw = w; ph = h;
    over = cb_mouse_x() >= x && cb_mouse_x() < x + w && cb_mouse_y() >= y && cb_mouse_y() < y + h;
    if (fitted == 2) { fit(); fitted = 1; }
    if (over) {
        double wheel = cb_mouse_wheel();
        if (wheel != 0) {   /* zoom about the cursor */
            double mx = cb_mouse_x(), my = cb_mouse_y();
            double wx = (mx - px) / zoom + pan_x, wy = (my - py) / zoom + pan_y;
            zoom *= wheel > 0 ? 1.12 : 1 / 1.12;
            if (zoom < 0.15) zoom = 0.15;
            if (zoom > 3) zoom = 3;
            pan_x = wx - (mx - px) / zoom;
            pan_y = wy - (my - py) / zoom;
        }
        if (cb_mouse_down(0) || cb_mouse_down(2)) {
            Vector2 d = cb_mouse_motion();
            pan_x -= d.x / zoom;
            pan_y -= d.y / zoom;
        }
        if (cb_key_pressed(CB_KEY_F)) fit();
    }
    cb_clip(x, y, w, h);
    cb_draw_rect(x, y, w, h, cb_rgb(30, 31, 36));
    {   /* a dotted grid every 40 world units, so panning is visible */
        double step = 40 * zoom, gx, gy;
        if (step >= 12) {
            for (gx = fmod(px - pan_x * zoom, step) + px - step; gx < px + pw; gx += step)
                for (gy = fmod(py - pan_y * zoom, step) + py - step; gy < py + ph; gy += step)
                    cb_draw_rect((int)gx, (int)gy, 2, 2, cb_rgb(52, 54, 62));
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
    if (!have_graph || note[0]) {
        cb_draw_text(note[0] ? note : "no shader graph yet", x + 14, y + 14, 15, cb_rgb(255, 170, 120));
    }
    cb_draw_text("drag: pan   wheel: zoom   F: fit", x + 10, y + h - 24, 14, cb_color("LIGHTGRAY"));
    cb_clip_end();
}

int editor_node_count(void) { return nnodes; }
int editor_wire_count(void) { return nwires; }
const char *editor_note(void) { return note; }
