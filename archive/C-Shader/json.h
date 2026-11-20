#ifndef JSON_H
#define JSON_H
/* A small JSON reader for .cbshader files and the node table. Values: 0 null, 1 number, 2 string, 3 array, 4 object, 5 bool. */
typedef struct jv {
    int t, count;
    double n;
    char *s;
    struct jv *kids;   /* array items / object values */
    char **keys;       /* object keys */
} jv;
jv *json_parse(const char *text);                   /* NULL when the text is not JSON */
const jv *json_get(const jv *o, const char *key);   /* NULL when absent */
const jv *json_at(const jv *a, int i);
const char *json_str(const jv *v, const char *dflt);
double json_num(const jv *v, double dflt);
void json_free(jv *v);
#endif
