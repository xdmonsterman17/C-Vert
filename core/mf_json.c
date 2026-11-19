#include <stdlib.h>
#include <string.h>
#include "mf_json.h"

static void ws(const char **p) { while (**p == ' ' || **p == '\n' || **p == '\r' || **p == '\t') (*p)++; }

static void free_inner(mf_jv *v) {
    int i;
    for (i = 0; i < v->count; i++) {
        free_inner(&v->kids[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->kids);
    free(v->keys);
    free(v->s);
}

static char *parse_string(const char **p) {
    size_t cap = 16, n = 0;
    char *s = (char *)malloc(cap);
    if (!s) return NULL;
    (*p)++;
    while (**p && **p != '"') {
        char c = **p;
        if (c == '\\' && (*p)[1]) {
            (*p)++;
            c = **p;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'u') { c = '?'; if ((*p)[1] && (*p)[2] && (*p)[3] && (*p)[4]) *p += 4; }
        }
        if (n + 2 > cap) { char *g = (char *)realloc(s, cap *= 2); if (!g) { free(s); return NULL; } s = g; }
        s[n++] = c;
        (*p)++;
    }
    if (**p != '"') { free(s); return NULL; }
    (*p)++;
    s[n] = 0;
    return s;
}

static int parse(const char **p, mf_jv *v, int depth) {
    memset(v, 0, sizeof *v);
    ws(p);
    if (depth > 64) return 0;
    if (**p == '"') { v->s = parse_string(p); v->t = 2; return v->s != NULL; }
    if (**p == '[' || **p == '{') {
        int obj = **p == '{', cap = 0;
        char close = obj ? '}' : ']';
        v->t = obj ? 4 : 3;
        (*p)++;
        ws(p);
        if (**p == close) { (*p)++; return 1; }
        for (;;) {
            mf_jv item;
            char *key = NULL;
            ws(p);
            if (obj) {
                if (**p != '"') return 0;
                key = parse_string(p);
                if (!key) return 0;
                ws(p);
                if (**p != ':') { free(key); return 0; }
                (*p)++;
            }
            if (!parse(p, &item, depth + 1)) { free(key); free_inner(&item); return 0; }
            if (v->count == cap) {
                int nc = cap ? cap * 2 : 4;
                mf_jv *k = (mf_jv *)realloc(v->kids, sizeof(mf_jv) * (size_t)nc);
                char **ks = obj ? (char **)realloc(v->keys, sizeof(char *) * (size_t)nc) : NULL;
                if (!k || (obj && !ks)) { free(key); free_inner(&item); if (k) v->kids = k; if (ks) v->keys = ks; return 0; }
                v->kids = k;
                if (obj) v->keys = ks;
                cap = nc;
            }
            v->kids[v->count] = item;
            if (obj) v->keys[v->count] = key;
            v->count++;
            ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == close) { (*p)++; return 1; }
            return 0;
        }
    }
    if (strncmp(*p, "true", 4) == 0) { *p += 4; v->t = 5; v->n = 1; return 1; }
    if (strncmp(*p, "false", 5) == 0) { *p += 5; v->t = 5; return 1; }
    if (strncmp(*p, "null", 4) == 0) { *p += 4; return 1; }
    {
        char *end;
        v->n = strtod(*p, &end);
        if (end == *p) return 0;
        *p = end;
        v->t = 1;
        return 1;
    }
}

mf_jv *mf_json_parse(const char *text) {
    mf_jv *v = (mf_jv *)malloc(sizeof(mf_jv));
    const char *p = text;
    if (!v) return NULL;
    if (!parse(&p, v, 0)) { free_inner(v); free(v); return NULL; }
    return v;
}

const mf_jv *mf_json_get(const mf_jv *o, const char *key) {
    int i;
    if (!o || o->t != 4) return NULL;
    for (i = 0; i < o->count; i++) if (strcmp(o->keys[i], key) == 0) return &o->kids[i];
    return NULL;
}
const mf_jv *mf_json_at(const mf_jv *a, int i) { return a && a->t == 3 && i >= 0 && i < a->count ? &a->kids[i] : NULL; }
const char *mf_json_str(const mf_jv *v, const char *dflt) { return v && v->t == 2 ? v->s : dflt; }
double mf_json_num(const mf_jv *v, double dflt) { return v && (v->t == 1 || v->t == 5) ? v->n : dflt; }
void mf_json_free(mf_jv *v) { if (v) { free_inner(v); free(v); } }
