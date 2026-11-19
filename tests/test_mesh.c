/* C-Vert core tests: plain C, no C-Bricks. Run with tests/run_core_tests.sh (strict flags).
   Add a case for every bug found (and note it in BUGS.md). */
#include "mf_mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int checks = 0, failures = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b) do { checks++; if (fabs((double)(a) - (double)(b)) > 1e-4) { failures++; printf("FAIL %s:%d  %s (%g) != %s (%g)\n", __FILE__, __LINE__, #a, (double)(a), #b, (double)(b)); } } while (0)

static mf_mesh *cube(void) {
    mf_mesh *m = mf_new();
    if (!m || mf_make_cube(m, 1.0f) != 0) { printf("FAIL: couldn't make a cube\n"); exit(2); }
    return m;
}

static void expect_closed(mf_mesh *m, int v, int e, int f, const char *what) {
    mf_stats s = mf_stats_get(m);
    char msg[128];
    if (s.verts != v || s.edges != e || s.faces != f || s.euler != 2 || s.boundary != 0 || s.nonmanifold != 0) {
        failures++;
        printf("FAIL %s: V%d E%d F%d euler %d boundary %d nonmanifold %d (wanted V%d E%d F%d, euler 2, closed)\n",
               what, s.verts, s.edges, s.faces, s.euler, s.boundary, s.nonmanifold, v, e, f);
    }
    checks++;
    CHECK(mf_check(m, msg, sizeof msg) == 0);
}

static void test_cube(void) {
    mf_mesh *m = cube();
    int f;
    expect_closed(m, 8, 12, 6, "cube");
    /* every face's normal points away from the centre: the winding is outward */
    for (f = 0; f < m->nf; f++) CHECK(mf_dot(mf_face_normal(m, f), mf_face_center(m, f)) > 0.4f);
    CHECK_NEAR(mf_face_normal(m, 0).z, 1.0f);   /* +z first */
    mf_free(m);
}

static void test_extrude_one_face(void) {
    mf_mesh *m = cube();
    int top = -1, f, added;
    for (f = 0; f < m->nf; f++) if (mf_face_normal(m, f).y > 0.9f) top = f;
    CHECK(top >= 0);
    mf_sel_face(m, top, 1);
    added = mf_extrude_selected(m, 0.5f);
    CHECK(added == 4);
    expect_closed(m, 12, 20, 10, "extruded cube");
    CHECK_NEAR(mf_face_center(m, top).y, 1.0f);   /* 0.5 + 0.5 */
    CHECK(mf_face_normal(m, top).y > 0.99f);
    /* the new side walls point outward too */
    for (f = 6; f < m->nf; f++) CHECK(fabsf(mf_face_normal(m, f).y) < 1e-4f);
    CHECK(mf_sel_face_count(m) == 1);
    mf_free(m);
}

static void test_extrude_region(void) {
    mf_mesh *m = cube();
    int f, added;
    for (f = 0; f < m->nf; f++) {
        mf_vec3 n = mf_face_normal(m, f);
        if (n.y > 0.9f || n.z > 0.9f) mf_sel_face(m, f, 1);   /* top and front: share an edge */
    }
    added = mf_extrude_selected(m, 0.25f);
    CHECK(added == 6);   /* the rim of two joined quads has six edges */
    expect_closed(m, 14, 24, 12, "two-face region extrude");
    mf_free(m);
}

static void test_extrude_everything_and_nothing(void) {
    mf_mesh *m = cube();
    int f;
    CHECK(mf_extrude_selected(m, 1.0f) == 0);   /* nothing selected: nothing happens */
    CHECK(m->nv == 8 && m->nf == 6);
    for (f = 0; f < m->nf; f++) mf_sel_face(m, f, 1);
    CHECK(mf_extrude_selected(m, 1.0f) == 0);   /* no rim: no side walls (the old vertices are left unused, see PROGRESS.md) */
    CHECK(m->nv == 16 && m->nf == 6);
    CHECK(mf_stats_get(m).nonmanifold == 0);
    mf_free(m);
}

static void test_extrude_open_mesh(void) {
    mf_mesh *m = mf_new();
    int q[4], added;
    mf_stats s;
    mf_add_vert(m, 0, 0, 0); mf_add_vert(m, 1, 0, 0); mf_add_vert(m, 1, 1, 0); mf_add_vert(m, 0, 1, 0);
    q[0] = 0; q[1] = 1; q[2] = 2; q[3] = 3;
    mf_add_face(m, q, 4);
    mf_sel_face(m, 0, 1);
    added = mf_extrude_selected(m, 1.0f);
    s = mf_stats_get(m);
    CHECK(added == 4);
    CHECK(s.verts == 8 && s.faces == 5 && s.edges == 12 && s.boundary == 4 && s.nonmanifold == 0);
    CHECK(s.euler == 1);   /* a disc: an open box */
    mf_free(m);
}

static void test_translate(void) {
    mf_mesh *m = cube();
    int f;
    for (f = 0; f < m->nf; f++) if (mf_face_normal(m, f).x > 0.9f) mf_sel_face(m, f, 1);
    CHECK(mf_sel_vert_count(m) == 4);
    mf_translate_selection(m, mf_v3(2, 0, 0));
    CHECK_NEAR(mf_face_center(m, 2).x, 2.5f);   /* +x face */
    CHECK_NEAR(mf_face_center(m, 3).x, -0.5f);  /* -x face untouched */
    mf_free(m);
}

static void test_clone(void) {
    mf_mesh *m = cube(), *c;
    mf_sel_face(m, 1, 1);
    c = mf_clone(m);
    CHECK(c && c->nv == 8 && c->nf == 6 && c->fsel[1] == 1);
    mf_extrude_selected(m, 1.0f);
    CHECK(c->nv == 8 && c->nf == 6);   /* the snapshot didn't change */
    CHECK(mf_stats_get(c).euler == 2);
    mf_free(c); mf_free(m);
}

static void test_obj_forms(void) {
    mf_mesh *m = mf_new();
    char err[128] = "";
    const char *text =
        "# a comment\r\n"
        "o thing\r\n"
        "v 0 0 0\r\nv 1 0 0\r\nv 1 1 0\r\nv 0 1 0\r\nv 0.5 2 0 1.0\r\n"
        "vt 0 0\nvn 0 0 1\n"
        "usemtl m\n"
        "f 1 2 3 4\n"            /* a quad */
        "f 3/1/1 4//1 5/2\n"     /* a triangle in the other index forms */
        "f -3 -2 -1\n"           /* negative: the last three vertices (3, 4, 5) */
        "s off\n";
    CHECK(mf_obj_parse(m, text, err, sizeof err) == 0);
    CHECK(m->nv == 5 && m->nf == 3);
    CHECK(mf_face_size(m, 0) == 4 && mf_face_size(m, 1) == 3);
    CHECK(mf_face_verts(m, 1)[0] == 2 && mf_face_verts(m, 1)[2] == 4);
    CHECK(mf_face_verts(m, 2)[0] == 2 && mf_face_verts(m, 2)[2] == 4);
    CHECK_NEAR(m->v[4].y, 2.0f);
    mf_free(m);
}

static void test_obj_errors(void) {
    mf_mesh *m = mf_new();
    char err[128];
    err[0] = 0;
    CHECK(mf_obj_parse(m, "v 0 0 0\nv 1 0 0\nf 1 2 3\n", err, sizeof err) == -1);
    CHECK(strstr(err, "line 3") != NULL);
    mf_clear(m); err[0] = 0;
    CHECK(mf_obj_parse(m, "v 0 0 0\nv 1 0 0\nv 1 1 0\nf 1 2\n", err, sizeof err) == -1);
    CHECK(strstr(err, "line 4") != NULL && strstr(err, "three") != NULL);
    mf_clear(m); err[0] = 0;
    CHECK(mf_obj_parse(m, "v 0 0\n", err, sizeof err) == -1);
    CHECK(strstr(err, "line 1") != NULL);
    mf_clear(m); err[0] = 0;
    CHECK(mf_obj_parse(m, "v 0 0 0\nv 1 0 0\nv 1 1 0\nf 1 2 x\n", err, sizeof err) == -1);
    mf_clear(m); err[0] = 0;
    CHECK(mf_obj_parse(m, "v 0 0 0\nv 1 0 0\nv 1 1 0\nf 0 1 2\n", err, sizeof err) == -1);   /* OBJ is 1-based */
    CHECK(mf_obj_read(m, "/definitely/not/here.obj", err, sizeof err) == -1);
    CHECK(strstr(err, "can't open") != NULL);
    mf_free(m);
}

static void test_obj_round_trip(void) {
    mf_mesh *m = cube(), *r = mf_new();
    char err[128];
    int f, i, top = -1;
    for (f = 0; f < m->nf; f++) if (mf_face_normal(m, f).y > 0.9f) top = f;
    mf_sel_face(m, top, 1);
    mf_extrude_selected(m, 0.3f);
    CHECK(mf_obj_write(m, "mf_round_trip.obj", err, sizeof err) == 0);
    CHECK(mf_obj_read(r, "mf_round_trip.obj", err, sizeof err) == 0);
    CHECK(r->nv == m->nv && r->nf == m->nf && r->nc == m->nc);
    for (i = 0; i < m->nv && i < r->nv; i++) {
        CHECK_NEAR(r->v[i].x, m->v[i].x); CHECK_NEAR(r->v[i].y, m->v[i].y); CHECK_NEAR(r->v[i].z, m->v[i].z);
    }
    for (i = 0; i < m->nc && i < r->nc; i++) CHECK(r->fidx[i] == m->fidx[i]);
    expect_closed(r, 12, 20, 10, "round-tripped mesh");
    remove("mf_round_trip.obj");
    mf_free(m); mf_free(r);
}

static void test_topology_problems(void) {
    mf_mesh *m = mf_new();
    int a[3] = {0, 1, 2}, b[3] = {0, 1, 3}, c[3] = {1, 0, 4}, bad[3] = {0, 0, 1};
    char msg[128];
    mf_stats s;
    mf_add_vert(m, 0, 0, 0); mf_add_vert(m, 1, 0, 0); mf_add_vert(m, 0, 1, 0); mf_add_vert(m, 0, -1, 0); mf_add_vert(m, 0, 0, 1);
    mf_add_face(m, a, 3);
    mf_add_face(m, b, 3);          /* edge 0->1 again, the same direction: inconsistent winding */
    s = mf_stats_get(m);
    CHECK(s.nonmanifold == 1);
    mf_add_face(m, c, 3);          /* now three faces on edge 0-1 */
    s = mf_stats_get(m);
    CHECK(s.nonmanifold == 1);
    CHECK(mf_add_face(m, bad, 3) >= 0);   /* storing it is allowed; mf_check reports it */
    CHECK(mf_check(m, msg, sizeof msg) == -1 && strstr(msg, "used twice") != NULL);
    {
        int out_of_range[3] = {0, 1, 99};
        CHECK(mf_add_face(m, out_of_range, 3) == -1);   /* refused at the door */
    }
    mf_free(m);
}

static void test_picking(void) {
    mf_mesh *m = cube();
    float t = 0;
    int f = mf_pick_face(m, mf_v3(0, 0, 5), mf_v3(0, 0, -1), &t);
    CHECK(f == 0);                       /* the +z face */
    CHECK_NEAR(t, 4.5f);
    f = mf_pick_face(m, mf_v3(0, 0, -5), mf_v3(0, 0, 1), &t);
    CHECK(f == 1);                       /* from behind: the -z face */
    CHECK(mf_pick_face(m, mf_v3(3, 3, 5), mf_v3(0, 0, -1), &t) == -1);   /* a miss */
    CHECK(mf_pick_face(m, mf_v3(0, 0, 5), mf_v3(0, 0, 1), &t) == -1);    /* pointing away */
    mf_free(m);
}

