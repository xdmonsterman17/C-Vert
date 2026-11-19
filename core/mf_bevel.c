/* C-Vert core: the general edge bevel. See mf_mesh.h (mf_bevel_edges_ex).

   How it works, vertex by vertex. Round a vertex V the faces form a fan; call the edges E_0, E_1, ... in order, face f_j lying between
   E_j (the edge its corner leaves along) and E_(j+1) (the one it arrives by). Some edges are beveled. They cut the fan into wedges of
   faces. For every beveled edge E two new points sit at V, one in each neighbouring face, `width` away from E measured inside that
   face (Rv: in the face after E, Lv: in the face before it). Where they go depends on the wedge between two beveled edges E_a and E_b:
     - no edge between them (one face): both beveled edges' offset lines meet at one point M (a miter), Rv = Lv = M;
     - one plain edge between: Rv and Lv are the same point S, slid along that edge;
     - two or more: Rv slides along the first plain edge, Lv along the last, the ones between end at a new point W (V's place).
   A face's corner at V is then replaced by the points its two edges end at (one or two of them). The strip along each beveled edge is
   `segments` quads wide, between two profile curves (one per end) that run from Lv to Rv through a rounded (or flat, or squared) shape.
   A closed fan leaves a hole at V (the ends of the profile curves joined by the wedge paths): it is filled with one face. A fan on a
   mesh boundary needs no filling: the hole is the outside.  Everything is computed from the ORIGINAL positions, then built into a new mesh. */
#include "mf_mesh.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int corner_prev(const mf_mesh *m, int c) {
    int f = m->cface[c], s = m->fstart[f];
    return c > s ? c - 1 : m->fstart[f + 1] - 1;
}
static int rot_next(const mf_mesh *m, int c) { return m->twin[corner_prev(m, c)]; }          /* the corner of the next face round the vertex, or -1 */
static int rot_prev(const mf_mesh *m, int c) { int t = m->twin[c]; return t < 0 ? -1 : mf_corner_next(m, t); }

static mf_vec3 unit3(mf_vec3 a, float *len) {
    float l = sqrtf(mf_dot(a, a));
    if (len) *len = l;
    return l > 1e-12f ? mf_mul(a, 1.0f / l) : mf_v3(0, 0, 0);
}

/* How far along `slide` (a unit direction) the offset line of the beveled edge `bev` (unit) meets it: width / sin(angle), kept sane. */
static float slide_len(mf_vec3 bev, mf_vec3 slide, float w, float room) {
    float cs = mf_dot(bev, slide), sn = sqrtf(1.0f - cs * cs), l;
    if (sn < 0.15f) sn = 0.15f;
    l = w / sn;
    if (l > room * 0.49f) l = room * 0.49f;   /* both ends of an edge may slide along it: never past the middle */
    return l;
}

typedef struct {
    const mf_mesh *m;
    mf_mesh *n;           /* the mesh being built */
    int seg;
    float w, prof;
    unsigned char *bev;   /* per corner: the edge leaving it is beveled */
    int *chain;           /* per beveled corner: seg + 1 vertex ids at its start vertex, from the next-face side (0) to the previous-face side (seg) */
    int *l0, *l1;         /* per corner: the vertices that replace it in its face (l1 = -1: just one) */
    int *cap, ncap, capcap;   /* the corner holes to fill: [length, entries...] per record; entries < 0 are chain slots */
    float *capn; int nrec;    /* per record: the average normal of the faces at the vertex (which way the new face looks), then the vertex itself */
    int bad;
} bctx;

static int newv(bctx *b, mf_vec3 p) {
    int id = mf_add_vert(b->n, p.x, p.y, p.z);
    if (id < 0) b->bad = 1;
    return id < 0 ? 0 : id;
}

/* the other end of the edge that leaves corner c */
static mf_vec3 other_end(const bctx *b, int c) { return b->m->v[b->m->fidx[mf_corner_next(b->m, c)]]; }

