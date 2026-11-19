/* C-Vert core: milestone 3 operators (delete, merge, flip, inset, loop cut, bevel). See mf_mesh.h. */
#include "mf_mesh.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* m takes over n's contents (n is freed). 0 ok. */
static int swap_in(mf_mesh *m, mf_mesh *n) {
    mf_mesh t;
    if (!n) return -1;
    t = *m; *m = *n; *n = t;
    mf_free(n);
    m->topo_ok = 0;
    return mf_build(m);
}

/* A rebuilt copy of m: faces with keepf[f] == 0 are dropped (keepf NULL: all kept); vertices go through rep (rep[v] = the
   vertex v is merged into, rep NULL: none merged); a face loses consecutive duplicate corners and is dropped when fewer than 3
   remain or a vertex still repeats; then a vertex that no kept face uses is removed when cand is NULL or cand[v] != 0
   (merged-away vertices always go). A merged vertex is selected when any vertex merged into it was. NULL: no memory. */
static mf_mesh *rebuilt(const mf_mesh *m, const unsigned char *keepf, const int *rep, const unsigned char *cand) {
    mf_mesh *n = mf_new();
    int *loop = (int *)malloc((size_t)(m->nc > 0 ? m->nc : 1) * sizeof(int)), *newid = (int *)malloc((size_t)(m->nv > 0 ? m->nv : 1) * sizeof(int));
    unsigned char *used = (unsigned char *)calloc((size_t)(m->nv > 0 ? m->nv : 1), 1), *heir = (unsigned char *)calloc((size_t)(m->nv > 0 ? m->nv : 1), 1), *vs = (unsigned char *)calloc((size_t)(m->nv > 0 ? m->nv : 1), 1);
    int *fs = (int *)malloc((size_t)(m->nf + 1) * sizeof(int)), *fo = (int *)malloc((size_t)(m->nf > 0 ? m->nf : 1) * sizeof(int));
    int f, i, k, nl = 0, nfk = 0, v;
    if (!n || !loop || !newid || !used || !heir || !vs || !fs || !fo) goto fail;
    for (f = 0; f < m->nf; f++) {   /* the cleaned loops */
        int s = m->fstart[f], e = m->fstart[f + 1], first = nl, cnt = 0, ok = 1;
        if (keepf && !keepf[f]) continue;
        for (i = s; i < e; i++) {
            int w = rep ? rep[m->fidx[i]] : m->fidx[i];
            if (cnt > 0 && loop[first + cnt - 1] == w) continue;
            loop[first + cnt++] = w;
        }
        if (cnt > 1 && loop[first] == loop[first + cnt - 1]) cnt--;
        if (cnt < 3) continue;
        for (i = 0; i < cnt && ok; i++) for (k = i + 1; k < cnt; k++) if (loop[first + i] == loop[first + k]) { ok = 0; break; }
        if (!ok) continue;
        fs[nfk] = first; fo[nfk] = f; nfk++; nl = first + cnt;
        for (i = 0; i < cnt; i++) used[loop[first + i]] = 1;
    }
    for (v = 0; v < m->nv; v++) { if (m->vsel[v]) vs[rep ? rep[v] : v] = 1; if (rep && rep[v] != v) heir[rep[v]] = 1; }
    for (v = 0; v < m->nv; v++) {
        int gone = (rep && rep[v] != v) || (!used[v] && !heir[v] && (!cand || cand[v]));   /* a vertex others merged into stays, even loose */
        newid[v] = -1;
        if (gone) continue;
        newid[v] = mf_add_vert(n, m->v[v].x, m->v[v].y, m->v[v].z);
        if (newid[v] < 0) goto fail;
        n->vsel[newid[v]] = vs[v];
        n->vhid[newid[v]] = m->vhid[v];
    }
    for (f = 0; f < nfk; f++) {
        int s = fs[f], e = f + 1 < nfk ? fs[f + 1] : nl, cnt = 0, tmp[64], big = 0, *p;
        int *dyn = NULL;
        /* fs[] marks starts; the loop's end is the next start (loops are stored back to back) */
        cnt = e - s;
        if (cnt > 64) { dyn = (int *)malloc((size_t)cnt * sizeof(int)); if (!dyn) goto fail; big = 1; }
        p = big ? dyn : tmp;
        for (i = 0; i < cnt; i++) p[i] = newid[loop[s + i]];
        i = mf_add_face(n, p, cnt);
        free(dyn);
        if (i < 0) goto fail;
        n->fsel[i] = m->fsel[fo[f]];
        n->fhid[i] = m->fhid[fo[f]];
    }
    free(loop); free(newid); free(used); free(heir); free(vs); free(fs); free(fo);
    return n;
fail:
    free(loop); free(newid); free(used); free(heir); free(vs); free(fs); free(fo); mf_free(n);
    return NULL;
}

