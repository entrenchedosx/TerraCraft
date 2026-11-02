#include "world/jjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Parser {
    const char *p;
    bool ok;
} Parser;

static void skip_ws(Parser *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r') {
        ps->p++;
    }
}

static JJson *node(JJType t)
{
    JJson *j = (JJson *)calloc(1, sizeof(JJson));
    if (j != NULL) {
        j->type = t;
    }
    return j;
}

static char *parse_string_raw(Parser *ps)
{
    if (*ps->p != '"') {
        ps->ok = false;
        return NULL;
    }
    ps->p++;
    size_t cap = 16, n = 0;
    char *s = (char *)malloc(cap);
    while (s != NULL && *ps->p != '"' && *ps->p != '\0') {
        char c = *ps->p++;
        if (c == '\\') {
            char e = *ps->p++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': /* BMP escape: keep ASCII range, replace others. */
                {
                    unsigned v = 0;
                    for (int i = 0; i < 4 && *ps->p != '\0'; ++i) {
                        char h = *ps->p++;
                        v = v * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
                    }
                    c = v < 128 ? (char)v : '?';
                }
                break;
            default: c = e; break;
            }
        }
        if (n + 1 >= cap) {
            cap *= 2;
            char *g = (char *)realloc(s, cap);
            if (g == NULL) {
                free(s);
                ps->ok = false;
                return NULL;
            }
            s = g;
        }
        s[n++] = c;
    }
    if (s == NULL || *ps->p != '"') {
        free(s);
        ps->ok = false;
        return NULL;
    }
    ps->p++;
    s[n] = '\0';
    return s;
}

static JJson *parse_value(Parser *ps, int depth);

static JJson *parse_array(Parser *ps, int depth)
{
    JJson *a = node(JJ_ARR);
    ps->p++;
    skip_ws(ps);
    int cap = 0;
    if (*ps->p == ']') {
        ps->p++;
        return a;
    }
    for (;;) {
        JJson *v = parse_value(ps, depth + 1);
        if (v == NULL || !ps->ok) {
            jj_free(v);
            ps->ok = false;
            return a;
        }
        if (a->count == cap) {
            cap = cap ? cap * 2 : 4;
            a->items = (JJson **)realloc(a->items, (size_t)cap * sizeof(JJson *));
        }
        a->items[a->count++] = v;
        skip_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == ']') {
            ps->p++;
            return a;
        }
        ps->ok = false;
        return a;
    }
}

static JJson *parse_object(Parser *ps, int depth)
{
    JJson *o = node(JJ_OBJ);
    ps->p++;
    skip_ws(ps);
    int cap = 0;
    if (*ps->p == '}') {
        ps->p++;
        return o;
    }
    for (;;) {
        skip_ws(ps);
        char *k = parse_string_raw(ps);
        if (k == NULL) {
            return o;
        }
        skip_ws(ps);
        if (*ps->p != ':') {
            free(k);
            ps->ok = false;
            return o;
        }
        ps->p++;
        JJson *v = parse_value(ps, depth + 1);
        if (v == NULL || !ps->ok) {
            free(k);
            jj_free(v);
            ps->ok = false;
            return o;
        }
        if (o->count == cap) {
            cap = cap ? cap * 2 : 4;
            o->items = (JJson **)realloc(o->items, (size_t)cap * sizeof(JJson *));
            o->keys = (char **)realloc(o->keys, (size_t)cap * sizeof(char *));
        }
        o->keys[o->count] = k;
        o->items[o->count++] = v;
        skip_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == '}') {
            ps->p++;
            return o;
        }
        ps->ok = false;
        return o;
    }
}

static JJson *parse_value(Parser *ps, int depth)
{
    if (depth > 128) {
        ps->ok = false;
        return NULL;
    }
    skip_ws(ps);
    char c = *ps->p;
    if (c == '{') {
        return parse_object(ps, depth);
    }
    if (c == '[') {
        return parse_array(ps, depth);
    }
    if (c == '"') {
        JJson *j = node(JJ_STR);
        j->text = parse_string_raw(ps);
        return j;
    }
    if (strncmp(ps->p, "true", 4) == 0 || strncmp(ps->p, "false", 5) == 0) {
        JJson *j = node(JJ_BOOL);
        j->boolean = ps->p[0] == 't';
        ps->p += j->boolean ? 4 : 5;
        return j;
    }
    if (strncmp(ps->p, "null", 4) == 0) {
        ps->p += 4;
        return node(JJ_NULL);
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const char *s = ps->p;
        while ((*ps->p >= '0' && *ps->p <= '9') || *ps->p == '-' || *ps->p == '+' || *ps->p == '.' ||
               *ps->p == 'e' || *ps->p == 'E') {
            ps->p++;
        }
        JJson *j = node(JJ_NUM);
        size_t n = (size_t)(ps->p - s);
        j->text = (char *)malloc(n + 1);
        memcpy(j->text, s, n);
        j->text[n] = '\0';
        return j;
    }
    ps->ok = false;
    return NULL;
}

JJson *jj_parse(const char *text)
{
    Parser ps = {text, true};
    JJson *j = parse_value(&ps, 0);
    if (!ps.ok) {
        jj_free(j);
        return NULL;
    }
    return j;
}

JJson *jj_parse_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (64L << 20)) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)n + 1);
    size_t got = buf != NULL ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    if (buf == NULL) {
        return NULL;
    }
    buf[got] = '\0';
    JJson *j = jj_parse(buf);
    free(buf);
    return j;
}

void jj_free(JJson *j)
{
    if (j == NULL) {
        return;
    }
    for (int i = 0; i < j->count; ++i) {
        jj_free(j->items[i]);
        if (j->keys != NULL) {
            free(j->keys[i]);
        }
    }
    free(j->items);
    free(j->keys);
    free(j->text);
    free(j);
}

const JJson *jj_get(const JJson *j, const char *key)
{
    if (j == NULL || j->type != JJ_OBJ) {
        return NULL;
    }
    for (int i = 0; i < j->count; ++i) {
        if (strcmp(j->keys[i], key) == 0) {
            return j->items[i];
        }
    }
    return NULL;
}

double jj_double(const JJson *j)
{
    return (j != NULL && j->type == JJ_NUM) ? strtod(j->text, NULL) : 0.0;
}

float jj_float(const JJson *j)
{
    return (j != NULL && j->type == JJ_NUM) ? strtof(j->text, NULL) : 0.0f;
}
