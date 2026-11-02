#include "world/jbiome.h"

#include "world/jjson.h"
#include "world/jrandom.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIMS 7

typedef struct RNode {
    int64_t lo[DIMS], hi[DIMS];
    int leaf;                 /* Leaf index, or -1 for a sub tree. */
    struct RNode **children;
    int nchildren;
} RNode;

struct JBiomeList {
    int count;
    int64_t (*lo)[DIMS];
    int64_t (*hi)[DIMS];
    int *name_idx;
    char **names;
    int nnames;
    RNode *root;
    RNode **all_nodes;        /* For freeing. */
    int all_count, all_cap;
    int last_leaf;            /* RTree.lastResult (single thread). */
};

int64_t jclimate_quantize(float f)
{
    float scaled = f * 10000.0f;
    return (int64_t)scaled;
}

JTargetPoint jclimate_sample(const JRandomState *rs, int qx, int qy, int qz)
{
    JTargetPoint t;
    memset(&t, 0, sizeof(t));
    int x = qx * 4, y = qy * 4, z = qz * 4;
    const JDensity *te = jrs_router(rs, "temperature");
    const JDensity *hu = jrs_router(rs, "vegetation");
    const JDensity *co = jrs_router(rs, "continents");
    const JDensity *er = jrs_router(rs, "erosion");
    const JDensity *de = jrs_router(rs, "depth");
    const JDensity *we = jrs_router(rs, "ridges");
    if (te == NULL || hu == NULL || co == NULL || er == NULL || de == NULL || we == NULL) {
        return t;
    }
    t.temperature = jclimate_quantize((float)jdf_compute(te, x, y, z));
    t.humidity = jclimate_quantize((float)jdf_compute(hu, x, y, z));
    t.continentalness = jclimate_quantize((float)jdf_compute(co, x, y, z));
    t.erosion = jclimate_quantize((float)jdf_compute(er, x, y, z));
    t.depth = jclimate_quantize((float)jdf_compute(de, x, y, z));
    t.weirdness = jclimate_quantize((float)jdf_compute(we, x, y, z));
    return t;
}

/* ---- R-tree construction ------------------------------------------------------ */

static RNode *new_node(JBiomeList *l)
{
    RNode *n = (RNode *)calloc(1, sizeof(RNode));
    if (n == NULL) {
        return NULL;
    }
    n->leaf = -1;
    if (l->all_count == l->all_cap) {
        l->all_cap = l->all_cap ? l->all_cap * 2 : 1024;
        l->all_nodes = (RNode **)realloc(l->all_nodes, (size_t)l->all_cap * sizeof(RNode *));
    }
    l->all_nodes[l->all_count++] = n;
    return n;
}

/* SubTree(List): bounding parameter space of the children (copied). */
static RNode *make_subtree(JBiomeList *l, RNode **kids, int n)
{
    RNode *s = new_node(l);
    s->children = (RNode **)malloc((size_t)n * sizeof(RNode *));
    memcpy(s->children, kids, (size_t)n * sizeof(RNode *));
    s->nchildren = n;
    for (int d = 0; d < DIMS; ++d) {
        int64_t lo = kids[0]->lo[d], hi = kids[0]->hi[d];
        for (int i = 1; i < n; ++i) {
            if (kids[i]->lo[d] < lo) {
                lo = kids[i]->lo[d];
            }
            if (kids[i]->hi[d] > hi) {
                hi = kids[i]->hi[d];
            }
        }
        s->lo[d] = lo;
        s->hi[d] = hi;
    }
    return s;
}

static int64_t llabs64(int64_t v)
{
    return v < 0 ? -v : v;
}

static int64_t mid_key(const RNode *n, int d, bool abs_)
{
    int64_t m = (n->lo[d] + n->hi[d]) / 2;
    return abs_ ? llabs64(m) : m;
}

typedef struct SortCtx {
    int first;
    bool abs_;
} SortCtx;