int mf_remove_unused(mf_mesh *m) {
    int before = m->nv;
    if (swap_in(m, rebuilt(m, NULL, NULL, NULL)) != 0) return -1;
    return before - m->nv;
}

/* the vertices of the faces dropped by keepf (candidates for removal when nothing else uses them) */
static unsigned char *dropped_verts(const mf_mesh *m, const unsigned char *keepf) {
    unsigned char *c = (unsigned char *)calloc((size_t)(m->nv > 0 ? m->nv : 1), 1);
    int f, i;
    if (!c) return NULL;
    for (f = 0; f < m->nf; f++) if (!keepf[f]) for (i = m->fstart[f]; i < m->fstart[f + 1]; i++) c[m->fidx[i]] = 1;
    return c;
}

static int delete_with(mf_mesh *m, unsigned char *keepf, const unsigned char *extra_cand) {
    unsigned char *cand = dropped_verts(m, keepf);
    int before = m->nv + m->nf, v, r;
    if (!cand) { free(keepf); return -1; }
    if (extra_cand) for (v = 0; v < m->nv; v++) if (extra_cand[v]) cand[v] = 1;
    r = swap_in(m, rebuilt(m, keepf, NULL, cand));
    free(cand); free(keepf);
    return r != 0 ? -1 : before - (m->nv + m->nf);
}

int mf_delete_verts(mf_mesh *m) {
    unsigned char *keep = (unsigned char *)malloc((size_t)(m->nf > 0 ? m->nf : 1));
    int f, i;
    if (!keep) return -1;
    for (f = 0; f < m->nf; f++) {
        keep[f] = 1;
        for (i = m->fstart[f]; i < m->fstart[f + 1]; i++) if (m->vsel[m->fidx[i]]) keep[f] = 0;
    }
    return delete_with(m, keep, m->vsel);
}

int mf_delete_faces(mf_mesh *m) {
    unsigned char *keep = (unsigned char *)malloc((size_t)(m->nf > 0 ? m->nf : 1));
    int f;
    if (!keep) return -1;
    for (f = 0; f < m->nf; f++) keep[f] = !m->fsel[f];
    return delete_with(m, keep, NULL);
}

int mf_delete_edges(mf_mesh *m) {   /* faces that use a selected edge (both ends in vsel) */
    unsigned char *keep = (unsigned char *)malloc((size_t)(m->nf > 0 ? m->nf : 1));
    int f, i;
    if (!keep) return -1;
    for (f = 0; f < m->nf; f++) {
        keep[f] = 1;
        for (i = m->fstart[f]; i < m->fstart[f + 1]; i++)
            if (m->vsel[m->fidx[i]] && m->vsel[m->fidx[i + 1 < m->fstart[f + 1] ? i + 1 : m->fstart[f]]]) keep[f] = 0;
    }
    return delete_with(m, keep, NULL);
}

int mf_flip_faces(mf_mesh *m, int all) {
    int f, i, j, count = 0;
    for (f = 0; f < m->nf; f++) {
        if ((!all && !m->fsel[f]) || m->fhid[f]) continue;
        for (i = m->fstart[f], j = m->fstart[f + 1] - 1; i < j; i++, j--) { int t = m->fidx[i]; m->fidx[i] = m->fidx[j]; m->fidx[j] = t; }
        count++;
    }
    m->topo_ok = 0;
    mf_build(m);
    return count;
}