/* one fan: cs[0..n) are its corners in order; open = it ends on the boundary at both ends */
static void do_fan(bctx *b, const int *cs, int n, int open) {
    const mf_mesh *m = b->m;
    int V = m->fidx[cs[0]], j, q, mb = 0, *bi, *att;
    mf_vec3 P = m->v[V];
    bi = (int *)malloc((size_t)(n + 1) * sizeof(int));
    att = (int *)malloc((size_t)(n + 1) * sizeof(int));
    if (!bi || !att) { free(bi); free(att); b->bad = 1; return; }
    for (j = 0; j <= n; j++) att[j] = -1;
    for (j = 0; j < n; j++) if (b->bev[cs[j]]) bi[mb++] = j;
    if (mb == 0) { free(bi); free(att); return; }
    {
#define EC(j) (cs[((j) % n + n) % n])                       /* the corner whose outgoing edge is E_j (closed fans wrap) */
#define EDIR(j, len) unit3(mf_sub(((j) == n && open) ? m->v[m->fidx[corner_prev(m, cs[n - 1])]] : other_end(b, EC(j)), P), (len))
        int nw = open ? mb + 1 : mb;   /* wedges: closed, one after each beveled edge; open, one more: the stretch before the first */
        for (q = 0; q < nw; q++) {
            int a, bb, k, i;
            float la, lb, lr, ll;
            mf_vec3 ua, ub, u1, uk, pos_r;
            int rv, lv, wv = -1;
            if (open) {   /* q = 0: from the fan's start to the first beveled edge; then between beveled edges; q = mb: from the last one to the end */
                a = q == 0 ? -1 : bi[q - 1];
                bb = q == mb ? n + 1 : bi[q];   /* n + 1: none */
            } else { a = bi[q]; bb = bi[(q + 1) % mb]; }
            if (open && a < 0) {   /* start wedge: faces 0 .. bb-1, edges E_0 .. E_(bb-1) plain, Lv slides along E_(bb-1) */
                float ln_, room;
                ub = EDIR(bb, NULL);
                uk = EDIR(bb - 1, &room);
                ln_ = slide_len(ub, uk, b->w, room);
                lv = newv(b, mf_add(P, mf_mul(uk, ln_)));
                att[bb - 1] = lv;
                if (bb - 1 > 0) { wv = newv(b, P); for (i = 0; i < bb - 1; i++) att[i] = wv; }
                b->chain[EC(bb) * (b->seg + 1) + b->seg] = lv;
                continue;
            }
            if (open && bb == n + 1) {   /* end wedge: faces a .. n-1, edges E_(a+1) .. E_n plain, Rv slides along E_(a+1) */
                float ln_, room;
                ua = EDIR(a, NULL);
                u1 = EDIR(a + 1, &room);
                ln_ = slide_len(ua, u1, b->w, room);
                rv = newv(b, mf_add(P, mf_mul(u1, ln_)));
                att[a + 1] = rv;
                if (a + 2 <= n) { wv = newv(b, P); for (i = a + 2; i <= n; i++) att[i] = wv; }
                b->chain[EC(a) * (b->seg + 1)] = rv;
                continue;
            }
            k = open ? bb - a - 1 : ((bb - a - 1) % n + n) % n;   /* plain edges between the two beveled ones */
            ua = EDIR(a, NULL); ub = EDIR(bb, NULL);
            if (k == 0) {   /* one face: both offset lines meet at a point */
                mf_vec3 n1, n2;
                float d, lenA, lenB, cap, ml;
                ua = EDIR(a, &lenA); ub = EDIR(bb, &lenB);
                n1 = unit3(mf_sub(ub, mf_mul(ua, mf_dot(ua, ub))), NULL);
                n2 = unit3(mf_sub(ua, mf_mul(ub, mf_dot(ua, ub))), NULL);
                d = 1.0f + mf_dot(n1, n2);
                if (d < 0.2f) d = 0.2f;
                pos_r = mf_mul(mf_add(n1, n2), b->w / d);
                ml = sqrtf(mf_dot(pos_r, pos_r));
                cap = 0.49f * (lenA < lenB ? lenA : lenB);
                if (ml > cap && ml > 1e-12f) pos_r = mf_mul(pos_r, cap / ml);
                rv = lv = newv(b, mf_add(P, pos_r));
            } else if (k == 1) {   /* one plain edge: both slide along it and meet */
                float room;
                u1 = EDIR(a + 1, &room);
                la = slide_len(ua, u1, b->w, room);
                lb = slide_len(ub, u1, b->w, room);
                rv = lv = newv(b, mf_add(P, mf_mul(u1, 0.5f * (la + lb))));
                att[((a + 1) % n + n) % n] = rv;
            } else {   /* two or more: Rv on the first plain edge, Lv on the last, the ones between end at W */
                float room1, roomk;
                u1 = EDIR(a + 1, &room1);
                uk = EDIR(a + k, &roomk);
                lr = slide_len(ua, u1, b->w, room1);
                ll = slide_len(ub, uk, b->w, roomk);
                rv = newv(b, mf_add(P, mf_mul(u1, lr)));
                lv = newv(b, mf_add(P, mf_mul(uk, ll)));
                att[((a + 1) % n + n) % n] = rv;
                att[((a + k) % n + n) % n] = lv;
                if (k >= 3) {
                    wv = newv(b, P);
                    for (i = 2; i <= k - 1; i++) att[((a + i) % n + n) % n] = wv;
                }
            }
            b->chain[EC(a) * (b->seg + 1)] = rv;
            b->chain[EC(bb) * (b->seg + 1) + b->seg] = lv;
        }
        att[n] = open ? att[n] : att[0];
    }
    /* profile curves: the points between a beveled edge's two ends at this vertex (a rational quadratic Bezier about the vertex). Where two
       edges of a straight run meet here their curves are the same, so a point that is already there is shared, not made twice. */
    if (b->seg > 1) {
        int made[16 * 8], nmade = 0, pc;
        for (pc = 0; pc < mb; pc++) {
            int c = cs[bi[pc]], s2;
            mf_vec3 A = b->n->v[b->chain[c * (b->seg + 1)]], B = b->n->v[b->chain[c * (b->seg + 1) + b->seg]], da, db;
            float la, lb, cg, wgt;
            da = unit3(mf_sub(A, P), &la); db = unit3(mf_sub(B, P), &lb);
            cg = mf_dot(da, db);   /* cos of the angle at the vertex */
            if (la < 1e-9f || lb < 1e-9f) cg = 0.0f;
            if (cg > 1.0f) cg = 1.0f;
            if (cg < -1.0f) cg = -1.0f;
            wgt = sqrtf(0.5f * (1.0f - cg)) * 2.0f * b->prof;   /* sin(angle / 2) is the circle's weight; the profile 0.5 gives the circle */
            for (s2 = 1; s2 < b->seg; s2++) {
                float t = (float)s2 / (float)b->seg, u = 1.0f - t, den = u * u + 2.0f * wgt * t * u + t * t, eps = 1e-4f * b->w;
                mf_vec3 pt = mf_mul(mf_add(mf_add(mf_mul(A, u * u), mf_mul(P, 2.0f * wgt * t * u)), mf_mul(B, t * t)), 1.0f / den);
                int id = -1, r;
                for (r = 0; r < nmade && id < 0; r++) {
                    mf_vec3 d = mf_sub(b->n->v[made[r]], pt);
                    if (mf_dot(d, d) < eps * eps) id = made[r];
                }
                if (id < 0) {
                    id = newv(b, pt);
                    if (nmade < (int)(sizeof made / sizeof made[0])) made[nmade++] = id;
                }
                b->chain[c * (b->seg + 1) + s2] = id;
            }
        }
    }
    /* the corner lists: face f_j arrives by E_(j+1) and leaves along E_j */
    for (j = 0; j < n; j++) {
        int in_e = open ? j + 1 : (j + 1) % n, in_id, out_id, c = cs[j];
        in_id = (in_e < n && b->bev[cs[in_e]]) ? b->chain[cs[in_e] * (b->seg + 1) + b->seg] : att[in_e];
        out_id = b->bev[c] ? b->chain[c * (b->seg + 1)] : att[j];
        b->l0[c] = in_id;
        b->l1[c] = out_id == in_id ? -1 : out_id;
    }
    /* a closed fan leaves a hole: Lv .. Rv of every beveled edge, then the wedge's path (W when it has one), round again. The profile points
       do not exist yet, so the corner is noted down (as chain slots) and filled in once they do. */
    if (!open) {
        int need = mb * (b->seg + 2) + 2, np = 0, base;
        mf_vec3 facen = mf_v3(0, 0, 0);
        if (b->ncap + need + 1 > b->capcap) {
            int nc2 = (b->ncap + need + 1) * 2;
            int *ni = (int *)realloc(b->cap, (size_t)nc2 * sizeof(int));
            if (!ni) { b->bad = 1; free(bi); free(att); return; }
            b->cap = ni; b->capcap = nc2;
        }
        {
            float *nn = (float *)realloc(b->capn, (size_t)(b->nrec + 1) * 6 * sizeof(float));
            if (!nn) { b->bad = 1; free(bi); free(att); return; }
            b->capn = nn;
        }
        base = b->ncap++;   /* the record's length goes here */
        for (q = 0; q < mb; q++) {
            int a = bi[q], bb = bi[(q + 1) % mb], k = ((bb - a - 1) % n + n) % n, s;
            for (s = b->seg; s >= 0; s--) { b->cap[b->ncap++] = -(cs[a] * (b->seg + 1) + s) - 1; np++; }
            if (k >= 3) { b->cap[b->ncap++] = att[((a + 2) % n + n) % n]; np++; }   /* W: what the strictly interior edges end at */
        }
        b->cap[base] = np;
        for (j = 0; j < n; j++) facen = mf_add(facen, mf_face_normal(m, m->cface[cs[j]]));
        b->capn[6 * b->nrec] = facen.x; b->capn[6 * b->nrec + 1] = facen.y; b->capn[6 * b->nrec + 2] = facen.z;
        b->capn[6 * b->nrec + 3] = P.x; b->capn[6 * b->nrec + 4] = P.y; b->capn[6 * b->nrec + 5] = P.z;
        b->nrec++;
    }
    free(bi); free(att);
#undef EC
#undef EDIR
}

