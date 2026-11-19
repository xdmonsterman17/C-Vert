/* C-Vert core: picking rays and bounds. See mf_mesh.h. */
#include "mf_mesh.h"
#include <math.h>

/* Moeller-Trumbore, both sides. Gives the distance along dir, or a negative number for a miss. */
static float ray_triangle(mf_vec3 o, mf_vec3 d, mf_vec3 a, mf_vec3 b, mf_vec3 c) {
    mf_vec3 e1 = mf_sub(b, a), e2 = mf_sub(c, a);
    mf_vec3 p = mf_cross(d, e2);
    float det = mf_dot(e1, p), inv, u, v;
    mf_vec3 s, q;
    if (fabsf(det) < 1e-9f) return -1.0f;
    inv = 1.0f / det;
    s = mf_sub(o, a);
    u = mf_dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    q = mf_cross(s, e1);
    v = mf_dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    return mf_dot(e2, q) * inv;
}

static int pick_face_ex(const mf_mesh *m, mf_vec3 origin, mf_vec3 dir, float *t, int honour_hidden) {
    int f, i, best = -1;
    float best_t = 0.0f;
    for (f = 0; f < m->nf; f++) {
        const int *vi = mf_face_verts(m, f);
        if (honour_hidden && m->fhid[f]) continue;
        int n = mf_face_size(m, f);
        for (i = 1; i + 1 < n; i++) {   /* a fan: right for convex faces, fine for a first version */
            float h = ray_triangle(origin, dir, m->v[vi[0]], m->v[vi[i]], m->v[vi[i + 1]]);
            if (h > 0.0f && (best < 0 || h < best_t)) { best = f; best_t = h; }
        }
    }
    if (best >= 0 && t) *t = best_t;
    return best;
}
int mf_pick_face(const mf_mesh *m, mf_vec3 origin, mf_vec3 dir, float *t) { return pick_face_ex(m, origin, dir, t, 1); }
int mf_pick_face_any(const mf_mesh *m, mf_vec3 origin, mf_vec3 dir, float *t) { return pick_face_ex(m, origin, dir, t, 0); }

float mf_face_ray_hit(const mf_mesh *m, int f, mf_vec3 origin, mf_vec3 dir) {
    const int *vi = mf_face_verts(m, f);
    int n = mf_face_size(m, f), i;
    float best = -1.0f;
    for (i = 1; i + 1 < n; i++) {
        float h = ray_triangle(origin, dir, m->v[vi[0]], m->v[vi[i]], m->v[vi[i + 1]]);
        if (h > 0.0f && (best < 0.0f || h < best)) best = h;
    }
    return best;
}

mf_vec3 mf_camera_ray(mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float px, float py) {
    mf_vec3 fwd = mf_sub(target, eye), right, up, d;
    float len = sqrtf(mf_dot(fwd, fwd)), half, aspect, nx, ny;
    if (len < 1e-9f || w <= 0 || h <= 0) return mf_v3(0, 0, -1);
    fwd = mf_mul(fwd, 1.0f / len);
    right = mf_cross(fwd, mf_v3(0, 1, 0));
    len = sqrtf(mf_dot(right, right));
    if (len < 1e-6f) right = mf_v3(1, 0, 0);          /* looking straight up or down */
    else right = mf_mul(right, 1.0f / len);
    up = mf_cross(right, fwd);
    half = tanf(fov_deg * 0.5f * 3.14159265358979f / 180.0f);
    aspect = (float)w / (float)h;
    nx = (2.0f * px / (float)w - 1.0f) * aspect * half;
    ny = (1.0f - 2.0f * py / (float)h) * half;
    d = mf_add(fwd, mf_add(mf_mul(right, nx), mf_mul(up, ny)));
    len = sqrtf(mf_dot(d, d));
    return mf_mul(d, 1.0f / len);
}

void mf_bounds(const mf_mesh *m, mf_vec3 *lo, mf_vec3 *hi) {
    int i;
    if (m->nv == 0) { *lo = mf_v3(0, 0, 0); *hi = mf_v3(0, 0, 0); return; }
    *lo = *hi = m->v[0];
    for (i = 1; i < m->nv; i++) {
        mf_vec3 p = m->v[i];
        if (p.x < lo->x) lo->x = p.x;
        if (p.y < lo->y) lo->y = p.y;
        if (p.z < lo->z) lo->z = p.z;
        if (p.x > hi->x) hi->x = p.x;
        if (p.y > hi->y) hi->y = p.y;
        if (p.z > hi->z) hi->z = p.z;
    }
}

int mf_ray_plane(mf_vec3 o, mf_vec3 d, mf_vec3 p0, mf_vec3 n, mf_vec3 *hit) {
    float den = mf_dot(d, n), t;
    if (fabsf(den) < 1e-7f) return 0;
    t = mf_dot(mf_sub(p0, o), n) / den;
    if (t <= 0.0f) return 0;
    *hit = mf_add(o, mf_mul(d, t));
    return 1;
}

int mf_ray_axis_param(mf_vec3 o, mf_vec3 d, mf_vec3 p0, mf_vec3 axis, float *t) {
    mf_vec3 w0 = mf_sub(p0, o);
    float a = mf_dot(axis, axis), b = mf_dot(axis, d), c = mf_dot(d, d);
    float dd = mf_dot(axis, w0), e = mf_dot(d, w0);
    float den = a * c - b * b;
    if (fabsf(den) < 1e-6f) return 0;
    *t = (b * e - c * dd) / den;
    return 1;
}
