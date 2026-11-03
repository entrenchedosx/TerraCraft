#include "world/jfeature.h"

#include "world/jjson.h"
#include "world/jrandom.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

#define MIN_Y (-64)
#define HEIGHT 384
#define STEPS 11
#define NAME_LEN 72

/* ---- Mth ------------------------------------------------------------------------ */

static float g_sin[65536];
static bool g_sin_ready;

static double mth_sin(double d)
{
    if (!g_sin_ready) {
        for (int i = 0; i < 65536; ++i) {
            g_sin[i] = (float)sin((double)i * 3.14159265358979323846 * 2.0 / 65536.0);
        }
        g_sin_ready = true;
    }
    return (double)g_sin[(int)((long long)(d * 10430.378350470453) & 65535)];
}

static int mth_floor(double v)
{
    int i = (int)v;
    return v < (double)i ? i - 1 : i;
}

static int mth_ceil_f(float v)
{
    int i = (int)v;
    return v > (float)i ? i + 1 : i;
}

static double mth_lerp(double d, double s, double e)
{
    return s + d * (e - s);
}

static int random_between_inclusive(JrRandom *r, int lo, int hi)
{
    return jr_next_int_bound(r, hi - lo + 1) + lo;
}

/* ---- data model ------------------------------------------------------------------------ */

typedef struct IntProv {
    int kind;  /* 0 constant, 1 uniform, -1 unsupported */
    int a, b;
} IntProv;

typedef struct Anchor {
    int kind;  /* 0 absolute, 1 above_bottom, 2 below_top */
    int value;
} Anchor;

typedef struct HeightProv {
    int kind;  /* 0 uniform, 1 trapezoid, 2 constant anchor, -1 unsupported */
    Anchor lo, hi;
    int plateau;
} HeightProv;

typedef enum ModKind { M_COUNT, M_IN_SQUARE, M_HEIGHT_RANGE, M_RARITY, M_BIOME, M_UNSUPPORTED } ModKind;

typedef struct Mod {
    ModKind kind;
    IntProv count;
    HeightProv height;
    int chance;
} Mod;

typedef struct OreTarget {
    int kind;  /* 0 tag_match, 1 block_match, -1 unsupported */
    char tag[NAME_LEN];
    int block;
    int state;
} OreTarget;

typedef struct PlacedFeature {
    char name[NAME_LEN];
    Mod *mods;
    int mod_count;
    char type[NAME_LEN];  /* configured feature type, e.g. minecraft:ore */
    int ore_size;
    float ore_discard;
    OreTarget *targets;
    int target_count;
    bool mods_supported;
    bool loaded;
} PlacedFeature;

typedef struct BiomeFeatures {
    char name[NAME_LEN];
    int count[STEPS];
    int *pf[STEPS];  /* indices into JFeatures.pf */
} BiomeFeatures;

typedef struct TagSet {
    char name[NAME_LEN];
    bool has[256];
    int built_for;  /* block count the set was built for */
} TagSet;

struct JFeatures {
    char root[1024];
    PlacedFeature *pf;
    int pf_count, pf_cap;
    BiomeFeatures *biomes;
    int biome_count;
    int *step_list[STEPS];  /* FeatureSorter result: indices into pf per step */
    int step_len[STEPS];
    int *index_in_step;     /* pf index -> index inside its step list (-1 if unused) */
    TagSet *tags;
    int tag_count, tag_cap;
    int changed;
};

/* ---- parsing -------------------------------------------------------------------------------- */

static const char *jstr(const JJson *j)
{
    return (j != NULL && j->type == JJ_STR) ? j->text : "";
}

static const char *local_name(const char *id)
{
    const char *c = strchr(id, ':');
    return c != NULL ? c + 1 : id;
}

static int anchor_resolve(const Anchor *a)
{
    switch (a->kind) {
    case 0: return a->value;
    case 1: return MIN_Y + a->value;
    default: return MIN_Y + HEIGHT - 1 - a->value;
    }
}

static Anchor parse_anchor(const JJson *j)
{
    Anchor a = {0, 0};
    const JJson *v;
    if ((v = jj_get(j, "absolute")) != NULL) {
        a.kind = 0;
        a.value = (int)jj_double(v);
    } else if ((v = jj_get(j, "above_bottom")) != NULL) {
        a.kind = 1;
        a.value = (int)jj_double(v);
    } else if ((v = jj_get(j, "below_top")) != NULL) {
        a.kind = 2;
        a.value = (int)jj_double(v);
    }
    return a;
}