static void test_camera_ray(void) {
    mf_vec3 d = mf_camera_ray(mf_v3(0, 0, 5), mf_v3(0, 0, 0), 60.0f, 800, 450, 400, 225);
    float tan_half;
    CHECK_NEAR(d.x, 0.0f); CHECK_NEAR(d.y, 0.0f); CHECK_NEAR(d.z, -1.0f);
    d = mf_camera_ray(mf_v3(0, 0, 5), mf_v3(0, 0, 0), 60.0f, 800, 450, 400, 0);   /* top edge */
    CHECK(d.y > 0.0f);
    tan_half = d.y / -d.z;
    CHECK_NEAR(tan_half, tanf(30.0f * 3.14159265f / 180.0f));
    d = mf_camera_ray(mf_v3(0, 0, 5), mf_v3(0, 0, 0), 60.0f, 800, 450, 800, 225);   /* right edge: wider by the aspect */
    CHECK(d.x > 0.0f);
    CHECK_NEAR(d.x / -d.z, tanf(30.0f * 3.14159265f / 180.0f) * 800.0f / 450.0f);
    d = mf_camera_ray(mf_v3(0, 5, 0), mf_v3(0, 0, 0), 60.0f, 800, 450, 400, 225);   /* straight down: no NaN */
    CHECK(d.y < -0.99f && d.x == d.x);
    /* end to end: click the middle of the screen with the cube in front of the camera */
    {
        mf_mesh *m = cube();
        mf_vec3 eye = mf_v3(0, 0, 5);
        d = mf_camera_ray(eye, mf_v3(0, 0, 0), 60.0f, 800, 450, 400, 225);
        CHECK(mf_pick_face(m, eye, d, NULL) == 0);
        mf_free(m);
    }
}

static void test_bounds(void) {
    mf_mesh *m = cube();
    mf_vec3 lo, hi;
    mf_bounds(m, &lo, &hi);
    CHECK_NEAR(lo.x, -0.5f); CHECK_NEAR(hi.z, 0.5f);
    mf_clear(m);
    mf_bounds(m, &lo, &hi);
    CHECK_NEAR(hi.x, 0.0f);
    mf_free(m);
}

/* ---- select modes, masks, screen picking, transforms (mf_edit.c) ---- */

static void test_select_modes(void) {
    mf_mesh *m = cube();
    unsigned char mark[8];
    int v, f, n;
    /* a face selects its 4 vertices in the mask */
    mf_sel_face(m, 0, 1);
    CHECK(mf_sel_mask(m, mark) == 4);
    /* face -> vertex mode: the faces' vertices become vertex selection, faces clear */
    mf_sel_convert(m, MF_SEL_VERT);
    CHECK(mf_sel_face_count(m) == 0);
    for (v = 0, n = 0; v < m->nv; v++) n += m->vsel[v];
    CHECK(n == 4);
    /* vertex -> face mode: the same 4 vertices give back exactly that face */
    mf_sel_convert(m, MF_SEL_FACE);
    CHECK(mf_sel_face_count(m) == 1 && m->fsel[0] == 1);
    for (v = 0, n = 0; v < m->nv; v++) n += m->vsel[v];
    CHECK(n == 0);
    /* three of a face's four vertices select no face */
    mf_sel_clear(m);
    { const int *vi = mf_face_verts(m, 0); mf_sel_vert(m, vi[0], 1); mf_sel_vert(m, vi[1], 1); mf_sel_vert(m, vi[2], 1); }
    mf_sel_convert(m, MF_SEL_FACE);
    CHECK(mf_sel_face_count(m) == 0);
    /* all */
    mf_sel_all(m, 1);
    CHECK(mf_sel_mask(m, mark) == 8 && mf_sel_face_count(m) == 6);
    mf_sel_all(m, 0);
    CHECK(mf_sel_mask(m, mark) == 0);
    for (f = 0; f < m->nf; f++) CHECK(m->fsel[f] == 0);
    /* centre of the marked vertices (the +z face of a unit cube) */
    mf_sel_face(m, 0, 1);
    mf_sel_mask(m, mark);
    CHECK_NEAR(mf_sel_center(m, mark).z, 0.5f);
    CHECK_NEAR(mf_sel_center(m, mark).x, 0.0f);
    mf_free(m);
}

static void test_project_and_screen_pick(void) {
    mf_mesh *m = cube();
    mf_vec3 eye = mf_v3(0, 0, 5), tgt = mf_v3(0, 0, 0);
    float sx, sy, px, py;
    int v, c, a, b;
    /* the target is the middle of the view; a point behind the eye does not project */
    CHECK(mf_project(eye, tgt, 50.0f, 800, 600, tgt, &sx, &sy));
    CHECK_NEAR(sx, 400.0f); CHECK_NEAR(sy, 300.0f);
    CHECK(!mf_project(eye, tgt, 50.0f, 800, 600, mf_v3(0, 0, 9), &sx, &sy));
    /* project then camera_ray round-trips: the ray through the pixel passes through the point */
    {
        mf_vec3 p = mf_v3(0.5f, 0.5f, 0.5f), d, onray;
        float t;
        CHECK(mf_project(eye, tgt, 50.0f, 800, 600, p, &sx, &sy));
        d = mf_camera_ray(eye, tgt, 50.0f, 800, 600, sx, sy);
        t = mf_dot(mf_sub(p, eye), d);
        onray = mf_add(eye, mf_mul(d, t));
        CHECK_NEAR(onray.x, 0.5f); CHECK_NEAR(onray.y, 0.5f);
    }
    /* vertex picking: click on a corner, with a small offset; far from any vertex gives -1 */
    mf_project(eye, tgt, 50.0f, 800, 600, mf_v3(0.5f, 0.5f, 0.5f), &px, &py);
    v = mf_pick_vert_screen(m, eye, tgt, 50.0f, 800, 600, px + 2, py - 2, 10.0f);
    CHECK(v >= 0);
    if (v >= 0) { CHECK_NEAR(m->v[v].x, 0.5f); CHECK_NEAR(m->v[v].y, 0.5f); }
    CHECK(mf_pick_vert_screen(m, eye, tgt, 50.0f, 800, 600, 5.0f, 5.0f, 10.0f) == -1);
    /* edge picking: the middle of the top front edge (y = +.5, z = +.5) */
    mf_project(eye, tgt, 50.0f, 800, 600, mf_v3(0.0f, 0.5f, 0.5f), &px, &py);
    c = mf_pick_edge_screen(m, eye, tgt, 50.0f, 800, 600, px, py - 1, 6.0f);
    CHECK(c >= 0);
    if (c >= 0) {
        a = m->fidx[c]; b = m->fidx[mf_corner_next(m, c)];
        CHECK_NEAR(m->v[a].y, 0.5f); CHECK_NEAR(m->v[b].y, 0.5f);
        CHECK_NEAR(m->v[a].z, 0.5f); CHECK_NEAR(m->v[b].z, 0.5f);
    }
    CHECK(mf_pick_edge_screen(m, eye, tgt, 50.0f, 800, 600, 5.0f, 5.0f, 6.0f) == -1);
    mf_free(m);
}

static void test_xform(void) {
    mf_mesh *m = cube();
    unsigned char mark[8];
    mf_vec3 orig[8], c;
    int v;
    mf_sel_face(m, 0, 1);                   /* +z face */
    mf_sel_mask(m, mark);
    for (v = 0; v < 8; v++) orig[v] = m->v[v];
    /* move: applied from the originals, so doing it twice does not add up */
    mf_xform(m, orig, mark, MF_XF_MOVE, mf_v3(0, 0, 0), mf_v3(1, 0, 0), 0.0f);
    mf_xform(m, orig, mark, MF_XF_MOVE, mf_v3(0, 0, 0), mf_v3(1, 0, 0), 0.0f);
    for (v = 0; v < 8; v++) CHECK_NEAR(m->v[v].x - orig[v].x, mark[v] ? 1.0f : 0.0f);
    /* rotate 90 degrees about z through the face centre: a corner (.5,.5,.5) -> (-.5,.5,.5) */
    mf_xform(m, orig, mark, MF_XF_ROTATE, mf_v3(0, 0, 0.5f), mf_v3(0, 0, 1), 3.14159265f * 0.5f);
    for (v = 0; v < 8; v++) {
        if (mark[v]) {
            CHECK_NEAR(m->v[v].z, 0.5f);
            CHECK_NEAR(fabsf(m->v[v].x), 0.5f);   /* still a square of the same size */
            CHECK_NEAR(fabsf(m->v[v].y), 0.5f);
            if (orig[v].x > 0.4f && orig[v].y > 0.4f) { CHECK_NEAR(m->v[v].x, -0.5f); CHECK_NEAR(m->v[v].y, 0.5f); }
        } else {
            CHECK_NEAR(m->v[v].x, orig[v].x);
        }
    }
    /* scale the face by 2 in x only, about its own centre */
    mf_xform(m, orig, mark, MF_XF_SCALE, mf_v3(0, 0, 0.5f), mf_v3(2, 1, 1), 0.0f);
    for (v = 0; v < 8; v++) if (mark[v]) { CHECK_NEAR(fabsf(m->v[v].x), 1.0f); CHECK_NEAR(fabsf(m->v[v].y), 0.5f); }
    c = mf_sel_center(m, mark);
    CHECK_NEAR(c.x, 0.0f);
    mf_free(m);
}

static void test_ray_helpers(void) {
    mf_vec3 hit;
    float t;
    /* a ray down -z from (1,2,5) meets the plane z = 0 at (1,2,0); a ray pointing away or parallel doesn't */
    CHECK(mf_ray_plane(mf_v3(1, 2, 5), mf_v3(0, 0, -1), mf_v3(0, 0, 0), mf_v3(0, 0, 1), &hit));
    CHECK_NEAR(hit.x, 1.0f); CHECK_NEAR(hit.y, 2.0f); CHECK_NEAR(hit.z, 0.0f);
    CHECK(!mf_ray_plane(mf_v3(1, 2, 5), mf_v3(0, 0, 1), mf_v3(0, 0, 0), mf_v3(0, 0, 1), &hit));
    CHECK(!mf_ray_plane(mf_v3(1, 2, 5), mf_v3(1, 0, 0), mf_v3(0, 0, 0), mf_v3(0, 0, 1), &hit));
    /* the x axis through the origin, a ray down -z from (3, 1, 5): closest point on the axis is x = 3 */
    CHECK(mf_ray_axis_param(mf_v3(3, 1, 5), mf_v3(0, 0, -1), mf_v3(0, 0, 0), mf_v3(1, 0, 0), &t));
    CHECK_NEAR(t, 3.0f);
    /* with the axis starting at x = 1 the parameter is relative to that start */
    CHECK(mf_ray_axis_param(mf_v3(3, 1, 5), mf_v3(0, 0, -1), mf_v3(1, 0, 0), mf_v3(1, 0, 0), &t));
    CHECK_NEAR(t, 2.0f);
    /* a ray parallel to the axis has no answer */
    CHECK(!mf_ray_axis_param(mf_v3(0, 1, 5), mf_v3(1, 0, 0), mf_v3(0, 0, 0), mf_v3(1, 0, 0), &t));
}


