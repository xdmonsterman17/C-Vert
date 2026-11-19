/* C-Vert core: modifiers (Mirror, Subdivide, Array). Each takes a mesh and builds a NEW one; the input is never
   changed, so a modifier stack is just a chain of these (mf_mod_eval). Selection is not carried over. See mf_mesh.h. */
#include "mf_mesh.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MOD_MAX_FACES 120000   /* refuse results bigger than this: the Pi's picking and drawing are per-face */

static float *axis_of(mf_vec3 *p, int axis) { return axis == 0 ? &p->x : (axis == 1 ? &p->y : &p->z); }

static int append_face(mf_mesh *out, const mf_mesh *in, int f, const int *map, int flip) {
    int idx[256], n = mf_face_size(in, f), i;
    const int *vi = mf_face_verts(in, f);
    if (n > 256) return -1;
    for (i = 0; i < n; i++) idx[i] = map[vi[flip ? n - 1 - i : i]];
    return mf_add_face(out, idx, n) < 0 ? -1 : 0;
}

/* The mesh plus its reflection through the plane axis = 0. Vertices on the plane are shared; a face lying entirely in it
   (it would be buried inside the result) is dropped from both halves. */
static mf_mesh *mod_mirror(const mf_mesh *in, int axis) {
    mf_mesh *out = mf_new();
    int *map = NULL, *onp = NULL, v, f, i;
    const float eps = 1e-5f;
    if (!out) return NULL;
    map = (int *)malloc(sizeof(int) * (size_t)(in->nv ? in->nv : 1));
    onp = (int *)malloc(sizeof(int) * (size_t)(in->nv ? in->nv : 1));
    if (!map || !onp) goto fail;
    for (v = 0; v < in->nv; v++) {   /* the originals */
        mf_vec3 p = in->v[v];
        if ((map[v] = mf_add_vert(out, p.x, p.y, p.z)) < 0) goto fail;
        onp[v] = fabsf(*axis_of(&p, axis)) <= eps;
    }
    for (f = 0; f < in->nf; f++) {
        const int *vi = mf_face_verts(in, f);
        int n = mf_face_size(in, f), all = 1;
        for (i = 0; i < n; i++) if (!onp[vi[i]]) all = 0;
        if (!all && append_face(out, in, f, map, 0) < 0) goto fail;
    }
    {   /* the reflected vertices (on-plane ones map to the original) */
        int *mmap = (int *)malloc(sizeof(int) * (size_t)(in->nv ? in->nv : 1));
        if (!mmap) goto fail;
        for (v = 0; v < in->nv; v++) {
            mf_vec3 p = in->v[v];
            if (onp[v]) { mmap[v] = map[v]; continue; }
            *axis_of(&p, axis) = -*axis_of(&p, axis);
            if ((mmap[v] = mf_add_vert(out, p.x, p.y, p.z)) < 0) { free(mmap); goto fail; }
        }
        for (f = 0; f < in->nf; f++) {
            const int *vi = mf_face_verts(in, f);
            int n = mf_face_size(in, f), all = 1;
            for (i = 0; i < n; i++) if (!onp[vi[i]]) all = 0;
            if (!all && append_face(out, in, f, mmap, 1) < 0) { free(mmap); goto fail; }   /* reflected: reverse the loop */
        }
        free(mmap);
    }
    free(map); free(onp);
    return out;
fail:
    free(map); free(onp); mf_free(out);
    return NULL;
}

/* count copies, copy k moved by k * step along axis (step is an absolute distance). */
static mf_mesh *mod_array(const mf_mesh *in, int axis, int count, float step) {
    mf_mesh *out = mf_new();
    int *map = (int *)malloc(sizeof(int) * (size_t)(in->nv ? in->nv : 1)), k, v, f;
    if (!out || !map) { free(map); mf_free(out); return NULL; }
    for (k = 0; k < count; k++) {
        for (v = 0; v < in->nv; v++) {
            mf_vec3 p = in->v[v];
            *axis_of(&p, axis) += step * (float)k;
            if ((map[v] = mf_add_vert(out, p.x, p.y, p.z)) < 0) { free(map); mf_free(out); return NULL; }
        }
        for (f = 0; f < in->nf; f++)
            if (append_face(out, in, f, map, 0) < 0) { free(map); mf_free(out); return NULL; }
    }
    free(map);
    return out;
}