static IntProv parse_int_provider(const JJson *j)
{
    IntProv p = {-1, 0, 0};
    if (j == NULL) {
        return p;
    }
    if (j->type == JJ_NUM) {
        p.kind = 0;
        p.a = (int)jj_double(j);
        return p;
    }
    const char *t = local_name(jstr(jj_get(j, "type")));
    if (strcmp(t, "constant") == 0) {
        p.kind = 0;
        p.a = (int)jj_double(jj_get(j, "value"));
    } else if (strcmp(t, "uniform") == 0) {
        p.kind = 1;
        p.a = (int)jj_double(jj_get(j, "min_inclusive"));
        p.b = (int)jj_double(jj_get(j, "max_inclusive"));
    }
    return p;
}

static HeightProv parse_height_provider(const JJson *j)
{
    HeightProv h;
    memset(&h, 0, sizeof(h));
    h.kind = -1;
    const JJson *type = jj_get(j, "type");
    if (type == NULL) {
        h.kind = 2;
        h.lo = parse_anchor(j);
        return h;
    }
    const char *t = local_name(jstr(type));
    if (strcmp(t, "uniform") == 0 || strcmp(t, "trapezoid") == 0) {
        h.kind = strcmp(t, "uniform") == 0 ? 0 : 1;
        h.lo = parse_anchor(jj_get(j, "min_inclusive"));
        h.hi = parse_anchor(jj_get(j, "max_inclusive"));
        const JJson *pl = jj_get(j, "plateau");
        h.plateau = pl != NULL ? (int)jj_double(pl) : 0;
    } else if (strcmp(t, "constant") == 0) {
        h.kind = 2;
        h.lo = parse_anchor(jj_get(j, "value"));
    }
    return h;
}

static int sample_int(const IntProv *p, JrRandom *r)
{
    if (p->kind == 0) {
        return p->a;
    }
    return random_between_inclusive(r, p->a, p->b);
}

static int sample_height(const HeightProv *h, JrRandom *r)
{
    int i = anchor_resolve(&h->lo);
    if (h->kind == 2) {
        return i;
    }
    int j = anchor_resolve(&h->hi);
    if (i > j) {
        return i;
    }
    if (h->kind == 0) {
        return random_between_inclusive(r, i, j);
    }
    int k = j - i;
    if (h->plateau >= k) {
        return random_between_inclusive(r, i, j);
    }
    int l = (k - h->plateau) / 2;
    int m = k - l;
    return i + random_between_inclusive(r, 0, m) + random_between_inclusive(r, 0, l);
}

static bool load_placed(JFeatures *f, PlacedFeature *pf)
{
    char path[1200];
    snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/placed_feature/%s.json", f->root, local_name(pf->name));
    JJson *j = jj_parse_file(path);
    if (j == NULL) {
        return false;
    }
    const JJson *placement = jj_get(j, "placement");
    pf->mods_supported = true;
    if (placement != NULL && placement->type == JJ_ARR) {
        pf->mods = (Mod *)calloc((size_t)placement->count, sizeof(Mod));
        pf->mod_count = placement->count;
        for (int i = 0; i < placement->count; ++i) {
            const JJson *m = placement->items[i];
            const char *t = local_name(jstr(jj_get(m, "type")));
            Mod *mod = &pf->mods[i];
            mod->kind = M_UNSUPPORTED;
            if (strcmp(t, "count") == 0) {
                mod->kind = M_COUNT;
                mod->count = parse_int_provider(jj_get(m, "count"));
                if (mod->count.kind < 0) {
                    mod->kind = M_UNSUPPORTED;
                }
            } else if (strcmp(t, "in_square") == 0) {
                mod->kind = M_IN_SQUARE;
            } else if (strcmp(t, "height_range") == 0) {
                mod->kind = M_HEIGHT_RANGE;
                mod->height = parse_height_provider(jj_get(m, "height"));
                if (mod->height.kind < 0) {
                    mod->kind = M_UNSUPPORTED;
                }
            } else if (strcmp(t, "rarity_filter") == 0) {
                mod->kind = M_RARITY;
                mod->chance = (int)jj_double(jj_get(m, "chance"));
            } else if (strcmp(t, "biome") == 0) {
                mod->kind = M_BIOME;
            }
            if (mod->kind == M_UNSUPPORTED) {
                pf->mods_supported = false;
            }
        }
    }
    const JJson *feature = jj_get(j, "feature");
    JJson *cf = NULL;
    const JJson *cfv = feature;
    if (feature != NULL && feature->type == JJ_STR) {
        snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/configured_feature/%s.json", f->root, local_name(feature->text));
        cf = jj_parse_file(path);
        cfv = cf;
    }
    if (cfv != NULL) {
        snprintf(pf->type, sizeof(pf->type), "%s", jstr(jj_get(cfv, "type")));
        const JJson *cfg = jj_get(cfv, "config");
        if (strcmp(pf->type, "minecraft:ore") == 0 && cfg != NULL) {
            pf->ore_size = (int)jj_double(jj_get(cfg, "size"));
            pf->ore_discard = jj_float(jj_get(cfg, "discard_chance_on_air_exposure"));
            const JJson *targets = jj_get(cfg, "targets");
            if (targets != NULL && targets->type == JJ_ARR) {
                pf->targets = (OreTarget *)calloc((size_t)targets->count, sizeof(OreTarget));
                pf->target_count = targets->count;
                for (int i = 0; i < targets->count; ++i) {
                    OreTarget *t = &pf->targets[i];
                    const JJson *test = jj_get(targets->items[i], "target");
                    const char *pt = local_name(jstr(jj_get(test, "predicate_type")));
                    t->kind = -1;
                    if (strcmp(pt, "tag_match") == 0) {
                        t->kind = 0;
                        snprintf(t->tag, sizeof(t->tag), "%s", jstr(jj_get(test, "tag")));
                    } else if (strcmp(pt, "block_match") == 0) {
                        t->kind = 1;
                        t->block = jfill_block_register(jstr(jj_get(test, "block")));
                    }
                    t->state = jfill_block_register(jstr(jj_get(jj_get(targets->items[i], "state"), "Name")));
                }
            }
        }
    }
    jj_free(cf);
    jj_free(j);
    pf->loaded = true;
    return true;
}