/* signed volume of a closed mesh (positive when the faces point outward) */
static double volume(const mf_mesh *m) {
    double vol = 0.0;
    int f, i;
    for (f = 0; f < m->nf; f++) {
        const int *vi = mf_face_verts(m, f);
        int n = mf_face_size(m, f);
        for (i = 1; i + 1 < n; i++) vol += (double)mf_dot(m->v[vi[0]], mf_cross(m->v[vi[i]], m->v[vi[i + 1]])) / 6.0;
    }
    return vol;
}

static void closed_with_euler(mf_mesh *m, int v, int e, int f, int euler, const char *what) {
    mf_stats s = mf_stats_get(m);
    char msg[128];
    checks++;
    if (s.verts != v || s.edges != e || s.faces != f || s.euler != euler || s.boundary != 0 || s.nonmanifold != 0) {
        failures++;
        printf("FAIL %s: V%d E%d F%d euler %d boundary %d nonmanifold %d (wanted V%d E%d F%d euler %d, closed)\n",
               what, s.verts, s.edges, s.faces, s.euler, s.boundary, s.nonmanifold, v, e, f, euler);
    }
    CHECK(mf_check(m, msg, sizeof msg) == 0);
    CHECK(volume(m) > 0.0);   /* normals out */
}

static void test_primitives(void) {
    mf_mesh *m = mf_new();
    mf_stats s;
    CHECK(mf_make_plane(m, 2.0f) == 0);
    s = mf_stats_get(m);
    CHECK(s.verts == 4 && s.faces == 1 && s.boundary == 4);
    CHECK_NEAR(mf_face_normal(m, 0).y, 1.0f);
    mf_clear(m);
    CHECK(mf_make_circle(m, 12, 1.0f) == 0);
    s = mf_stats_get(m);
    CHECK(s.verts == 12 && s.faces == 1 && s.boundary == 12);
    CHECK_NEAR(mf_face_normal(m, 0).y, 1.0f);
    mf_clear(m);
    CHECK(mf_make_cylinder(m, 8, 1.0f, 2.0f, 1) == 0);
    closed_with_euler(m, 16, 24, 10, 2, "cylinder");
    CHECK_NEAR(volume(m), 0.5 * 8 * sin(6.28318530718 / 8) * 2.0);   /* the exact volume of the 8-gon prism */
    mf_clear(m);
    CHECK(mf_make_cylinder(m, 8, 1.0f, 2.0f, 0) == 0);   /* no caps: an open tube */
    s = mf_stats_get(m);
    CHECK(s.faces == 8 && s.boundary == 16);
    mf_clear(m);
    CHECK(mf_make_cone(m, 8, 1.0f, 0.0f, 2.0f, 1) == 0);
    closed_with_euler(m, 9, 16, 9, 2, "cone (point)");
    mf_clear(m);
    CHECK(mf_make_cone(m, 8, 1.0f, 0.5f, 2.0f, 1) == 0);
    closed_with_euler(m, 16, 24, 10, 2, "truncated cone");
    mf_clear(m);
    CHECK(mf_make_cone(m, 8, 0.0f, 0.0f, 2.0f, 1) == -1);   /* nothing to build */
    mf_clear(m);
    CHECK(mf_make_uvsphere(m, 8, 4, 1.0f) == 0);
    closed_with_euler(m, 26, 56, 32, 2, "uv sphere");
    {   /* every vertex sits on the sphere */
        int v, ok = 1;
        for (v = 0; v < m->nv; v++) if (fabs(sqrt((double)mf_dot(m->v[v], m->v[v])) - 1.0) > 1e-4) ok = 0;
        CHECK(ok);
    }
    mf_clear(m);
    CHECK(mf_make_torus(m, 8, 6, 1.0f, 0.25f) == 0);
    closed_with_euler(m, 48, 96, 48, 0, "torus");
    mf_clear(m);
    CHECK(mf_make_cylinder(m, 1, 1.0f, 1.0f, 1) == 0);   /* too few sides is raised to 3 */
    CHECK(m->nv == 6);
    mf_clear(m);
    CHECK(mf_make_cube(m, 1.0f) == 0);
    {   /* a second primitive is appended and can be moved on its own */
        int first = m->nv;
        mf_vec3 lo, hi;
        CHECK(mf_make_plane(m, 1.0f) == 0);
        mf_move_verts_from(m, first, mf_v3(10, 0, 0));
        mf_bounds(m, &lo, &hi);
        CHECK_NEAR(hi.x, 10.5f);
        CHECK_NEAR(m->v[0].x, -0.5f);   /* the cube did not move */
    }
    mf_free(m);
}

static void test_extrude_verts_and_edges(void) {
    mf_mesh *m = cube();
    mf_stats s;
    int v, f;
    /* the user's case: four vertices (a face's corners) selected, no face selected -> they make a face */
    for (v = 0; v < m->nv; v++) if (m->v[v].y > 0.0f) mf_sel_vert(m, v, 1);
    CHECK(mf_sel_vert_count(m) == 4 && mf_sel_face_count(m) == 0);
    CHECK(mf_sel_faces_from_verts(m) == 1);
    CHECK(mf_extrude_selected(m, 0.5f) == 4);
    s = mf_stats_get(m);
    CHECK(s.verts == 12 && s.faces == 10 && s.euler == 2 && s.nonmanifold == 0);
    mf_free(m);

    m = cube();   /* three corners of a face: no face is covered */
    for (v = 0, f = 0; v < m->nv && f < 3; v++) if (m->v[v].y > 0.0f) { mf_sel_vert(m, v, 1); f++; }
    CHECK(mf_sel_faces_from_verts(m) == 0);
    mf_free(m);

    m = mf_new();   /* an edge of an open plane: one quad grows out of it */
    mf_make_plane(m, 2.0f);
    mf_sel_vert(m, 0, 1); mf_sel_vert(m, 1, 1);
    CHECK(mf_extrude_edges(m) == 1);
    s = mf_stats_get(m);
    CHECK(s.verts == 6 && s.faces == 2 && s.edges == 7 && s.boundary == 6 && s.nonmanifold == 0 && s.euler == 1);
    CHECK(m->vsel[0] && m->vsel[1] && !m->vsel[4] && !m->vsel[5]);   /* the caller selects the copies (index >= 4) */
    CHECK_NEAR(m->v[4].x, m->v[0].x);
    mf_free(m);

    m = cube();   /* a lone vertex has no edge: nothing happens, nothing breaks */
    mf_sel_vert(m, 0, 1);
    CHECK(mf_extrude_edges(m) == 0);
    CHECK(m->nv == 8 && m->nf == 6);
    mf_free(m);

    m = cube();   /* two edges that share a vertex share its copy */
    mf_sel_vert(m, 0, 1); mf_sel_vert(m, 1, 1); mf_sel_vert(m, 2, 1);
    {
        int n = mf_extrude_edges(m);
        CHECK(n >= 2 && m->nv == 8 + 3);
    }
    mf_free(m);
}

static void test_trs(void) {
    mf_trs t = mf_trs_identity(), u;
    mf_vec3 p, q, e = mf_v3(0, 0, 0);
    mf_mesh *m = cube();
    int i;
    /* a 90 degree turn about Z takes +X to +Y, about Y takes +X to -Z (right-handed) */
    t.rot = mf_v3(0, 0, 1.5707963f);
    p = mf_trs_point(&t, mf_v3(1, 0, 0));
    CHECK_NEAR(p.x, 0); CHECK_NEAR(p.y, 1); CHECK_NEAR(p.z, 0);
    t.rot = mf_v3(0, 1.5707963f, 0);
    p = mf_trs_point(&t, mf_v3(1, 0, 0));
    CHECK_NEAR(p.x, 0); CHECK_NEAR(p.z, -1);
    /* scale first, then rotate, then move */
    t = mf_trs_identity();
    t.scl = mf_v3(2, 1, 1); t.rot = mf_v3(0, 0, 1.5707963f); t.pos = mf_v3(10, 0, 0);
    p = mf_trs_point(&t, mf_v3(1, 0, 0));   /* (2,0,0) -> (0,2,0) -> (10,2,0) */
    CHECK_NEAR(p.x, 10); CHECK_NEAR(p.y, 2); CHECK_NEAR(p.z, 0);
    /* the inverse really is one, for a general transform */
    t.rot = mf_v3(0.4f, -0.7f, 1.1f); t.scl = mf_v3(2, 0.5f, 3); t.pos = mf_v3(1, -2, 3);
    p = mf_v3(0.3f, 0.9f, -1.7f);
    q = mf_trs_unpoint(&t, mf_trs_point(&t, p));
    CHECK_NEAR(q.x, p.x); CHECK_NEAR(q.y, p.y); CHECK_NEAR(q.z, p.z);
    q = mf_trs_undir(&t, mf_trs_dir(&t, p));
    CHECK_NEAR(q.x, p.x); CHECK_NEAR(q.y, p.y); CHECK_NEAR(q.z, p.z);
    /* a whole mesh, there and back */
    mf_mesh_to_world(m, &t);
    CHECK(fabsf(m->v[0].x - 0.0f) > 0.01f || fabsf(m->v[0].y) > 0.01f);
    mf_mesh_to_local(m, &t);
    for (i = 0; i < 8; i++) CHECK(fabsf(fabsf(m->v[i].x) - 0.5f) < 1e-4f && fabsf(fabsf(m->v[i].y) - 0.5f) < 1e-4f);
    mf_free(m);
    /* rotating about a pivot: the position swings round it, the orientation is composed */
    t = mf_trs_identity(); t.pos = mf_v3(2, 0, 0);
    mf_trs_rotate_world(&t, mf_v3(0, 0, 0), mf_v3(0, 0, 1), 1.5707963f);
    CHECK_NEAR(t.pos.x, 0); CHECK_NEAR(t.pos.y, 2);
    p = mf_trs_point(&t, mf_v3(1, 0, 0));   /* local +X now points along world +Y, from (0,2,0) */
    CHECK_NEAR(p.x, 0); CHECK_NEAR(p.y, 3);
    /* two turns about different world axes compose like matrices: X then Y, applied to a point */
    t = mf_trs_identity();
    mf_trs_rotate_world(&t, e, mf_v3(1, 0, 0), 1.5707963f);
    mf_trs_rotate_world(&t, e, mf_v3(0, 1, 0), 1.5707963f);
    p = mf_trs_point(&t, mf_v3(0, 1, 0));   /* +Y -(X 90)-> +Z -(Y 90)-> +X */
    CHECK_NEAR(p.x, 1); CHECK_NEAR(p.y, 0); CHECK_NEAR(p.z, 0);
    /* gimbal lock (Y = 90 degrees) still gives the same orientation */
    t = mf_trs_identity();
    mf_trs_rotate_world(&t, e, mf_v3(0, 1, 0), 1.5707963f);
    mf_trs_rotate_world(&t, e, mf_v3(1, 0, 0), 0.5f);
    u = t;
    p = mf_trs_point(&u, mf_v3(0.2f, 0.7f, -0.4f));
    t = mf_trs_identity(); t.rot = u.rot;
    q = mf_trs_point(&t, mf_v3(0.2f, 0.7f, -0.4f));
    CHECK_NEAR(p.x, q.x); CHECK_NEAR(p.y, q.y); CHECK_NEAR(p.z, q.z);
    /* scale about a pivot */
    t = mf_trs_identity(); t.pos = mf_v3(3, 0, 0);
    mf_trs_scale_about(&t, mf_v3(1, 0, 0), mf_v3(2, 1, 1));
    CHECK_NEAR(t.pos.x, 5); CHECK_NEAR(t.scl.x, 2); CHECK_NEAR(t.scl.y, 1);
}