/* ---- Catmull-Clark subdivision, one level ---- */
typedef struct { long key; int f0, f1, nfaces, pt; } sedge;   /* key = lo * nv + hi */
static int cmp_edge(const void *a, const void *b) { long x = ((const sedge *)a)->key, y = ((const sedge *)b)->key; return x < y ? -1 : (x > y); }
static long ekey(int a, int b, int nv) { return a < b ? (long)a * nv + b : (long)b * nv + a; }
static sedge *efind(sedge *e, int ne, long key) {
    sedge k;
    k.key = key;
    return (sedge *)bsearch(&k, e, (size_t)ne, sizeof *e, cmp_edge);
}

static mf_mesh *mod_subdiv1(const mf_mesh *in) {
    mf_mesh *out = mf_new();
    sedge *e = NULL;
    int *fp = NULL, *valence = NULL, *bcount = NULL;
    mf_vec3 *fsum = NULL, *msum = NULL, *bsum = NULL;
    int ne = 0, f, i, v, ok = 0;
    if (!out) return NULL;
    if (in->nf > MOD_MAX_FACES / 4) { mf_free(out); return NULL; }
    e = (sedge *)malloc(sizeof *e * (size_t)(in->nc ? in->nc : 1));
    fp = (int *)malloc(sizeof(int) * (size_t)(in->nf ? in->nf : 1));
    valence = (int *)calloc((size_t)(in->nv ? in->nv : 1), sizeof(int));
    bcount = (int *)calloc((size_t)(in->nv ? in->nv : 1), sizeof(int));
    fsum = (mf_vec3 *)calloc((size_t)(in->nv ? in->nv : 1), sizeof *fsum);
    msum = (mf_vec3 *)calloc((size_t)(in->nv ? in->nv : 1), sizeof *msum);
    bsum = (mf_vec3 *)calloc((size_t)(in->nv ? in->nv : 1), sizeof *bsum);
    if (!e || !fp || !valence || !bcount || !fsum || !msum || !bsum) goto done;
    for (f = 0; f < in->nf; f++) {   /* every directed edge, then merge the two directions */
        const int *vi = mf_face_verts(in, f);
        int n = mf_face_size(in, f);
        for (i = 0; i < n; i++) { e[ne].key = ekey(vi[i], vi[(i + 1) % n], in->nv); e[ne].f0 = f; e[ne].f1 = -1; e[ne].nfaces = 1; ne++; }
    }
    qsort(e, (size_t)ne, sizeof *e, cmp_edge);
    {
        int w = 0;
        for (i = 0; i < ne; i++) {
            if (w > 0 && e[w - 1].key == e[i].key) { if (e[w - 1].nfaces == 1) e[w - 1].f1 = e[i].f0; e[w - 1].nfaces++; }
            else e[w++] = e[i];
        }
        ne = w;
    }
    for (v = 0; v < in->nv; v++) if (mf_add_vert(out, in->v[v].x, in->v[v].y, in->v[v].z) < 0) goto done;   /* 0..nv-1: moved below */
    for (f = 0; f < in->nf; f++) {   /* face points */
        const int *vi = mf_face_verts(in, f);
        int n = mf_face_size(in, f);
        mf_vec3 c = mf_face_center(in, f);   /* mean of its vertices */
        (void)vi; (void)n;
        if ((fp[f] = mf_add_vert(out, c.x, c.y, c.z)) < 0) goto done;
        for (i = 0; i < n; i++) { fsum[vi[i]] = mf_add(fsum[vi[i]], c); valence[vi[i]]++; }   /* valence counts faces here */
    }
    for (i = 0; i < ne; i++) {   /* edge points; accumulate what the vertex rule needs */
        int a = (int)(e[i].key / in->nv), b = (int)(e[i].key % in->nv);
        mf_vec3 mid = mf_mul(mf_add(in->v[a], in->v[b]), 0.5f), p = mid;
        if (e[i].nfaces == 2) p = mf_mul(mf_add(mf_add(in->v[a], in->v[b]), mf_add(mf_face_center(in, e[i].f0), mf_face_center(in, e[i].f1))), 0.25f);
        else { bcount[a]++; bcount[b]++; bsum[a] = mf_add(bsum[a], in->v[b]); bsum[b] = mf_add(bsum[b], in->v[a]); }
        msum[a] = mf_add(msum[a], mid); msum[b] = mf_add(msum[b], mid);
        if ((e[i].pt = mf_add_vert(out, p.x, p.y, p.z)) < 0) goto done;
    }
    for (v = 0; v < in->nv; v++) {   /* the original vertices move */
        int n = valence[v];
        mf_vec3 p = in->v[v], q = p;
        if (n == 0) continue;
        if (bcount[v] == 2) q = mf_mul(mf_add(mf_mul(p, 6.0f), bsum[v]), 0.125f);       /* boundary: cubic B-spline curve */
        else if (bcount[v] == 0 && n >= 3) {                                           /* interior: (F + 2R + (n-3)P) / n */
            mf_vec3 F = mf_mul(fsum[v], 1.0f / (float)n), R = mf_mul(msum[v], 1.0f / (float)n);
            q = mf_mul(mf_add(mf_add(F, mf_mul(R, 2.0f)), mf_mul(p, (float)(n - 3))), 1.0f / (float)n);
        }
        out->v[v] = q;
    }
    for (f = 0; f < in->nf; f++) {   /* one quad per corner: (v, next edge point, face point, previous edge point) */
        const int *vi = mf_face_verts(in, f);
        int n = mf_face_size(in, f);
        for (i = 0; i < n; i++) {
            int q[4];
            sedge *en = efind(e, ne, ekey(vi[i], vi[(i + 1) % n], in->nv)), *ep = efind(e, ne, ekey(vi[(i + n - 1) % n], vi[i], in->nv));
            if (!en || !ep) goto done;
            q[0] = vi[i]; q[1] = en->pt; q[2] = fp[f]; q[3] = ep->pt;
            if (mf_add_face(out, q, 4) < 0) goto done;
        }
    }
    ok = 1;
done:
    free(e); free(fp); free(valence); free(bcount); free(fsum); free(msum); free(bsum);
    if (!ok) { mf_free(out); return NULL; }
    return out;
}

