/* C-Vert core: select modes, masks, screen-space picking, modal-transform maths. See mf_mesh.h. */
#include "mf_mesh.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

int mf_sel_mask(const mf_mesh *m, unsigned char *mark) {
    int v, f, i, n = 0;
    for (v = 0; v < m->nv; v++) mark[v] = m->vsel[v];
    for (f = 0; f < m->nf; f++)
        if (m->fsel[f])
            for (i = m->fstart[f]; i < m->fstart[f + 1]; i++) mark[m->fidx[i]] = 1;
    for (v = 0; v < m->nv; v++) n += mark[v];
    return n;
}

void mf_sel_vert(mf_mesh *m, int v, int on) {
    if (v >= 0 && v < m->nv) m->vsel[v] = on ? 1 : 0;
}

void mf_sel_all(mf_mesh *m, int on) {   /* hidden vertices and faces never join in */
    int i;
    for (i = 0; i < m->nv; i++) m->vsel[i] = (unsigned char)(on && !m->vhid[i]);
    for (i = 0; i < m->nf; i++) m->fsel[i] = (unsigned char)(on && !m->fhid[i]);
}

void mf_sel_convert(mf_mesh *m, mf_selmode to) {
    unsigned char *mark = (unsigned char *)calloc((size_t)(m->nv > 0 ? m->nv : 1), 1);
    int f, i;
    if (!mark) return;
    mf_sel_mask(m, mark);
    if (to == MF_SEL_FACE) {
        for (f = 0; f < m->nf; f++) {
            int all = 1;
            for (i = m->fstart[f]; i < m->fstart[f + 1]; i++) if (!mark[m->fidx[i]]) all = 0;
            m->fsel[f] = (unsigned char)(all && !m->fhid[f]);
        }
        if (m->nv > 0) memset(m->vsel, 0, (size_t)m->nv);
    } else {
        if (m->nv > 0) memcpy(m->vsel, mark, (size_t)m->nv);
        if (m->nf > 0) memset(m->fsel, 0, (size_t)m->nf);
    }
    free(mark);
}

mf_vec3 mf_sel_center(const mf_mesh *m, const unsigned char *mark) {
    mf_vec3 s = mf_v3(0, 0, 0);
    int v, n = 0;
    for (v = 0; v < m->nv; v++)
        if (mark[v]) { s = mf_add(s, m->v[v]); n++; }
    return n ? mf_mul(s, 1.0f / (float)n) : s;
}

int mf_project(mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, mf_vec3 p, float *sx, float *sy) {
    mf_vec3 fwd = mf_sub(target, eye), right, up, rel;
    float len = sqrtf(mf_dot(fwd, fwd)), half, aspect, z, x, y;
    if (len < 1e-9f || w <= 0 || h <= 0) return 0;
    fwd = mf_mul(fwd, 1.0f / len);
    right = mf_cross(fwd, mf_v3(0, 1, 0));
    len = sqrtf(mf_dot(right, right));
    if (len < 1e-6f) right = mf_v3(1, 0, 0);
    else right = mf_mul(right, 1.0f / len);
    up = mf_cross(right, fwd);
    rel = mf_sub(p, eye);
    z = mf_dot(rel, fwd);
    if (z < 0.01f) return 0;
    half = tanf(fov_deg * 0.5f * 3.14159265358979f / 180.0f);
    aspect = (float)w / (float)h;
    x = mf_dot(rel, right) / (z * half * aspect);
    y = mf_dot(rel, up) / (z * half);
    *sx = (x + 1.0f) * 0.5f * (float)w;
    *sy = (1.0f - y) * 0.5f * (float)h;
    return 1;
}

int mf_pick_vert_screen_vis(const mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float px, float py, float radius, const unsigned char *vis) {
    int v, best = -1;
    float best_d = radius * radius;
    for (v = 0; v < m->nv; v++) {
        float sx, sy, dx, dy, d;
        if ((vis && !vis[v]) || m->vhid[v]) continue;
        if (!mf_project(eye, target, fov_deg, w, h, m->v[v], &sx, &sy)) continue;
        dx = sx - px; dy = sy - py; d = dx * dx + dy * dy;
        if (d <= best_d) { best = v; best_d = d; }
    }
    return best;
}