int mf_merge_center(mf_mesh *m) {
    int *rep = (int *)malloc((size_t)(m->nv > 0 ? m->nv : 1) * sizeof(int));
    int v, first = -1, cnt = 0, before = m->nv;
    mf_vec3 c = mf_v3(0, 0, 0);
    if (!rep) return -1;
    for (v = 0; v < m->nv; v++) {
        rep[v] = v;
        if (m->vsel[v]) { if (first < 0) first = v; c = mf_add(c, m->v[v]); cnt++; }
    }
    if (cnt < 2) { free(rep); return 0; }
    for (v = 0; v < m->nv; v++) if (m->vsel[v]) rep[v] = first;
    m->v[first] = mf_mul(c, 1.0f / (float)cnt);
    v = swap_in(m, rebuilt(m, NULL, rep, NULL));
    free(rep);
    return v != 0 ? -1 : before - m->nv;
}

/* A hash grid with cells of `dist`: a vertex only has to look at the 27 cells around its own. (The plain all-pairs loop took 3 s at 30k vertices.)
   Same result as all-pairs: vertices in index order, each takes every later unmerged vertex within dist. */
static long cell_of(float x, float cell) {   /* floor(x / cell), kept inside what a long holds */
    float q = floorf(x / cell);
    return q > 1e15f ? 1000000000000000L : (q < -1e15f ? -1000000000000000L : (long)q);
}
static unsigned cell_hash(long x, long y, long z, unsigned mask) {
    unsigned long long h = (unsigned long long)x * 73856093ULL ^ (unsigned long long)y * 19349663ULL ^ (unsigned long long)z * 83492791ULL;
    h ^= h >> 29; h *= 0xbf58476d1ce4e5b9ULL; h ^= h >> 32;
    return (unsigned)h & mask;
}

int mf_merge_by_distance(mf_mesh *m, float dist, int only_selected) {
    int *rep = (int *)malloc((size_t)(m->nv > 0 ? m->nv : 1) * sizeof(int));
    int *head, *next, a, b, before = m->nv, r;
    unsigned size = 16, mask;
    float d2 = dist * dist, cell = dist > 1e-6f ? dist : 1e-6f;
    if (!rep) return -1;
    while (size < (unsigned)m->nv * 2u) size <<= 1;
    mask = size - 1;
    head = (int *)malloc((size_t)size * sizeof(int));
    next = (int *)malloc((size_t)(m->nv > 0 ? m->nv : 1) * sizeof(int));
    if (!head || !next) { free(rep); free(head); free(next); return -1; }
    for (a = 0; a < (int)size; a++) head[a] = -1;
    for (a = m->nv - 1; a >= 0; a--) {   /* backwards, so each bucket lists its vertices in ascending order */
        unsigned h;
        rep[a] = a;
        if ((only_selected && !m->vsel[a]) || m->vhid[a]) continue;
        h = cell_hash(cell_of(m->v[a].x, cell), cell_of(m->v[a].y, cell), cell_of(m->v[a].z, cell), mask);
        next[a] = head[h]; head[h] = a;
    }
    for (a = 0; a < m->nv; a++) {
        long cx, cy, cz;
        int dx, dy, dz;
        if (rep[a] != a || (only_selected && !m->vsel[a]) || m->vhid[a]) continue;
        cx = cell_of(m->v[a].x, cell); cy = cell_of(m->v[a].y, cell); cz = cell_of(m->v[a].z, cell);
        for (dx = -1; dx <= 1; dx++) for (dy = -1; dy <= 1; dy++) for (dz = -1; dz <= 1; dz++)
            for (b = head[cell_hash(cx + dx, cy + dy, cz + dz, mask)]; b >= 0; b = next[b]) {
                mf_vec3 d;
                if (b <= a || rep[b] != b) continue;
                d = mf_sub(m->v[a], m->v[b]);
                if (mf_dot(d, d) <= d2) rep[b] = a;
            }
    }
    free(head); free(next);
    r = swap_in(m, rebuilt(m, NULL, rep, NULL));
    free(rep);
    return r != 0 ? -1 : before - m->nv;
}

