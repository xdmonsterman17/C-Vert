/* objinfo: prints an OBJ's mesh stats using the C-Vert core (no graphics).
   Build + run: tools/objinfo.sh FILE.obj       Exit code: 0 closed manifold, 1 problems, 2 can't read. */
#include "mf_mesh.h"
#include <stdio.h>

int main(int argc, char **argv) {
    mf_mesh *m = mf_new();
    mf_stats s;
    char err[200];
    if (argc < 2) { fprintf(stderr, "usage: objinfo FILE.obj\n"); return 2; }
    if (mf_obj_read(m, argv[1], err, sizeof err) != 0) { fprintf(stderr, "%s: %s\n", argv[1], err); return 2; }
    s = mf_stats_get(m);
    printf("%s: V%d E%d F%d euler %d boundary %d nonmanifold %d\n", argv[1], s.verts, s.edges, s.faces, s.euler, s.boundary, s.nonmanifold);
    if (mf_check(m, err, sizeof err) != 0) { printf("check: %s\n", err); return 1; }
    mf_free(m);
    return (s.boundary == 0 && s.nonmanifold == 0) ? 0 : 1;
}
