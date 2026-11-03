#include "world/jfeature.h"

#include "world/jfeature_internal.h"
#include "world/jjson.h"
#include "world/jrandom.h"
#include "world/jtree.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

#define MIN_Y JF_MIN_Y
#define HEIGHT JF_HEIGHT
#define STEPS 11
#define NAME_LEN JF_NAME_LEN

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

/* Mth.sin(double) / Mth.cos(double): table lookups returning float */
static void sin_table_init(void)
{
    if (!g_sin_ready) {
        for (int i = 0; i < 65536; ++i) {
            g_sin[i] = (float)sin((double)i * 3.14159265358979323846 * 2.0 / 65536.0);
        }
        g_sin_ready = true;
    }
}

float jf_mth_sin(double d)
{
    sin_table_init();
    return g_sin[(int)((long long)(d * 10430.378350470453) & 65535)];
}

float jf_mth_cos(double d)
{
    sin_table_init();
    return g_sin[(int)((long long)(d * 10430.378350470453 + 16384.0) & 65535)];
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

typedef struct Anchor {
    int kind;  /* 0 absolute, 1 above_bottom, 2 below_top */
    int value;
} Anchor;

typedef struct HeightProv {
    int kind;  /* 0 uniform, 1 trapezoid, 2 constant anchor, -1 unsupported */
    Anchor lo, hi;
    int plateau;
} HeightProv;

typedef enum ModKind {
    M_COUNT,
    M_IN_SQUARE,
    M_HEIGHT_RANGE,
    M_RARITY,
    M_BIOME,
    M_HEIGHTMAP,
    M_NOISE_THRESHOLD_COUNT,
    M_PRED_FILTER,
    M_SURFACE_WATER_DEPTH,
    M_UNSUPPORTED
} ModKind;

/* BlockPredicate subset: matching_blocks, matching_fluids, not, all_of, any_of, true. */
typedef enum PredKind { P_TRUE, P_BLOCKS, P_FLUIDS, P_NOT, P_ALL, P_ANY, P_REPLACEABLE, P_WOULD_SURVIVE, P_UNSUPPORTED } PredKind;

typedef struct Pred {
    PredKind kind;
    int blocks[8];
    int block_count;
    int fluids[2];  /* JB_WATER / JB_LAVA, or -1 for "empty" */
    int survive_kind; /* would_survive: 0 sapling (dirt or farmland below), 1 mangrove propagule (also clay) */
    int fluid_count;
    int off[3];
    struct Pred **children;
    int child_count;
} Pred;

typedef struct Mod {
    ModKind kind;
    IntProv count;
    HeightProv height;
    int chance;
    int hm_type;  /* 0 WORLD_SURFACE_WG, 1 OCEAN_FLOOR_WG, 2 MOTION_BLOCKING */
    int above_noise, below_noise;
    double noise_level;
    Pred *pred;
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
    int self_index;  /* index in JFeatures.pf, or -1 for an inline placed feature */
    /* random_patch / flower */
    int patch_tries, patch_xz, patch_y;
    struct PlacedFeature *inner;
    /* simple_block */
    StateProvider provider;
    bool feature_supported;
    /* tree */
    struct JTreeCfg *tree;
    /* random_selector / simple_random_selector / random_boolean_selector */
    struct PlacedFeature **sel;
    float *sel_chance;
    int sel_count;
    struct PlacedFeature *sel_default;
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
    JTreeTags tree_tags;
    struct ConfiguredCache {
        char name[NAME_LEN];
        PlacedFeature *pf;
    } *cfg_cache;
    int cfg_cache_count;
};

/* ---- parsing -------------------------------------------------------------------------------- */

const char *jf_jstr(const JJson *j)
{
    return (j != NULL && j->type == JJ_STR) ? j->text : "";
}

const char *jf_local_name(const char *id)
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

IntProv jf_parse_int_provider(const JJson *j)
{
    IntProv p;
    memset(&p, 0, sizeof(p));
    p.kind = -1;
    if (j == NULL) {
        return p;
    }
    if (j->type == JJ_NUM) {
        p.kind = 0;
        p.a = (int)jj_double(j);
        return p;
    }
    const char *t = jf_local_name(jf_jstr(jj_get(j, "type")));
    if (strcmp(t, "constant") == 0) {
        p.kind = 0;
        p.a = (int)jj_double(jj_get(j, "value"));
    } else if (strcmp(t, "uniform") == 0) {
        p.kind = 1;
        p.a = (int)jj_double(jj_get(j, "min_inclusive"));
        p.b = (int)jj_double(jj_get(j, "max_inclusive"));
    } else if (strcmp(t, "weighted_list") == 0) {
        const JJson *dist = jj_get(j, "distribution");
        if (dist != NULL && dist->type == JJ_ARR && dist->count >= 1 && dist->count <= JF_WEIGHTED_MAX) {
            p.kind = 2;
            p.wn = dist->count;
            for (int i = 0; i < dist->count; ++i) {
                IntProv v = jf_parse_int_provider(jj_get(dist->items[i], "data"));
                if (v.kind != 0) {
                    p.kind = -1; /* only constant entries are ported */
                    return p;
                }
                p.wv[i] = v.a;
                p.ww[i] = (int)jj_double(jj_get(dist->items[i], "weight"));
                p.wtotal += p.ww[i];
            }
        }
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
    const char *t = jf_local_name(jf_jstr(type));
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

int jf_sample_int(const IntProv *p, JrRandom *r)
{
    if (p->kind == 0) {
        return p->a;
    }
    if (p->kind == 2) {
        int k = jr_next_int_bound(r, p->wtotal);
        for (int i = 0; i < p->wn; ++i) {
            k -= p->ww[i];
            if (k < 0) {
                return p->wv[i];
            }
        }
        return p->wv[p->wn - 1];
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
    int first = random_between_inclusive(r, 0, m);
    return i + first + random_between_inclusive(r, 0, l);
}

static void pred_free(Pred *p)
{
    if (p == NULL) {
        return;
    }
    for (int i = 0; i < p->child_count; ++i) {
        pred_free(p->children[i]);
    }
    free(p->children);
    free(p);
}

static Pred *parse_pred(const JJson *j)
{
    Pred *p = (Pred *)calloc(1, sizeof(Pred));
    if (p == NULL) {
        return NULL;
    }
    const char *t = jf_local_name(jf_jstr(jj_get(j, "type")));
    p->kind = P_UNSUPPORTED;
    const JJson *off = jj_get(j, "offset");
    if (off != NULL && off->type == JJ_ARR && off->count == 3) {
        for (int i = 0; i < 3; ++i) {
            p->off[i] = (int)jj_double(off->items[i]);
        }
    }
    if (strcmp(t, "true") == 0) {
        p->kind = P_TRUE;
    } else if (strcmp(t, "replaceable") == 0) {
        p->kind = P_REPLACEABLE;
    } else if (strcmp(t, "would_survive") == 0) {
        const char *n = jf_local_name(jf_jstr(jj_get(jj_get(j, "state"), "Name")));
        size_t len = strlen(n);
        if (len > 8 && strcmp(n + len - 8, "_sapling") == 0) {
            p->kind = P_WOULD_SURVIVE;
            p->survive_kind = 0;
        } else if (strcmp(n, "mangrove_propagule") == 0) {
            p->kind = P_WOULD_SURVIVE;
            p->survive_kind = 1;
        }
    } else if (strcmp(t, "matching_blocks") == 0 || strcmp(t, "matching_fluids") == 0) {
        bool blocks = t[9] == 'b';
        const JJson *list = jj_get(j, blocks ? "blocks" : "fluids");
        p->kind = blocks ? P_BLOCKS : P_FLUIDS;
        if (list != NULL && list->type == JJ_STR) {
            list = NULL;
            const char *one = jf_jstr(jj_get(j, blocks ? "blocks" : "fluids"));
            if (blocks) {
                p->blocks[p->block_count++] = jfill_block_register(one);
            } else {
                p->fluids[p->fluid_count++] = strcmp(one, "minecraft:water") == 0 || strcmp(one, "minecraft:flowing_water") == 0
                                                  ? JB_WATER
                                                  : (strcmp(one, "minecraft:lava") == 0 ? JB_LAVA : -1);
            }
        } else if (list != NULL && list->type == JJ_ARR) {
            for (int i = 0; i < list->count && i < 8; ++i) {
                const char *one = jf_jstr(list->items[i]);
                if (blocks) {
                    p->blocks[p->block_count++] = jfill_block_register(one);
                } else {
                    p->fluids[p->fluid_count < 2 ? p->fluid_count++ : 1] =
                        strcmp(one, "minecraft:water") == 0 ? JB_WATER : (strcmp(one, "minecraft:lava") == 0 ? JB_LAVA : -1);
                }
            }
        } else {
            p->kind = P_UNSUPPORTED;
        }
    } else if (strcmp(t, "not") == 0) {
        p->kind = P_NOT;
        p->children = (Pred **)calloc(1, sizeof(Pred *));
        p->children[0] = parse_pred(jj_get(j, "predicate"));
        p->child_count = 1;
    } else if (strcmp(t, "all_of") == 0 || strcmp(t, "any_of") == 0) {
        p->kind = t[1] == 'l' ? P_ALL : P_ANY;
        const JJson *list = jj_get(j, "predicates");
        if (list != NULL && list->type == JJ_ARR) {
            p->children = (Pred **)calloc((size_t)list->count + 1, sizeof(Pred *));
            p->child_count = list->count;
            for (int i = 0; i < list->count; ++i) {
                p->children[i] = parse_pred(list->items[i]);
            }
        }
    }
    return p;
}

static bool pred_supported(const Pred *p)
{
    if (p == NULL || p->kind == P_UNSUPPORTED) {
        return false;
    }
    for (int i = 0; i < p->child_count; ++i) {
        if (!pred_supported(p->children[i])) {
            return false;
        }
    }
    return true;
}

static void mark_plant_state(int id);

StateProvider jf_parse_provider(const JJson *j)
{
    StateProvider sp;
    memset(&sp, 0, sizeof(sp));
    sp.kind = -1;
    const char *t = jf_local_name(jf_jstr(jj_get(j, "type")));
    if (strcmp(t, "simple_state_provider") == 0) {
        sp.kind = 0;
        sp.states[0] = jfill_block_register(jf_jstr(jj_get(jj_get(j, "state"), "Name")));
        mark_plant_state(sp.states[0]);
        sp.count = 1;
    } else if (strcmp(t, "weighted_state_provider") == 0) {
        const JJson *entries = jj_get(j, "entries");
        if (entries != NULL && entries->type == JJ_ARR && entries->count > 0 && entries->count <= 16) {
            sp.kind = 1;
            sp.count = entries->count;
            for (int i = 0; i < entries->count; ++i) {
                sp.states[i] = jfill_block_register(jf_jstr(jj_get(jj_get(entries->items[i], "data"), "Name")));
                mark_plant_state(sp.states[i]);
                sp.weights[i] = (int)jj_double(jj_get(entries->items[i], "weight"));
                sp.total_weight += sp.weights[i];
            }
        }
    }
    return sp;
}

static void free_placed_contents(PlacedFeature *pf);
static bool parse_placed(JFeatures *f, PlacedFeature *pf, const JJson *j);
static void parse_feature(JFeatures *f, PlacedFeature *pf, const JJson *cfv);

/* A placed feature given as an id ("minecraft:oak_checked") or inline ({"feature": ..., "placement": [...]}). */
static PlacedFeature *parse_placed_value(JFeatures *f, const JJson *v)
{
    if (v == NULL) {
        return NULL;
    }
    PlacedFeature *pf = (PlacedFeature *)calloc(1, sizeof(PlacedFeature));
    if (pf == NULL) {
        return NULL;
    }
    pf->self_index = -1;
    bool ok = false;
    if (v->type == JJ_STR) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/placed_feature/%s.json", f->root, jf_local_name(v->text));
        JJson *file = jj_parse_file(path);
        if (file != NULL) {
            ok = parse_placed(f, pf, file);
            jj_free(file);
        }
    } else if (v->type == JJ_OBJ) {
        ok = parse_placed(f, pf, v);
    }
    if (!ok) {
        free_placed_contents(pf);
        free(pf);
        return NULL;
    }
    return pf;
}

/* Parse a placed-feature object ({"feature": ..., "placement": [...]}) into pf. */
static bool parse_placed(JFeatures *f, PlacedFeature *pf, const JJson *j)
{
    char path[1200];
    const JJson *placement = jj_get(j, "placement");
    pf->mods_supported = true;
    pf->feature_supported = true;
    if (placement != NULL && placement->type == JJ_ARR) {
        pf->mods = (Mod *)calloc((size_t)placement->count + 1, sizeof(Mod));
        pf->mod_count = placement->count;
        for (int i = 0; i < placement->count; ++i) {
            const JJson *m = placement->items[i];
            const char *t = jf_local_name(jf_jstr(jj_get(m, "type")));
            Mod *mod = &pf->mods[i];
            mod->kind = M_UNSUPPORTED;
            if (strcmp(t, "count") == 0) {
                mod->kind = M_COUNT;
                mod->count = jf_parse_int_provider(jj_get(m, "count"));
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
            } else if (strcmp(t, "heightmap") == 0) {
                const char *hm = jf_jstr(jj_get(m, "heightmap"));
                mod->hm_type = strcmp(hm, "WORLD_SURFACE_WG") == 0 ? 0 : (strcmp(hm, "OCEAN_FLOOR_WG") == 0 ? 1 : (strcmp(hm, "MOTION_BLOCKING") == 0 ? 2 : (strcmp(hm, "MOTION_BLOCKING_NO_LEAVES") == 0 ? 3 : (strcmp(hm, "OCEAN_FLOOR") == 0 ? 4 : (strcmp(hm, "WORLD_SURFACE") == 0 ? 5 : -1)))));
                mod->kind = mod->hm_type >= 0 ? M_HEIGHTMAP : M_UNSUPPORTED;
            } else if (strcmp(t, "noise_threshold_count") == 0) {
                mod->kind = M_NOISE_THRESHOLD_COUNT;
                mod->above_noise = (int)jj_double(jj_get(m, "above_noise"));
                mod->below_noise = (int)jj_double(jj_get(m, "below_noise"));
                mod->noise_level = jj_double(jj_get(m, "noise_level"));
            } else if (strcmp(t, "surface_water_depth_filter") == 0) {
                mod->kind = M_SURFACE_WATER_DEPTH;
                mod->chance = (int)jj_double(jj_get(m, "max_water_depth"));
            } else if (strcmp(t, "block_predicate_filter") == 0) {
                mod->pred = parse_pred(jj_get(m, "predicate"));
                mod->kind = pred_supported(mod->pred) ? M_PRED_FILTER : M_UNSUPPORTED;
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
        snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/configured_feature/%s.json", f->root, jf_local_name(feature->text));
        cf = jj_parse_file(path);
        cfv = cf;
    }
    if (cfv != NULL) {
        parse_feature(f, pf, cfv);
    }
    jj_free(cf);
    pf->loaded = true;
    return true;
}

/* Parse a configured feature ({"type": ..., "config": {...}}) into pf (type and feature data only). */
static void parse_feature(JFeatures *f, PlacedFeature *pf, const JJson *cfv)
{
    char path[1200];
    pf->feature_supported = true;
    {
        snprintf(pf->type, sizeof(pf->type), "%s", jf_jstr(jj_get(cfv, "type")));
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
                    const char *pt = jf_local_name(jf_jstr(jj_get(test, "predicate_type")));
                    t->kind = -1;
                    if (strcmp(pt, "tag_match") == 0) {
                        t->kind = 0;
                        snprintf(t->tag, sizeof(t->tag), "%s", jf_jstr(jj_get(test, "tag")));
                    } else if (strcmp(pt, "block_match") == 0) {
                        t->kind = 1;
                        t->block = jfill_block_register(jf_jstr(jj_get(test, "block")));
                    }
                    t->state = jfill_block_register(jf_jstr(jj_get(jj_get(targets->items[i], "state"), "Name")));
                }
            }
        } else if ((strcmp(pf->type, "minecraft:random_patch") == 0 || strcmp(pf->type, "minecraft:flower") == 0) && cfg != NULL) {
            pf->patch_tries = (int)jj_double(jj_get(cfg, "tries"));
            pf->patch_xz = (int)jj_double(jj_get(cfg, "xz_spread"));
            pf->patch_y = (int)jj_double(jj_get(cfg, "y_spread"));
            const JJson *inner = jj_get(cfg, "feature");
            JJson *inner_file = NULL;
            if (inner != NULL && inner->type == JJ_STR) {
                snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/placed_feature/%s.json", f->root, jf_local_name(inner->text));
                inner_file = jj_parse_file(path);
                inner = inner_file;
            }
            pf->inner = (PlacedFeature *)calloc(1, sizeof(PlacedFeature));
            if (pf->inner != NULL && inner != NULL) {
                pf->inner->self_index = -1;
                parse_placed(f, pf->inner, inner);
            } else {
                pf->feature_supported = false;
            }
            jj_free(inner_file);
        } else if (strcmp(pf->type, "minecraft:simple_block") == 0 && cfg != NULL) {
            pf->provider = jf_parse_provider(jj_get(cfg, "to_place"));
            pf->feature_supported = pf->provider.kind >= 0;
        } else if (strcmp(pf->type, "minecraft:tree") == 0 && cfg != NULL) {
            pf->tree = jtree_parse(cfg);
            pf->feature_supported = pf->tree != NULL;
        } else if (strcmp(pf->type, "minecraft:random_selector") == 0 && cfg != NULL) {
            const JJson *list = jj_get(cfg, "features");
            int n = list != NULL && list->type == JJ_ARR ? list->count : 0;
            pf->sel = (PlacedFeature **)calloc((size_t)n + 1, sizeof(PlacedFeature *));
            pf->sel_chance = (float *)calloc((size_t)n + 1, sizeof(float));
            pf->sel_count = n;
            for (int i = 0; i < n; ++i) {
                pf->sel[i] = parse_placed_value(f, jj_get(list->items[i], "feature"));
                pf->sel_chance[i] = jj_float(jj_get(list->items[i], "chance"));
            }
            pf->sel_default = parse_placed_value(f, jj_get(cfg, "default"));
        } else if (strcmp(pf->type, "minecraft:simple_random_selector") == 0 && cfg != NULL) {
            const JJson *list = jj_get(cfg, "features");
            int n = list != NULL && list->type == JJ_ARR ? list->count : 0;
            if (list != NULL && list->type == JJ_STR) {
                n = 0; /* a single named placed feature is not used by the vanilla data pack */
            }
            pf->sel = (PlacedFeature **)calloc((size_t)n + 1, sizeof(PlacedFeature *));
            pf->sel_count = n;
            for (int i = 0; i < n; ++i) {
                pf->sel[i] = parse_placed_value(f, list->items[i]);
            }
        } else if (strcmp(pf->type, "minecraft:random_boolean_selector") == 0 && cfg != NULL) {
            pf->sel = (PlacedFeature **)calloc(2, sizeof(PlacedFeature *));
            pf->sel_count = 2;
            pf->sel[0] = parse_placed_value(f, jj_get(cfg, "feature_true"));
            pf->sel[1] = parse_placed_value(f, jj_get(cfg, "feature_false"));
        }
    }
}

static bool load_placed(JFeatures *f, PlacedFeature *pf)
{
    char path[1200];
    snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/placed_feature/%s.json", f->root, jf_local_name(pf->name));
    JJson *j = jj_parse_file(path);
    if (j == NULL) {
        return false;
    }
    bool ok = parse_placed(f, pf, j);
    jj_free(j);
    return ok;
}

static void free_placed_contents(PlacedFeature *pf)
{
    for (int i = 0; i < pf->mod_count; ++i) {
        pred_free(pf->mods[i].pred);
    }
    free(pf->mods);
    free(pf->targets);
    if (pf->inner != NULL) {
        free_placed_contents(pf->inner);
        free(pf->inner);
    }
    jtree_free(pf->tree);
    for (int i = 0; i < pf->sel_count; ++i) {
        if (pf->sel[i] != NULL) {
            free_placed_contents(pf->sel[i]);
            free(pf->sel[i]);
        }
    }
    free(pf->sel);
    free(pf->sel_chance);
    if (pf->sel_default != NULL) {
        free_placed_contents(pf->sel_default);
        free(pf->sel_default);
    }
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
    pf->self_index = f->pf_count;
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
        free_placed_contents(&f->pf[i]);
    }
    free(f->pf);
    for (int i = 0; i < f->cfg_cache_count; ++i) {
        free_placed_contents(f->cfg_cache[i].pf);
        free(f->cfg_cache[i].pf);
    }
    free(f->cfg_cache);
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
        snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/biome/%s.json", datapack_root, jf_local_name(b->name));
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
                b->pf[st][k] = pf_index(f, jf_jstr(list->items[k]));
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
    snprintf(path, sizeof(path), "%s/data/minecraft/tags/block/%s.json", f->root, jf_local_name(tag));
    JJson *j = jj_parse_file(path);
    const JJson *values = jj_get(j, "values");
    if (values != NULL && values->type == JJ_ARR) {
        for (int i = 0; i < values->count; ++i) {
            const char *v = jf_jstr(values->items[i]);
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

bool jf_tag_has(JFeatures *f, const char *tag, int block)
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

const JTreeTags *jf_tree_tags(Level *l)
{
    JTreeTags *t = &l->f->tree_tags;
    if (t->built_for != jfill_block_count()) {
        for (int b = 0; b < 256; ++b) {
            t->leaves[b] = jf_tag_has(l->f, "minecraft:leaves", b);
            t->replaceable_by_trees[b] = jf_tag_has(l->f, "minecraft:replaceable_by_trees", b);
            t->logs[b] = jf_tag_has(l->f, "minecraft:logs", b);
            t->dirt[b] = jf_tag_has(l->f, "minecraft:dirt", b);
        }
        t->built_for = jfill_block_count();
    }
    return t;
}

static int chunk_slot(const Level *l, int x, int z)
{
    int dx = (x >> 4) - (l->region->target_x - 1);
    int dz = (z >> 4) - (l->region->target_z - 1);
    if (dx < 0 || dx > 2 || dz < 0 || dz > 2) {
        return -1;
    }
    return dz * 3 + dx;
}

int jf_level_get(const Level *l, int x, int y, int z)
{
    int slot = chunk_slot(l, x, z);
    if (slot < 0 || y < MIN_Y || y >= MIN_Y + HEIGHT) {
        return JB_AIR;
    }
    return l->region->blocks[slot][JFILL_INDEX(x & 15, z & 15, y)];
}

static void log_push(Level *l, int x, int y, int z, uint8_t before)
{
    if (l->log_n == l->log_cap) {
        l->log_cap = l->log_cap != 0 ? l->log_cap * 2 : 1024;
        l->log = (JfLogEntry *)realloc(l->log, (size_t)l->log_cap * sizeof(JfLogEntry));
    }
    l->log[l->log_n].x = x;
    l->log[l->log_n].y = y;
    l->log[l->log_n].z = z;
    l->log[l->log_n].before = before;
    ++l->log_n;
}

/* Direct section write (no heightmap update), as OreFeature does through BulkSectionAccess. */
static void level_set_raw(Level *l, int x, int y, int z, int state)
{
    int slot = chunk_slot(l, x, z);
    if (slot >= 0 && y >= MIN_Y && y < MIN_Y + HEIGHT) {
        if (l->logging) {
            log_push(l, x, y, z, l->region->blocks[slot][JFILL_INDEX(x & 15, z & 15, y)]);
        }
        l->region->blocks[slot][JFILL_INDEX(x & 15, z & 15, y)] = (uint8_t)state;
        l->f->changed++;
    }
}

bool jf_level_can_write(const Level *l, int x, int z)
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
        return jf_tag_has(f, t->tag, block);
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
        if (jblock_is_air(jf_level_get(l, x + D[i][0], y + D[i][1], z + D[i][2]))) {
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
                                    if (jf_level_can_write(l, s, w)) {
                                        int slot = chunk_slot(l, s, w);
                                        if (slot >= 0) {
                                            int state = jf_level_get(l, s, u, w);
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

/* Plants: which blocks may stand where (Block.canSurvive), by block name. */
enum { PLANT_NONE = 0, PLANT_DIRT, PLANT_DIRT_DOUBLE, PLANT_DEAD_BUSH, PLANT_FREE };

static int plant_kind(const char *name)
{
    static const char *const SINGLE[] = {"short_grass", "fern", "dandelion", "poppy", "blue_orchid", "allium", "azure_bluet",
                                         "red_tulip", "orange_tulip", "white_tulip", "pink_tulip", "oxeye_daisy",
                                         "cornflower", "lily_of_the_valley", "torchflower"};
    static const char *const DOUBLE[] = {"tall_grass", "large_fern", "sunflower", "lilac", "rose_bush", "peony"};
    static const char *const FREE[] = {"pumpkin", "melon"};
    if (strncmp(name, "minecraft:", 10) != 0) {
        return PLANT_NONE;
    }
    const char *n = name + 10;
    for (size_t i = 0; i < sizeof(SINGLE) / sizeof(SINGLE[0]); ++i) {
        if (strcmp(n, SINGLE[i]) == 0) {
            return PLANT_DIRT;
        }
    }
    for (size_t i = 0; i < sizeof(DOUBLE) / sizeof(DOUBLE[0]); ++i) {
        if (strcmp(n, DOUBLE[i]) == 0) {
            return PLANT_DIRT_DOUBLE;
        }
    }
    if (strcmp(n, "dead_bush") == 0) {
        return PLANT_DEAD_BUSH;
    }
    for (size_t i = 0; i < sizeof(FREE) / sizeof(FREE[0]); ++i) {
        if (strcmp(n, FREE[i]) == 0) {
            return PLANT_FREE;
        }
    }
    return PLANT_NONE;
}

static void mark_plant_state(int id)
{
    int k = plant_kind(jfill_block_name((JFillBlock)id));
    if (k == PLANT_DIRT || k == PLANT_DIRT_DOUBLE || k == PLANT_DEAD_BUSH) {
        jblock_set_no_collision(id);
    }
}

static bool provider_supported(const StateProvider *sp)
{
    if (sp->kind < 0 || sp->count <= 0) {
        return false;
    }
    for (int i = 0; i < sp->count; ++i) {
        if (plant_kind(jfill_block_name((JFillBlock)sp->states[i])) == PLANT_NONE) {
            return false;
        }
    }
    return true;
}

/* ---- heightmaps -------------------------------------------------------------------------------------------
 * WORLD_SURFACE_WG / OCEAN_FLOOR_WG are maintained by every setBlock (the chunk status is CARVERS, whose
 * heightmapsAfter set is exactly these two). ChunkStatusTasks.generateFeatures primes MOTION_BLOCKING,
 * MOTION_BLOCKING_NO_LEAVES, OCEAN_FLOOR and WORLD_SURFACE of the decorated chunk before decoration starts;
 * those four are never updated afterwards (they are not in the CARVERS set), so trees placed earlier in the
 * step do not show up in them. The neighbouring chunks of the region prime them lazily at the first query. */

struct JPostHeightmaps {
    bool ready[4][9]; /* 0 MOTION_BLOCKING, 1 MOTION_BLOCKING_NO_LEAVES, 2 OCEAN_FLOOR, 3 WORLD_SURFACE */
    int h[4][9][256];
};

static bool motion_or_fluid(int b)
{
    return jblock_blocks_motion(b) || jblock_is_fluid(b);
}

static bool post_pred(Level *l, int kind, int b)
{
    switch (kind) {
    case 0:
        return motion_or_fluid(b);
    case 1:
        return motion_or_fluid(b) && !jf_tree_tags(l)->leaves[b & 255];
    case 2:
        return jblock_blocks_motion(b);
    default:
        return !jblock_is_air(b);
    }
}

/* Heightmap.primeHeightmaps for one chunk and one type */
static void post_prime(Level *l, int kind, int slot)
{
    const uint8_t *blocks = l->region->blocks[slot];
    for (int lz = 0; lz < 16; ++lz) {
        for (int lx = 0; lx < 16; ++lx) {
            int h = MIN_Y;
            for (int y = MIN_Y + HEIGHT - 1; y >= MIN_Y; --y) {
                if (post_pred(l, kind, blocks[JFILL_INDEX(lx, lz, y)])) {
                    h = y + 1;
                    break;
                }
            }
            l->region->post->h[kind][slot][lz * 16 + lx] = h;
        }
    }
    l->region->post->ready[kind][slot] = true;
}

/* Allocate the region's post-feature heightmaps and prime the decorated (centre) chunk. */
static bool post_begin(Level *l)
{
    if (l->region->post != NULL) {
        return false;
    }
    l->region->post = (JPostHeightmaps *)calloc(1, sizeof(JPostHeightmaps));
    for (int kind = 0; kind < 4; ++kind) {
        post_prime(l, kind, 4);
    }
    return true;
}

void jfeatures_region_reset(JRegion *r)
{
    if (r != NULL) {
        free(r->post);
        r->post = NULL;
    }
}

/* LevelReader.getHeight(type, x, z): first available Y. type: 0 WORLD_SURFACE_WG, 1 OCEAN_FLOOR_WG,
 * 2 MOTION_BLOCKING, 3 MOTION_BLOCKING_NO_LEAVES, 4 OCEAN_FLOOR, 5 WORLD_SURFACE */
int jf_level_height(Level *l, int type, int x, int z)
{
    int slot = chunk_slot(l, x, z);
    if (slot < 0) {
        return MIN_Y;
    }
    int idx = (z & 15) * 16 + (x & 15);
    if (type == 0) {
        return l->region->heightmaps[slot]->world_surface[idx];
    }
    if (type == 1) {
        return l->region->heightmaps[slot]->ocean_floor[idx];
    }
    if (l->region->post == NULL) {
        post_begin(l);
    }
    int kind = type - 2;
    if (!l->region->post->ready[kind][slot]) {
        post_prime(l, kind, slot);
    }
    return l->region->post->h[kind][slot][idx];
}

/* ChunkAccess.setBlockState through WorldGenRegion.setBlock: block plus the generation heightmaps. */
void jf_level_set(Level *l, int x, int y, int z, int state)
{
    int slot = chunk_slot(l, x, z);
    if (slot < 0 || y < MIN_Y || y >= MIN_Y + HEIGHT) {
        return;
    }
    int lx = x & 15, lz = z & 15;
    uint8_t *blocks = l->region->blocks[slot];
    if (l->logging) {
        log_push(l, x, y, z, blocks[JFILL_INDEX(lx, lz, y)]);
    }
    blocks[JFILL_INDEX(lx, lz, y)] = (uint8_t)state;
    l->f->changed++;
    jheightmaps_update(l->region->heightmaps[slot], blocks, lx, y, lz, state);
}

/* ---- block predicates, state providers, simple_block, random_patch --------------------------------------- */

static bool pred_test(Level *l, const Pred *p, int x, int y, int z)
{
    int px = x + p->off[0], py = y + p->off[1], pz = z + p->off[2];
    switch (p->kind) {
    case P_TRUE:
        return true;
    case P_BLOCKS: {
        int b = jf_level_get(l, px, py, pz);
        for (int i = 0; i < p->block_count; ++i) {
            if (p->blocks[i] == b) {
                return true;
            }
        }
        return false;
    }
    case P_FLUIDS: {
        int b = jf_level_get(l, px, py, pz);
        int fluid = jblock_is_fluid(b) ? b : -1;
        for (int i = 0; i < p->fluid_count; ++i) {
            if (p->fluids[i] == fluid) {
                return true;
            }
        }
        return false;
    }
    case P_REPLACEABLE: {
        /* BlockState.canBeReplaced(): air, liquids and replaceable plants. */
        int b = jf_level_get(l, px, py, pz);
        if (jblock_is_air(b) || jblock_is_fluid(b)) {
            return true;
        }
        const char *n = jfill_block_name((JFillBlock)b);
        return strcmp(n, "minecraft:short_grass") == 0 || strcmp(n, "minecraft:tall_grass") == 0 ||
               strcmp(n, "minecraft:fern") == 0 || strcmp(n, "minecraft:large_fern") == 0 ||
               strcmp(n, "minecraft:dead_bush") == 0;
    }
    case P_WOULD_SURVIVE: {
        /* BlockState.canSurvive for saplings: VegetationBlock.mayPlaceOn(below) = dirt tag or farmland (+ clay for propagules). */
        int below = jf_level_get(l, px, py - 1, pz);
        if (jf_tag_has(l->f, "minecraft:dirt", below)) {
            return true;
        }
        int farmland = jfill_block_from_name("minecraft:farmland");
        if (farmland >= 0 && below == farmland) {
            return true;
        }
        if (p->survive_kind == 1) {
            int clay = jfill_block_from_name("minecraft:clay");
            return clay >= 0 && below == clay;
        }
        return false;
    }
    case P_NOT:
        return !pred_test(l, p->children[0], x, y, z);
    case P_ALL:
        for (int i = 0; i < p->child_count; ++i) {
            if (!pred_test(l, p->children[i], x, y, z)) {
                return false;
            }
        }
        return true;
    case P_ANY:
        for (int i = 0; i < p->child_count; ++i) {
            if (pred_test(l, p->children[i], x, y, z)) {
                return true;
            }
        }
        return false;
    case P_UNSUPPORTED:
        break;
    }
    return false;
}

int jf_provider_state(const StateProvider *sp, JrRandom *r)
{
    if (sp->kind == 0) {
        return sp->states[0];
    }
    int k = jr_next_int_bound(r, sp->total_weight);
    for (int i = 0; i < sp->count; ++i) {
        k -= sp->weights[i];
        if (k < 0) {
            return sp->states[i];
        }
    }
    return sp->states[sp->count - 1];
}

static bool state_survives(Level *l, int state, int x, int y, int z)
{
    switch (plant_kind(jfill_block_name((JFillBlock)state))) {
    case PLANT_DIRT:
    case PLANT_DIRT_DOUBLE:
        return jf_tag_has(l->f, "minecraft:dirt", jf_level_get(l, x, y - 1, z));
    case PLANT_DEAD_BUSH:
        return jf_tag_has(l->f, "minecraft:dry_vegetation_may_place_on", jf_level_get(l, x, y - 1, z));
    case PLANT_FREE:
        return true;
    default:
        return false;
    }
}

/* SimpleBlockFeature.place */
static bool simple_block_place(Level *l, const PlacedFeature *pf, int x, int y, int z)
{
    int state = jf_provider_state(&pf->provider, l->rnd);
    if (!state_survives(l, state, x, y, z)) {
        return false;
    }
    if (plant_kind(jfill_block_name((JFillBlock)state)) == PLANT_DIRT_DOUBLE) {
        if (!jblock_is_air(jf_level_get(l, x, y + 1, z))) {
            return false;
        }
        jf_level_set(l, x, y, z, state);
        jf_level_set(l, x, y + 1, z, state);
        return true;
    }
    jf_level_set(l, x, y, z, state);
    return true;
}

static bool run_pf(Level *l, const PlacedFeature *pf, int mi, int x, int y, int z);

/* RandomPatchFeature.place */
static bool random_patch_place(Level *l, const PlacedFeature *pf, int ox, int oy, int oz)
{
    int placed = 0;
    int j = pf->patch_xz + 1;
    int k = pf->patch_y + 1;
    for (int i = 0; i < pf->patch_tries; ++i) {
        int a1 = jr_next_int_bound(l->rnd, j);
        int a2 = jr_next_int_bound(l->rnd, j);
        int b1 = jr_next_int_bound(l->rnd, k);
        int b2 = jr_next_int_bound(l->rnd, k);
        int c1 = jr_next_int_bound(l->rnd, j);
        int c2 = jr_next_int_bound(l->rnd, j);
        if (run_pf(l, pf->inner, 0, ox + (a1 - a2), oy + (b1 - b2), oz + (c1 - c2))) {
            ++placed;
        }
    }
    return placed > 0;
}

static bool run_pf(Level *l, const PlacedFeature *pf, int mi, int x, int y, int z)
{
    if (mi == pf->mod_count) {
        if (strcmp(pf->type, "minecraft:ore") == 0) {
            return ore_place(l, pf, x, y, z);
        }
        if (!pf->feature_supported) {
            return false;
        }
        if (strcmp(pf->type, "minecraft:random_patch") == 0 || strcmp(pf->type, "minecraft:flower") == 0) {
            return random_patch_place(l, pf, x, y, z);
        }
        if (strcmp(pf->type, "minecraft:simple_block") == 0) {
            return simple_block_place(l, pf, x, y, z);
        }
        /* Feature.place(...) first checks WorldGenLevel.ensureCanWrite(origin). */
        if (strcmp(pf->type, "minecraft:tree") == 0) {
            return jf_level_can_write(l, x, z) && jtree_place(l, pf->tree, x, y, z);
        }
        if (strcmp(pf->type, "minecraft:random_selector") == 0) {
            if (!jf_level_can_write(l, x, z)) {
                return false;
            }
            for (int i = 0; i < pf->sel_count; ++i) {
                if (jr_next_float(l->rnd) < pf->sel_chance[i]) {
                    return run_pf(l, pf->sel[i], 0, x, y, z);
                }
            }
            return run_pf(l, pf->sel_default, 0, x, y, z);
        }
        if (strcmp(pf->type, "minecraft:simple_random_selector") == 0) {
            if (!jf_level_can_write(l, x, z)) {
                return false;
            }
            int i = jr_next_int_bound(l->rnd, pf->sel_count);
            return run_pf(l, pf->sel[i], 0, x, y, z);
        }
        if (strcmp(pf->type, "minecraft:random_boolean_selector") == 0) {
            if (!jf_level_can_write(l, x, z)) {
                return false;
            }
            return run_pf(l, pf->sel[jr_next_boolean(l->rnd) ? 0 : 1], 0, x, y, z);
        }
        return false;
    }
    const Mod *m = &pf->mods[mi];
    switch (m->kind) {
    case M_COUNT: {
        int n = jf_sample_int(&m->count, l->rnd);
        bool any = false;
        for (int i = 0; i < n; ++i) {
            if (run_pf(l, pf, mi + 1, x, y, z)) {
                any = true;
            }
        }
        return any;
    }
    case M_NOISE_THRESHOLD_COUNT: {
        double d = jsurface_simplex_noise(2, (double)x / 200.0, (double)z / 200.0);
        int n = d < m->noise_level ? m->below_noise : m->above_noise;
        bool any = false;
        for (int i = 0; i < n; ++i) {
            if (run_pf(l, pf, mi + 1, x, y, z)) {
                any = true;
            }
        }
        return any;
    }
    case M_IN_SQUARE: {
        int nx = jr_next_int_bound(l->rnd, 16) + x;
        int nz = jr_next_int_bound(l->rnd, 16) + z;
        return run_pf(l, pf, mi + 1, nx, y, nz);
    }
    case M_HEIGHT_RANGE: {
        int ny = sample_height(&m->height, l->rnd);
        return run_pf(l, pf, mi + 1, x, ny, z);
    }
    case M_HEIGHTMAP: {
        int h = jf_level_height(l, m->hm_type, x, z);
        if (h == MIN_Y) {
            return false;
        }
        return run_pf(l, pf, mi + 1, x, h, z);
    }
    case M_RARITY:
        if (jr_next_float(l->rnd) < 1.0f / (float)m->chance) {
            return run_pf(l, pf, mi + 1, x, y, z);
        }
        return false;
    case M_BIOME: {
        const char *biome = jsurface_biome_at(l->surface, x, y, z);
        if (pf->self_index >= 0 && biome_has_feature(l->f, biome, pf->self_index)) {
            return run_pf(l, pf, mi + 1, x, y, z);
        }
        return false;
    }
    case M_SURFACE_WATER_DEPTH: {
        /* SurfaceWaterDepthFilter: WORLD_SURFACE height - OCEAN_FLOOR height <= maxWaterDepth */
        int floor_h = jf_level_height(l, 4, x, z);
        int surf_h = jf_level_height(l, 5, x, z);
        if (surf_h - floor_h <= m->chance) {
            return run_pf(l, pf, mi + 1, x, y, z);
        }
        return false;
    }
    case M_PRED_FILTER:
        if (pred_test(l, m->pred, x, y, z)) {
            return run_pf(l, pf, mi + 1, x, y, z);
        }
        return false;
    case M_UNSUPPORTED:
        break;
    }
    return false;
}

/* ---- decoration ---------------------------------------------------------------------------------------------------------- */

/* Every modifier, feature and nested placed feature of pf is implemented. */
static bool pf_all_supported(const PlacedFeature *pf)
{
    if (pf == NULL || !pf->mods_supported || !pf->feature_supported) {
        return false;
    }
    if (pf->inner != NULL && !pf_all_supported(pf->inner)) {
        return false;
    }
    for (int i = 0; i < pf->sel_count; ++i) {
        if (!pf_all_supported(pf->sel[i])) {
            return false;
        }
    }
    if (pf->sel_chance != NULL && !pf_all_supported(pf->sel_default)) {
        return false;
    }
    return true;
}

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
                        int type_count, const char *const *names, int name_count)
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
    Level lvl;
    memset(&lvl, 0, sizeof(lvl));
    lvl.f = f;
    lvl.surface = surface;
    lvl.region = region;
    lvl.rnd = &rnd;
    bool own_post = post_begin(&lvl);
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
            if (name_count > 0 && !type_allowed(pf->name, names, name_count)) {
                continue;
            }
            if (!pf_all_supported(pf)) {
                fprintf(stderr, "jfeature: placed feature %s uses an unsupported modifier, feature or predicate\n", pf->name);
                ok = false;
                continue;
            }
            /* WorldgenRandom.setFeatureSeed(decorationSeed, index, step) */
            jr_worldgen_xoroshiro_init(&rnd, decoration_seed + (int64_t)index + (int64_t)(10000 * step));
            run_pf(&lvl, pf, 0, tx * 16, MIN_Y, tz * 16);
        }
    }
    free(mark);
    if (own_post) {
        jfeatures_region_reset(region);
    }
    return ok;
}

/* ---- configured-feature test hook ------------------------------------------------------------------------------------------ */

typedef struct ChangeKey {
    int x, y, z;
    int seq;
} ChangeKey;

static int change_key_cmp(const void *a, const void *b)
{
    const ChangeKey *p = (const ChangeKey *)a, *q = (const ChangeKey *)b;
    if (p->x != q->x) {
        return p->x < q->x ? -1 : 1;
    }
    if (p->y != q->y) {
        return p->y < q->y ? -1 : 1;
    }
    if (p->z != q->z) {
        return p->z < q->z ? -1 : 1;
    }
    return p->seq < q->seq ? -1 : (p->seq > q->seq ? 1 : 0);
}

int jfeatures_place_configured(JFeatures *f, JSurface *surface, JRegion *region, const char *name, int64_t feature_seed,
                               int x, int y, int z, JBlockChange *out, int cap, int *count)
{
    if (f == NULL || surface == NULL || region == NULL || name == NULL) {
        return -1;
    }
    PlacedFeature *pf = NULL;
    for (int i = 0; i < f->cfg_cache_count; ++i) {
        if (strcmp(f->cfg_cache[i].name, name) == 0) {
            pf = f->cfg_cache[i].pf;
            break;
        }
    }
    if (pf == NULL) {
        char path[1300];
        snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/configured_feature/%s.json", f->root, jf_local_name(name));
        JJson *j = jj_parse_file(path);
        if (j == NULL) {
            return -1;
        }
        pf = (PlacedFeature *)calloc(1, sizeof(PlacedFeature));
        pf->self_index = -1;
        pf->mods_supported = true;
        snprintf(pf->name, sizeof(pf->name), "%s", name);
        parse_feature(f, pf, j);
        jj_free(j);
        f->cfg_cache = (struct ConfiguredCache *)realloc(f->cfg_cache, (size_t)(f->cfg_cache_count + 1) * sizeof(*f->cfg_cache));
        snprintf(f->cfg_cache[f->cfg_cache_count].name, NAME_LEN, "%s", name);
        f->cfg_cache[f->cfg_cache_count].pf = pf;
        ++f->cfg_cache_count;
    }
    if (!pf_all_supported(pf)) {
        return -1;
    }
    JrRandom rnd;
    jr_worldgen_xoroshiro_init(&rnd, feature_seed);
    Level lvl;
    memset(&lvl, 0, sizeof(lvl));
    lvl.f = f;
    lvl.surface = surface;
    lvl.region = region;
    lvl.rnd = &rnd;
    lvl.logging = true;
    post_begin(&lvl);
    bool placed = run_pf(&lvl, pf, 0, x, y, z);
    int n = 0;
    if (lvl.log_n > 0) {
        ChangeKey *keys = (ChangeKey *)malloc((size_t)lvl.log_n * sizeof(ChangeKey));
        for (int i = 0; i < lvl.log_n; ++i) {
            keys[i].x = lvl.log[i].x;
            keys[i].y = lvl.log[i].y;
            keys[i].z = lvl.log[i].z;
            keys[i].seq = i;
        }
        qsort(keys, (size_t)lvl.log_n, sizeof(ChangeKey), change_key_cmp);
        for (int i = 0; i < lvl.log_n;) {
            int j = i;
            while (j < lvl.log_n && keys[j].x == keys[i].x && keys[j].y == keys[i].y && keys[j].z == keys[i].z) {
                ++j;
            }
            int before = lvl.log[keys[i].seq].before;
            int now = jf_level_get(&lvl, keys[i].x, keys[i].y, keys[i].z);
            if (now != before) {
                if (n < cap) {
                    out[n].x = keys[i].x;
                    out[n].y = keys[i].y;
                    out[n].z = keys[i].z;
                    out[n].block = now;
                }
                ++n;
            }
            i = j;
        }
        free(keys);
    }
    /* undo the placement so that every test placement starts from the generated region */
    lvl.logging = false;
    for (int i = lvl.log_n - 1; i >= 0; --i) {
        jf_level_set(&lvl, lvl.log[i].x, lvl.log[i].y, lvl.log[i].z, lvl.log[i].before);
    }
    free(lvl.log);
    if (count != NULL) {
        *count = n;
    }
    return placed ? 1 : 0;
}
