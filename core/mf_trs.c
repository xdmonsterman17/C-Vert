/* C-Vert core: object transforms (position, Euler rotation, scale) and box select. See mf_mesh.h. */
#include "mf_mesh.h"
#include <math.h>

static void rot_mat(mf_vec3 e, float m[9]) {   /* R = Rz * Ry * Rx */
    float cx = cosf(e.x), sx = sinf(e.x), cy = cosf(e.y), sy = sinf(e.y), cz = cosf(e.z), sz = sinf(e.z);
    m[0] = cz * cy; m[1] = cz * sy * sx - sz * cx; m[2] = cz * sy * cx + sz * sx;
    m[3] = sz * cy; m[4] = sz * sy * sx + cz * cx; m[5] = sz * sy * cx - cz * sx;
    m[6] = -sy;     m[7] = cy * sx;                m[8] = cy * cx;
}

static mf_vec3 euler_from(const float m[9]) {
    float s = -m[6], x, y, z;
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    y = asinf(s);
    if (fabsf(m[6]) < 0.99999f) { x = atan2f(m[7], m[8]); z = atan2f(m[3], m[0]); }
    else { x = atan2f(m[6] < 0.0f ? m[1] : -m[1], m[4]); z = 0.0f; }   /* gimbal lock: all of it goes into X */
    return mf_v3(x, y, z);
}

static void mat_mul(const float a[9], const float b[9], float r[9]) {   /* r = a * b */
    int i, j, k;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            float s = 0.0f;
            for (k = 0; k < 3; k++) s += a[i * 3 + k] * b[k * 3 + j];
            r[i * 3 + j] = s;
        }
}

static mf_vec3 mat_apply(const float m[9], mf_vec3 v) {
    return mf_v3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z);
}

static mf_vec3 mat_apply_t(const float m[9], mf_vec3 v) {   /* the transpose = the inverse of a rotation */
    return mf_v3(m[0] * v.x + m[3] * v.y + m[6] * v.z, m[1] * v.x + m[4] * v.y + m[7] * v.z, m[2] * v.x + m[5] * v.y + m[8] * v.z);
}

static void axis_mat(mf_vec3 a, float ang, float m[9]) {   /* Rodrigues */
    float c = cosf(ang), s = sinf(ang), t = 1.0f - c;
    m[0] = c + a.x * a.x * t;       m[1] = a.x * a.y * t - a.z * s; m[2] = a.x * a.z * t + a.y * s;
    m[3] = a.y * a.x * t + a.z * s; m[4] = c + a.y * a.y * t;       m[5] = a.y * a.z * t - a.x * s;
    m[6] = a.z * a.x * t - a.y * s; m[7] = a.z * a.y * t + a.x * s; m[8] = c + a.z * a.z * t;
}

static float safe(float s) { return fabsf(s) < 1e-6f ? (s < 0.0f ? -1e-6f : 1e-6f) : s; }

mf_trs mf_trs_identity(void) {
    mf_trs t;
    t.pos = mf_v3(0, 0, 0); t.rot = mf_v3(0, 0, 0); t.scl = mf_v3(1, 1, 1);
    return t;
}

mf_vec3 mf_trs_dir(const mf_trs *t, mf_vec3 d) {
    float m[9];
    rot_mat(t->rot, m);
    return mat_apply(m, mf_v3(d.x * t->scl.x, d.y * t->scl.y, d.z * t->scl.z));
}

mf_vec3 mf_trs_undir(const mf_trs *t, mf_vec3 d) {
    float m[9];
    mf_vec3 r;
    rot_mat(t->rot, m);
    r = mat_apply_t(m, d);
    return mf_v3(r.x / safe(t->scl.x), r.y / safe(t->scl.y), r.z / safe(t->scl.z));
}

mf_vec3 mf_trs_point(const mf_trs *t, mf_vec3 p) { return mf_add(mf_trs_dir(t, p), t->pos); }
mf_vec3 mf_trs_unpoint(const mf_trs *t, mf_vec3 p) { return mf_trs_undir(t, mf_sub(p, t->pos)); }

