/* C-Vert core: mesh primitives (Add menu). All are built around the origin, Y up, faces counter-clockwise
   seen from outside (normals out), and APPENDED to the mesh; mf_move_verts_from() places them. See mf_mesh.h. */
#include "mf_mesh.h"
#include <stdlib.h>
#include <math.h>

#define TAU 6.28318530718f
#define EPS_R 1e-5f

/* A quad / triangle whose corners may coincide (a cone's apex): the repeats are dropped. */
static int add_face_dedupe(mf_mesh *m, const int *idx, int n) {
    int out[8], k = 0, i;
    if (n > 8) n = 8;
    for (i = 0; i < n; i++)
        if (k == 0 || out[k - 1] != idx[i]) out[k++] = idx[i];
    while (k > 1 && out[k - 1] == out[0]) k--;
    if (k < 3) return 0;
    return mf_add_face(m, out, k) < 0 ? -1 : 0;
}

/* n vertices on a circle of radius r at height y (one shared vertex when r is ~0). */
static int add_ring(mf_mesh *m, int n, float r, float y, int *ids) {
    int i, one = -1;
    for (i = 0; i < n; i++) {
        if (r <= EPS_R) {
            if (one < 0 && (one = mf_add_vert(m, 0.0f, y, 0.0f)) < 0) return -1;
            ids[i] = one;
        } else {
            float a = TAU * (float)i / (float)n;
            if ((ids[i] = mf_add_vert(m, r * cosf(a), y, -r * sinf(a))) < 0) return -1;
        }
    }
    return 0;
}

void mf_move_verts_from(mf_mesh *m, int first, mf_vec3 by) {
    int v;
    for (v = first < 0 ? 0 : first; v < m->nv; v++) m->v[v] = mf_add(m->v[v], by);
}

int mf_make_plane(mf_mesh *m, float size) {
    float h = size * 0.5f;
    int a = mf_add_vert(m, -h, 0, h), b = mf_add_vert(m, h, 0, h), c = mf_add_vert(m, h, 0, -h), d = mf_add_vert(m, -h, 0, -h);
    int q[4];
    if (a < 0 || b < 0 || c < 0 || d < 0) return -1;
    q[0] = a; q[1] = b; q[2] = c; q[3] = d;
    return mf_add_face(m, q, 4) < 0 ? -1 : 0;
}

int mf_make_circle(mf_mesh *m, int n, float radius) {
    int *id, r;
    if (n < 3) n = 3;
    id = (int *)malloc((size_t)n * sizeof(int));
    if (!id) return -1;
    r = add_ring(m, n, radius > EPS_R ? radius : EPS_R * 2.0f, 0.0f, id);
    if (r == 0 && mf_add_face(m, id, n) < 0) r = -1;
    free(id);
    return r;
}

int mf_make_cone(mf_mesh *m, int n, float r1, float r2, float depth, int caps) {
    int *lo, *hi, i, res = 0;
    float h = depth * 0.5f;
    if (n < 3) n = 3;
    if (r1 <= EPS_R && r2 <= EPS_R) return -1;
    lo = (int *)malloc((size_t)n * 2 * sizeof(int));
    if (!lo) return -1;
    hi = lo + n;
    if (add_ring(m, n, r1, -h, lo) != 0 || add_ring(m, n, r2, h, hi) != 0) res = -1;
    for (i = 0; res == 0 && i < n; i++) {
        int j = (i + 1) % n, q[4];
        q[0] = lo[i]; q[1] = lo[j]; q[2] = hi[j]; q[3] = hi[i];
        res = add_face_dedupe(m, q, 4);
    }
    if (res == 0 && caps && r2 > EPS_R && mf_add_face(m, hi, n) < 0) res = -1;
    if (res == 0 && caps && r1 > EPS_R) {   /* the base faces down: reversed */
        int *rev = (int *)malloc((size_t)n * sizeof(int));
        if (!rev) res = -1;
        else {
            for (i = 0; i < n; i++) rev[i] = lo[n - 1 - i];
            if (mf_add_face(m, rev, n) < 0) res = -1;
            free(rev);
        }
    }
    free(lo);
    return res;
}

int mf_make_cylinder(mf_mesh *m, int n, float radius, float depth, int caps) {
    return mf_make_cone(m, n, radius, radius, depth, caps);
}

int mf_make_uvsphere(mf_mesh *m, int n, int rings, float radius) {
    int i, j, top, bot, base;
    if (n < 3) n = 3;
    if (rings < 2) rings = 2;
    if ((top = mf_add_vert(m, 0, radius, 0)) < 0) return -1;
    base = m->nv;   /* ring j (1 .. rings-1), point i: base + (j - 1) * n + i */
    for (j = 1; j < rings; j++) {
        float phi = TAU * 0.5f * (float)j / (float)rings, y = radius * cosf(phi), rho = radius * sinf(phi);
        for (i = 0; i < n; i++) {
            float a = TAU * (float)i / (float)n;
            if (mf_add_vert(m, rho * cosf(a), y, -rho * sinf(a)) < 0) return -1;
        }
    }
    if ((bot = mf_add_vert(m, 0, -radius, 0)) < 0) return -1;
    for (i = 0; i < n; i++) {
        int k = (i + 1) % n, t[3];
        t[0] = top; t[1] = base + i; t[2] = base + k;
        if (mf_add_face(m, t, 3) < 0) return -1;
    }
    for (j = 1; j + 1 < rings; j++)
        for (i = 0; i < n; i++) {
            int k = (i + 1) % n, q[4], a = base + (j - 1) * n, b = base + j * n;   /* a above b */
            q[0] = b + i; q[1] = b + k; q[2] = a + k; q[3] = a + i;
            if (mf_add_face(m, q, 4) < 0) return -1;
        }
    for (i = 0; i < n; i++) {
        int k = (i + 1) % n, t[3], last = base + (rings - 2) * n;
        t[0] = bot; t[1] = last + k; t[2] = last + i;
        if (mf_add_face(m, t, 3) < 0) return -1;
    }
    return 0;
}

int mf_make_torus(mf_mesh *m, int major_n, int minor_n, float major_r, float minor_r) {
    int i, j, base = m->nv;
    if (major_n < 3) major_n = 3;
    if (minor_n < 3) minor_n = 3;
    for (i = 0; i < major_n; i++) {
        float th = TAU * (float)i / (float)major_n;
        for (j = 0; j < minor_n; j++) {
            float ph = TAU * (float)j / (float)minor_n, rr = major_r + minor_r * cosf(ph);
            if (mf_add_vert(m, rr * cosf(th), minor_r * sinf(ph), -rr * sinf(th)) < 0) return -1;
        }
    }
    for (i = 0; i < major_n; i++)
        for (j = 0; j < minor_n; j++) {
            int i2 = (i + 1) % major_n, j2 = (j + 1) % minor_n, q[4];
            q[0] = base + i * minor_n + j; q[1] = base + i2 * minor_n + j;
            q[2] = base + i2 * minor_n + j2; q[3] = base + i * minor_n + j2;
            if (mf_add_face(m, q, 4) < 0) return -1;
        }
    return 0;
}