mf_mesh *mf_mod_eval(const mf_mesh *in, const mf_modifier *mods, int nmods) {
    mf_mesh *cur = mf_clone(in), *next;
    int i, k;
    if (!cur) return NULL;
    for (i = 0; i < nmods; i++) {
        const mf_modifier *md = &mods[i];
        int axis = md->axis < 0 ? 0 : (md->axis > 2 ? 2 : md->axis);
        next = NULL;
        if (md->off) continue;   /* switched off: the stack passes the mesh through */
        if (md->type == MF_MOD_MIRROR) next = mod_mirror(cur, axis);
        else if (md->type == MF_MOD_ARRAY) {
            mf_vec3 lo, hi;
            float size, step;
            int cnt = md->n < 1 ? 1 : (md->n > 64 ? 64 : md->n);
            mf_bounds(cur, &lo, &hi);
            size = *axis_of(&hi, axis) - *axis_of(&lo, axis);
            step = size * md->v;   /* relative offset: 1.0 puts the copies side by side */
            if ((long)cur->nf * cnt > MOD_MAX_FACES) { mf_free(cur); return NULL; }
            next = mod_array(cur, axis, cnt, step);
        } else if (md->type == MF_MOD_SUBDIV) {
            int lv = md->n < 1 ? 1 : (md->n > 4 ? 4 : md->n);
            next = mf_clone(cur);
            for (k = 0; next && k < lv; k++) {
                mf_mesh *s = mod_subdiv1(next);
                mf_free(next);
                next = s;
            }
        } else next = mf_clone(cur);
        mf_free(cur);
        if (!next) return NULL;
        cur = next;
    }
    mf_sel_clear(cur);
    return cur;
}