static void test_box_select(void) {
    mf_mesh *m = cube();
    mf_vec3 eye = mf_v3(0, 0, 5), tg = mf_v3(0, 0, 0);
    mf_trs t = mf_trs_identity();
    int n, f, top = 0;
    /* looking down -Z at a unit cube at 800 x 600: the cube fills a small square in the middle */
    n = mf_box_select_verts(m, eye, tg, 50.0f, 800, 600, 0, 0, 800, 600, 0);
    CHECK(n == 8 && mf_sel_vert_count(m) == 8);
    n = mf_box_select_verts(m, eye, tg, 50.0f, 800, 600, 700, 500, 800, 600, 0);   /* a corner of the screen: nothing, and it clears */
    CHECK(n == 0 && mf_sel_vert_count(m) == 0);
    n = mf_box_select_verts(m, eye, tg, 50.0f, 800, 600, 0, 0, 400, 300, 0);       /* the upper left quarter: the 2 x 2 x ... left-top verts (x<0, y>0) */
    CHECK(n == 2 && mf_sel_vert_count(m) == 2);                                    /* front and back: X-ray picks both */
    n = mf_box_select_verts(m, eye, tg, 50.0f, 800, 600, 800, 600, 400, 300, 1);   /* corners given backwards, adding: the lower right quarter */
    CHECK(n == 2 && mf_sel_vert_count(m) == 4);
    mf_sel_clear(m);
    n = mf_box_select_faces(m, eye, tg, 50.0f, 800, 600, 0, 0, 800, 600, 0);
    CHECK(n == 6 && mf_sel_face_count(m) == 6);
    for (f = 0; f < m->nf; f++) if (mf_face_normal(m, f).y > 0.9f) top = f;
    n = mf_box_select_faces(m, eye, tg, 50.0f, 800, 600, 0, 0, 800, 295, 0);       /* above the middle: only the top face's centre (y > 0) */
    CHECK(n == 1 && m->fsel[top] == 1 && mf_sel_face_count(m) == 1);
    CHECK(mf_mesh_in_box(m, NULL, eye, tg, 50.0f, 800, 600, 300, 200, 500, 400) == 1);
    CHECK(mf_mesh_in_box(m, NULL, eye, tg, 50.0f, 800, 600, 0, 0, 20, 20) == 0);
    t.pos = mf_v3(-20, 0, 0);   /* moved out of the view: the object's own transform counts */
    CHECK(mf_mesh_in_box(m, &t, eye, tg, 50.0f, 800, 600, 300, 200, 500, 400) == 0);
    mf_free(m);
}


static void test_linked_visible(void) {
    mf_mesh *m = cube();
    mf_vec3 eye = mf_v3(0, 0, 5), tg = mf_v3(0, 0, 0);
    unsigned char vis[64];
    int a, b, n, v, f, nfront = 0;
    /* two separate cubes in one mesh: linked selects only the one the seed is in */
    a = m->nv;
    for (v = 0; v < 8; v++) mf_add_vert(m, m->v[v].x + 10.0f, m->v[v].y, m->v[v].z);
    for (f = 0; f < 6; f++) {
        int idx[8], k = mf_face_size(m, f), i;
        const int *vi = mf_face_verts(m, f);
        for (i = 0; i < k; i++) idx[i] = vi[i] + a;
        mf_add_face(m, idx, k);
    }
    mf_sel_clear(m);
    n = mf_select_linked(m, 2, 0, 1);
    CHECK(n == 8 && mf_sel_vert_count(m) == 8);
    for (v = 0; v < 8; v++) CHECK(m->vsel[v] == 1 && m->vsel[v + a] == 0);
    n = mf_select_linked(m, 2, 0, 0);   /* deselect it again */
    CHECK(n == 8 && mf_sel_vert_count(m) == 0);
    mf_sel_clear(m);
    n = mf_select_linked(m, a + 1, 1, 1);   /* face mode: the faces of the second cube */
    CHECK(n == 8 && mf_sel_face_count(m) == 6);
    for (f = 0; f < 6; f++) CHECK(m->fsel[f] == 0 && m->fsel[f + 6] == 1);
    CHECK(mf_select_linked(m, -1, 0, 1) == -1 && mf_select_linked(m, m->nv, 0, 1) == -1);
    b = mf_add_vert(m, 0, 5, 0);   /* an isolated vertex links only to itself */
    CHECK(mf_select_linked(m, b, 0, 1) == 1);
    mf_free(m);
    /* visibility: from +Z only the near face's four vertices are in view */
    m = cube();
    n = mf_visible_verts(m, eye, vis);
    for (v = 0; v < 8; v++) if (vis[v]) { nfront++; CHECK(m->v[v].z > 0.0f); }
    CHECK(n == 4 && nfront == 4);
    /* the visible-only picks skip the hidden ones: the back vertex behind the same pixel is not chosen */
    {
        float sx, sy;
        int vf = -1, vb = -1;
        for (v = 0; v < 8; v++) if (m->v[v].x < 0 && m->v[v].y > 0) { if (m->v[v].z > 0) vf = v; else vb = v; }
        CHECK(vf >= 0 && vb >= 0 && vis[vf] && !vis[vb]);
        mf_project(eye, tg, 50.0f, 800, 600, m->v[vb], &sx, &sy);
        CHECK(mf_pick_vert_screen_vis(m, eye, tg, 50.0f, 800, 600, sx, sy, 40.0f, vis) == vf);
        mf_sel_clear(m);
        n = mf_box_select_verts_vis(m, eye, tg, 50.0f, 800, 600, 0, 0, 800, 600, 0, vis);
        CHECK(n == 4 && mf_sel_vert_count(m) == 4);
        n = mf_box_select_faces_vis(m, eye, tg, 50.0f, 800, 600, 0, 0, 800, 600, 0, vis);
        CHECK(n >= 1 && n < 6);   /* the near face (and side faces touching a visible vertex), never all six */
    }
    mf_free(m);
}

static void test_modifiers(void) {
    mf_mesh *m = cube(), *r;
    mf_modifier md;
    mf_stats st;
    mf_vec3 lo, hi;
    int v, k;
    /* mirror a cube that sits wholly on one side: two cubes */
    for (v = 0; v < m->nv; v++) m->v[v].x += 2.0f;
    md.type = MF_MOD_MIRROR; md.axis = 0; md.n = 1; md.v = 1.0f; md.off = 0;
    r = mf_mod_eval(m, &md, 1);
    CHECK(r && r->nv == 16 && r->nf == 12);
    if (r) { st = mf_stats_get(r); CHECK(st.boundary == 0 && st.nonmanifold == 0 && st.euler == 4); mf_bounds(r, &lo, &hi); CHECK_NEAR(lo.x, -2.5f); CHECK_NEAR(hi.x, 2.5f); }
    CHECK(m->nv == 8 && m->v[0].x > 1.0f);   /* the input is untouched */
    mf_free(r);
    /* a cube touching the plane: the shared face is buried and dropped, the box is 2 wide */
    for (v = 0; v < m->nv; v++) m->v[v].x -= 2.0f - 0.5f;   /* x now spans 0 .. 1 */
    r = mf_mod_eval(m, &md, 1);
    CHECK(r && r->nv == 12 && r->nf == 10);
    if (r) { st = mf_stats_get(r); CHECK(st.boundary == 0 && st.nonmanifold == 0 && st.euler == 2); mf_bounds(r, &lo, &hi); CHECK_NEAR(lo.x, -1.0f); CHECK_NEAR(hi.x, 1.0f); }
    mf_free(r);
    mf_free(m);
    /* every face of a mirrored mesh still points outward (the volume check: the sum of face areas * normal . centre > 0) */
    m = cube();
    for (v = 0; v < m->nv; v++) m->v[v].y += 3.0f;
    md.axis = 1;
    r = mf_mod_eval(m, &md, 1);
    CHECK(r != NULL);
    if (r) {
        float vol = 0.0f;
        int f;
        for (f = 0; f < r->nf; f++) {
            mf_vec3 c = mf_face_center(r, f), n = mf_face_normal(r, f);
            vol += mf_dot(n, c);
        }
        CHECK(vol > 0.0f);   /* a reversed half would give about 0 */
        CHECK(r->nf == 12);
    }
    mf_free(r);
    mf_free(m);
    /* array: three cubes side by side, offset 1.5 sizes apart */
    m = cube();
    md.type = MF_MOD_ARRAY; md.axis = 2; md.n = 3; md.v = 1.5f;
    r = mf_mod_eval(m, &md, 1);
    CHECK(r && r->nv == 24 && r->nf == 18);
    if (r) { mf_bounds(r, &lo, &hi); CHECK_NEAR(hi.z - lo.z, 1.0f + 2.0f * 1.5f); }
    mf_free(r);
    md.n = 1;
    r = mf_mod_eval(m, &md, 1);
    CHECK(r && r->nv == 8 && r->nf == 6);
    mf_free(r);
    /* subdivide a cube: 26 vertices, 24 quads, still a closed sphere, pulled in from the corners */
    md.type = MF_MOD_SUBDIV; md.n = 1;
    r = mf_mod_eval(m, &md, 1);
    CHECK(r && r->nv == 26 && r->nf == 24);
    if (r) {
        st = mf_stats_get(r);
        CHECK(st.boundary == 0 && st.nonmanifold == 0 && st.euler == 2 && st.edges == 48);
        mf_bounds(r, &lo, &hi);
        CHECK(hi.x <= 0.5001f && lo.x >= -0.5001f);
        CHECK_NEAR(fabsf(r->v[0].x), 5.0f / 18.0f); CHECK_NEAR(fabsf(r->v[0].y), 5.0f / 18.0f); CHECK_NEAR(fabsf(r->v[0].z), 5.0f / 18.0f);   /* the known Catmull-Clark corner: 5/9 of it */
        CHECK(mf_face_size(r, 0) == 4);
    }
    mf_free(r);
    md.n = 2;
    r = mf_mod_eval(m, &md, 1);
    CHECK(r && r->nf == 96 && r->nv == 98);
    mf_free(r);
    /* an open plane keeps its boundary and its corner positions follow the boundary rule */
    mf_free(m);
    m = mf_new();
    mf_make_plane(m, 2.0f);
    md.n = 1;
    r = mf_mod_eval(m, &md, 1);
    CHECK(r && r->nf == 4 && r->nv == 9);
    if (r) { st = mf_stats_get(r); CHECK(st.boundary == 8 && st.nonmanifold == 0); }
    mf_free(r);
    /* a stack: mirror then array then subdivide runs in order (counts multiply), and an empty stack is a copy */
    mf_free(m);
    m = cube();
    {
        mf_modifier st3[3];
        st3[0].type = MF_MOD_ARRAY; st3[0].axis = 0; st3[0].n = 2; st3[0].v = 2.0f;
        st3[1].type = MF_MOD_MIRROR; st3[1].axis = 1; st3[1].n = 1; st3[1].v = 1.0f;
        st3[2].type = MF_MOD_SUBDIV; st3[2].axis = 0; st3[2].n = 1; st3[2].v = 1.0f;
        st3[0].off = st3[1].off = st3[2].off = 0;
        r = mf_mod_eval(m, st3, 3);
        CHECK(r && r->nf == 6 * 2 * 2 * 4);
        mf_free(r);
        r = mf_mod_eval(m, st3, 0);
        CHECK(r && r != m && r->nv == 8 && r->nf == 6);
        mf_free(r);
        /* a switched-off modifier is skipped: the others still run; all off is a plain copy */
        st3[1].off = 1;
        r = mf_mod_eval(m, st3, 3);
        CHECK(r && r->nf == 6 * 2 * 4);
        mf_free(r);
        st3[0].off = st3[2].off = 1;
        r = mf_mod_eval(m, st3, 3);
        CHECK(r && r->nv == 8 && r->nf == 6);
        mf_free(r);
    }
    /* a runaway stack is refused rather than allocated */
    md.type = MF_MOD_SUBDIV; md.n = 4;
    r = mf_mod_eval(m, &md, 1);
    CHECK(r && r->nf == 6 * 256);
    mf_free(r);
    {
        mf_modifier big[3];
        for (k = 0; k < 3; k++) big[k] = md;
        r = mf_mod_eval(m, big, 3);   /* 6 * 256^3 faces */
        CHECK(r == NULL);
    }
    mf_free(m);
}


