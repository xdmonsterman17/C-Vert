#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cbricks.h"
#include "mf_shader.h"
#include "mf_shed.h"

/* The Shader workspace. The file is read by polling (a djb2 hash of its text, no file-time call); on a change the shader is
   unloaded (its slot freed) and loaded again. A .cbshader says its kind ("kind": "post"); a GLSL file is post when it defines cb_post. */

static int on = 0, inited = 0;
static char path[512] = "";
static unsigned long last_hash = 0;
static int have_hash = 0, loads = 0, kind = CB_SHADER_SURFACE, use_model = 0;
static double since_poll = 1.0, spin = 0;
static cb_shader current;
static char status[700] = "no shader file: open one with --shader PATH";
static cb_mesh *ball = NULL, *plane = NULL;

static char *read_text(const char *p, unsigned long *hash) {
    FILE *f = fopen(p, "rb");
    char *text;
    long n;
    size_t i;
    unsigned long h = 5381;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)(n > 0 ? n : 0) + 1);
    if (!text) { fclose(f); return NULL; }
    n = (long)fread(text, 1, (size_t)(n > 0 ? n : 0), f);
    text[n] = 0;
    fclose(f);
    for (i = 0; i < (size_t)n; i++) h = h * 33 + (unsigned char)text[i];
    *hash = h;
    return text;
}

static int kind_of(const char *text) {
    const char *k = strstr(text, "\"kind\"");
    if (k) {
        const char *q = strchr(k + 6, '"');
        return q && strncmp(q + 1, "post", 4) == 0 ? CB_SHADER_POST : CB_SHADER_SURFACE;
    }
    return strstr(text, "cb_post") ? CB_SHADER_POST : CB_SHADER_SURFACE;
}

static void unload(void) {
    if (current.id > 0) cb_unload_shader(current);   /* frees the slot; in use -> the built-in look returns */
    current.id = 0;
}

static void reload(const char *text) {
    mf_shed_load(text);
    unload();
    kind = kind_of(text);
    current = cb_load_shader(kind, path);
    loads++;
    if (current.id > 0) {
        cb_use_shader(current);
        snprintf(status, sizeof status, "%s shader loaded (%d)", kind == CB_SHADER_POST ? "post" : "surface", loads);
    } else {
        snprintf(status, sizeof status, "did not compile (%d): the errors are on the console; showing the built-in look", loads);
    }
    printf("[meshforge] shader: %s, nodes %d wires %d\n", status, mf_shed_node_count(), mf_shed_wire_count());   /* the tests read this line */
}

static void poll(double dt) {
    char *text;
    unsigned long h;
    since_poll += dt;
    if (since_poll < 0.25 || !*path) return;
    since_poll = 0;
    text = read_text(path, &h);
    if (!text) {
        if (!have_hash) snprintf(status, sizeof status, "waiting for %s", path);
        return;
    }
    if (!have_hash || h != last_hash) {
        have_hash = 1;
        last_hash = h;
        if (text[0]) reload(text);   /* a half-written (empty) file is skipped */
    }
    free(text);
}

/* A UV sphere and a floor quad, both with normals and UVs (a surface shader reads them). */
static cb_mesh *make_sphere(void) {
    enum { LON = 40, LAT = 20 };
    cb_mesh *m = cb_mesh_new();
    int i, j;
    cb_rgba white = cb_color("WHITE");
    for (j = 0; j < LAT; j++) {
        for (i = 0; i < LON; i++) {
            double us[4], vs[4];
            Vector3 p[4];
            Vector2 t[4];
            int k;
            us[0] = (double)i / LON; vs[0] = (double)j / LAT; us[1] = (double)(i + 1) / LON; vs[1] = vs[0];
            us[2] = us[1]; vs[2] = (double)(j + 1) / LAT; us[3] = us[0]; vs[3] = vs[2];
            for (k = 0; k < 4; k++) {
                double lon = us[k] * 2 * 3.14159265358979, lat = (0.5 - vs[k]) * 3.14159265358979;
                p[k] = cb_v3(cos(lat) * cos(lon), sin(lat), cos(lat) * sin(lon));
                t[k] = cb_v2(us[k], vs[k]);
            }
            cb_mesh_triangle_nuv(m, p[0], p[1], p[2], p[0], p[1], p[2], t[0], t[1], t[2], white);
            cb_mesh_triangle_nuv(m, p[0], p[2], p[3], p[0], p[2], p[3], t[0], t[2], t[3], white);
        }
    }
    cb_mesh_finish(m, true, true, 0, 0);
    return m;
}