int mf_bevel_edges_ex(mf_mesh *m, float width, int segments, float profile) {
    bctx b;
    int c, nb = 0, f, v, i, result = -1, nv0, nc0, *cs = NULL;
    unsigned char *seen = NULL;
    mf_mesh *n = NULL;
    memset(&b, 0, sizeof b);
    if (width <= 0.0f) return 0;
    if (segments < 1) segments = 1;
    if (segments > 16) segments = 16;
    if (profile < 0.05f) profile = 0.05f;
    if (profile > 1.0f) profile = 1.0f;
    if (!m->topo_ok && mf_build(m) != 0) return -1;
    if (m->nnonmanifold) return -2;
    nv0 = m->nv; nc0 = m->nc;
    b.m = m; b.seg = segments; b.w = width; b.prof = profile;
    b.bev = (unsigned char *)calloc((size_t)(nc0 > 0 ? nc0 : 1), 1);
    for (c = 0; c < nc0; c++) {   /* the selected edges: both ends selected, two faces (boundary edges cannot be beveled) */
        int t = m->twin[c], a = m->fidx[c], e = m->fidx[mf_corner_next(m, c)];
        if (!b.bev || t < 0 || !m->vsel[a] || !m->vsel[e] || m->fhid[m->cface[c]] || m->fhid[m->cface[t]]) continue;
        if (!b.bev[c]) { b.bev[c] = b.bev[t] = 1; nb++; }
    }
    if (!b.bev) return -1;
    if (nb == 0) { free(b.bev); return 0; }
    b.chain = (int *)malloc((size_t)nc0 * (size_t)(segments + 1) * sizeof(int));
    b.l0 = (int *)malloc((size_t)nc0 * sizeof(int));
    b.l1 = (int *)malloc((size_t)nc0 * sizeof(int));
    seen = (unsigned char *)calloc((size_t)nc0, 1);
    cs = (int *)malloc((size_t)nc0 * sizeof(int));
    n = mf_new();
    if (!b.chain || !b.l0 || !b.l1 || !seen || !cs || !n) goto done;
    b.n = n;
    for (c = 0; c < nc0 * (segments + 1); c++) b.chain[c] = -1;
    for (c = 0; c < nc0; c++) { b.l0[c] = m->fidx[c]; b.l1[c] = -1; }
    for (v = 0; v < nv0; v++) {   /* the old vertices come first (unused ones go at the end) */
        if (mf_add_vert(n, m->v[v].x, m->v[v].y, m->v[v].z) < 0) goto done;
        n->vhid[v] = m->vhid[v];
    }
    for (c = 0; c < nc0; c++) {   /* every fan once */
        int start, cnt = 0, open = 0, p, guard;
        if (seen[c]) continue;
        start = c;
        for (guard = 0; guard < nc0; guard++) {   /* back to the first corner of the fan (or round to c: closed) */
            p = rot_prev(m, start);
            if (p < 0) { open = 1; break; }
            if (p == c) { start = c; break; }
            start = p;
        }
        p = start;
        do { cs[cnt++] = p; seen[p] = 1; p = rot_next(m, p); } while (p >= 0 && p != start && cnt < nc0);
        if (p < 0) open = 1;
        do_fan(&b, cs, cnt, open);
        if (b.bad) goto done;
    }
    /* the corner faces, now that every point exists */
    {   int pos = 0, rec;
        for (rec = 0; rec < b.nrec; rec++) {
            int len = b.cap[pos++], np = len, *poly = b.cap + pos, w2 = 0, nd = 0, j, u;
            pos += len;
            mf_vec3 nrm = mf_v3(0, 0, 0), facen = mf_v3(b.capn[6 * rec], b.capn[6 * rec + 1], b.capn[6 * rec + 2]), corner = mf_v3(b.capn[6 * rec + 3], b.capn[6 * rec + 4], b.capn[6 * rec + 5]);
            for (j = 0; j < np; j++) if (poly[j] < 0) poly[j] = b.chain[-poly[j] - 1];
            for (j = 0; j < np; j++) if (w2 == 0 || poly[w2 - 1] != poly[j]) poly[w2++] = poly[j];   /* drop repeats, also round the corner */
            while (w2 > 1 && poly[w2 - 1] == poly[0]) w2--;
            np = w2;
            for (j = 0; j < np; j++) { int dup = 0; for (u = 0; u < j; u++) if (poly[u] == poly[j]) dup = 1; nd += !dup; }
            if (np >= 3 && nd == np) {   /* (a corner whose curves coincide has no area: the strips already meet along it) */
                int id;
                for (j = 0; j < np; j++) {   /* Newell, from the new vertices' positions */
                    mf_vec3 p0 = n->v[poly[j]], p1 = n->v[poly[(j + 1) % np]];
                    nrm.x += (p0.y - p1.y) * (p0.z + p1.z); nrm.y += (p0.z - p1.z) * (p0.x + p1.x); nrm.z += (p0.x - p1.x) * (p0.y + p1.y);
                }
                if (mf_dot(nrm, facen) < 0.0f) for (j = 0; j < np / 2; j++) { int t = poly[j]; poly[j] = poly[np - 1 - j]; poly[np - 1 - j] = t; }
                {   /* a corner whose curves bulge is not flat: one n-gon would crease, so it gets a middle point, pulled toward the old
                       corner (about a fifth of the way from the ring's centre at profile 0.5: that is where the sphere of a three-way cube corner has its middle, whatever the segment count) */
                    mf_vec3 cen = mf_v3(0, 0, 0), un = unit3(nrm, NULL);
                    float dmax = 0.0f, kap = 0.42f * b.prof;
                    for (j = 0; j < np; j++) cen = mf_add(cen, n->v[poly[j]]);
                    cen = mf_mul(cen, 1.0f / (float)np);
                    for (j = 0; j < np; j++) { float d = fabsf(mf_dot(mf_sub(n->v[poly[j]], cen), un)); if (d > dmax) dmax = d; }
                    if (np >= 5 && dmax > 0.05f * b.w) {
                        int mid, tri[3];
                        if (kap > 0.8f) kap = 0.8f;
                        cen = mf_add(cen, mf_mul(mf_sub(corner, cen), kap));
                        mid = mf_add_vert(n, cen.x, cen.y, cen.z);
                        if (mid < 0) goto done;
                        for (j = 0; j < np; j++) {
                            tri[0] = mid; tri[1] = poly[j]; tri[2] = poly[(j + 1) % np];
                            id = mf_add_face(n, tri, 3);
                            if (id < 0) goto done;
                            n->fsel[id] = 1;
                        }
                        continue;
                    }
                }
                id = mf_add_face(n, poly, np);
                if (id < 0) goto done;
                n->fsel[id] = 1;
            }
        }
    }
    /* the faces: the old ones with their corners replaced */
    for (f = 0; f < m->nf; f++) {
        int *loop = (int *)malloc((size_t)(2 * (m->fstart[f + 1] - m->fstart[f]) + 2) * sizeof(int)), cnt = 0, id;
        if (!loop) goto done;
        for (c = m->fstart[f]; c < m->fstart[f + 1]; c++) {
            loop[cnt++] = b.l0[c];
            if (b.l1[c] >= 0) loop[cnt++] = b.l1[c];
        }
        id = mf_add_face(n, loop, cnt);
        free(loop);
        if (id < 0) goto done;
        n->fhid[id] = m->fhid[f];
    }
    /* the strips: segments quads along each beveled edge */
    for (c = 0; c < nc0; c++) {
        int t = m->twin[c], s;
        mf_vec3 fn;
        if (!b.bev[c] || t < c) continue;
        fn = mf_add(mf_face_normal(m, m->cface[c]), mf_face_normal(m, m->cface[t]));
        for (s = 0; s < segments; s++) {
            int q[4], id, k;
            mf_vec3 nr = mf_v3(0, 0, 0);
            q[0] = b.chain[c * (segments + 1) + s]; q[1] = b.chain[c * (segments + 1) + s + 1];
            q[2] = b.chain[t * (segments + 1) + segments - s - 1]; q[3] = b.chain[t * (segments + 1) + segments - s];
            for (k = 0; k < 4; k++) {
                mf_vec3 p0 = n->v[q[k]], p1 = n->v[q[(k + 1) % 4]];
                nr.x += (p0.y - p1.y) * (p0.z + p1.z); nr.y += (p0.z - p1.z) * (p0.x + p1.x); nr.z += (p0.x - p1.x) * (p0.y + p1.y);
            }
            if (mf_dot(nr, fn) < 0.0f) { int tmp = q[1]; q[1] = q[3]; q[3] = tmp; }
            id = mf_add_face(n, q, 4);
            if (id < 0) goto done;
            n->fsel[id] = 1;
        }
    }
    for (i = 0; i < n->nf; i++) if (n->fhid[i]) n->fsel[i] = 0;
    {   mf_mesh t = *m; *m = *n; *n = t; }   /* the caller's mesh object takes the new contents */
    m->topo_ok = 0;
    mf_free(n); n = NULL;
    if (mf_build(m) != 0) goto done;
    if (mf_remove_unused(m) < 0) goto done;
    result = nb;
done:
    if (n) mf_free(n);
    free(b.bev); free(b.chain); free(b.l0); free(b.l1); free(seen); free(cs); free(b.cap); free(b.capn);
    return result;
}

int mf_bevel_edges(mf_mesh *m, float width) { return mf_bevel_edges_ex(m, width, 1, 0.5f); }