/* ---- milestone 3 operators (mf_tools.c) ---- */
static int vert_at(const mf_mesh *m, float x, float y, float z) {
    int v;
    for (v = 0; v < m->nv; v++) if (fabsf(m->v[v].x - x) < 1e-4f && fabsf(m->v[v].y - y) < 1e-4f && fabsf(m->v[v].z - z) < 1e-4f) return v;
    return -1;
}
static void sel_verts_where(mf_mesh *m, int axis, float val) {
    int v;
    for (v = 0; v < m->nv; v++) { float c = axis == 0 ? m->v[v].x : axis == 1 ? m->v[v].y : m->v[v].z; if (fabsf(c - val) < 1e-4f) mf_sel_vert(m, v, 1); }
}
static int top_face(const mf_mesh *m) {
    int f;
    for (f = 0; f < m->nf; f++) { mf_vec3 n = mf_face_normal(m, f); if (n.y > 0.99f) return f; }
    return -1;
}
static void test_tools(void) {
    mf_mesh *m;
    mf_stats s;
    char msg[128];
    int r, f;

    m = cube(); mf_add_vert(m, 5, 5, 5);   /* remove unused */
    CHECK(m->nv == 9);
    CHECK(mf_remove_unused(m) == 1);
    expect_closed(m, 8, 12, 6, "remove_unused");
    CHECK(mf_remove_unused(m) == 0);
    mf_free(m);

    m = cube();   /* delete faces: one, then everything */
    mf_sel_face(m, top_face(m), 1);
    CHECK(mf_delete_faces(m) == 1);   /* one face; all 8 vertices still used */
    s = mf_stats_get(m);
    CHECK(s.verts == 8 && s.faces == 5 && s.boundary == 4 && s.nonmanifold == 0);
    CHECK(mf_check(m, msg, sizeof msg) == 0);
    mf_sel_all(m, 1);
    CHECK(mf_delete_faces(m) == 8 + 5);
    CHECK(m->nv == 0 && m->nf == 0);
    mf_free(m);

    m = cube();   /* delete a vertex: its 3 faces and itself go; the other 3 faces keep their vertices */
    mf_sel_vert(m, vert_at(m, -0.5f, -0.5f, -0.5f), 1);
    CHECK(mf_delete_verts(m) == 1 + 3);
    s = mf_stats_get(m);
    CHECK(s.verts == 7 && s.faces == 3 && s.nonmanifold == 0);
    CHECK(mf_check(m, msg, sizeof msg) == 0);
    mf_free(m);

    m = cube();   /* delete an edge: the two faces that share it */
    mf_sel_vert(m, vert_at(m, -0.5f, 0.5f, -0.5f), 1); mf_sel_vert(m, vert_at(m, 0.5f, 0.5f, -0.5f), 1);
    CHECK(mf_delete_edges(m) == 2);
    s = mf_stats_get(m);
    CHECK(s.verts == 8 && s.faces == 4 && s.nonmanifold == 0);
    mf_free(m);

    m = cube();   /* flip */
    {   double v0 = volume(m);
        CHECK(mf_flip_faces(m, 1) == 6);
        CHECK_NEAR(volume(m), -v0);
        expect_closed(m, 8, 12, 6, "flip all");
        mf_flip_faces(m, 1);
        CHECK_NEAR(volume(m), v0);
        mf_sel_face(m, 0, 1);
        CHECK(mf_flip_faces(m, 0) == 1);
        CHECK(mf_stats_get(m).nonmanifold > 0);   /* one face turned round: its edges now run the same way as the neighbours' */
    }
    mf_free(m);

    m = cube();   /* merge at center: the top four vertices make a pyramid */
    sel_verts_where(m, 1, 0.5f);
    CHECK(mf_merge_center(m) == 3);
    expect_closed(m, 5, 8, 5, "merge center");
    CHECK(vert_at(m, 0, 0.5f, 0) >= 0);
    CHECK(mf_merge_center(m) == 0);   /* a single selected vertex: nothing to do */
    mf_free(m);
    m = cube();   /* everything merged: one loose vertex stays (not zero) */
    mf_sel_all(m, 1);
    CHECK(mf_merge_center(m) == 7);
    CHECK(m->nv == 1 && m->nf == 0);
    mf_free(m);

    m = mf_new();   /* two quads with a doubled seam: merge by distance sews them */
    {   int a[4] = {0, 1, 2, 3}, b[4] = {4, 5, 6, 7};
        mf_add_vert(m, 0, 0, 0); mf_add_vert(m, 1, 0, 0); mf_add_vert(m, 1, 1, 0); mf_add_vert(m, 0, 1, 0);
        mf_add_vert(m, 1, 0, 0.0004f); mf_add_vert(m, 2, 0, 0); mf_add_vert(m, 2, 1, 0); mf_add_vert(m, 1, 1, 0.0004f);
        mf_add_face(m, a, 4); mf_add_face(m, b, 4);
    }
    CHECK(mf_merge_by_distance(m, 0.001f, 0) == 2);
    s = mf_stats_get(m);
    CHECK(s.verts == 6 && s.edges == 7 && s.faces == 2 && s.boundary == 6);
    CHECK(mf_merge_by_distance(m, 0.001f, 0) == 0);
    mf_free(m);

    m = cube();   /* inset */
    f = top_face(m);
    mf_sel_face(m, f, 1);
    CHECK(mf_inset_faces(m, 0.1f) == 4);
    expect_closed(m, 12, 20, 10, "inset");
    CHECK(mf_sel_face_count(m) == 1);
    {   int sf = -1, i;
        float lo = 9, hi = -9;
        for (i = 0; i < m->nf; i++) if (m->fsel[i]) sf = i;
        CHECK(sf >= 0 && mf_face_normal(m, sf).y > 0.99f);
        for (i = m->fstart[sf]; i < m->fstart[sf + 1]; i++) { float x = m->v[m->fidx[i]].x; if (x < lo) lo = x; if (x > hi) hi = x; CHECK_NEAR(m->v[m->fidx[i]].y, 0.5f); }
        CHECK_NEAR(lo, -0.4f); CHECK_NEAR(hi, 0.4f);
        CHECK_NEAR(volume(m), 1.0);
    }
    mf_free(m);
    m = cube();   /* a huge inset is clamped: still a valid closed mesh, nothing inverted */
    mf_sel_face(m, top_face(m), 1);
    CHECK(mf_inset_faces(m, 50.0f) == 4);
    expect_closed(m, 12, 20, 10, "inset clamped");
    CHECK(volume(m) > 0.99);
    mf_free(m);
    m = cube();   /* all faces: every face inset on its own */
    mf_sel_all(m, 1);
    CHECK(mf_inset_faces(m, 0.1f) == 24);
    CHECK(m->nf == 30 && mf_check(m, msg, sizeof msg) == 0);
    mf_free(m);
    m = cube();
    CHECK(mf_inset_faces(m, 0.1f) == 0);   /* nothing selected */
    mf_free(m);

    m = cube();   /* loop cut through a cube edge */
    {   double v0 = volume(m);
        CHECK(mf_loop_cut(m, 0, 1) == 4);
        expect_closed(m, 12, 20, 10, "loop cut");
        CHECK_NEAR(volume(m), v0);
        CHECK(mf_sel_vert_count(m) == 4);
    }
    mf_free(m);
    m = cube();
    CHECK(mf_loop_cut(m, 5, 3) == 12);   /* three cuts: 4 faces become 16 */
    expect_closed(m, 8 + 12, 12 + 4 * 3 + 12, 6 + 12 + 0, "loop cut x3");
    mf_free(m);
    m = mf_new();   /* an open ring on a flat strip of three quads: cut across the middle quad's edge */
    {   int q0[4] = {0, 1, 5, 4}, q1[4] = {1, 2, 6, 5}, q2[4] = {2, 3, 7, 6};
        int i;
        for (i = 0; i < 4; i++) mf_add_vert(m, (float)i, 0, 0);
        for (i = 0; i < 4; i++) mf_add_vert(m, (float)i, 0, 1);
        mf_add_face(m, q0, 4); mf_add_face(m, q1, 4); mf_add_face(m, q2, 4);
        mf_build(m);
        /* corner 1 of q0 is 1 -> 5: its ring crosses all three quads and puts a vertex on 0-4, 1-5, 2-6 and 3-7 */
        r = mf_loop_cut(m, 1, 1);
        CHECK(r == 3);
        s = mf_stats_get(m);
        CHECK(s.verts == 12 && s.faces == 6 && s.boundary == 10 && s.nonmanifold == 0);
    }
    mf_free(m);
    m = mf_new();   /* a ring that runs into a triangle is refused and leaves the mesh alone */
    {   int q0[4] = {0, 1, 4, 3}, t1[3] = {1, 2, 4};
        mf_add_vert(m, 0, 0, 0); mf_add_vert(m, 1, 0, 0); mf_add_vert(m, 2, 0, 0);
        mf_add_vert(m, 0, 0, 1); mf_add_vert(m, 1, 0, 1);
        mf_add_face(m, q0, 4); mf_add_face(m, t1, 3);
        mf_build(m);
        r = mf_loop_cut(m, 1, 1);   /* corner 1 of q0 is 1 -> 4, shared with the triangle */
        CHECK(r == -2);
        CHECK(m->nv == 5 && m->nf == 2);
    }
    mf_free(m);

    m = cube();   /* bevel one cube edge */
    {   double v0 = volume(m);
        mf_sel_vert(m, vert_at(m, -0.5f, 0.5f, -0.5f), 1); mf_sel_vert(m, vert_at(m, 0.5f, 0.5f, -0.5f), 1);
        CHECK(mf_bevel_edges(m, 0.1f) == 1);
        expect_closed(m, 10, 15, 7, "bevel one edge");
        CHECK(mf_sel_face_count(m) == 1);
        CHECK(fabs(volume(m) - (v0 - 0.5 * 0.1 * 0.1 * 1.0)) < 1e-4);   /* a triangular prism of 0.1 x 0.1 x 1 is cut away */
    }
    mf_free(m);
    m = cube();   /* the four edges round a face: they share their corners (two beveled edges meet at each) */
    sel_verts_where(m, 2, -0.5f);
    CHECK(mf_bevel_edges(m, 0.1f) == 4);
    expect_closed(m, 12, 20, 10, "bevel a face's four edges");
    {   int f2, ok = 1; mf_vec3 mid = mf_v3(0, 0, 0); int v2;
        for (v2 = 0; v2 < m->nv; v2++) mid = mf_add(mid, m->v[v2]);
        mid = mf_mul(mid, 1.0f / (float)m->nv);
        for (f2 = 0; f2 < m->nf; f2++) if (mf_dot(mf_face_normal(m, f2), mf_sub(mf_face_center(m, f2), mid)) <= 0.0f) ok = 0;
        CHECK(ok);   /* every face looks outward: the strips and corners have the right winding */
    }
    mf_free(m);
    m = cube();
    mf_sel_vert(m, vert_at(m, -0.5f, 0.5f, -0.5f), 1); mf_sel_vert(m, vert_at(m, 0.5f, 0.5f, -0.5f), 1);
    mf_sel_vert(m, vert_at(m, -0.5f, -0.5f, 0.5f), 1); mf_sel_vert(m, vert_at(m, 0.5f, -0.5f, 0.5f), 1);   /* two opposite, parallel edges */
    CHECK(mf_bevel_edges(m, 0.1f) == 2);
    expect_closed(m, 12, 18, 8, "bevel two edges");
    mf_free(m);
    m = mf_new();   /* edges on a boundary cannot be beveled: nothing happens */
    mf_make_plane(m, 1.0f);
    mf_sel_all(m, 1);
    CHECK(mf_bevel_edges(m, 0.1f) == 0 && m->nv == 4 && m->nf == 1);
    mf_free(m);
}