static int cmp_chain(const RNode *a, const RNode *b, const SortCtx *c)
{
    for (int j = 0; j < DIMS; ++j) {
        int d = (c->first + j) % DIMS;
        int64_t ka = mid_key(a, d, c->abs_);
        int64_t kb = mid_key(b, d, c->abs_);
        if (ka != kb) {
            return ka < kb ? -1 : 1;
        }
    }
    return 0;
}

static int64_t sum_mid_abs(const RNode *n)
{
    int64_t s = 0;
    for (int d = 0; d < DIMS; ++d) {
        s += llabs64((n->lo[d] + n->hi[d]) / 2);
    }
    return s;
}

/* Stable merge sort of node pointers; mode 0: chain comparator, 1: sum of |mid|. */
static void msort(RNode **a, RNode **tmp, int n, int mode, const SortCtx *c)
{
    if (n < 2) {
        return;
    }
    int h = n / 2;
    msort(a, tmp, h, mode, c);
    msort(a + h, tmp, n - h, mode, c);
    int i = 0, j = h, k = 0;
    while (i < h && j < n) {
        int cmp;
        if (mode == 1) {
            int64_t x = sum_mid_abs(a[i]), y = sum_mid_abs(a[j]);
            cmp = x < y ? -1 : (x > y ? 1 : 0);
        } else {
            cmp = cmp_chain(a[i], a[j], c);
        }
        tmp[k++] = (cmp <= 0) ? a[i++] : a[j++];
    }
    while (i < h) {
        tmp[k++] = a[i++];
    }
    while (j < n) {
        tmp[k++] = a[j++];
    }
    memcpy(a, tmp, (size_t)n * sizeof(RNode *));
}

static void sort_nodes(RNode **a, int n, int mode, int first, bool abs_)
{
    RNode **tmp = (RNode **)malloc((size_t)(n > 0 ? n : 1) * sizeof(RNode *));
    SortCtx c = {first, abs_};
    msort(a, tmp, n, mode, &c);
    free(tmp);
}

static int64_t space_cost(const RNode *n)
{
    int64_t s = 0;
    for (int d = 0; d < DIMS; ++d) {
        s += llabs64(n->hi[d] - n->lo[d]);
    }
    return s;
}

/* Climate.RTree.bucketize. Returns the new subtree array (count in *out_n). */
static RNode **bucketize(JBiomeList *l, RNode **kids, int n, int *out_n)
{
    int per = (int)pow(6.0, floor(log((double)n - 0.01) / log(6.0)));
    int cap = n / (per > 0 ? per : 1) + 2;
    RNode **out = (RNode **)malloc((size_t)cap * sizeof(RNode *));
    int count = 0;
    int start = 0;
    for (int i = 0; i < n; ++i) {
        if (i + 1 - start >= per) {
            out[count++] = make_subtree(l, kids + start, i + 1 - start);
            start = i + 1;
        }
    }
    if (start < n) {
        out[count++] = make_subtree(l, kids + start, n - start);
    }
    *out_n = count;
    return out;
}

static RNode *build(JBiomeList *l, RNode **kids_in, int n)
{
    if (n == 1) {
        return kids_in[0];
    }
    RNode **kids = (RNode **)malloc((size_t)n * sizeof(RNode *));
    memcpy(kids, kids_in, (size_t)n * sizeof(RNode *));
    if (n <= 6) {
        sort_nodes(kids, n, 1, 0, false);
        RNode *s = make_subtree(l, kids, n);
        free(kids);
        return s;
    }
    int64_t best = INT64_MAX;
    int best_k = -1;
    RNode **best_list = NULL;
    int best_n = 0;
    for (int k = 0; k < DIMS; ++k) {
        sort_nodes(kids, n, 0, k, false);
        int cnt = 0;
        RNode **list = bucketize(l, kids, n, &cnt);
        int64_t m = 0;
        for (int i = 0; i < cnt; ++i) {
            m += space_cost(list[i]);
        }
        if (best > m) {
            best = m;
            best_k = k;
            free(best_list);
            best_list = list;
            best_n = cnt;
        } else {
            free(list);
        }
    }
    sort_nodes(best_list, best_n, 0, best_k, true);
    RNode **built = (RNode **)malloc((size_t)best_n * sizeof(RNode *));
    for (int i = 0; i < best_n; ++i) {
        built[i] = build(l, best_list[i]->children, best_list[i]->nchildren);
    }
    RNode *s = make_subtree(l, built, best_n);
    free(built);
    free(best_list);
    free(kids);
    return s;
}

