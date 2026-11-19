/* Writes a UV sphere (32x16) subdivided N times as an OBJ: the stress test's big mesh.   make_big N FILE */
#include "mf_mesh.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    mf_mesh *m = mf_new(), *e;
    mf_modifier md[1] = {{MF_MOD_SUBDIV, 0, 1, 0.0f, 0}};
    char err[80];
    if (argc < 3) { fprintf(stderr, "usage: make_big LEVELS FILE\n"); return 2; }
    md[0].n = atoi(argv[1]);
    mf_make_uvsphere(m, 32, 16, 1.0f);
    e = mf_mod_eval(m, md, 1);
    if (!e) return 1;
    return mf_obj_write(e, argv[2], err, sizeof err) ? 1 : 0;
}
