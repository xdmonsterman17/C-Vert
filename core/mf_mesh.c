/* C-Vert core: mesh storage, topology (twins), checks, selection. See mf_mesh.h. */
#include "mf_mesh.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Grows an array so it holds `need` elements; keeps old contents. 0 ok, -1 no memory. */
static int grow(void **p, int *cap, int need, size_t elem) {
    int ncap;
    void *np;
    if (need <= *cap) return 0;
    ncap = *cap > 0 ? *cap : 8;
    while (ncap < need) ncap *= 2;
    np = realloc(*p, (size_t)ncap * elem);
    if (!np) return -1;
    *p = np;
    *cap = ncap;
    return 0;
}

mf_mesh *mf_new(void) {
    mf_mesh *m = (mf_mesh *)calloc(1, sizeof *m);
    if (!m) return NULL;
    /* fstart always holds nf + 1 entries: the leading 0 */
    if (grow((void **)&m->fstart, &m->cf, 1, sizeof(int)) != 0) { free(m); return NULL; }
    m->fstart[0] = 0;
    return m;
}

void mf_free(mf_mesh *m) {
    if (!m) return;
    free(m->v); free(m->vsel); free(m->vhid); free(m->fstart); free(m->fidx);
    free(m->fsel); free(m->fhid); free(m->twin); free(m->cface);
    free(m);
}

void mf_clear(mf_mesh *m) {
    m->nv = 0; m->nf = 0; m->nc = 0;
    m->fstart[0] = 0;
    m->topo_ok = 0;
}

mf_mesh *mf_clone(const mf_mesh *m) {
    mf_mesh *c = mf_new();
    if (!c) return NULL;
    if (m->nv > 0) {
        if (grow((void **)&c->v, &c->cv, m->nv, sizeof(mf_vec3)) != 0) goto fail;
        c->vsel = (unsigned char *)realloc(c->vsel, (size_t)c->cv);
        c->vhid = (unsigned char *)realloc(c->vhid, (size_t)c->cv);
        if (!c->vsel || !c->vhid) goto fail;
        memcpy(c->v, m->v, (size_t)m->nv * sizeof(mf_vec3));
        memcpy(c->vsel, m->vsel, (size_t)m->nv);
        memcpy(c->vhid, m->vhid, (size_t)m->nv);
    }
    if (m->nf > 0) {
        free(c->fstart); c->fstart = NULL; c->cf = 0;
        if (grow((void **)&c->fstart, &c->cf, m->nf + 1, sizeof(int)) != 0) goto fail;
        c->fsel = (unsigned char *)realloc(c->fsel, (size_t)c->cf);
        c->fhid = (unsigned char *)realloc(c->fhid, (size_t)c->cf);
        if (!c->fsel || !c->fhid) goto fail;
        memcpy(c->fstart, m->fstart, (size_t)(m->nf + 1) * sizeof(int));
        memcpy(c->fsel, m->fsel, (size_t)m->nf);
        memcpy(c->fhid, m->fhid, (size_t)m->nf);
        if (grow((void **)&c->fidx, &c->cc, m->nc, sizeof(int)) != 0) goto fail;
        memcpy(c->fidx, m->fidx, (size_t)m->nc * sizeof(int));
    }
    c->nv = m->nv; c->nf = m->nf; c->nc = m->nc;
    c->topo_ok = 0;
    return c;
fail:
    mf_free(c);
    return NULL;
}

int mf_add_vert(mf_mesh *m, float x, float y, float z) {
    if (grow((void **)&m->v, &m->cv, m->nv + 1, sizeof(mf_vec3)) != 0) return -1;
    {   /* vsel shares v's capacity */
        unsigned char *ns = (unsigned char *)realloc(m->vsel, (size_t)m->cv);
        if (!ns) return -1;
        m->vsel = ns;
        ns = (unsigned char *)realloc(m->vhid, (size_t)m->cv);
        if (!ns) return -1;
        m->vhid = ns;
    }
    m->v[m->nv] = mf_v3(x, y, z);
    m->vsel[m->nv] = 0;
    m->vhid[m->nv] = 0;
    m->topo_ok = 0;
    return m->nv++;
}

int mf_add_face(mf_mesh *m, const int *idx, int n) {
    int i;
    if (n < 3) return -1;
    for (i = 0; i < n; i++)
        if (idx[i] < 0 || idx[i] >= m->nv) return -1;
    if (grow((void **)&m->fstart, &m->cf, m->nf + 2, sizeof(int)) != 0) return -1;
    if (grow((void **)&m->fidx, &m->cc, m->nc + n, sizeof(int)) != 0) return -1;
    {   /* fsel shares fstart's capacity (one less entry is used) */
        unsigned char *ns = (unsigned char *)realloc(m->fsel, (size_t)m->cf);
        if (!ns) return -1;
        m->fsel = ns;
        ns = (unsigned char *)realloc(m->fhid, (size_t)m->cf);
        if (!ns) return -1;
        m->fhid = ns;
    }
    memcpy(m->fidx + m->nc, idx, (size_t)n * sizeof(int));
    m->nc += n;
    m->fsel[m->nf] = 0;
    m->fhid[m->nf] = 0;
    m->fstart[m->nf + 1] = m->nc;
    m->topo_ok = 0;
    return m->nf++;
}