/* ---- inset (each selected face on its own, Blender's "individual") ---- */
int mf_inset_faces(mf_mesh *m, float amount) {
    int f, nf0 = m->nf, added = 0;
    if (mf_sel_face_count(m) == 0 || amount <= 0) return 0;
    for (f = 0; f < nf0; f++) {
        int s, n, k, q[4];
        mf_vec3 nrm, c, off[64];
        float maxa = 1e30f;
        const int *vs;
        int ring[64];
        if (!m->fsel[f]) continue;
        n = mf_face_size(m, f);
        if (n > 64) continue;
        s = m->fstart[f];
        vs = m->fidx + s;
        nrm = mf_face_normal(m, f);
        c = mf_face_center(m, f);
        for (k = 0; k < n; k++) {   /* inward offset of every corner so each edge moves in by `amount` */
            mf_vec3 p = m->v[vs[k]], pp = m->v[vs[(k + n - 1) % n]], pn = m->v[vs[(k + 1) % n]];
            mf_vec3 e1 = mf_sub(p, pp), e2 = mf_sub(pn, p), i1, i2;
            float l1 = sqrtf(mf_dot(e1, e1)), l2 = sqrtf(mf_dot(e2, e2)), dd;
            if (l1 < 1e-9f || l2 < 1e-9f) { off[k] = mf_v3(0, 0, 0); continue; }
            e1 = mf_mul(e1, 1.0f / l1); e2 = mf_mul(e2, 1.0f / l2);
            i1 = mf_cross(nrm, e1); i2 = mf_cross(nrm, e2);   /* inward for a counter-clockwise face seen along its normal */
            dd = 1.0f + mf_dot(i1, i2);
            if (dd < 0.05f) dd = 0.05f;
            off[k] = mf_mul(mf_add(i1, i2), 1.0f / dd);
            {   /* the most the edge before may move before it passes the centre */
                float dist = mf_dot(mf_sub(c, p), i1);
                if (dist > 1e-9f && dist < maxa) maxa = dist;
            }
        }
        {   float a = amount < maxa * 0.95f ? amount : maxa * 0.95f;
            for (k = 0; k < n; k++) {
                mf_vec3 p = mf_add(m->v[vs[k]], mf_mul(off[k], a));
                ring[k] = mf_add_vert(m, p.x, p.y, p.z);
                if (ring[k] < 0) return -1;
            }
        }
        vs = m->fidx + m->fstart[f];   /* (vertex adds do not move the corner list, but stay safe) */
        for (k = 0; k < n; k++) {   /* a quad of the rim for every edge: (a, b, b', a'), carries a -> b like the old face */
            vs = m->fidx + m->fstart[f];   /* mf_add_face may have moved the corner list */
            q[0] = vs[k]; q[1] = vs[(k + 1) % n]; q[2] = ring[(k + 1) % n]; q[3] = ring[k];
            if (mf_add_face(m, q, 4) < 0) return -1;
            added++;
        }
        for (k = 0; k < n; k++) m->fidx[m->fstart[f] + k] = ring[k];   /* the face itself shrinks onto the new ring */
    }
    m->topo_ok = 0;
    mf_build(m);
    return added;
}

/* ---- loop cut ---- */
typedef struct ringface { int f, c1, c2; } ringface;

