#define _POSIX_C_SOURCE 199309L
/* CPU timings of the core's heavier calls on this machine (the real CPU: only the GPU is missing on the virtual display).
   tests/run_bench.sh builds it with -O2 and prints one line per call. Not part of run_all (timings vary). */
#include "mf_mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6; }

int main(void) {
    int sizes[3][2] = {{16, 8}, {32, 16}, {64, 32}}, k, r;
    for (k = 0; k < 3; k++) {
        mf_mesh *m = mf_new(), *e;
        unsigned char *vis;
        double t0;
        mf_modifier md[1] = {{MF_MOD_SUBDIV, 0, 1, 0.0f, 0}};
        mf_make_uvsphere(m, sizes[k][0], sizes[k][1], 1.0f);
        vis = (unsigned char *)malloc((size_t)m->nv);
        printf("UV sphere %dx%d: %d verts, %d faces\n", sizes[k][0], sizes[k][1], m->nv, m->nf);
        t0 = now(); for (r = 0; r < 5; r++) mf_visible_verts(m, mf_v3(0, 0, 5), vis); printf("  mf_visible_verts (every camera move, X-ray off): %.2f ms\n", (now() - t0) / 5);
        t0 = now(); for (r = 0; r < 5; r++) { m->topo_ok = 0; mf_build(m); } printf("  mf_build: %.2f ms\n", (now() - t0) / 5);
        t0 = now(); for (r = 0; r < 5; r++) { mf_mesh *c = mf_clone(m); mf_free(c); } printf("  mf_clone (one undo snapshot): %.2f ms\n", (now() - t0) / 5);
        t0 = now(); e = NULL; for (r = 0; r < 3; r++) { if (e) mf_free(e); e = mf_mod_eval(m, md, 1); } printf("  Subdivision level 1: %.2f ms -> %d faces\n", (now() - t0) / 3, e ? e->nf : -1);
        if (e) mf_free(e);
        t0 = now(); r = mf_pick_edge_screen(m, mf_v3(0, 0, 5), mf_v3(0, 0, 0), 50.0f, 960, 540, 480.0f, 270.0f, 10.0f); printf("  mf_pick_edge_screen: %.2f ms (hit %d)\n", now() - t0, r);
        free(vis); mf_free(m);
    }
    /* the stress section: ~32k faces / 32k vertices (where the editor starts to feel heavy), every operator once */
    {
        mf_mesh *b = mf_new(), *big, *w;
        mf_modifier md3[1] = {{MF_MOD_SUBDIV, 0, 3, 0.0f, 0}};
        unsigned char *vis, *mask;
        double t0;
        int v, f, n;
        char err[80];
        mf_make_uvsphere(b, 32, 16, 1.0f);
        t0 = now(); big = mf_mod_eval(b, md3, 1); printf("\nSTRESS: %d verts, %d faces\n  Subdivision level 3 of a 32x16 sphere: %.1f ms\n", big->nv, big->nf, now() - t0);
        vis = (unsigned char *)malloc((size_t)big->nv); mask = (unsigned char *)malloc((size_t)big->nv);
#define TIME(label, stmt) do { t0 = now(); stmt; printf("  %-46s %7.1f ms\n", label, now() - t0); } while (0)
#define FRESH() do { if (w) mf_free(w); w = mf_clone(big); mf_build(w); } while (0)
        w = NULL;
        FRESH();
        TIME("mf_build", { w->topo_ok = 0; mf_build(w); });
        TIME("mf_clone (one undo snapshot)", { mf_mesh *c = mf_clone(w); mf_free(c); });
        {
            mf_vis *vs = NULL;
            TIME("visibility: build the tree", vs = mf_vis_new(w));
            TIME("visibility: refit after a move", mf_vis_refit(vs, w));
            TIME("visibility: rays for every vertex", mf_vis_run(vs, w, mf_v3(0, 0, 5), vis, NULL, 0, w->nv));
            mf_vis_free(vs);
        }
        TIME("mf_sel_mask", mf_sel_mask(w, mask));
        TIME("mf_sel_vert_count", mf_sel_vert_count(w));
        TIME("mf_pick_face (a ray)", mf_pick_face(w, mf_v3(0, 0, 5), mf_v3(0, 0, -1), NULL));
        TIME("mf_pick_edge_screen (a click)", mf_pick_edge_screen(w, mf_v3(0, 0, 5), mf_v3(0, 0, 0), 50.0f, 960, 540, 480.0f, 270.0f, 10.0f));
        TIME("mf_pick_vert_screen (a click)", mf_pick_vert_screen(w, mf_v3(0, 0, 5), mf_v3(0, 0, 0), 50.0f, 960, 540, 480.0f, 270.0f, 10.0f));
        TIME("mf_box_select_verts (the whole window)", mf_box_select_verts(w, mf_v3(0, 0, 5), mf_v3(0, 0, 0), 50.0f, 960, 540, 0, 0, 960, 540, 0));
        TIME("mf_select_linked", mf_select_linked(w, 0, 0, 1));
        TIME("mf_select_edge_loop", mf_select_edge_loop(w, 5000, 0));
        TIME("mf_sel_invert", mf_sel_invert(w, 0));
        FRESH();
        TIME("mf_loop_cut (1 cut)", mf_loop_cut(w, 5000, 1));
        FRESH(); mf_sel_clear(w);
        for (f = 0; f < w->nf; f += 7) mf_sel_face(w, f, 1);
        TIME("mf_extrude_selected (every 7th face)", mf_extrude_selected(w, 0.05f));
        FRESH(); mf_sel_clear(w);
        for (f = 0; f < w->nf; f += 7) mf_sel_face(w, f, 1);
        TIME("mf_inset_faces (every 7th face)", mf_inset_faces(w, 0.001f));
        FRESH(); mf_sel_clear(w);
        for (f = 0; f < w->nf / 3; f++) mf_sel_face(w, f, 1);
        TIME("mf_inset_region (a third of the faces)", mf_inset_region(w, 0.001f));
        FRESH(); mf_sel_clear(w);
        for (f = 0; f < w->nf; f += 7) mf_sel_face(w, f, 1);
        TIME("mf_delete_faces (every 7th face)", mf_delete_faces(w));
        FRESH(); mf_sel_clear(w);
        for (v = 0; v < w->nv; v += 50) w->vsel[v] = 1;
        TIME("mf_delete_verts (every 50th vertex)", mf_delete_verts(w));
        FRESH(); mf_sel_all(w, 1);
        TIME("mf_merge_by_distance (everything, nothing merges)", mf_merge_by_distance(w, 0.001f, 0));
        FRESH(); mf_sel_clear(w);
        for (v = 0; v < w->nv; v += 3) w->vsel[v] = 1;
        TIME("mf_merge_center (every 3rd vertex)", mf_merge_center(w));
        FRESH();
        TIME("mf_flip_faces (all)", mf_flip_faces(w, 1));
        FRESH(); mf_sel_clear(w);
        for (v = 0; v < w->nv; v += 400) w->vsel[v] = 1;
        TIME("mf_bevel_edges (a few edges)", mf_bevel_edges(w, 0.01f));
        TIME("mf_obj_write", mf_obj_write(big, ".build/bench_big.obj", err, sizeof err));
        n = 0; (void)n;
        mf_free(w); mf_free(big); mf_free(b); free(vis); free(mask);
    }
    return 0;
}
