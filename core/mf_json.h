#ifndef MF_JSON_H
#define MF_JSON_H
/* A small JSON reader for .cbshader files and the node table. Values: 0 null, 1 number, 2 string, 3 array, 4 object, 5 bool. */
typedef struct mf_jv {
    int t, count;
    double n;
    char *s;
    struct mf_jv *kids;   /* array items / object values */
    char **keys;       /* object keys */
} mf_jv;
mf_jv *mf_json_parse(const char *text);                   /* NULL when the text is not JSON */
const mf_jv *mf_json_get(const mf_jv *o, const char *key);   /* NULL when absent */
const mf_jv *mf_json_at(const mf_jv *a, int i);
const char *mf_json_str(const mf_jv *v, const char *dflt);
double mf_json_num(const mf_jv *v, double dflt);
void mf_json_free(mf_jv *v);
#endif