int mf_pick_edge_screen_vis(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float px, float py, float radius, const unsigned char *vis) {
    int c, best = -1;
    float best_d = radius * radius;
    if (!m->topo_ok && mf_build(m) != 0) return -1;
    for (c = 0; c < m->nc; c++) {
        int t = m->twin[c];
        float ax, ay, bx, by, abx, aby, l2, u, dx, dy, d;
        if (t >= 0 && t < c) continue;   /* each edge once */
        if (m->fhid[m->cface[c]] && (t < 0 || m->fhid[m->cface[t]])) continue;   /* an edge of hidden faces only */
        if (vis && !vis[m->fidx[c]] && !vis[m->fidx[mf_corner_next(m, c)]]) continue;
        if (!mf_project(eye, target, fov_deg, w, h, m->v[m->fidx[c]], &ax, &ay)) continue;
        if (!mf_project(eye, target, fov_deg, w, h, m->v[m->fidx[mf_corner_next(m, c)]], &bx, &by)) continue;
        abx = bx - ax; aby = by - ay;
        l2 = abx * abx + aby * aby;
        u = l2 > 1e-9f ? ((px - ax) * abx + (py - ay) * aby) / l2 : 0.0f;
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
        dx = ax + abx * u - px; dy = ay + aby * u - py;
        d = dx * dx + dy * dy;
        if (d <= best_d) { best = c; best_d = d; }
    }
    return best;
}

void mf_xform(mf_mesh *m, const mf_vec3 *orig, const unsigned char *mark, mf_xfkind kind, mf_vec3 pivot, mf_vec3 a, float angle) {
    int v;
    float cs = cosf(angle), sn = sinf(angle);
    for (v = 0; v < m->nv; v++) {
        mf_vec3 r;
        if (!mark[v]) continue;
        r = mf_sub(orig[v], pivot);
        if (kind == MF_XF_MOVE) {
            m->v[v] = mf_add(orig[v], a);
        } else if (kind == MF_XF_SCALE) {
            m->v[v] = mf_add(pivot, mf_v3(r.x * a.x, r.y * a.y, r.z * a.z));
        } else {   /* Rodrigues */
            mf_vec3 rot = mf_add(mf_add(mf_mul(r, cs), mf_mul(mf_cross(a, r), sn)), mf_mul(a, mf_dot(a, r) * (1.0f - cs)));
            m->v[v] = mf_add(pivot, rot);
        }
    }
}

int mf_pick_vert_screen(const mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float px, float py, float radius) {
    return mf_pick_vert_screen_vis(m, eye, target, fov_deg, w, h, px, py, radius, NULL);
}
int mf_pick_edge_screen(mf_mesh *m, mf_vec3 eye, mf_vec3 target, float fov_deg, int w, int h, float px, float py, float radius) {
    return mf_pick_edge_screen_vis(m, eye, target, fov_deg, w, h, px, py, radius, NULL);
}

/* ---- select linked, visibility ---- */
#include <stdlib.h>
static int uf_find(int *p, int a) {
    while (p[a] != a) { p[a] = p[p[a]]; a = p[a]; }
    return a;
}

int mf_select_linked(mf_mesh *m, int seed, int faces, int on) {
    int *par, f, i, v, n = 0, root;
    if (seed < 0 || seed >= m->nv) return -1;
    par = (int *)malloc((size_t)m->nv * sizeof(int));
    if (!par) return -1;
    for (v = 0; v < m->nv; v++) par[v] = v;
    for (f = 0; f < m->nf; f++) {
        const int *vi = mf_face_verts(m, f);
        int k = mf_face_size(m, f), a = uf_find(par, vi[0]);
        if (m->fhid[f]) continue;   /* a hidden face joins nothing */
        for (i = 1; i < k; i++) { int b = uf_find(par, vi[i]); if (b != a) par[b] = a; }
    }
    root = uf_find(par, seed);
    for (v = 0; v < m->nv; v++)
        if (uf_find(par, v) == root && !m->vhid[v]) { n++; if (!faces) m->vsel[v] = on ? 1 : 0; }
    if (faces)
        for (f = 0; f < m->nf; f++)
            if (!m->fhid[f] && uf_find(par, mf_face_verts(m, f)[0]) == root) m->fsel[f] = on ? 1 : 0;
    free(par);
    return n;
}