/* ---- the general bevel: any valence, shared vertices, segments, profile ---- */
static int faces_outward(const mf_mesh *m) {   /* for a convex result: every face looks away from the middle */
    int f, v, ok = 1;
    mf_vec3 mid = mf_v3(0, 0, 0);
    for (v = 0; v < m->nv; v++) mid = mf_add(mid, m->v[v]);
    mid = mf_mul(mid, 1.0f / (float)m->nv);
    for (f = 0; f < m->nf; f++) if (mf_dot(mf_face_normal(m, f), mf_sub(mf_face_center(m, f), mid)) <= 0.0f) ok = 0;
    return ok;
}

static void test_bevel_general(void) {
    mf_mesh *m;
    int v, r, near = 0, hv, hf;
    mf_stats st;
    char msg[96];
    /* every edge of a cube: 24 vertices, 12 strips + 8 corner triangles + the 6 faces */
    m = cube(); mf_sel_all(m, 1);
    CHECK(mf_bevel_edges(m, 0.1f) == 12);
    expect_closed(m, 24, 48, 26, "bevel every cube edge");
    CHECK(faces_outward(m));
    mf_free(m);
    /* segments: one edge, 3 segments: 4 profile points at each end, on a circle of radius w round the edge's inner axis */
    m = cube();
    mf_sel_vert(m, vert_at(m, -0.5f, 0.5f, -0.5f), 1); mf_sel_vert(m, vert_at(m, 0.5f, 0.5f, -0.5f), 1);
    CHECK(mf_bevel_edges_ex(m, 0.2f, 3, 0.5f) == 1);
    expect_closed(m, 14, 23, 11, "bevel one edge, 3 segments");
    CHECK(mf_sel_face_count(m) == 5);   /* 3 strip quads and a cap at each end */
    for (v = 0; v < m->nv; v++) {
        float dy = m->v[v].y - 0.3f, dz = m->v[v].z + 0.3f, d = sqrtf(dy * dy + dz * dz);
        if (fabsf(fabsf(m->v[v].x) - 0.5f) < 1e-4f && fabsf(d - 0.2f) < 2e-3f) near++;
    }
    CHECK(near == 8);
    CHECK(faces_outward(m));
    mf_free(m);
    /* every edge with 3 segments: each corner is a rounded patch; its points lie on the sphere (radius w) of the corner */
    m = cube(); mf_sel_all(m, 1);
    CHECK(mf_bevel_edges_ex(m, 0.15f, 3, 0.5f) == 12);
    st = mf_stats_get(m);
    CHECK(st.boundary == 0 && st.nonmanifold == 0 && st.euler == 2);
    CHECK(mf_check(m, msg, sizeof msg) == 0);
    CHECK(faces_outward(m));
    {   int cnt = 0, off = 0;
        float o = 0.5f - 0.15f;
        for (v = 0; v < m->nv; v++) {
            float dx = m->v[v].x - o, dy = m->v[v].y - o, dz = m->v[v].z - o;
            if (dx > 1e-4f && dy > 1e-4f && dz > 1e-4f) {   /* the (+,+,+) corner patch */
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                cnt++;
                if (fabsf(d - 0.15f) > 0.006f) off++;
            }
        }
        CHECK(cnt >= 7 && off == 0);
    }
    mf_free(m);
    /* profile: smaller is flatter (a profile point nearer the chord), bigger is squarer (nearer the corner) */
    {   float dist[3], pr[3] = {0.2f, 0.5f, 0.9f};
        int k;
        for (k = 0; k < 3; k++) {
            int best = -1;
            float bd = 1e9f;
            m = cube();
            mf_sel_vert(m, vert_at(m, -0.5f, 0.5f, -0.5f), 1); mf_sel_vert(m, vert_at(m, 0.5f, 0.5f, -0.5f), 1);
            CHECK(mf_bevel_edges_ex(m, 0.2f, 2, pr[k]) == 1);
            for (v = 0; v < m->nv; v++) {   /* the middle profile point at x = 0.5: nearest to the cube's old corner (0.5, 0.5, -0.5) */
                float dx = m->v[v].x - 0.5f, dy = m->v[v].y - 0.5f, dz = m->v[v].z + 0.5f, d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (fabsf(m->v[v].x - 0.5f) < 1e-4f && d > 1e-3f && d < bd && m->v[v].y < 0.4999f && m->v[v].z > -0.4999f) { bd = d; best = v; }
            }
            dist[k] = best >= 0 ? bd : -1.0f;
            mf_free(m);
        }
        CHECK(dist[0] > dist[1] && dist[1] > dist[2] && dist[2] > 0.0f);
    }
    /* valence 4: a whole ring of a torus (its vertices share edges two at a time), with segments */
    m = mf_new(); mf_make_torus(m, 10, 6, 1.0f, 0.3f); mf_build(m);
    mf_select_edge_loop(m, 0, 0);
    r = mf_bevel_edges_ex(m, 0.03f, 2, 0.5f);
    CHECK(r > 3);
    st = mf_stats_get(m);
    CHECK(st.boundary == 0 && st.nonmanifold == 0 && st.euler == 0);   /* still a closed torus */
    CHECK(mf_check(m, msg, sizeof msg) == 0);
    mf_free(m);
    /* a pole: ten or more faces meet at the vertex (the fan has a long run of plain edges: the W vertex) */
    m = mf_new(); mf_make_uvsphere(m, 12, 8, 1.0f); mf_build(m);
    {   int pole = 0, ring = -1, c;
        for (v = 0; v < m->nv; v++) if (m->v[v].y > m->v[pole].y) pole = v;
        for (c = 0; c < m->nc && ring < 0; c++) if (m->fidx[c] == pole) ring = m->fidx[mf_corner_next(m, c)];
        mf_sel_vert(m, pole, 1); mf_sel_vert(m, ring, 1);
        r = mf_bevel_edges_ex(m, 0.05f, 1, 0.5f);
        CHECK(r == 1);
        st = mf_stats_get(m);
        CHECK(st.boundary == 0 && st.nonmanifold == 0 && st.euler == 2);
        CHECK(mf_check(m, msg, sizeof msg) == 0);
    }
    mf_free(m);
    /* an open mesh: a cube without its top; bevel a vertical edge: its end on the rim has an open fan */
    m = cube();
    {   int f2;
        for (f2 = 0; f2 < m->nf; f2++) if (mf_face_normal(m, f2).y > 0.9f) mf_sel_face(m, f2, 1);
        mf_delete_faces(m);
        mf_sel_clear(m);
        mf_sel_vert(m, vert_at(m, -0.5f, 0.5f, 0.5f), 1); mf_sel_vert(m, vert_at(m, -0.5f, -0.5f, 0.5f), 1);
        st = mf_stats_get(m);
        CHECK(st.boundary == 4 && st.euler == 1);
        CHECK(mf_bevel_edges_ex(m, 0.1f, 2, 0.5f) == 1);
        st = mf_stats_get(m);
        CHECK(st.nonmanifold == 0 && st.euler == 1 && st.boundary >= 4);
        CHECK(mf_check(m, msg, sizeof msg) == 0);
    }
    mf_free(m);
    /* hidden faces: an edge beside one is not beveled; nothing selected that can be: nothing changes */
    m = cube();
    sel_verts_where(m, 2, -0.5f);
    mf_sel_clear(m);
    mf_sel_face(m, 0, 1); mf_hide_selected(m, 1);
    mf_sel_clear(m);
    mf_sel_all(m, 1);
    r = mf_bevel_edges(m, 0.1f);
    mf_hidden_count(m, &hv, &hf);
    CHECK(r > 0 && hf == 1 && mf_check(m, msg, sizeof msg) == 0);
    mf_free(m);
    /* a width bigger than the mesh allows is clamped, not a tangle */
    m = cube(); mf_sel_all(m, 1);
    CHECK(mf_bevel_edges_ex(m, 5.0f, 2, 0.5f) == 12);
    st = mf_stats_get(m);
    CHECK(st.boundary == 0 && st.nonmanifold == 0 && st.euler == 2);
    {   int inside = 1;   /* nothing slides past the cube's own corners */
        for (v = 0; v < m->nv; v++)
            if (fabsf(m->v[v].x) > 0.5001f || fabsf(m->v[v].y) > 0.5001f || fabsf(m->v[v].z) > 0.5001f) inside = 0;
        CHECK(inside);
    }
    mf_free(m);
    m = cube();   /* the same for one edge (it slides along plain edges instead of mitring) */
    mf_sel_vert(m, vert_at(m, -0.5f, 0.5f, -0.5f), 1); mf_sel_vert(m, vert_at(m, 0.5f, 0.5f, -0.5f), 1);
    CHECK(mf_bevel_edges_ex(m, 5.0f, 2, 0.5f) == 1);
    st = mf_stats_get(m);
    CHECK(st.boundary == 0 && st.nonmanifold == 0 && st.euler == 2);
    {   int inside = 1;
        for (v = 0; v < m->nv; v++)
            if (fabsf(m->v[v].x) > 0.5001f || fabsf(m->v[v].y) > 0.5001f || fabsf(m->v[v].z) > 0.5001f) inside = 0;
        CHECK(inside);
    }
    mf_free(m);
}

/* the tree-based visibility must equal the plain "nearest face along the ray" test, on meshes where it matters */
static void test_visible_tree(void) {
    int shape, e, v, same = 1, total = 0, hidden = 0;
    for (shape = 0; shape < 3; shape++) {
        mf_mesh *m = mf_new();
        unsigned char *vis;
        if (shape == 0) mf_make_torus(m, 12, 8, 1.0f, 0.4f);
        else if (shape == 1) mf_make_uvsphere(m, 14, 8, 1.0f);
        else { mf_make_cube(m, 1.0f); mf_make_cylinder(m, 9, 0.3f, 2.0f, 1); }
        vis = (unsigned char *)malloc((size_t)m->nv);
        for (e = 0; e < 12; e++) {
            mf_vec3 eye = mf_v3(4.0f * cosf((float)e * 0.7f), 3.0f * sinf((float)e * 1.3f), 4.0f * sinf((float)e * 0.7f));
            mf_visible_verts(m, eye, vis);
            for (v = 0; v < m->nv; v++) {
                mf_vec3 d = mf_sub(m->v[v], eye);
                float dist = sqrtf(mf_dot(d, d)), t = 0.0f;
                int hit = mf_pick_face(m, eye, mf_mul(d, 1.0f / dist), &t);
                int ref = (hit < 0 || t >= dist * 0.995f - 1e-4f) ? 1 : 0;
                total++; hidden += !ref;
                if (ref != vis[v]) same = 0;
            }
        }
        free(vis); mf_free(m);
    }
    CHECK(same);
    CHECK(total > 500 && hidden > 100 && hidden < total - 100);   /* the comparison saw both visible and hidden vertices */
}