static cb_mesh *make_floor(void) {
    cb_mesh *m = cb_mesh_new();
    Vector3 n = cb_v3(0, 1, 0);
    cb_rgba white = cb_color("WHITE");
    cb_mesh_triangle_nuv(m, cb_v3(-1, 0, -1), cb_v3(-1, 0, 1), cb_v3(1, 0, 1), n, n, n, cb_v2(0, 0), cb_v2(0, 1), cb_v2(1, 1), white);
    cb_mesh_triangle_nuv(m, cb_v3(-1, 0, -1), cb_v3(1, 0, 1), cb_v3(1, 0, -1), n, n, n, cb_v2(0, 0), cb_v2(1, 1), cb_v2(1, 0), white);
    cb_mesh_finish(m, true, true, 0, 0);
    return m;
}

int mf_shader_active(void) { return on; }
int mf_shader_split(void) { return (int)(cb_screen_width() * 0.40); }
const char *mf_shader_status(void) { return status; }

static void init_once(void) {
    if (inited) return;
    inited = 1;
    ball = make_sphere();
    plane = make_floor();
    mf_shed_init("");
}

void mf_shader_open(const char *p) {
    init_once();
    if (!p && !*path) p = "assets/checker.cbshader";   /* F4 with nothing chosen yet: the sample graph */
    if (p && *p && strcmp(p, path) != 0) {
        snprintf(path, sizeof path, "%s", p);
        mf_shed_set_path(path);
        have_hash = 0;
        since_poll = 1.0;
        snprintf(status, sizeof status, "waiting for %s", path);
    } else if (*path) {   /* the same file again: read it afresh */
        have_hash = 0;
        since_poll = 1.0;
    }
    on = 1;
}

void mf_shader_close(void) {
    on = 0;
    unload();   /* the model goes back to its own look */
    have_hash = 0;
}

void mf_shader_typed(const char *chars, int n) { if (on) mf_shed_type(chars, n); }

void mf_shader_update(void) {
    double dt = cb_frame_time();
    if (!on) return;
    {   /* the canvas made a new file: open it */
        const char *o = mf_shed_take_open();
        if (o && *o) mf_shader_open(o);
    }
    if (dt <= 0 || dt > 0.25) dt = 1.0 / 60.0;
    poll(dt);
    if (!mf_shed_editing() && cb_key_pressed(CB_KEY_M)) use_model = !use_model;
    spin += dt * 25.0;
}

float mf_shader_shift(void) { return on ? 1.0f - (float)mf_shader_split() / (float)cb_screen_width() : 0.0f; }

void mf_shader_draw3d(float cx, float cy, float cz, float size, void *model, int model_tris) {
    if (!on) return;
    if (current.id > 0) cb_use_shader(current);
    if (use_model && model && model_tris > 0) {
        cb_draw_mesh((cb_mesh *)model, cb_v3(0, 0, 0), cb_v3(1, 1, 1), 0.0, cb_color("WHITE"));
    } else {
        cb_draw_mesh(plane, cb_v3(cx, cy - size, cz), cb_v3(size * 3, 1, size * 3), 0, cb_color("LIGHTGRAY"));
        cb_draw_mesh(ball, cb_v3(cx, cy, cz), cb_v3(size, size, size), spin, cb_color("WHITE"));
    }
}

void mf_shader_draw_hud(void) {
    int split = mf_shader_split();
    if (!on) return;
    mf_shed_frame(split, 0, cb_screen_width() - split, cb_screen_height());
    mf_shed_chip(status, 10, 34, 14, 1);
    mf_shed_chip(use_model ? "drag: turn   M: sphere   F4: back" : "drag: turn   M: model   F4: back", 10, cb_screen_height() - 34, 14, 0);
}

void mf_shader_shutdown(void) {
    unload();
    on = 0;
}