/* A bounding-volume tree over the faces, so each vertex's "is anything in front of me" ray visits a few faces, not all. */
typedef struct { mf_vec3 lo, hi; int a, b; } vbox;   /* leaf: faces order[a .. a+b), b > 0; inner: children a and b, as -(index) - 1 */
typedef struct { vbox *nodes; int *order; int nn; const mf_mesh *m; mf_vec3 *cen; } vtree;

static void vt_box(const vtree *t, int s, int e, mf_vec3 *lo, mf_vec3 *hi) {
    int i, k;
    *lo = mf_v3(1e30f, 1e30f, 1e30f); *hi = mf_v3(-1e30f, -1e30f, -1e30f);
    for (i = s; i < e; i++) {
        const int *vi = mf_face_verts(t->m, t->order[i]);
        int n = mf_face_size(t->m, t->order[i]);
        for (k = 0; k < n; k++) {
            mf_vec3 p = t->m->v[vi[k]];
            if (p.x < lo->x) lo->x = p.x;
            if (p.y < lo->y) lo->y = p.y;
            if (p.z < lo->z) lo->z = p.z;
            if (p.x > hi->x) hi->x = p.x;
            if (p.y > hi->y) hi->y = p.y;
            if (p.z > hi->z) hi->z = p.z;
        }
    }
    lo->x -= 1e-4f; lo->y -= 1e-4f; lo->z -= 1e-4f;   /* a little slack: rays through shared edges must not slip between boxes */
    hi->x += 1e-4f; hi->y += 1e-4f; hi->z += 1e-4f;
}

static float vt_axis(mf_vec3 p, int ax) { return ax == 0 ? p.x : ax == 1 ? p.y : p.z; }

static int vt_build(vtree *t, int s, int e) {   /* returns the node index */
    int id = t->nn++, i, j, ax = 0;
    mf_vec3 ext, clo, chi;
    float mid;
    if (e - s <= 4) {
        t->nodes[id].a = s; t->nodes[id].b = e - s;
        vt_box(t, s, e, &t->nodes[id].lo, &t->nodes[id].hi);
        return id;
    }
    clo = chi = t->cen[t->order[s]];   /* split about the middle of the face centres' longest axis (cheaper than the faces' own box) */
    for (i = s + 1; i < e; i++) {
        mf_vec3 c = t->cen[t->order[i]];
        if (c.x < clo.x) clo.x = c.x;
        if (c.y < clo.y) clo.y = c.y;
        if (c.z < clo.z) clo.z = c.z;
        if (c.x > chi.x) chi.x = c.x;
        if (c.y > chi.y) chi.y = c.y;
        if (c.z > chi.z) chi.z = c.z;
    }
    ext = mf_sub(chi, clo);
    if (ext.y > ext.x) ax = 1;
    if (ext.z > vt_axis(ext, ax)) ax = 2;
    mid = 0.5f * (vt_axis(clo, ax) + vt_axis(chi, ax));
    i = s; j = e - 1;
    while (i <= j) {   /* partition the face centres about the middle */
        if (vt_axis(t->cen[t->order[i]], ax) < mid) i++;
        else { int tmp = t->order[i]; t->order[i] = t->order[j]; t->order[j] = tmp; j--; }
    }
    if (i == s || i == e) i = (s + e) / 2;   /* all on one side: split by count */
    {
        int l = vt_build(t, s, i), r = vt_build(t, i, e);
        const vbox *lb = &t->nodes[l], *rb = &t->nodes[r];
        t->nodes[id].a = -l - 1; t->nodes[id].b = -r - 1;
        t->nodes[id].lo = mf_v3(lb->lo.x < rb->lo.x ? lb->lo.x : rb->lo.x, lb->lo.y < rb->lo.y ? lb->lo.y : rb->lo.y, lb->lo.z < rb->lo.z ? lb->lo.z : rb->lo.z);
        t->nodes[id].hi = mf_v3(lb->hi.x > rb->hi.x ? lb->hi.x : rb->hi.x, lb->hi.y > rb->hi.y ? lb->hi.y : rb->hi.y, lb->hi.z > rb->hi.z ? lb->hi.z : rb->hi.z);
    }
    return id;
}