/* ---- speed-ups: the fast code must agree with the plain way of doing it ---- */

/* the plain O(corners^2) reference for mf_build's edge counts and twins */
static void ref_topology(const mf_mesh *m, int *ne, int *nb, int *nnm, int *twin) {
    int c, d;
    *ne = *nb = *nnm = 0;
    for (c = 0; c < m->nc; c++) {
        int a = m->fidx[c], b = m->fidx[mf_corner_next(m, c)], same = 0, rev = 0, rc = -1, first = c;
        for (d = 0; d < m->nc; d++) {
            int da = m->fidx[d], db = m->fidx[mf_corner_next(m, d)];
            if (d == c) continue;
            if (da == a && db == b) { same++; if (d < first) first = d; }
            if (da == b && db == a) { rev++; rc = d; if (d < first) first = d; }
        }
        twin[c] = (rev == 1 && same == 0) ? rc : -1;
        if (first != c) continue;
        (*ne)++;
        if (same + rev == 0) (*nb)++;
        else if (!(same + rev == 1 && rev == 1)) (*nnm)++;
    }
}

static int build_agrees(mf_mesh *m) {
    int ne, nb, nnm, c, ok = 1, *twin = (int *)malloc((size_t)(m->nc + 1) * sizeof(int));
    ref_topology(m, &ne, &nb, &nnm, twin);
    m->topo_ok = 0;
    if (mf_build(m) != 0) ok = 0;
    else {
        if (m->ne != ne || m->nboundary != nb || m->nnonmanifold != nnm) ok = 0;
        for (c = 0; c < m->nc; c++) if (m->twin[c] != twin[c]) ok = 0;
    }
    free(twin);
    return ok;
}

static void test_build_matches_reference(void) {
    mf_mesh *m = mf_new();
    int tri[3], v, k;
    CHECK(mf_make_cube(m, 1.0f) == 0 && build_agrees(m));
    mf_free(m); m = mf_new(); mf_make_torus(m, 9, 5, 1.0f, 0.3f); CHECK(build_agrees(m));
    mf_free(m); m = mf_new(); mf_make_plane(m, 2.0f); CHECK(build_agrees(m) && m->nboundary == 4);                  /* open: boundary edges */
    mf_free(m); m = mf_new(); mf_make_cube(m, 1.0f); mf_make_cylinder(m, 7, 0.3f, 2.0f, 1); CHECK(build_agrees(m));     /* two bodies */
    mf_free(m); m = mf_new();   /* three triangles on one edge, and two faces running the same way along another: non-manifold both */
    for (v = 0; v < 6; v++) mf_add_vert(m, (float)v, (float)(v % 2), (float)(v / 2));
    tri[0] = 0; tri[1] = 1; tri[2] = 2; mf_add_face(m, tri, 3);
    tri[0] = 1; tri[1] = 0; tri[2] = 3; mf_add_face(m, tri, 3);
    tri[0] = 0; tri[1] = 1; tri[2] = 4; mf_add_face(m, tri, 3);   /* 0->1 again: same direction as the first face */
    tri[0] = 1; tri[1] = 0; tri[2] = 5; mf_add_face(m, tri, 3);
    CHECK(build_agrees(m) && m->nnonmanifold > 0);
    mf_free(m);
    /* a hub: merging every other vertex of a sphere into one gives it a valence in the thousands; the build must stay fast and exact */
    m = mf_new(); mf_make_uvsphere(m, 40, 20, 1.0f);
    for (v = 0; v < m->nv; v += 2) m->vsel[v] = 1;
    k = mf_merge_center(m);
    CHECK(k > 100);
    CHECK(build_agrees(m));
    mf_free(m);
}

static void test_merge_grid(void) {
    int round, ok = 1, agree = 1;
    for (round = 0; round < 4; round++) {
        mf_mesh *m = mf_new();
        int n = 300, i, j, tri[3], greedy = 0, nvb;
        float dist = 0.05f + 0.03f * (float)round;
        unsigned seed = 12345u + (unsigned)round * 7919u;
        unsigned char *gone;
        for (i = 0; i < n; i++) {
            float p[3];
            for (j = 0; j < 3; j++) { seed = seed * 1664525u + 1013904223u; p[j] = (float)((seed >> 8) & 0xffff) / 65535.0f; }
            mf_add_vert(m, p[0], p[1], p[2]);
        }
        for (i = 0; i + 2 < n; i += 3) { tri[0] = i; tri[1] = i + 1; tri[2] = i + 2; mf_add_face(m, tri, 3); }
        for (i = 0; i < n; i++) {   /* a second triangle per vertex pair so most vertices stay used after merging */
            tri[0] = i; tri[1] = (i * 7 + 3) % n; tri[2] = (i * 13 + 5) % n;
            if (tri[0] != tri[1] && tri[1] != tri[2] && tri[0] != tri[2]) mf_add_face(m, tri, 3);
        }
        gone = (unsigned char *)calloc((size_t)n, 1);
        for (i = 0; i < n; i++) {   /* the greedy rule written out plainly */
            if (gone[i]) continue;
            greedy++;
            for (j = i + 1; j < n; j++) {
                mf_vec3 d = mf_sub(m->v[i], m->v[j]);
                if (!gone[j] && mf_dot(d, d) <= dist * dist) gone[j] = 1;
            }
        }
        nvb = m->nv;
        mf_merge_by_distance(m, dist, 0);
        if (m->nv > greedy || m->nv >= nvb) agree = 0;   /* never more than the greedy survivors (unused ones drop out as well) */
        for (i = 0; i < m->nv; i++)
            for (j = i + 1; j < m->nv; j++) {
                mf_vec3 d = mf_sub(m->v[i], m->v[j]);
                if (mf_dot(d, d) <= dist * dist) ok = 0;   /* nothing left within the distance */
            }
        CHECK(mf_merge_by_distance(m, dist, 0) == 0);      /* and it is done: a second pass finds nothing */
        free(gone); mf_free(m);
    }
    CHECK(ok);
    CHECK(agree);
}

static void test_vis_pieces(void) {
    mf_mesh *m = mf_new(), *other = mf_new();
    unsigned char *a, *b, *want;
    mf_vis *vs;
    int v, same = 1, e, n1, n2;
    mf_vec3 eye = mf_v3(0.5f, 2.0f, 5.0f);
    mf_make_uvsphere(m, 16, 10, 1.0f);
    a = (unsigned char *)malloc((size_t)m->nv); b = (unsigned char *)malloc((size_t)m->nv); want = (unsigned char *)calloc((size_t)m->nv, 1);
    vs = mf_vis_new(m);
    CHECK(vs != NULL);
    n1 = mf_visible_verts(m, eye, a);
    n2 = mf_vis_run(vs, m, eye, b, NULL, 0, m->nv);
    CHECK(n1 == n2 && memcmp(a, b, (size_t)m->nv) == 0);
    for (e = 0; e < 3; e++) {   /* the mesh moves (grown, shifted): a refit follows it, the answer matches a tree built afresh */
        for (v = 0; v < m->nv; v++) { m->v[v] = mf_mul(m->v[v], 1.15f); m->v[v].x += 0.2f; }
        CHECK(mf_vis_refit(vs, m) == 1);
        mf_visible_verts(m, eye, a);
        mf_vis_run(vs, m, eye, b, NULL, 0, m->nv);
        if (memcmp(a, b, (size_t)m->nv) != 0) same = 0;
    }
    CHECK(same);
    memset(b, 7, (size_t)m->nv);   /* in pieces, and with a want list: unwanted vertices come out visible, the rest as before */
    for (v = 0; v < m->nv; v += 2) want[v] = 1;
    mf_vis_run(vs, m, eye, b, want, 0, m->nv / 2);
    mf_vis_run(vs, m, eye, b, want, m->nv / 2, m->nv);
    for (v = 0; v < m->nv; v++) CHECK(want[v] ? b[v] == a[v] : b[v] == 1);
    mf_make_cube(other, 1.0f);
    CHECK(mf_vis_run(vs, other, eye, b, NULL, 0, other->nv) == -1);   /* not the mesh the tree was built for: refused, not read */
    CHECK(mf_vis_refit(vs, other) == 0);
    mf_vis_free(vs);
    mf_free(other); mf_free(m);
    {   mf_mesh *empty = mf_new();   /* nothing to hide behind: no tree */
        CHECK(mf_vis_new(empty) == NULL);
        mf_free(empty);
    }
    free(a); free(b); free(want);
}

/* ---- hide (H / Alt+H) ---- */
static int vsel_count(const mf_mesh *m) { int v, n = 0; for (v = 0; v < m->nv; v++) n += m->vsel[v]; return n; }

