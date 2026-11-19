/* C-Vert core: primitives and edit operations. See mf_mesh.h. */
#include "mf_mesh.h"
#include <stdlib.h>
#include <math.h>

int mf_make_cube(mf_mesh *m, float size) {
    static const int faces[6][4] = {
        {4, 5, 6, 7},   /* +z */
        {1, 0, 3, 2},   /* -z */
        {5, 1, 2, 6},   /* +x */
        {0, 4, 7, 3},   /* -x */
        {3, 7, 6, 2},   /* +y */
        {0, 1, 5, 4},   /* -y */
    };
    float h = size * 0.5f;
    int base = m->nv, i, k;
    for (i = 0; i < 8; i++) {
        float x = (i & 1) ^ ((i >> 1) & 1) ? h : -h;  /* 0:-  1:+  2:+  3:-  4:-  5:+  6:+  7:- */
        float y = (i >> 1) & 1 ? h : -h;               /* 0:-  1:-  2:+  3:+  ... */
        float z = (i >> 2) & 1 ? h : -h;
        if (mf_add_vert(m, x, y, z) < 0) return -1;
    }
    for (i = 0; i < 6; i++) {
        int idx[4];
        for (k = 0; k < 4; k++) idx[k] = base + faces[i][k];
        if (mf_add_face(m, idx, 4) < 0) return -1;
    }
    return 0;
}

void mf_translate_selection(mf_mesh *m, mf_vec3 by) {
    unsigned char *mark = (unsigned char *)calloc((size_t)(m->nv > 0 ? m->nv : 1), 1);
    int v, f, i;
    if (!mark) return;
    for (v = 0; v < m->nv; v++) mark[v] = m->vsel[v];
    for (f = 0; f < m->nf; f++)
        if (m->fsel[f])
            for (i = m->fstart[f]; i < m->fstart[f + 1]; i++) mark[m->fidx[i]] = 1;
    for (v = 0; v < m->nv; v++)
        if (mark[v]) m->v[v] = mf_add(m->v[v], by);
    free(mark);
}

typedef struct border { int a, b; } border;   /* an old-vertex edge a -> b on the region's rim */

int mf_extrude_selected(mf_mesh *m, float dist) {
    int old_nv, f, c, i, added = 0, nb = 0, capb = 0;
    int *map = NULL;
    mf_vec3 *nrm = NULL;
    border *rim = NULL;
    int result = -1;

    if (mf_sel_face_count(m) == 0) return 0;
    if (!m->topo_ok && mf_build(m) != 0) return -1;
    old_nv = m->nv;
    map = (int *)malloc((size_t)old_nv * sizeof(int));
    nrm = (mf_vec3 *)calloc((size_t)old_nv, sizeof(mf_vec3));
    if (!map || !nrm) goto done;
    for (i = 0; i < old_nv; i++) map[i] = -1;

    /* 1. the rim, read before any index changes: corners whose opposite face is missing or unselected */
    for (f = 0; f < m->nf; f++) {
        if (!m->fsel[f]) continue;
        for (c = m->fstart[f]; c < m->fstart[f + 1]; c++) {
            int t = m->twin[c];
            if (t < 0 || !m->fsel[m->cface[t]]) {
                if (nb == capb) {
                    int ncap = capb ? capb * 2 : 16;
                    border *nr = (border *)realloc(rim, (size_t)ncap * sizeof *rim);
                    if (!nr) goto done;
                    rim = nr; capb = ncap;
                }
                rim[nb].a = m->fidx[c];
                rim[nb].b = m->fidx[mf_corner_next(m, c)];
                nb++;
            }
        }
    }

    /* 2. a new vertex for every vertex of the region; the region's faces move onto them; their
          normals (unchanged so far: the copies sit on the originals) steer the move */
    for (f = 0; f < m->nf; f++) {
        mf_vec3 n;
        if (!m->fsel[f]) continue;
        n = mf_face_normal(m, f);
        for (c = m->fstart[f]; c < m->fstart[f + 1]; c++) {
            int v = m->fidx[c];
            nrm[v] = mf_add(nrm[v], n);
            if (map[v] < 0) {
                mf_vec3 p = m->v[v];
                map[v] = mf_add_vert(m, p.x, p.y, p.z);   /* may move m->v: p was copied first */
                if (map[v] < 0) goto done;
            }
        }
    }
    for (f = 0; f < m->nf; f++) {
        if (!m->fsel[f]) continue;
        for (c = m->fstart[f]; c < m->fstart[f + 1]; c++) m->fidx[c] = map[m->fidx[c]];
    }

    /* 3. side quads (a, b, b', a'): they carry a -> b, opposite to the unselected neighbour's b -> a */
    for (i = 0; i < nb; i++) {
        int q[4];
        q[0] = rim[i].a; q[1] = rim[i].b; q[2] = map[rim[i].b]; q[3] = map[rim[i].a];
        if (mf_add_face(m, q, 4) < 0) goto done;
        added++;
    }

    /* 4. move the new vertices along their summed region normals */
    for (i = 0; i < old_nv; i++) {
        float len;
        if (map[i] < 0) continue;
        len = sqrtf(mf_dot(nrm[i], nrm[i]));
        if (len > 1e-12f) m->v[map[i]] = mf_add(m->v[map[i]], mf_mul(nrm[i], dist / len));
    }
    m->topo_ok = 0;
    mf_build(m);
    result = added;
done:
    free(map); free(nrm); free(rim);
    return result;
}