static int vt_slab(const vbox *b, mf_vec3 o, mf_vec3 inv, float tmax) {
    float t0 = 0.0f, t1 = tmax, a, c, lo, hi;
    a = (b->lo.x - o.x) * inv.x; c = (b->hi.x - o.x) * inv.x; lo = a < c ? a : c; hi = a < c ? c : a;
    if (lo > t0) t0 = lo;
    if (hi < t1) t1 = hi;
    a = (b->lo.y - o.y) * inv.y; c = (b->hi.y - o.y) * inv.y; lo = a < c ? a : c; hi = a < c ? c : a;
    if (lo > t0) t0 = lo;
    if (hi < t1) t1 = hi;
    a = (b->lo.z - o.z) * inv.z; c = (b->hi.z - o.z) * inv.z; lo = a < c ? a : c; hi = a < c ? c : a;
    if (lo > t0) t0 = lo;
    if (hi < t1) t1 = hi;
    return t0 <= t1 + 1e-4f;
}

static float vt_inv(float d) { return (d > -1e-12f && d < 1e-12f) ? (d < 0 ? -1e12f : 1e12f) : 1.0f / d; }

/* 1 when some face is hit by the ray nearer than tmax */
static int vt_any(const vtree *t, mf_vec3 o, mf_vec3 dir, float tmax) {
    int stack[96], sp = 0;
    mf_vec3 inv = mf_v3(vt_inv(dir.x), vt_inv(dir.y), vt_inv(dir.z));
    stack[sp++] = 0;
    while (sp > 0) {
        const vbox *b = &t->nodes[stack[--sp]];
        if (!vt_slab(b, o, inv, tmax)) continue;
        if (b->b > 0) {
            int i;
            for (i = b->a; i < b->a + b->b; i++) {
                float h;
                if (t->m->fhid[t->order[i]]) continue;   /* hidden faces are not drawn: nothing hides behind them */
                h = mf_face_ray_hit(t->m, t->order[i], o, dir);
                if (h > 0.0f && h < tmax) return 1;
            }
        } else if (sp + 2 <= 96) {
            stack[sp++] = -b->a - 1; stack[sp++] = -b->b - 1;
        }
    }
    return 0;
}

struct mf_vis { vtree t; int nf, nc; };   /* the tree plus the shape it was built for */

static void vis_centres(vtree *t) {
    int f;
    for (f = 0; f < t->m->nf; f++) {
        const int *vi = mf_face_verts(t->m, f);
        int k, sz = mf_face_size(t->m, f);
        mf_vec3 c = mf_v3(0, 0, 0);
        for (k = 0; k < sz; k++) c = mf_add(c, t->m->v[vi[k]]);
        t->cen[f] = mf_mul(c, 1.0f / (float)sz);
        t->order[f] = f;
    }
}

mf_vis *mf_vis_new(const mf_mesh *m) {
    mf_vis *vs;
    if (m->nf == 0) return NULL;
    vs = (mf_vis *)calloc(1, sizeof *vs);
    if (!vs) return NULL;
    vs->t.m = m; vs->nf = m->nf; vs->nc = m->nc;
    vs->t.nodes = (vbox *)malloc((size_t)(2 * m->nf + 2) * sizeof(vbox));
    vs->t.order = (int *)malloc((size_t)(m->nf + 1) * sizeof(int));
    vs->t.cen = (mf_vec3 *)malloc((size_t)(m->nf + 1) * sizeof(mf_vec3));
    if (!vs->t.nodes || !vs->t.order || !vs->t.cen) { mf_vis_free(vs); return NULL; }
    vis_centres(&vs->t);
    vt_build(&vs->t, 0, m->nf);
    return vs;
}

void mf_vis_free(mf_vis *vs) {
    if (!vs) return;
    free(vs->t.nodes); free(vs->t.order); free(vs->t.cen); free(vs);
}

/* After vertices MOVED (same faces): recompute every box from the leaves up, keeping the tree's shape. Children come after their parent
   in the node array, so a backwards walk sees both children first. 0 when the mesh is not the shape the tree was built for. */