/* ---- loading ------------------------------------------------------------------ */

static void param_range(const JJson *p, int64_t *lo, int64_t *hi)
{
    if (p != NULL && p->type == JJ_ARR && p->count == 2) {
        *lo = jclimate_quantize(jj_float(p->items[0]));
        *hi = jclimate_quantize(jj_float(p->items[1]));
    } else {
        int64_t v = jclimate_quantize(jj_float(p));
        *lo = v;
        *hi = v;
    }
}

JBiomeList *jbiome_load(const char *root)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/reports/biome_parameters/minecraft/overworld.json", root);
    JJson *j = jj_parse_file(path);
    const JJson *arr = jj_get(j, "biomes");
    if (arr == NULL || arr->type != JJ_ARR || arr->count < 1) {
        jj_free(j);
        return NULL;
    }
    JBiomeList *l = (JBiomeList *)calloc(1, sizeof(JBiomeList));
    l->count = arr->count;
    l->lo = (int64_t(*)[DIMS])calloc((size_t)l->count, sizeof(int64_t[DIMS]));
    l->hi = (int64_t(*)[DIMS])calloc((size_t)l->count, sizeof(int64_t[DIMS]));
    l->name_idx = (int *)calloc((size_t)l->count, sizeof(int));
    l->last_leaf = -1;
    static const char *KEYS[6] = {"temperature", "humidity", "continentalness", "erosion", "depth", "weirdness"};
    for (int i = 0; i < l->count; ++i) {
        const JJson *e = arr->items[i];
        const JJson *name = jj_get(e, "biome");
        const JJson *pr = jj_get(e, "parameters");
        if (name == NULL || pr == NULL) {
            jbiome_free(l);
            jj_free(j);
            return NULL;
        }
        int ni = -1;
        for (int k = 0; k < l->nnames; ++k) {
            if (strcmp(l->names[k], name->text) == 0) {
                ni = k;
            }
        }
        if (ni < 0) {
            l->names = (char **)realloc(l->names, (size_t)(l->nnames + 1) * sizeof(char *));
            size_t len = strlen(name->text);
            l->names[l->nnames] = (char *)malloc(len + 1);
            memcpy(l->names[l->nnames], name->text, len + 1);
            ni = l->nnames++;
        }
        l->name_idx[i] = ni;
        for (int d = 0; d < 6; ++d) {
            param_range(jj_get(pr, KEYS[d]), &l->lo[i][d], &l->hi[i][d]);
        }
        int64_t off = jclimate_quantize(jj_float(jj_get(pr, "offset")));
        l->lo[i][6] = off;
        l->hi[i][6] = off;
    }
    jj_free(j);

    /* RTree.create: leaves in list order, then build(7, leaves). */
    RNode **leaves = (RNode **)malloc((size_t)l->count * sizeof(RNode *));
    for (int i = 0; i < l->count; ++i) {
        RNode *n = new_node(l);
        n->leaf = i;
        for (int d = 0; d < DIMS; ++d) {
            n->lo[d] = l->lo[i][d];
            n->hi[d] = l->hi[i][d];
        }
        leaves[i] = n;
    }
    l->root = build(l, leaves, l->count);
    free(leaves);
    return l;
}

void jbiome_free(JBiomeList *l)
{
    if (l == NULL) {
        return;
    }
    for (int i = 0; i < l->all_count; ++i) {
        free(l->all_nodes[i]->children);
        free(l->all_nodes[i]);
    }
    free(l->all_nodes);
    for (int i = 0; i < l->nnames; ++i) {
        free(l->names[i]);
    }
    free(l->names);
    free(l->lo);
    free(l->hi);
    free(l->name_idx);
    free(l);
}