int mf_loop_cut(mf_mesh *m, int corner, int cuts) {
    int cap, nr = 0, i, k, c, ok = 1, closed = 0, added = 0, result = -1;
    ringface *rf = NULL;
    unsigned char *seen = NULL;
    int *ev = NULL;   /* per canonical edge corner: cuts vertex ids (index = corner * cuts) */
    if (cuts < 1 || cuts > 16 || corner < 0) return 0;
    if (!m->topo_ok && mf_build(m) != 0) return -1;
    if (corner >= m->nc) return 0;
    cap = m->nf;
    rf = (ringface *)malloc((size_t)(cap > 0 ? cap : 1) * sizeof *rf);
    seen = (unsigned char *)calloc((size_t)(m->nf > 0 ? m->nf : 1), 1);
    ev = (int *)malloc((size_t)m->nc * (size_t)cuts * sizeof(int));
    if (!rf || !seen || !ev) goto done;
    for (i = 0; i < m->nc * cuts; i++) ev[i] = -1;
    for (k = 0; k < 2 && ok && !closed; k++) {   /* forward from the corner, then backward from its twin */
        int cur = k == 0 ? corner : m->twin[corner];
        if (cur < 0) break;
        for (;;) {
            int f = m->cface[cur], opp;
            if (mf_face_size(m, f) != 4 || m->fhid[f]) { ok = 0; break; }   /* a hidden face ends the ring like a triangle does */
            if (seen[f]) { if (k == 0 && f == m->cface[corner] && nr > 0) closed = 1; else ok = 0; break; }
            opp = m->fstart[f] + ((cur - m->fstart[f] + 2) % 4);
            seen[f] = 1; rf[nr].f = f; rf[nr].c1 = cur; rf[nr].c2 = opp; nr++;
            if (m->twin[opp] < 0) break;   /* an open end on the boundary */
            cur = m->twin[opp];
        }
    }
    if (!ok) { result = -2; goto done; }
    if (nr == 0) { result = 0; goto done; }
    for (i = 0; i < nr; i++) {   /* the new vertices of every ring edge, in the direction of its lower corner */
        for (k = 0; k < 2; k++) {
            int x = k == 0 ? rf[i].c1 : rf[i].c2, t = m->twin[x], can = (t >= 0 && t < x) ? t : x;
            mf_vec3 a, b;
            if (ev[can * cuts] >= 0) continue;
            a = m->v[m->fidx[can]]; b = m->v[m->fidx[mf_corner_next(m, can)]];
            for (c = 0; c < cuts; c++) {
                float u = (float)(c + 1) / (float)(cuts + 1);
                int id = mf_add_vert(m, a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u);
                if (id < 0) goto done;
                ev[can * cuts + c] = id;
                m->vsel[id] = 1;
            }
        }
    }
    for (i = 0; i < nr; i++) {
        int f = rf[i].f, s = m->fstart[f], n1 = rf[i].c1 - s, vq[4], S[18], T[18], strip, sel = m->fsel[f];
        for (k = 0; k < 4; k++) vq[k] = m->fidx[s + (n1 + k) % 4];   /* v0 v1 v2 v3: edge c1 is v0 -> v1, edge c2 is v2 -> v3 */
        S[0] = vq[0]; T[0] = vq[3];
        for (c = 1; c <= cuts; c++) {
            int x = rf[i].c1, t = m->twin[x], can = (t >= 0 && t < x) ? t : x, rev = can != x;
            int y = rf[i].c2, ty = m->twin[y], cany = (ty >= 0 && ty < y) ? ty : y, revy = cany != y;
            S[c] = ev[can * cuts + (rev ? cuts - c : c - 1)];   /* along v0 -> v1 */
            T[c] = ev[cany * cuts + (revy ? c - 1 : cuts - c)]; /* along v3 -> v2: edge c2 runs v2 -> v3, so this is its reverse */
        }
        S[cuts + 1] = vq[1]; T[cuts + 1] = vq[2];
        for (strip = 0; strip <= cuts; strip++) {
            int q[4];
            q[0] = S[strip]; q[1] = S[strip + 1]; q[2] = T[strip + 1]; q[3] = T[strip];
            if (strip == 0) { for (k = 0; k < 4; k++) m->fidx[s + k] = q[k]; }
            else {
                int nfi = mf_add_face(m, q, 4);
                if (nfi < 0) goto done;
                m->fsel[nfi] = (unsigned char)sel;
                added++;
            }
        }
    }
    m->topo_ok = 0;
    mf_build(m);
    result = added;
done:
    free(rf); free(seen); free(ev);
    return result;
}