int mf_vis_refit(mf_vis *vs, const mf_mesh *m) {
    int id;
    if (!vs || m->nf != vs->nf || m->nc != vs->nc) return 0;
    vs->t.m = m;
    for (id = vs->t.nn - 1; id >= 0; id--) {
        vbox *b = &vs->t.nodes[id];
        if (b->b > 0) vt_box(&vs->t, b->a, b->a + b->b, &b->lo, &b->hi);
        else {
            const vbox *l = &vs->t.nodes[-b->a - 1], *r = &vs->t.nodes[-b->b - 1];
            b->lo = mf_v3(l->lo.x < r->lo.x ? l->lo.x : r->lo.x, l->lo.y < r->lo.y ? l->lo.y : r->lo.y, l->lo.z < r->lo.z ? l->lo.z : r->lo.z);
            b->hi = mf_v3(l->hi.x > r->hi.x ? l->hi.x : r->hi.x, l->hi.y > r->hi.y ? l->hi.y : r->hi.y, l->hi.z > r->hi.z ? l->hi.z : r->hi.z);
        }
    }
    return 1;
}

int mf_vis_run(mf_vis *vs, const mf_mesh *m, mf_vec3 eye, unsigned char *vis, const unsigned char *want, int from, int to) {
    int v, n = 0;
    if (m->nf != vs->nf || m->nc != vs->nc) return -1;   /* the faces changed under the tree: build a new one */
    vs->t.m = m;   /* (the mesh may have moved in memory since the tree was built: indices stay valid, boxes may be a little off until a refit) */
    for (v = from; v < to; v++) {
        mf_vec3 d;
        float dist;
        if (want && !want[v]) { vis[v] = 1; n++; continue; }   /* nobody looks at it (off screen): not worth a ray */
        d = mf_sub(m->v[v], eye);
        dist = sqrtf(mf_dot(d, d));
        if (dist < 1e-6f) { vis[v] = 1; n++; continue; }
        vis[v] = vt_any(&vs->t, eye, mf_mul(d, 1.0f / dist), dist * 0.995f - 1e-4f) ? 0 : 1;
        n += vis[v];
    }
    return n;
}

int mf_visible_verts(const mf_mesh *m, mf_vec3 eye, unsigned char *vis) {
    mf_vis *vs = mf_vis_new(m);
    int n, v;
    if (!vs) {   /* no memory or nothing to hide behind: everything shows */
        for (v = 0; v < m->nv; v++) vis[v] = 1;
        return m->nv;
    }
    n = mf_vis_run(vs, m, eye, vis, NULL, 0, m->nv);
    mf_vis_free(vs);
    return n;
}

/* ---- edge loops and invert ---- */

/* 1 when exactly four quads meet at the vertex that corner c0 starts at, none open. Walks round the vertex (the corner before c is
   in the same face and ends here; its twin starts here: the next face round) instead of scanning every corner of the mesh. */
static int loop_pole_ok(const mf_mesh *m, int c0) {
    int c = c0, n = 0;
    do {
        int s = m->fstart[m->cface[c]], e = m->fstart[m->cface[c] + 1], prev, t;
        if (e - s != 4 || m->fhid[m->cface[c]]) return 0;
        prev = c > s ? c - 1 : e - 1;
        t = m->twin[prev];
        if (t < 0) return 0;
        c = t;
        if (++n > 4) return 0;
    } while (c != c0);
    return n == 4;
}

/* the edge that carries on straight from h (a -> b) past b: it starts at b; -1 when the loop ends */
static int loop_step(const mf_mesh *m, int h) {
    int n1 = mf_corner_next(m, h), t;
    if (!loop_pole_ok(m, n1)) return -1;
    t = m->twin[n1];
    if (t < 0) return -1;
    return mf_corner_next(m, t);
}