int jbiome_leaf_count(const JBiomeList *l)
{
    return l != NULL ? l->count : 0;
}

int64_t jbiome_list_hash(const JBiomeList *l)
{
    uint64_t h = 1125899906842597ULL;
    for (int i = 0; i < l->count; ++i) {
        h = 31u * h + (uint64_t)(int64_t)jr_java_string_hash(l->names[l->name_idx[i]]);
        for (int d = 0; d < 6; ++d) {
            h = 31u * h + (uint64_t)l->lo[i][d];
            h = 31u * h + (uint64_t)l->hi[i][d];
        }
        h = 31u * h + (uint64_t)l->lo[i][6];
    }
    return (int64_t)h;
}

/* ---- search --------------------------------------------------------------------- */

static int64_t dist1(int64_t lo, int64_t hi, int64_t x)
{
    int64_t a = x - hi;
    int64_t b = lo - x;
    return a > 0 ? a : (b > 0 ? b : 0);
}

static int64_t node_distance(const RNode *n, const int64_t t[DIMS])
{
    int64_t s = 0;
    for (int d = 0; d < DIMS; ++d) {
        int64_t v = dist1(n->lo[d], n->hi[d], t[d]);
        s += v * v;
    }
    return s;
}

/* SubTree.search / Leaf.search. cand < 0 means no candidate. */
static int tree_search(const JBiomeList *l, const RNode *n, const int64_t t[DIMS], int cand)
{
    if (n->leaf >= 0) {
        return n->leaf;
    }
    int64_t d = INT64_MAX;
    if (cand >= 0) {
        RNode tmp;
        memset(&tmp, 0, sizeof(tmp));
        for (int k = 0; k < DIMS; ++k) {
            tmp.lo[k] = l->lo[cand][k];
            tmp.hi[k] = l->hi[cand][k];
        }
        d = node_distance(&tmp, t);
    }
    int result = cand;
    for (int i = 0; i < n->nchildren; ++i) {
        const RNode *c = n->children[i];
        int64_t e = node_distance(c, t);
        if (d > e) {
            int leaf = tree_search(l, c, t, result);
            int64_t f;
            if (c->leaf >= 0 && leaf == c->leaf) {
                f = e;
            } else {
                RNode tmp;
                memset(&tmp, 0, sizeof(tmp));
                for (int k = 0; k < DIMS; ++k) {
                    tmp.lo[k] = l->lo[leaf][k];
                    tmp.hi[k] = l->hi[leaf][k];
                }
                f = node_distance(&tmp, t);
            }
            if (d > f) {
                d = f;
                result = leaf;
            }
        }
    }
    return result;
}

const char *jbiome_find(JBiomeList *l, const JTargetPoint *t)
{
    int64_t tp[DIMS] = {t->temperature, t->humidity, t->continentalness, t->erosion, t->depth, t->weirdness, 0};
    int leaf = tree_search(l, l->root, tp, l->last_leaf);
    l->last_leaf = leaf;
    return l->names[l->name_idx[leaf]];
}

const char *jbiome_find_brute(const JBiomeList *l, const JTargetPoint *t)
{
    int64_t tp[DIMS] = {t->temperature, t->humidity, t->continentalness, t->erosion, t->depth, t->weirdness, 0};
    int64_t best = INT64_MAX;
    int bi = 0;
    for (int i = 0; i < l->count; ++i) {
        RNode tmp;
        memset(&tmp, 0, sizeof(tmp));
        for (int k = 0; k < DIMS; ++k) {
            tmp.lo[k] = l->lo[i][k];
            tmp.hi[k] = l->hi[i][k];
        }
        int64_t f = node_distance(&tmp, tp);
        if (f < best) {
            best = f;
            bi = i;
        }
    }
    return l->names[l->name_idx[bi]];
}
