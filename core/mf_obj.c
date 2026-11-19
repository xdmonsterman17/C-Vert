/* C-Vert core: Wavefront OBJ reading and writing (positions and polygon faces). See mf_mesh.h. */
#include "mf_mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void fail(char *err, size_t errn, int line, const char *what) {
    if (err && errn) {
        if (line > 0) snprintf(err, errn, "line %d: %s", line, what);
        else snprintf(err, errn, "%s", what);
    }
}

/* Reads one face corner "a", "a/b", "a//c" or "a/b/c": its position index, resolved to 0-based
   against the vertices defined so far (negative counts back from the last). -1: malformed. */
static int corner_index(const char *tok, int nv) {
    char *end;
    long i = strtol(tok, &end, 10);
    if (end == tok || (*end != 0 && *end != '/')) return -1;
    if (i > 0) i -= 1;
    else if (i < 0) i = nv + i;
    else return -1;
    return (i >= 0 && i < nv) ? (int)i : -1;
}

int mf_obj_parse(mf_mesh *m, const char *text, char *err, size_t errn) {
    const char *p = text;
    int line = 0;
    int *tmp = NULL, cap = 0;
    int rc = 0;
    while (*p) {
        const char *eol = p;
        char buf[1024];
        size_t n;
        char *s;
        line++;
        while (*eol && *eol != '\n') eol++;
        n = (size_t)(eol - p);
        if (n >= sizeof buf) n = sizeof buf - 1;   /* an absurdly long line: its tail is ignored */
        memcpy(buf, p, n);
        buf[n] = 0;
        p = *eol ? eol + 1 : eol;
        s = buf;
        while (*s == ' ' || *s == '\t' || *s == '\r') s++;
        if (s[0] == 'v' && (s[1] == ' ' || s[1] == '\t')) {
            char *e1, *e2, *e3;
            double x = strtod(s + 1, &e1), y = strtod(e1, &e2), z = strtod(e2, &e3);
            if (e1 == s + 1 || e2 == e1 || e3 == e2) { fail(err, errn, line, "a vertex needs three numbers"); rc = -1; break; }
            if (mf_add_vert(m, (float)x, (float)y, (float)z) < 0) { fail(err, errn, 0, "out of memory"); rc = -1; break; }
        } else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
            int count = 0;
            char *tok = strtok(s + 1, " \t\r");
            while (tok) {
                int vi = corner_index(tok, m->nv);
                if (vi < 0) { fail(err, errn, line, "a face names a vertex that doesn't exist"); rc = -1; goto out; }
                if (count == cap) {
                    int ncap = cap ? cap * 2 : 16;
                    int *nt = (int *)realloc(tmp, (size_t)ncap * sizeof(int));
                    if (!nt) { fail(err, errn, 0, "out of memory"); rc = -1; goto out; }
                    tmp = nt; cap = ncap;
                }
                tmp[count++] = vi;
                tok = strtok(NULL, " \t\r");
            }
            if (count < 3) { fail(err, errn, line, "a face needs at least three vertices"); rc = -1; break; }
            if (mf_add_face(m, tmp, count) < 0) { fail(err, errn, 0, "out of memory"); rc = -1; break; }
        }
        /* vt, vn, o, g, s, usemtl, mtllib, comments and anything else: skipped */
    }
out:
    free(tmp);
    return rc;
}

int mf_obj_read(mf_mesh *m, const char *path, char *err, size_t errn) {
    FILE *f = fopen(path, "rb");
    long size;
    char *text;
    size_t got;
    int rc;
    if (!f) { if (err && errn) snprintf(err, errn, "can't open %s", path); return -1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); fail(err, errn, 0, "can't read the file"); return -1; }
    text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(f); fail(err, errn, 0, "out of memory"); return -1; }
    got = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[got] = 0;
    rc = mf_obj_parse(m, text, err, errn);
    free(text);
    return rc;
}

int mf_obj_write(const mf_mesh *m, const char *path, char *err, size_t errn) {
    FILE *f = fopen(path, "wb");
    int i, k;
    if (!f) { if (err && errn) snprintf(err, errn, "can't write %s", path); return -1; }
    fprintf(f, "# C-Vert OBJ: %d vertices, %d faces\n", m->nv, m->nf);
    for (i = 0; i < m->nv; i++) fprintf(f, "v %.9g %.9g %.9g\n", (double)m->v[i].x, (double)m->v[i].y, (double)m->v[i].z);
    for (i = 0; i < m->nf; i++) {
        const int *vi = mf_face_verts(m, i);
        int n = mf_face_size(m, i);
        fputc('f', f);
        for (k = 0; k < n; k++) fprintf(f, " %d", vi[k] + 1);
        fputc('\n', f);
    }
    if (fclose(f) != 0) { fail(err, errn, 0, "the write failed"); return -1; }
    return 0;
}