static void test_hide(void) {
    mf_mesh *m = mf_new(), *c;
    int hv, hf, f, v, i, hit, before;
    float t = 0.0f;
    mf_vec3 eye = mf_v3(3, 3, 3);
    /* vertex mode on a cube: one vertex hides itself and its three faces */
    CHECK(mf_make_cube(m, 2.0f) == 0);
    mf_sel_vert(m, 6, 1);
    CHECK(mf_hide_selected(m, 0) == 4);
    mf_hidden_count(m, &hv, &hf);
    CHECK(hv == 1 && hf == 3 && m->vhid[6] && vsel_count(m) == 0 && mf_sel_face_count(m) == 0);
    mf_sel_all(m, 1);
    CHECK(vsel_count(m) == 7 && mf_sel_face_count(m) == 3 && !m->vsel[6]);                 /* select all skips hidden things */
    CHECK(mf_sel_invert(m, 0) == 0 && mf_sel_invert(m, 1) == 0);                         /* invert too (everything visible was selected) */
    mf_sel_clear(m);
    CHECK(mf_sel_invert(m, 0) == 7 && !m->vsel[6]);
    mf_sel_clear(m);
    CHECK(mf_box_select_verts(m, eye, mf_v3(0, 0, 0), 50.0f, 960, 540, 0, 0, 960, 540, 0) <= 7 && !m->vsel[6]);   /* box select too */
    CHECK(!m->vsel[6]);
    /* picking: a ray at a hidden face goes through to the one behind it (or misses); _any still sees it */
    for (f = 0; f < m->nf; f++) if (m->fhid[f]) break;
    {   mf_vec3 cen = mf_face_center(m, f), d = mf_sub(cen, eye);
        float len = sqrtf(mf_dot(d, d));
        d = mf_mul(d, 1.0f / len);
        hit = mf_pick_face_any(m, eye, d, &t);
        CHECK(hit == f || m->fhid[hit]);
        hit = mf_pick_face(m, eye, d, &t);
        CHECK(hit < 0 || !m->fhid[hit]);
    }
    c = mf_clone(m);
    CHECK(c && c->vhid[6] && mf_hidden_count(c, &hv, &hf) == 4);                        /* a snapshot keeps what is hidden */
    mf_free(c);
    CHECK(mf_reveal_all(m, 0) == 4 && mf_hidden_count(m, NULL, NULL) == 0 && m->vsel[6]);   /* Alt+H: all back, and selected */
    CHECK(mf_reveal_all(m, 0) == 0);
    mf_free(m);
    /* face mode: a cube face hides alone (its vertices have other faces); hide them all and the vertices go too */
    m = mf_new(); mf_make_cube(m, 2.0f);
    mf_sel_face(m, 0, 1);
    CHECK(mf_hide_selected(m, 1) == 1);
    mf_hidden_count(m, &hv, &hf);
    CHECK(hv == 0 && hf == 1 && mf_sel_face_count(m) == 0);
    mf_sel_all(m, 1);
    CHECK(mf_sel_face_count(m) == 5);
    CHECK(mf_hide_selected(m, 1) == 5 + 8);                                            /* the rest: 5 faces and now all 8 vertices */
    mf_hidden_count(m, &hv, &hf);
    CHECK(hv == 8 && hf == 6);
    CHECK(mf_reveal_all(m, 1) == 14 && mf_sel_face_count(m) == 6 && mf_hidden_count(m, NULL, NULL) == 0);
    mf_free(m);
    /* hidden faces do not hide others: the front half of a sphere hidden, the vertices behind it show */
    m = mf_new(); mf_make_uvsphere(m, 16, 10, 1.0f);
    {   unsigned char *vis = (unsigned char *)malloc((size_t)m->nv);
        int n1, n2;
        mf_vec3 e2 = mf_v3(0, 0, 5);
        n1 = mf_visible_verts(m, e2, vis);
        for (f = 0; f < m->nf; f++) if (mf_face_center(m, f).z > 0.1f) mf_sel_face(m, f, 1);
        mf_hide_selected(m, 1);
        n2 = mf_visible_verts(m, e2, vis);
        CHECK(n2 > n1 + 20);
        for (v = 0; v < m->nv; v++) if (m->v[v].z < -0.5f && m->v[v].x * m->v[v].x + m->v[v].y * m->v[v].y < 0.2f) CHECK(vis[v]);   /* the far pole is in view */
        free(vis);
    }
    mf_free(m);
    /* operators that touch "everything" leave hidden parts alone */
    m = mf_new(); mf_make_cube(m, 2.0f);
    mf_sel_face(m, 2, 1); mf_hide_selected(m, 1);
    {   mf_vec3 n0 = mf_face_normal(m, 2), n1;
        mf_flip_faces(m, 1);
        n1 = mf_face_normal(m, 2);
        CHECK_NEAR(n0.x, n1.x); CHECK_NEAR(n0.y, n1.y); CHECK_NEAR(n0.z, n1.z);       /* flip all skipped it */
        CHECK(mf_face_normal(m, 0).x != 0.0f || mf_face_normal(m, 0).y != 0.0f || mf_face_normal(m, 0).z != 0.0f);
    }
    mf_free(m);
    m = mf_new();   /* two triangles on top of each other, the second hidden: merge by distance must not weld them */
    for (i = 0; i < 3; i++) mf_add_vert(m, (float)i, (float)(i % 2), 0.0f);
    for (i = 0; i < 3; i++) mf_add_vert(m, (float)i, (float)(i % 2), 0.0f);
    { int a[3] = {0, 1, 2}, b[3] = {3, 4, 5}; mf_add_face(m, a, 3); mf_add_face(m, b, 3); }
    mf_sel_face(m, 1, 1); mf_hide_selected(m, 1);
    before = m->nv;
    CHECK(mf_merge_by_distance(m, 0.001f, 0) == 0 && m->nv == before);
    mf_reveal_all(m, 1);
    mf_sel_clear(m);
    CHECK(mf_merge_by_distance(m, 0.001f, 0) == 3);                                    /* revealed: they weld */
    mf_free(m);
    /* an operator that rebuilds the mesh (delete faces) keeps the flags of what stays */
    m = mf_new(); mf_make_cube(m, 2.0f);
    mf_sel_face(m, 1, 1); mf_hide_selected(m, 1);
    mf_sel_face(m, 3, 1);
    CHECK(mf_delete_faces(m) > 0);
    mf_hidden_count(m, &hv, &hf);
    CHECK(hf == 1 && m->nf == 5);
    mf_free(m);
    /* loop cut: a ring that runs through a hidden face is refused, one that doesn't still works */
    m = mf_new(); mf_make_torus(m, 8, 6, 1.0f, 0.3f); mf_build(m);
    mf_sel_face(m, 0, 1); mf_hide_selected(m, 1);
    {   int c0, refused = 0, cut = 0;
        for (c0 = 0; c0 < m->nc; c0++) {
            mf_mesh *w = mf_clone(m);
            int r = mf_loop_cut(w, c0, 1);
            if (r == -2) refused++; else if (r > 0) cut++;
            mf_free(w);
        }
        CHECK(refused > 0 && cut > 0);
    }
    mf_free(m);
}

static void test_loops_invert(void) {
    mf_mesh *m = mf_new();
    int n8 = 0, n6 = 0, c, i, sel, a, b, k;
    CHECK(mf_make_torus(m, 8, 6, 1.0f, 0.3f) == 0);
    CHECK(mf_build(m) == 0);
    /* every edge of the torus lies in a loop of 8 (round the ring) or 6 (round the tube) edges; both kinds exist */
    for (c = 0; c < m->nc; c++) {
        int r = mf_select_edge_loop(m, c, 0);
        sel = 0; for (i = 0; i < m->nv; i++) sel += m->vsel[i];
        CHECK(r == sel);   /* a closed loop: as many vertices as edges */
        if (r == 8) n8++; else if (r == 6) n6++; else CHECK(0);
    }
    CHECK(n8 > 0 && n6 > 0);
    /* add keeps the earlier selection */
    mf_select_edge_loop(m, 0, 0);
    a = 0; for (i = 0; i < m->nv; i++) a += m->vsel[i];
    for (c = 0, k = 0; c < m->nc && !k; c++) {   /* a loop that shares nothing with the first */
        mf_select_edge_loop(m, 0, 0);
        mf_select_edge_loop(m, c, 1);
        b = 0; for (i = 0; i < m->nv; i++) b += m->vsel[i];
        if (b > a) k = 1;
    }
    CHECK(k == 1);
    CHECK(mf_select_edge_loop(m, -1, 0) == -1 && mf_select_edge_loop(m, m->nc, 0) == -1);
    mf_free(m);
    m = mf_new();   /* a cube has no four-way vertices: the loop is the edge itself */
    CHECK(mf_make_cube(m, 1.0f) == 0);
    CHECK(mf_select_edge_loop(m, 0, 0) == 1);
    sel = 0; for (i = 0; i < m->nv; i++) sel += m->vsel[i];
    CHECK(sel == 2);
    /* invert */
    mf_sel_clear(m);
    mf_sel_vert(m, 0, 1);
    CHECK(mf_sel_invert(m, 0) == 7 && !m->vsel[0] && m->vsel[1]);
    mf_sel_clear(m);
    mf_sel_face(m, 2, 1);
    CHECK(mf_sel_invert(m, 1) == 5 && !m->fsel[2] && m->fsel[0]);
    mf_free(m);
}

static void test_inset_region(void) {
    mf_mesh *m = mf_new();
    mf_stats st;
    char msg[100];
    int v, f, nnew = 0, ok = 1;
    CHECK(mf_make_cube(m, 1.0f) == 0);
    mf_sel_face(m, 0, 1);
    CHECK(mf_inset_region(m, 0.1f) == 4);
    CHECK(mf_check(m, msg, sizeof msg) == 0);
    st = mf_stats_get(m);
    CHECK(st.faces == 10 && st.verts == 12 && st.euler == 2 && st.boundary == 0 && st.nonmanifold == 0);
    for (v = 0; v < m->nv; v++) {
        float x = fabsf(m->v[v].x), y = fabsf(m->v[v].y), z = fabsf(m->v[v].z);
        int big = (fabsf(x - 0.5f) < 1e-4f) + (fabsf(y - 0.5f) < 1e-4f) + (fabsf(z - 0.5f) < 1e-4f);
        if (big == 3) continue;
        nnew++;
        if (!(big == 1 && ((fabsf(x - 0.4f) < 1e-4f) + (fabsf(y - 0.4f) < 1e-4f) + (fabsf(z - 0.4f) < 1e-4f)) == 2)) ok = 0;
    }
    CHECK(nnew == 4 && ok);   /* the inner corners sit 0.1 in from each rim edge */
    CHECK(mf_sel_face_count(m) == 1);
    mf_free(m);
    m = mf_new();   /* two neighbouring faces are one region: their shared edge gets no ring */
    CHECK(mf_make_cube(m, 1.0f) == 0);
    mf_sel_face(m, 0, 1);
    for (f = 1; f < 6; f++) {   /* a face that shares an edge with face 0 */
        int c, d, share = 0;
        for (c = m->fstart[0]; c < m->fstart[1]; c++) for (d = m->fstart[f]; d < m->fstart[f + 1]; d++) if (m->fidx[c] == m->fidx[d]) share++;
        if (share == 2) { mf_sel_face(m, f, 1); break; }
    }
    CHECK(mf_sel_face_count(m) == 2);
    CHECK(mf_inset_region(m, 0.05f) == 6);
    st = mf_stats_get(m);
    CHECK(st.faces == 12 && st.verts == 14 && st.euler == 2 && st.nonmanifold == 0);   /* 6 new corners; the 8 old ones all lie on the rim */
    mf_free(m);
    m = mf_new();   /* a hexagon: the miter keeps every edge 0.1 from its old place, so the corners go in by 0.1 / cos 60 degrees... */
    CHECK(mf_make_circle(m, 6, 1.0f) == 0);
    mf_sel_face(m, 0, 1);
    CHECK(mf_inset_region(m, 0.1f) == 6);
    nnew = 0; ok = 1;
    for (v = 0; v < m->nv; v++) {
        float r = sqrtf(m->v[v].x * m->v[v].x + m->v[v].z * m->v[v].z);
        if (fabsf(r - 1.0f) < 1e-4f) continue;
        nnew++;
        if (fabsf(r - (0.8660254f - 0.1f) / 0.8660254f) > 1e-3f) ok = 0;   /* apothem 0.866 shrinks by 0.1 */
    }
    CHECK(nnew == 6 && ok);
    mf_free(m);
    m = mf_new();   /* a whole closed mesh has no rim; nothing selected does nothing */
    CHECK(mf_make_cube(m, 1.0f) == 0);
    CHECK(mf_inset_region(m, 0.1f) == 0);
    mf_sel_all(m, 1);
    CHECK(mf_inset_region(m, 0.1f) == -2 && m->nf == 6 && m->nv == 8);
    mf_free(m);
}

int main(void) {
    test_cube();
    test_extrude_one_face();
    test_extrude_region();
    test_extrude_everything_and_nothing();
    test_extrude_open_mesh();
    test_translate();
    test_clone();
    test_obj_forms();
    test_obj_errors();
    test_obj_round_trip();
    test_topology_problems();
    test_picking();
    test_camera_ray();
    test_bounds();
    test_select_modes();
    test_project_and_screen_pick();
    test_xform();
    test_ray_helpers();
    test_primitives();
    test_extrude_verts_and_edges();
    test_trs();
    test_box_select();
    test_linked_visible();
    test_modifiers();
    test_tools();
    test_loops_invert();
    test_inset_region();
    test_visible_tree();
    test_build_matches_reference();
    test_merge_grid();
    test_vis_pieces();
    test_hide();
    test_bevel_general();
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