int mf_sel_faces_from_verts(mf_mesh *m) {
    int f, c, count = 0;
    for (f = 0; f < m->nf; f++) {
        int all = 1;
        for (c = m->fstart[f]; c < m->fstart[f + 1]; c++) if (!m->vsel[m->fidx[c]]) { all = 0; break; }
        if (all && !m->fhid[f]) m->fsel[f] = 1;
        if (m->fsel[f]) count++;
    }
    return count;
}

int mf_extrude_edges(mf_mesh *m) {
    int old_nv, c, i, ne = 0, added = 0, result = -1;
    int *map = NULL, *ea = NULL, *eb = NULL;
    if (!m->topo_ok && mf_build(m) != 0) return -1;
    old_nv = m->nv;
    map = (int *)malloc((size_t)(old_nv > 0 ? old_nv : 1) * sizeof(int));
    ea = (int *)malloc((size_t)(m->nc > 0 ? m->nc : 1) * sizeof(int));
    eb = (int *)malloc((size_t)(m->nc > 0 ? m->nc : 1) * sizeof(int));
    if (!map || !ea || !eb) goto done;
    for (i = 0; i < old_nv; i++) map[i] = -1;
    for (c = 0; c < m->nc; c++) {   /* the selected edges, each once (the lower corner of a twin pair) */
        int a = m->fidx[c], b = m->fidx[mf_corner_next(m, c)];
        if (m->twin[c] >= 0 && m->twin[c] < c) continue;
        if (m->vsel[a] && m->vsel[b]) { ea[ne] = a; eb[ne] = b; ne++; }
    }
    if (ne == 0) { result = 0; goto done; }
    for (i = 0; i < ne; i++) {
        int k, e2[2];
        e2[0] = ea[i]; e2[1] = eb[i];
        for (k = 0; k < 2; k++)
            if (map[e2[k]] < 0) {
                mf_vec3 p = m->v[e2[k]];
                if ((map[e2[k]] = mf_add_vert(m, p.x, p.y, p.z)) < 0) goto done;
            }
    }
    for (i = 0; i < ne; i++) {   /* (b, a, a', b'): carries b -> a, against the face that owns a -> b */
        int q[4];
        q[0] = eb[i]; q[1] = ea[i]; q[2] = map[ea[i]]; q[3] = map[eb[i]];
        if (mf_add_face(m, q, 4) < 0) goto done;
        added++;
    }
    m->topo_ok = 0;
    mf_build(m);
    result = added;
done:
    free(map); free(ea); free(eb);
    return result;
}