void mf_trs_rotate_world(mf_trs *t, mf_vec3 pivot, mf_vec3 axis, float angle) {
    float ra[9], r0[9], r1[9];
    mf_vec3 rel = mf_sub(t->pos, pivot);
    axis_mat(axis, angle, ra);
    t->pos = mf_add(pivot, mat_apply(ra, rel));
    rot_mat(t->rot, r0);
    mat_mul(ra, r0, r1);
    t->rot = euler_from(r1);
}

void mf_trs_scale_about(mf_trs *t, mf_vec3 pivot, mf_vec3 a) {
    mf_vec3 rel = mf_sub(t->pos, pivot);
    t->pos = mf_add(pivot, mf_v3(rel.x * a.x, rel.y * a.y, rel.z * a.z));
    t->scl = mf_v3(t->scl.x * a.x, t->scl.y * a.y, t->scl.z * a.z);
}

void mf_mesh_to_world(mf_mesh *m, const mf_trs *t) {
    int v;
    for (v = 0; v < m->nv; v++) m->v[v] = mf_trs_point(t, m->v[v]);
    m->topo_ok = 0;
}

void mf_mesh_to_local(mf_mesh *m, const mf_trs *t) {
    int v;
    for (v = 0; v < m->nv; v++) m->v[v] = mf_trs_unpoint(t, m->v[v]);
    m->topo_ok = 0;
}

/* ---- box select ---- */

static void order(float *a, float *b) { if (*a > *b) { float t = *a; *a = *b; *b = t; } }

static int in_box(mf_vec3 eye, mf_vec3 target, float fov, int w, int h, mf_vec3 p, float x0, float y0, float x1, float y1) {
    float sx, sy;
    if (!mf_project(eye, target, fov, w, h, p, &sx, &sy)) return 0;
    return sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
}

int mf_box_select_verts_vis(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float x0, float y0, float x1, float y1, int add, const unsigned char *vis) {
    int v, n = 0;
    order(&x0, &x1); order(&y0, &y1);
    if (!add) mf_sel_clear(m);
    for (v = 0; v < m->nv; v++)
        if ((!vis || vis[v]) && !m->vhid[v] && in_box(eye, target, fov_deg, w, h, m->v[v], x0, y0, x1, y1)) { m->vsel[v] = 1; n++; }
    return n;
}

int mf_box_select_faces_vis(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float x0, float y0, float x1, float y1, int add, const unsigned char *vis) {
    int f, n = 0;
    order(&x0, &x1); order(&y0, &y1);
    if (!add) mf_sel_clear(m);
    for (f = 0; f < m->nf; f++)
    {
        int ok = !m->fhid[f];
        if (ok && vis) {   /* a face counts as visible when any of its vertices is */
            const int *vi = mf_face_verts(m, f);
            int i, k = mf_face_size(m, f);
            ok = 0;
            for (i = 0; i < k; i++) if (vis[vi[i]]) ok = 1;
        }
        if (ok && in_box(eye, target, fov_deg, w, h, mf_face_center(m, f), x0, y0, x1, y1)) { m->fsel[f] = 1; n++; }
    }
    return n;
}
int mf_box_select_verts(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float x0, float y0, float x1, float y1, int add) {
    return mf_box_select_verts_vis(m, eye, target, fov_deg, w, h, x0, y0, x1, y1, add, NULL);
}
int mf_box_select_faces(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float x0, float y0, float x1, float y1, int add) {
    return mf_box_select_faces_vis(m, eye, target, fov_deg, w, h, x0, y0, x1, y1, add, NULL);
}

int mf_mesh_in_box(const mf_mesh *m, const mf_trs *t, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float x0, float y0, float x1, float y1) {
    int v;
    order(&x0, &x1); order(&y0, &y1);
    for (v = 0; v < m->nv; v++) {
        mf_vec3 p = t ? mf_trs_point(t, m->v[v]) : m->v[v];
        if (in_box(eye, target, fov_deg, w, h, p, x0, y0, x1, y1)) return 1;
    }
    return 0;
}