int mf_face_size(const mf_mesh *m, int f) { return m->fstart[f + 1] - m->fstart[f]; }
const int *mf_face_verts(const mf_mesh *m, int f) { return m->fidx + m->fstart[f]; }

int mf_corner_next(const mf_mesh *m, int c) {
    /* the face of corner c is found by its corner range: cface when built, else a search */
    int f;
    if (m->topo_ok) f = m->cface[c];
    else {
        int lo = 0, hi = m->nf - 1;
        while (lo < hi) {   /* last face whose start <= c */
            int mid = (lo + hi + 1) / 2;
            if (m->fstart[mid] <= c) lo = mid; else hi = mid - 1;
        }
        f = lo;
    }
    return c + 1 < m->fstart[f + 1] ? c + 1 : m->fstart[f];
}

mf_vec3 mf_face_normal(const mf_mesh *m, int f) {
    /* Newell's method: right for any planar (and nearly planar) polygon */
    const int *vi = mf_face_verts(m, f);
    int n = mf_face_size(m, f), i;
    mf_vec3 s = mf_v3(0, 0, 0);
    float len;
    for (i = 0; i < n; i++) {
        mf_vec3 a = m->v[vi[i]], b = m->v[vi[(i + 1) % n]];
        s.x += (a.y - b.y) * (a.z + b.z);
        s.y += (a.z - b.z) * (a.x + b.x);
        s.z += (a.x - b.x) * (a.y + b.y);
    }
    len = sqrtf(mf_dot(s, s));
    return len > 1e-12f ? mf_mul(s, 1.0f / len) : mf_v3(0, 0, 0);
}

mf_vec3 mf_face_center(const mf_mesh *m, int f) {
    const int *vi = mf_face_verts(m, f);
    int n = mf_face_size(m, f), i;
    mf_vec3 s = mf_v3(0, 0, 0);
    for (i = 0; i < n; i++) s = mf_add(s, m->v[vi[i]]);
    return mf_mul(s, 1.0f / (float)n);
}

/* ---- topology ---- */

/* Half-edge pairing without a sort: corners are bucketed twice (by start vertex and by end vertex: counting sorts, sequential). A corner
   a->b needs the corners b->a (reverse edge) and the other a->b (same edge again): they are in out[b] and in[a] / out[a] and in[b], and
   it looks through whichever of each pair is shorter, so a hub vertex of valence 40000 (merge to centre makes those) costs nothing.
   Ordinary vertices have a handful of corners: a few compares a corner, ~10x faster than the qsort this replaced at 30k faces.
   An edge is counted once, at its lowest corner. */
static int csr_build(const int *key, int nc, int nv, int *start, int *list) {   /* list[start[v] .. start[v+1]) = corners with key v, in corner order */
    int *fill = (int *)malloc((size_t)nv * sizeof(int)), v, c;
    if (!fill) return -1;
    for (v = 0; v <= nv; v++) start[v] = 0;
    for (c = 0; c < nc; c++) start[key[c] + 1]++;
    for (v = 0; v < nv; v++) start[v + 1] += start[v];
    memcpy(fill, start, (size_t)nv * sizeof(int));
    for (c = 0; c < nc; c++) list[fill[key[c]]++] = c;
    free(fill);
    return 0;
}

