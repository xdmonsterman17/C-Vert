#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cbricks.h"
#include "editor.h"
#include "preview.h"

/* C-Shader: shows a .cbshader (or .glsl) file on a lit sphere and a floor, and reloads it whenever the file's text changes.
   Usage:  c_shader --shader PATH [--shot out.png --frames N]
   The editor's Shader screen writes the file on Save; this program polls it four times a second (comparing a hash of the
   text, so it needs no file-time call) and compiles the new text with the runtime's own C emitter: the picture is the real
   GLSL on the real GPU. Mouse drag turns the camera, SPACE stops the spin, R restores the view. */

#define PI 3.14159265358979

static const char *path = "";
static unsigned long last_hash = 0;
static int have_hash = 0;
static double since_poll = 1.0;
static cb_shader current;
static int kind = CB_SHADER_SURFACE;
static char status[300] = "no shader file given: --shader PATH";
static int loads = 0;
static double spin = 0, yaw = 0.6, pitch = 0.35, dist = 6.5;
static int spinning = 1;
static int frames = 0;

/* The file's text (malloc'd, "" when missing) and a hash of it. */
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

/* A .cbshader says its kind ("kind": "post"); a GLSL file is a post shader when it defines cb_post. */
static int kind_of(const char *text) {
    const char *k = strstr(text, "\"kind\"");
    if (k) {
        const char *q = strchr(k + 6, '"');
        return q && strncmp(q + 1, "post", 4) == 0 ? CB_SHADER_POST : CB_SHADER_SURFACE;
    }
    return strstr(text, "cb_post") ? CB_SHADER_POST : CB_SHADER_SURFACE;
}

static void reload(const char *text) {
    editor_load(text);
    cb_unload_shader(current);   /* frees the slot; if it was in use the built-in look is back */
    current.id = 0;
    kind = kind_of(text);
    current = cb_load_shader(kind, path);
    loads++;
    if (current.id > 0) {
        cb_use_shader(current);
        snprintf(status, sizeof status, "%s shader loaded (%d)", kind == CB_SHADER_POST ? "post" : "surface", loads);
    } else {
        snprintf(status, sizeof status, "did not compile (%d): the errors are on the console; showing the built-in look", loads);
    }
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
static cb_mesh *sphere(void) {
    enum { LON = 40, LAT = 20 };
    cb_mesh *m = cb_mesh_new();
    int i, j;
    cb_rgba white = cb_color("WHITE");
    for (j = 0; j < LAT; j++) {
        for (i = 0; i < LON; i++) {
            double u0 = (double)i / LON, u1 = (double)(i + 1) / LON, v0 = (double)j / LAT, v1 = (double)(j + 1) / LAT;
            Vector3 p[4];
            Vector2 t[4];
            double us[4], vs[4];
            int k;
            us[0] = u0; vs[0] = v0; us[1] = u1; vs[1] = v0; us[2] = u1; vs[2] = v1; us[3] = u0; vs[3] = v1;
            for (k = 0; k < 4; k++) {
                double lon = us[k] * 2 * PI, lat = (0.5 - vs[k]) * PI;
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

static cb_mesh *floor_quad(void) {
    cb_mesh *m = cb_mesh_new();
    Vector3 n = cb_v3(0, 1, 0);
    cb_rgba white = cb_color("WHITE");
    cb_mesh_triangle_nuv(m, cb_v3(-1, 0, -1), cb_v3(-1, 0, 1), cb_v3(1, 0, 1), n, n, n, cb_v2(0, 0), cb_v2(0, 1), cb_v2(1, 1), white);
    cb_mesh_triangle_nuv(m, cb_v3(-1, 0, -1), cb_v3(1, 0, 1), cb_v3(1, 0, -1), n, n, n, cb_v2(0, 0), cb_v2(1, 1), cb_v2(1, 0), white);
    cb_mesh_finish(m, true, true, 0, 0);
    return m;
}

int preview_frame(void) {
    static cb_mesh *ball = NULL, *plane = NULL;
    static const char *shot = "";
    static int limit = 0;
    double dt = cb_frame_time();
    char line[400];
    int split;
    if (!ball) {
        ball = sphere();
        {   /* the folder of the program: where shader_nodes.json may sit */
            static char dir[512];
            char *slash;
            snprintf(dir, sizeof dir, "%s", cb_exe_path());
            slash = strrchr(dir, '/');
            if (slash) *slash = 0; else dir[0] = 0;
            editor_init(dir);
        }
        cb_register_app("c-shader");   /* the editor finds this program through the app registry */
        plane = floor_quad();
        path = cb_option("shader");
        if (!*path && cb_arg_count() > 0 && cb_arg(0)[0] != '-') path = cb_arg(0);
        shot = cb_option("shot");
        limit = atoi(cb_option("frames"));
        if (limit <= 0) limit = 20;
        if (*path) snprintf(status, sizeof status, "waiting for %s", path);
        since_poll = 1.0;
    }
    frames++;
    poll(dt);

    if (cb_key_pressed(CB_KEY_SPACE)) spinning = !spinning;
    if (cb_key_pressed(CB_KEY_R)) { yaw = 0.6; pitch = 0.35; dist = 6.5; }
    split = (int)(cb_screen_width() * 0.40);
    if (cb_mouse_down(0) && cb_mouse_x() < split) {
        Vector2 d = cb_mouse_motion();
        yaw -= d.x * 0.006;
        pitch += d.y * 0.006;
        if (pitch > 1.4) pitch = 1.4;
        if (pitch < -0.2) pitch = -0.2;
    }
    if (cb_mouse_x() < split) dist -= cb_mouse_wheel() * 0.4;
    if (dist < 3) dist = 3;
    if (dist > 14) dist = 14;
    if (spinning) spin += dt * 25.0;

    cb_clear(cb_color("DARKGRAY"));
    {   /* the camera slides sideways so the sphere sits in the middle of the left strip (the canvas covers the rest) */
        double aspect = (double)cb_screen_width() / (double)cb_screen_height();
        double shift = (1.0 - (double)split / cb_screen_width()) * dist * tan(22.5 * PI / 180.0) * aspect;
        double rx = cos(yaw) * shift, rz = -sin(yaw) * shift;
        cb_camera(cb_v3(sin(yaw) * cos(pitch) * dist + rx, sin(pitch) * dist + 0.5, cos(yaw) * cos(pitch) * dist + rz), cb_v3(rx, 0.5, rz), 45.0, false);
    }
    cb_set_light(cb_v3(-0.5, -0.8, -0.4), 0.35);
    cb_begin_3d();
    cb_draw_mesh(plane, cb_v3(0, -1, 0), cb_v3(4, 1, 4), 0, cb_color("LIGHTGRAY"));
    cb_draw_mesh(ball, cb_v3(0, 0.5, 0), cb_v3(1.5, 1.5, 1.5), spin, cb_color("WHITE"));
    cb_end_3d();

    editor_frame(split, 0, cb_screen_width() - split, cb_screen_height());
    snprintf(line, sizeof line, "%s", status);
    cb_draw_text(line, 10, 10, 16, cb_color("WHITE"));
    cb_draw_text("drag: turn   wheel: zoom   SPACE: spin   R: reset", 10, cb_screen_height() - 24, 14, cb_color("LIGHTGRAY"));

    if (*shot && frames >= limit) {
        printf("status: %s\nnodes %d wires %d\n", status, editor_node_count(), editor_wire_count());
        printf("shot %d\n", cb_screenshot(shot) ? 1 : 0);
        return 1;
    }
    return 0;
}