static int pf_index(JFeatures *f, const char *name)
{
    for (int i = 0; i < f->pf_count; ++i) {
        if (strcmp(f->pf[i].name, name) == 0) {
            return i;
        }
    }
    if (f->pf_count == f->pf_cap) {
        f->pf_cap = f->pf_cap != 0 ? f->pf_cap * 2 : 256;
        f->pf = (PlacedFeature *)realloc(f->pf, (size_t)f->pf_cap * sizeof(PlacedFeature));
    }
    PlacedFeature *pf = &f->pf[f->pf_count];
    memset(pf, 0, sizeof(*pf));
    snprintf(pf->name, sizeof(pf->name), "%s", name);
    return f->pf_count++;
}

/* ---- FeatureSorter ------------------------------------------------------------------------------ */

typedef struct Node {
    int step;
    int index;   /* feature index in order of first appearance */
    int pf;      /* placed feature index */
    int *edges;  /* node indices, sorted by (step, index) */
    int edge_count, edge_cap;
    int state;   /* 0 unvisited, 1 in progress, 2 visited */
} Node;

typedef struct Sorter {
    Node *nodes;
    int count, cap;
} Sorter;

static int node_cmp_key(const Node *a, const Node *b)
{
    if (a->step != b->step) {
        return a->step < b->step ? -1 : 1;
    }
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static int sorter_find(Sorter *s, int step, int index)
{
    for (int i = 0; i < s->count; ++i) {
        if (s->nodes[i].step == step && s->nodes[i].index == index) {
            return i;
        }
    }
    if (s->count == s->cap) {
        s->cap = s->cap != 0 ? s->cap * 2 : 512;
        s->nodes = (Node *)realloc(s->nodes, (size_t)s->cap * sizeof(Node));
    }
    Node *n = &s->nodes[s->count];
    memset(n, 0, sizeof(*n));
    n->step = step;
    n->index = index;
    return s->count++;
}

static void edge_add(Sorter *s, int from, int to)
{
    Node *n = &s->nodes[from];
    for (int i = 0; i < n->edge_count; ++i) {
        if (n->edges[i] == to) {
            return;
        }
    }
    if (n->edge_count == n->edge_cap) {
        n->edge_cap = n->edge_cap != 0 ? n->edge_cap * 2 : 4;
        n->edges = (int *)realloc(n->edges, (size_t)n->edge_cap * sizeof(int));
    }
    n->edges[n->edge_count++] = to;
}

static Sorter *g_sorter_for_cmp;

static int node_ptr_cmp(const void *a, const void *b)
{
    return node_cmp_key(&g_sorter_for_cmp->nodes[*(const int *)a], &g_sorter_for_cmp->nodes[*(const int *)b]);
}

static bool sorter_dfs(Sorter *s, int v, int *out, int *out_count)
{
    Node *n = &s->nodes[v];
    if (n->state == 2) {
        return false;
    }
    if (n->state == 1) {
        return true;
    }
    n->state = 1;
    for (int i = 0; i < n->edge_count; ++i) {
        if (sorter_dfs(s, n->edges[i], out, out_count)) {
            return true;
        }
    }
    s->nodes[v].state = 2;
    out[(*out_count)++] = v;
    return false;
}

/* FeatureSorter.buildFeaturesPerStep over the biomes in `order`. */
static bool build_steps(JFeatures *f, const int *biome_order, int nbiomes)
{
    Sorter s = {NULL, 0, 0};
    int *flat = NULL;
    int feature_counter = 0;
    int *feature_index = (int *)malloc((size_t)(f->pf_count + 1) * sizeof(int));
    for (int i = 0; i <= f->pf_count; ++i) {
        feature_index[i] = -1;
    }
    for (int bi = 0; bi < nbiomes; ++bi) {
        const BiomeFeatures *b = &f->biomes[biome_order[bi]];
        int total = 0;
        for (int st = 0; st < STEPS; ++st) {
            total += b->count[st];
        }
        flat = (int *)realloc(flat, (size_t)(total + 1) * sizeof(int));
        int n = 0;
        for (int st = 0; st < STEPS; ++st) {
            for (int k = 0; k < b->count[st]; ++k) {
                int pfi = b->pf[st][k];
                if (feature_index[pfi] < 0) {
                    feature_index[pfi] = feature_counter++;
                }
                int node = sorter_find(&s, st, feature_index[pfi]);
                s.nodes[node].pf = pfi;
                flat[n++] = node;
            }
        }
        for (int j = 0; j < n; ++j) {
            if (j < n - 1) {
                edge_add(&s, flat[j], flat[j + 1]);
            }
        }
    }
    /* TreeMap iteration order (sorted keys) and TreeSet child order. */
    g_sorter_for_cmp = &s;
    int *keys = (int *)malloc((size_t)(s.count + 1) * sizeof(int));
    for (int i = 0; i < s.count; ++i) {
        keys[i] = i;
    }
    qsort(keys, (size_t)s.count, sizeof(int), node_ptr_cmp);
    for (int i = 0; i < s.count; ++i) {
        qsort(s.nodes[i].edges, (size_t)s.nodes[i].edge_count, sizeof(int), node_ptr_cmp);
    }
    int *post = (int *)malloc((size_t)(s.count + 1) * sizeof(int));
    int post_count = 0;
    bool cycle = false;
    for (int i = 0; i < s.count && !cycle; ++i) {
        if (s.nodes[keys[i]].state == 2) {
            continue;
        }
        cycle = sorter_dfs(&s, keys[i], post, &post_count);
    }
    if (!cycle) {
        /* Collections.reverse, then one list per step in that order. */
        for (int st = 0; st < STEPS; ++st) {
            f->step_list[st] = (int *)malloc((size_t)(post_count + 1) * sizeof(int));
            f->step_len[st] = 0;
            for (int i = post_count - 1; i >= 0; --i) {
                if (s.nodes[post[i]].step == st) {
                    f->step_list[st][f->step_len[st]++] = s.nodes[post[i]].pf;
                }
            }
        }
    }
    for (int i = 0; i < s.count; ++i) {
        free(s.nodes[i].edges);
    }
    free(s.nodes);
    free(keys);
    free(post);
    free(flat);
    free(feature_index);
    return !cycle;
}

/* ---- loading ---------------------------------------------------------------------------------------- */

static BiomeFeatures *biome_features(JFeatures *f, const char *name)
{
    for (int i = 0; i < f->biome_count; ++i) {
        if (strcmp(f->biomes[i].name, name) == 0) {
            return &f->biomes[i];
        }
    }
    return NULL;
}

void jfeatures_free(JFeatures *f)
{
    if (f == NULL) {
        return;
    }
    for (int i = 0; i < f->pf_count; ++i) {
        free(f->pf[i].mods);
        free(f->pf[i].targets);
    }
    free(f->pf);
    for (int i = 0; i < f->biome_count; ++i) {
        for (int st = 0; st < STEPS; ++st) {
            free(f->biomes[i].pf[st]);
        }
    }
    free(f->biomes);
    for (int st = 0; st < STEPS; ++st) {
        free(f->step_list[st]);
    }
    free(f->index_in_step);
    free(f->tags);
    free(f);
}

JFeatures *jfeatures_load(const char *datapack_root, JSurface *surface)
{
    if (datapack_root == NULL || surface == NULL) {
        return NULL;
    }
    JFeatures *f = (JFeatures *)calloc(1, sizeof(JFeatures));
    if (f == NULL) {
        return NULL;
    }
    snprintf(f->root, sizeof(f->root), "%s", datapack_root);
    int nb = jsurface_possible_biome_count(surface);
    f->biomes = (BiomeFeatures *)calloc((size_t)(nb + 1), sizeof(BiomeFeatures));
    int *order = (int *)malloc((size_t)(nb + 1) * sizeof(int));
    for (int i = 0; i < nb; ++i) {
        BiomeFeatures *b = &f->biomes[f->biome_count];
        snprintf(b->name, sizeof(b->name), "%s", jsurface_possible_biome(surface, i));
        char path[1200];
        snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/biome/%s.json", datapack_root, local_name(b->name));
        JJson *j = jj_parse_file(path);
        const JJson *feats = jj_get(j, "features");
        if (feats == NULL || feats->type != JJ_ARR) {
            jj_free(j);
            free(order);
            jfeatures_free(f);
            return NULL;
        }
        for (int st = 0; st < feats->count && st < STEPS; ++st) {
            const JJson *list = feats->items[st];
            if (list->type != JJ_ARR) {
                continue;
            }
            b->pf[st] = (int *)malloc((size_t)(list->count + 1) * sizeof(int));
            b->count[st] = list->count;
            for (int k = 0; k < list->count; ++k) {
                b->pf[st][k] = pf_index(f, jstr(list->items[k]));
            }
        }
        jj_free(j);
        order[f->biome_count] = f->biome_count;
        f->biome_count++;
    }
    for (int i = 0; i < f->pf_count; ++i) {
        if (!load_placed(f, &f->pf[i])) {
            fprintf(stderr, "jfeature: cannot load placed feature %s\n", f->pf[i].name);
            free(order);
            jfeatures_free(f);
            return NULL;
        }
    }
    bool ok = build_steps(f, order, f->biome_count);
    free(order);
    if (!ok) {
        jfeatures_free(f);
        return NULL;
    }
    f->index_in_step = (int *)malloc((size_t)(f->pf_count + 1) * sizeof(int));
    for (int i = 0; i < f->pf_count; ++i) {
        f->index_in_step[i] = -1;
    }
    for (int st = 0; st < STEPS; ++st) {
        for (int i = 0; i < f->step_len[st]; ++i) {
            f->index_in_step[f->step_list[st][i]] = i;
        }
    }
    return f;
}

int jfeatures_step_count(const JFeatures *f)
{
    (void)f;
    return STEPS;
}

int jfeatures_step_size(const JFeatures *f, int step)
{
    return f->step_len[step];
}

const char *jfeatures_step_feature(const JFeatures *f, int step, int index)
{
    return f->pf[f->step_list[step][index]].name;
}

int jfeatures_last_changed(const JFeatures *f)
{
    return f->changed;
}

/* ---- tags ----------------------------------------------------------------------------------------------- */

static void tag_collect(const JFeatures *f, const char *tag, bool *has)
{
    char path[1300];
    snprintf(path, sizeof(path), "%s/data/minecraft/tags/block/%s.json", f->root, local_name(tag));
    JJson *j = jj_parse_file(path);
    const JJson *values = jj_get(j, "values");
    if (values != NULL && values->type == JJ_ARR) {
        for (int i = 0; i < values->count; ++i) {
            const char *v = jstr(values->items[i]);
            if (v[0] == '#') {
                tag_collect(f, v + 1, has);
            } else {
                int id = jfill_block_from_name(v);
                if (id >= 0) {
                    has[id] = true;
                }
            }
        }
    }
    jj_free(j);
}

static bool tag_has(JFeatures *f, const char *tag, int block)
{
    TagSet *t = NULL;
    for (int i = 0; i < f->tag_count; ++i) {
        if (strcmp(f->tags[i].name, tag) == 0) {
            t = &f->tags[i];
            break;
        }
    }
    if (t == NULL) {
        if (f->tag_count == f->tag_cap) {
            f->tag_cap = f->tag_cap != 0 ? f->tag_cap * 2 : 16;
            f->tags = (TagSet *)realloc(f->tags, (size_t)f->tag_cap * sizeof(TagSet));
        }
        t = &f->tags[f->tag_count++];
        memset(t, 0, sizeof(*t));
        snprintf(t->name, sizeof(t->name), "%s", tag);
        t->built_for = -1;
    }
    if (t->built_for != jfill_block_count()) {
        memset(t->has, 0, sizeof(t->has));
        tag_collect(f, tag, t->has);
        t->built_for = jfill_block_count();
    }
    return block >= 0 && block < 256 && t->has[block];
}

/* ---- level access ---------------------------------------------------------------------------------------- */

typedef struct Level {
    JFeatures *f;
    JSurface *surface;
    JRegion *region;
    JrRandom *rnd;
} Level;

static int chunk_slot(const Level *l, int x, int z)
{
    int dx = (x >> 4) - (l->region->target_x - 1);
    int dz = (z >> 4) - (l->region->target_z - 1);
    if (dx < 0 || dx > 2 || dz < 0 || dz > 2) {
        return -1;
    }
    return dz * 3 + dx;
}

static int level_get(const Level *l, int x, int y, int z)
{
    int slot = chunk_slot(l, x, z);
    if (slot < 0 || y < MIN_Y || y >= MIN_Y + HEIGHT) {
        return JB_AIR;
    }
    return l->region->blocks[slot][JFILL_INDEX(x & 15, z & 15, y)];
}

/* Direct section write (no heightmap update), as OreFeature does through BulkSectionAccess. */
static void level_set_raw(Level *l, int x, int y, int z, int state)
{
    int slot = chunk_slot(l, x, z);
    if (slot >= 0 && y >= MIN_Y && y < MIN_Y + HEIGHT) {
        l->region->blocks[slot][JFILL_INDEX(x & 15, z & 15, y)] = (uint8_t)state;
        l->f->changed++;
    }
}

static bool level_can_write(const Level *l, int x, int z)
{
    return x >= (l->region->target_x - 1) * 16 && x <= (l->region->target_x + 2) * 16 - 1 &&
           z >= (l->region->target_z - 1) * 16 && z <= (l->region->target_z + 2) * 16 - 1;
}

/* WorldGenLevel.getHeight(OCEAN_FLOOR_WG, x, z) */
static int level_ocean_floor(const Level *l, int x, int z)
{
    int slot = chunk_slot(l, x, z);
    if (slot < 0) {
        return MIN_Y;
    }
    return l->region->heightmaps[slot]->ocean_floor[(z & 15) * 16 + (x & 15)];
}

/* ---- OreFeature ---------------------------------------------------------------------------------------------- */

static bool target_test(JFeatures *f, const OreTarget *t, int block)
{
    if (t->kind == 0) {
        return tag_has(f, t->tag, block);
    }
    if (t->kind == 1) {
        return block == t->block;
    }
    return false;
}

static bool adjacent_to_air(const Level *l, int x, int y, int z)
{
    static const int D[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    for (int i = 0; i < 6; ++i) {
        if (jblock_is_air(level_get(l, x + D[i][0], y + D[i][1], z + D[i][2]))) {
            return true;
        }
    }
    return false;
}

static bool can_place_ore(Level *l, int block, const PlacedFeature *pf, const OreTarget *t, int x, int y, int z)
{
    if (!target_test(l->f, t, block)) {
        return false;
    }
    float chance = pf->ore_discard;
    bool skip;
    if (chance <= 0.0f) {
        skip = true;
    } else if (chance >= 1.0f) {
        skip = false;
    } else {
        skip = jr_next_float(l->rnd) >= chance;
    }
    if (skip) {
        return true;
    }
    return !adjacent_to_air(l, x, y, z);
}

static bool ore_do_place(Level *l, const PlacedFeature *pf, double min_x, double max_x, double min_z, double max_z,
                         double min_y, double max_y, int x, int y, int z, int width, int height)
{
    JrRandom *r = l->rnd;
    int placed = 0;
    int n = pf->ore_size;
    size_t bits = (size_t)width * (size_t)height * (size_t)width;
    uint8_t *bitset = (uint8_t *)calloc(bits + 1, 1);
    double *ds = (double *)malloc((size_t)n * 4 * sizeof(double));
    if (bitset == NULL || ds == NULL) {
        free(bitset);
        free(ds);
        return false;
    }
    for (int k = 0; k < n; ++k) {
        float f = (float)k / (float)n;
        double d = mth_lerp((double)f, min_x, max_x);
        double e = mth_lerp((double)f, min_y, max_y);
        double g = mth_lerp((double)f, min_z, max_z);
        double h = jr_next_double(r) * (double)n / 16.0;
        double rad = ((double)(float)(mth_sin((double)(3.1415927f * f)) + 1.0) * h + 1.0) / 2.0;
        ds[k * 4 + 0] = d;
        ds[k * 4 + 1] = e;
        ds[k * 4 + 2] = g;
        ds[k * 4 + 3] = rad;
    }
    for (int k = 0; k < n - 1; ++k) {
        if (!(ds[k * 4 + 3] <= 0.0)) {
            for (int m = k + 1; m < n; ++m) {
                if (!(ds[m * 4 + 3] <= 0.0)) {
                    double d = ds[k * 4 + 0] - ds[m * 4 + 0];
                    double e = ds[k * 4 + 1] - ds[m * 4 + 1];
                    double g = ds[k * 4 + 2] - ds[m * 4 + 2];
                    double h = ds[k * 4 + 3] - ds[m * 4 + 3];
                    if (h * h > d * d + e * e + g * g) {
                        if (h > 0.0) {
                            ds[m * 4 + 3] = -1.0;
                        } else {
                            ds[k * 4 + 3] = -1.0;
                        }
                    }
                }
            }
        }
    }
    for (int k = 0; k < n; ++k) {
        double d = ds[k * 4 + 3];
        if (d < 0.0) {
            continue;
        }
        double e = ds[k * 4 + 0], g = ds[k * 4 + 1], h = ds[k * 4 + 2];
        int m = mth_floor(e - d);
        if (m < x) {
            m = x;
        }
        int nn = mth_floor(g - d);
        if (nn < y) {
            nn = y;
        }
        int o = mth_floor(h - d);
        if (o < z) {
            o = z;
        }
        int p = mth_floor(e + d);
        if (p < m) {
            p = m;
        }
        int q = mth_floor(g + d);
        if (q < nn) {
            q = nn;
        }
        int rr = mth_floor(h + d);
        if (rr < o) {
            rr = o;
        }
        for (int s = m; s <= p; ++s) {
            double t = ((double)s + 0.5 - e) / d;
            if (t * t < 1.0) {
                for (int u = nn; u <= q; ++u) {
                    double v = ((double)u + 0.5 - g) / d;
                    if (t * t + v * v < 1.0) {
                        for (int w = o; w <= rr; ++w) {
                            double aa = ((double)w + 0.5 - h) / d;
                            if (t * t + v * v + aa * aa < 1.0 && !(u < MIN_Y || u >= MIN_Y + HEIGHT)) {
                                size_t ab = (size_t)((s - x) + (u - y) * width + (w - z) * width * height);
                                if (!bitset[ab]) {
                                    bitset[ab] = 1;
                                    if (level_can_write(l, s, w)) {
                                        int slot = chunk_slot(l, s, w);
                                        if (slot >= 0) {
                                            int state = level_get(l, s, u, w);
                                            for (int ti = 0; ti < pf->target_count; ++ti) {
                                                if (can_place_ore(l, state, pf, &pf->targets[ti], s, u, w)) {
                                                    level_set_raw(l, s, u, w, pf->targets[ti].state);
                                                    ++placed;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    free(bitset);
    free(ds);
    return placed > 0;
}

static bool ore_place(Level *l, const PlacedFeature *pf, int ox, int oy, int oz)
{
    JrRandom *r = l->rnd;
    float f = jr_next_float(r) * 3.1415927f;
    float g = (float)pf->ore_size / 8.0f;
    int i = mth_ceil_f(((float)pf->ore_size / 16.0f * 2.0f + 1.0f) / 2.0f);
    double d = (double)ox + sin((double)f) * (double)g;
    double e = (double)ox - sin((double)f) * (double)g;
    double h = (double)oz + cos((double)f) * (double)g;
    double j = (double)oz - cos((double)f) * (double)g;
    double l1 = (double)(oy + jr_next_int_bound(r, 3) - 2);
    double m1 = (double)(oy + jr_next_int_bound(r, 3) - 2);
    int n = ox - mth_ceil_f(g) - i;
    int o = oy - 2 - i;
    int p = oz - mth_ceil_f(g) - i;
    int q = 2 * (mth_ceil_f(g) + i);
    int rr = 2 * (2 + i);
    for (int s = n; s <= n + q; ++s) {
        for (int t = p; t <= p + q; ++t) {
            if (o <= level_ocean_floor(l, s, t)) {
                return ore_do_place(l, pf, d, e, h, j, l1, m1, n, o, p, q, rr);
            }
        }
    }
    return false;
}

/* ---- PlacedFeature ------------------------------------------------------------------------------------------------- */

static bool biome_has_feature(JFeatures *f, const char *biome, int pfi)
{
    const BiomeFeatures *b = biome_features(f, biome);
    if (b == NULL) {
        return false;
    }
    for (int st = 0; st < STEPS; ++st) {
        for (int k = 0; k < b->count[st]; ++k) {
            if (b->pf[st][k] == pfi) {
                return true;
            }
        }
    }
    return false;
}

static bool run_mods(Level *l, int pfi, int mi, int x, int y, int z)
{
    const PlacedFeature *pf = &l->f->pf[pfi];
    if (mi == pf->mod_count) {
        if (strcmp(pf->type, "minecraft:ore") == 0) {
            return ore_place(l, pf, x, y, z);
        }
        return false;
    }
    const Mod *m = &pf->mods[mi];
    switch (m->kind) {
    case M_COUNT: {
        int n = sample_int(&m->count, l->rnd);
        bool any = false;
        for (int i = 0; i < n; ++i) {
            if (run_mods(l, pfi, mi + 1, x, y, z)) {
                any = true;
            }
        }
        return any;
    }
    case M_IN_SQUARE: {
        int nx = jr_next_int_bound(l->rnd, 16) + x;
        int nz = jr_next_int_bound(l->rnd, 16) + z;
        return run_mods(l, pfi, mi + 1, nx, y, nz);
    }
    case M_HEIGHT_RANGE: {
        int ny = sample_height(&m->height, l->rnd);
        return run_mods(l, pfi, mi + 1, x, ny, z);
    }
    case M_RARITY:
        if (jr_next_float(l->rnd) < 1.0f / (float)m->chance) {
            return run_mods(l, pfi, mi + 1, x, y, z);
        }
        return false;
    case M_BIOME: {
        const char *biome = jsurface_biome_at(l->surface, x, y, z);
        if (biome_has_feature(l->f, biome, pfi)) {
            return run_mods(l, pfi, mi + 1, x, y, z);
        }
        return false;
    }
    case M_UNSUPPORTED:
        break;
    }
    return false;
}

/* ---- decoration ---------------------------------------------------------------------------------------------------------- */

static bool type_allowed(const char *type, const char *const *types, int count)
{
    for (int i = 0; i < count; ++i) {
        if (strcmp(type, types[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool jfeatures_decorate(JFeatures *f, JSurface *surface, int64_t seed, JRegion *region, const char *const *types,
                        int type_count)
{
    if (f == NULL || surface == NULL || region == NULL) {
        return false;
    }
    f->changed = 0;
    int tx = region->target_x, tz = region->target_z;
    JrRandom rnd;
    /* WorldgenRandom.setDecorationSeed(seed, originX, originZ) */
    jr_worldgen_xoroshiro_init(&rnd, seed);
    int64_t l1 = jr_next_long(&rnd) | 1;
    int64_t m1 = jr_next_long(&rnd) | 1;
    int64_t decoration_seed =
        (int64_t)(((uint64_t)(int64_t)(tx * 16) * (uint64_t)l1 + (uint64_t)(int64_t)(tz * 16) * (uint64_t)m1) ^ (uint64_t)seed);
    jr_worldgen_xoroshiro_init(&rnd, decoration_seed);

    /* Biomes stored in the 3 x 3 chunks around the target (the candidate set). */
    const char *biomes[128];
    int nbiomes = 0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const char *tmp[64];
            int n = jsurface_chunk_biomes(surface, tx + dx, tz + dz, tmp, 64);
            for (int i = 0; i < n; ++i) {
                bool seen = false;
                for (int k = 0; k < nbiomes; ++k) {
                    if (strcmp(biomes[k], tmp[i]) == 0) {
                        seen = true;
                        break;
                    }
                }
                if (!seen && nbiomes < 128) {
                    biomes[nbiomes++] = tmp[i];
                }
            }
        }
    }
    Level lvl = {f, surface, region, &rnd};
    bool ok = true;
    bool *mark = (bool *)calloc((size_t)f->pf_count + 1, 1);
    for (int step = 0; step < STEPS; ++step) {
        memset(mark, 0, (size_t)f->pf_count + 1);
        for (int b = 0; b < nbiomes; ++b) {
            const BiomeFeatures *bf = biome_features(f, biomes[b]);
            if (bf == NULL) {
                continue;
            }
            for (int k = 0; k < bf->count[step]; ++k) {
                mark[bf->pf[step][k]] = true;
            }
        }
        for (int index = 0; index < f->step_len[step]; ++index) {
            int pfi = f->step_list[step][index];
            if (!mark[pfi]) {
                continue;
            }
            const PlacedFeature *pf = &f->pf[pfi];
            if (!type_allowed(pf->type, types, type_count)) {
                continue;
            }
            if (!pf->mods_supported) {
                ok = false;
                continue;
            }
            /* WorldgenRandom.setFeatureSeed(decorationSeed, index, step) */
            jr_worldgen_xoroshiro_init(&rnd, decoration_seed + (int64_t)index + (int64_t)(10000 * step));
            run_mods(&lvl, pfi, 0, tx * 16, MIN_Y, tz * 16);
        }
    }
    free(mark);
    return ok;
}