int mf_build(mf_mesh *m) {
    int *os = NULL, *ol = NULL, *is = NULL, *il = NULL, *nxt = NULL;
    int f, c, nv = m->nv > 0 ? m->nv : 1, nc = m->nc > 0 ? m->nc : 1;
    free(m->twin); free(m->cface);
    m->twin = (int *)malloc((size_t)nc * sizeof(int));
    m->cface = (int *)malloc((size_t)nc * sizeof(int));
    os = (int *)malloc((size_t)(nv + 1) * sizeof(int)); is = (int *)malloc((size_t)(nv + 1) * sizeof(int));
    ol = (int *)malloc((size_t)nc * sizeof(int)); il = (int *)malloc((size_t)nc * sizeof(int));
    nxt = (int *)malloc((size_t)nc * sizeof(int));
    if (!m->twin || !m->cface || !os || !is || !ol || !il || !nxt) goto fail;
    for (f = 0; f < m->nf; f++)
        for (c = m->fstart[f]; c < m->fstart[f + 1]; c++) {
            m->cface[c] = f;
            m->twin[c] = -1;
            nxt[c] = m->fidx[c + 1 < m->fstart[f + 1] ? c + 1 : m->fstart[f]];
        }
    if (csr_build(m->fidx, m->nc, nv, os, ol) || csr_build(nxt, m->nc, nv, is, il)) goto fail;
    m->ne = 0; m->nboundary = 0; m->nnonmanifold = 0;
    for (c = 0; c < m->nc; c++) {
        int a = m->fidx[c], b = nxt[c], i, same = 0, rev = 0, first = c, rc = -1;
        if (os[b + 1] - os[b] <= is[a + 1] - is[a]) {   /* reverse corners: start at b and end at a */
            for (i = os[b]; i < os[b + 1]; i++) { int d = ol[i]; if (nxt[d] == a) { rev++; rc = d; if (d < first) first = d; } }
        } else {
            for (i = is[a]; i < is[a + 1]; i++) { int d = il[i]; if (m->fidx[d] == b) { rev++; rc = d; if (d < first) first = d; } }
        }
        if (os[a + 1] - os[a] <= is[b + 1] - is[b]) {   /* the same edge again: start at a and end at b */
            for (i = os[a]; i < os[a + 1]; i++) { int d = ol[i]; if (d != c && nxt[d] == b) { same++; if (d < first) first = d; } }
        } else {
            for (i = is[b]; i < is[b + 1]; i++) { int d = il[i]; if (d != c && m->fidx[d] == a) { same++; if (d < first) first = d; } }
        }
        if (rev == 1 && same == 0) m->twin[c] = rc;   /* a clean pair (the same on both sides: each corner finds the other) */
        if (first != c) continue;   /* the edge is counted at its lowest corner */
        m->ne++;
        if (1 + same + rev == 1) m->nboundary++;
        else if (!(1 + same + rev == 2 && rev == 1)) m->nnonmanifold++;
    }
    free(os); free(ol); free(is); free(il); free(nxt);
    m->topo_ok = 1;
    return 0;
fail:
    free(os); free(ol); free(is); free(il); free(nxt); free(m->twin); free(m->cface);
    m->twin = m->cface = NULL; m->topo_ok = 0;
    return -1;
}

mf_stats mf_stats_get(mf_mesh *m) {
    mf_stats s;
    if (!m->topo_ok) mf_build(m);
    s.verts = m->nv; s.faces = m->nf;
    s.edges = m->topo_ok ? m->ne : 0;
    s.boundary = m->topo_ok ? m->nboundary : 0;
    s.nonmanifold = m->topo_ok ? m->nnonmanifold : 0;
    s.euler = s.verts - s.edges + s.faces;
    return s;
}

int mf_check(const mf_mesh *m, char *msg, size_t msgn) {
    int f, i, j;
    for (f = 0; f < m->nf; f++) {
        const int *vi = mf_face_verts(m, f);
        int n = mf_face_size(m, f);
        for (i = 0; i < n; i++) {
            if (vi[i] < 0 || vi[i] >= m->nv) {
                if (msg && msgn) snprintf(msg, msgn, "face %d: vertex index %d out of range (0..%d)", f, vi[i], m->nv - 1);
                return -1;
            }
            for (j = i + 1; j < n; j++)
                if (vi[i] == vi[j]) {
                    if (msg && msgn) snprintf(msg, msgn, "face %d: vertex %d used twice", f, vi[i]);
                    return -1;
                }
        }
    }
    return 0;
}

/* ---- selection ---- */

void mf_sel_clear(mf_mesh *m) {
    if (m->nv > 0) memset(m->vsel, 0, (size_t)m->nv);
    if (m->nf > 0) memset(m->fsel, 0, (size_t)m->nf);
}

void mf_sel_face(mf_mesh *m, int f, int on) {
    if (f >= 0 && f < m->nf) m->fsel[f] = on ? 1 : 0;
}

int mf_sel_face_count(const mf_mesh *m) {
    int f, n = 0;
    for (f = 0; f < m->nf; f++) n += m->fsel[f];
    return n;
}

int mf_vert_selected(const mf_mesh *m, int v) {
    int f, i;
    if (v < 0 || v >= m->nv) return 0;
    if (m->vsel[v]) return 1;
    for (f = 0; f < m->nf; f++) {
        if (!m->fsel[f]) continue;
        for (i = m->fstart[f]; i < m->fstart[f + 1]; i++)
            if (m->fidx[i] == v) return 1;
    }
    return 0;
}

int mf_sel_vert_count(const mf_mesh *m) {
    unsigned char *mark = (unsigned char *)calloc((size_t)(m->nv > 0 ? m->nv : 1), 1);
    int v, f, i, n = 0;
    if (!mark) return 0;
    for (v = 0; v < m->nv; v++) mark[v] = m->vsel[v];
    for (f = 0; f < m->nf; f++)
        if (m->fsel[f])
            for (i = m->fstart[f]; i < m->fstart[f + 1]; i++) mark[m->fidx[i]] = 1;
    for (v = 0; v < m->nv; v++) n += mark[v];
    free(mark);
    return n;
}