/* ---- bevel: see mf_bevel.c ---- */

/* ---- region inset: the selected faces as one patch, its rim moved inward ---- */
int mf_inset_region(mf_mesh *m, float amount) {
    int nc0, c, f, v, nv0, result = -1;
    int *saved = NULL, *cnt = NULL;
    mf_vec3 *acc = NULL, *first = NULL, *dv = NULL;
    if (mf_sel_face_count(m) == 0) return 0;
    if (!m->topo_ok && mf_build(m) != 0) return -1;
    nc0 = m->nc; nv0 = m->nv;
    saved = (int *)malloc((size_t)nc0 * sizeof(int));
    cnt = (int *)calloc((size_t)nv0, sizeof(int));
    acc = (mf_vec3 *)calloc((size_t)nv0, sizeof(mf_vec3));
    first = (mf_vec3 *)calloc((size_t)nv0, sizeof(mf_vec3));
    dv = (mf_vec3 *)calloc((size_t)nv0, sizeof(mf_vec3));
    if (!saved || !cnt || !acc || !first || !dv) goto done;
    memcpy(saved, m->fidx, (size_t)nc0 * sizeof(int));
    /* the inward direction of every rim edge (left of a -> b for a counter-clockwise face), summed per vertex */
    for (f = 0; f < m->nf; f++) {
        mf_vec3 n;
        if (!m->fsel[f]) continue;
        n = mf_face_normal(m, f);
        for (c = m->fstart[f]; c < m->fstart[f + 1]; c++) {
            int t = m->twin[c], a, b;
            mf_vec3 d, in;
            float len;
            if (t >= 0 && m->fsel[m->cface[t]]) continue;
            a = m->fidx[c]; b = m->fidx[mf_corner_next(m, c)];
            d = mf_sub(m->v[b], m->v[a]);
            in = mf_cross(n, d);
            len = sqrtf(mf_dot(in, in));
            if (len < 1e-12f) continue;
            in = mf_mul(in, 1.0f / len);
            if (cnt[a] == 0) first[a] = in;
            if (cnt[b] == 0) first[b] = in;
            acc[a] = mf_add(acc[a], in); acc[b] = mf_add(acc[b], in);
            cnt[a]++; cnt[b]++;
        }
    }
    {   int any = 0;
        for (v = 0; v < nv0; v++) {
            if (cnt[v] == 0) continue;
            any = 1;
            if (cnt[v] == 2) {   /* a plain corner of the rim: the miter keeps both edges `amount` away */
                float d = 1.0f + mf_dot(first[v], mf_sub(acc[v], first[v]));
                dv[v] = mf_mul(acc[v], amount / (d > 0.2f ? d : 0.2f));
            } else {
                float len = sqrtf(mf_dot(acc[v], acc[v]));
                if (len > 1e-12f) dv[v] = mf_mul(acc[v], amount / len);
            }
        }
        if (!any) { result = -2; goto done; }   /* the region has no rim (the whole closed mesh) */
    }
    result = mf_extrude_selected(m, 0.0f);
    if (result < 0) goto done;
    for (f = 0; f < m->nf; f++) {   /* the region's corners were rewritten in place: saved[c] is the old vertex, fidx[c] its copy */
        if (!m->fsel[f]) continue;
        for (c = m->fstart[f]; c < m->fstart[f + 1] && c < nc0; c++)
            if (saved[c] < nv0) m->v[m->fidx[c]] = mf_add(m->v[m->fidx[c]], dv[saved[c]]);
    }
    mf_remove_unused(m);
    m->topo_ok = 0;
    mf_build(m);
done:
    free(saved); free(cnt); free(acc); free(first); free(dv);
    return result;
}