int mf_select_edge_loop(mf_mesh *m, int corner, int add) {
    int k, edges = 1, cur, limit;
    if (!m || corner < 0 || corner >= m->nc) return -1;
    if (!m->topo_ok && mf_build(m) != 0) return -1;
    if (!add) mf_sel_clear(m);
    mf_sel_vert(m, m->fidx[corner], 1);
    mf_sel_vert(m, m->fidx[mf_corner_next(m, corner)], 1);
    limit = m->nc;
    for (k = 0; k < 2; k++) {   /* forward, then backward from the twin */
        int steps = 0;
        cur = k == 0 ? corner : m->twin[corner];
        while (cur >= 0 && steps++ < limit) {
            cur = loop_step(m, cur);
            if (cur < 0) break;
            if (cur == corner || cur == m->twin[corner]) { if (k == 0) return edges; break; }
            mf_sel_vert(m, m->fidx[mf_corner_next(m, cur)], 1);
            edges++;
        }
    }
    return edges;
}

int mf_sel_invert(mf_mesh *m, int faces) {
    int i, n = 0;
    if (!m) return 0;
    if (faces) { for (i = 0; i < m->nf; i++) { m->fsel[i] = (unsigned char)(!m->fsel[i] && !m->fhid[i]); n += m->fsel[i]; } }
    else { for (i = 0; i < m->nv; i++) { m->vsel[i] = (unsigned char)(!m->vsel[i] && !m->vhid[i]); n += m->vsel[i]; } }
    return n;
}

/* ---- hide ---- */
int mf_hide_selected(mf_mesh *m, int faces_mode) {
    int f, i, v, n = 0;
    unsigned char *left = NULL;   /* per vertex: has a visible face */
    if (!m || (m->nv == 0 && m->nf == 0)) return 0;
    if (faces_mode) {
        for (f = 0; f < m->nf; f++) if (m->fsel[f] && !m->fhid[f]) { m->fhid[f] = 1; n++; }
    } else {
        for (v = 0; v < m->nv; v++) if (m->vsel[v] && !m->vhid[v]) { m->vhid[v] = 1; n++; }
        for (f = 0; f < m->nf; f++) {
            if (m->fhid[f]) continue;
            for (i = m->fstart[f]; i < m->fstart[f + 1]; i++)
                if (m->vhid[m->fidx[i]]) { m->fhid[f] = 1; n++; break; }
        }
    }
    left = (unsigned char *)calloc((size_t)(m->nv > 0 ? m->nv : 1), 1);
    if (!left) return n;
    for (f = 0; f < m->nf; f++)
        if (!m->fhid[f]) for (i = m->fstart[f]; i < m->fstart[f + 1]; i++) left[m->fidx[i]] = 1;
    if (faces_mode) {   /* a vertex whose faces are all hidden goes too (one that never had a face stays) */
        unsigned char *had = (unsigned char *)calloc((size_t)(m->nv > 0 ? m->nv : 1), 1);
        if (had) {
            for (f = 0; f < m->nf; f++) for (i = m->fstart[f]; i < m->fstart[f + 1]; i++) had[m->fidx[i]] = 1;
            for (v = 0; v < m->nv; v++) if (had[v] && !left[v] && !m->vhid[v]) { m->vhid[v] = 1; n++; }
            free(had);
        }
    }
    free(left);
    for (v = 0; v < m->nv; v++) if (m->vhid[v]) m->vsel[v] = 0;
    for (f = 0; f < m->nf; f++) if (m->fhid[f]) m->fsel[f] = 0;
    if (faces_mode && m->nv > 0) memset(m->vsel, 0, (size_t)m->nv);   /* face mode keeps its selection in fsel only */
    return n;
}

int mf_reveal_all(mf_mesh *m, int faces_mode) {
    int f, v, n = 0;
    if (!m) return 0;
    for (v = 0; v < m->nv; v++) if (m->vhid[v]) { m->vhid[v] = 0; n++; if (!faces_mode) m->vsel[v] = 1; }
    for (f = 0; f < m->nf; f++) if (m->fhid[f]) { m->fhid[f] = 0; n++; if (faces_mode) m->fsel[f] = 1; }
    return n;
}

int mf_hidden_count(const mf_mesh *m, int *verts, int *faces) {
    int v, f, nv = 0, nf = 0;
    if (!m) return 0;
    for (v = 0; v < m->nv; v++) nv += m->vhid[v];
    for (f = 0; f < m->nf; f++) nf += m->fhid[f];
    if (verts) *verts = nv;
    if (faces) *faces = nf;
    return nv + nf;
}
