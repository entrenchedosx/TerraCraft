#include "world/jtree.h"

#include "world/jfill.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

/* ---- configuration ------------------------------------------------------------------------------ */

typedef enum TrunkKind { TRUNK_STRAIGHT, TRUNK_FORKING, TRUNK_GIANT, TRUNK_MEGA_JUNGLE, TRUNK_DARK_OAK, TRUNK_FANCY, TRUNK_BENDING, TRUNK_CHERRY } TrunkKind;
typedef enum FoliageKind {
    FOL_BLOB, FOL_SPRUCE, FOL_PINE, FOL_BUSH, FOL_ACACIA, FOL_MEGA_PINE, FOL_JUNGLE, FOL_DARK_OAK, FOL_FANCY, FOL_RANDOM_SPREAD, FOL_CHERRY
} FoliageKind;

typedef enum DecoKind { DECO_BEEHIVE, DECO_PLACE_ON_GROUND, DECO_TRUNK_VINE, DECO_LEAVE_VINE, DECO_COCOA, DECO_ALTER_GROUND } DecoKind;

typedef struct Deco {
    DecoKind kind;
    float probability;
    int tries, radius, height;
    StateProvider provider;
} Deco;

#define MAX_DECO 4

struct JTreeCfg {
    Deco decos[MAX_DECO];
    int deco_count;
    TrunkKind trunk_kind;
    int base_height, rand_a, rand_b;

    FoliageKind foliage_kind;
    IntProv radius, offset;
    int blob_height;          /* blob / bush: fixed foliage height */
    IntProv pine_height;      /* pine: sampled foliage height */
    IntProv spruce_trunk_h;   /* spruce: trunk_height */
    int spread_attempts;      /* random_spread: leaf_placement_attempts */
    IntProv bend_length;      /* bending trunk */
    int min_height_for_leaves;
    /* cherry trunk */
    int cherry_vals[4], cherry_weights[4], cherry_n, cherry_total;
    IntProv cherry_h_len, cherry_start, cherry_start2, cherry_end;
    /* cherry foliage */
    float hole_wide, hole_corner, hang_chance, hang_ext;

    int size_kind;            /* 0 two_layers, 1 three_layers */
    int size_limit, size_lower, size_upper, size_middle, size_upper_limit;
    int min_clipped;          /* -1: none */

    int trunk_state;
    StateProvider dirt;
    StateProvider foliage;
    bool force_dirt;
    bool ignore_vines;
};

static void unsupported(const char *what, const char *name)
{
    if (getenv("TERRACRAFT_WORLDGEN_VERBOSE") != NULL) {
        fprintf(stderr, "jtree: unsupported %s %s\n", what, name);
    }
}

static int parse_state_name(const JJson *provider)
{
    /* trunk_provider / dirt_provider are simple_state_provider in every vanilla tree */
    if (strcmp(jf_local_name(jf_jstr(jj_get(provider, "type"))), "simple_state_provider") != 0) {
        return -1;
    }
    return jfill_block_register(jf_jstr(jj_get(jj_get(provider, "state"), "Name")));
}

JTreeCfg *jtree_parse(const JJson *config)
{
    JTreeCfg *t = (JTreeCfg *)calloc(1, sizeof(JTreeCfg));
    if (t == NULL) {
        return NULL;
    }
    const JJson *tp = jj_get(config, "trunk_placer");
    const char *tn = jf_local_name(jf_jstr(jj_get(tp, "type")));
    t->base_height = (int)jj_double(jj_get(tp, "base_height"));
    t->rand_a = (int)jj_double(jj_get(tp, "height_rand_a"));
    t->rand_b = (int)jj_double(jj_get(tp, "height_rand_b"));
    if (strcmp(tn, "straight_trunk_placer") == 0) {
        t->trunk_kind = TRUNK_STRAIGHT;
    } else if (strcmp(tn, "forking_trunk_placer") == 0) {
        t->trunk_kind = TRUNK_FORKING;
    } else if (strcmp(tn, "giant_trunk_placer") == 0) {
        t->trunk_kind = TRUNK_GIANT;
    } else if (strcmp(tn, "mega_jungle_trunk_placer") == 0) {
        t->trunk_kind = TRUNK_MEGA_JUNGLE;
    } else if (strcmp(tn, "dark_oak_trunk_placer") == 0) {
        t->trunk_kind = TRUNK_DARK_OAK;
    } else if (strcmp(tn, "fancy_trunk_placer") == 0) {
        t->trunk_kind = TRUNK_FANCY;
    } else if (strcmp(tn, "cherry_trunk_placer") == 0 && getenv("TERRACRAFT_EXPERIMENTAL_CHERRY") != NULL) {
        /* Ported but not matching the reference yet (leaf holes differ): disabled unless explicitly enabled. */
        t->trunk_kind = TRUNK_CHERRY;
        const JJson *bc = jj_get(tp, "branch_count");
        const JJson *dist = jj_get(bc, "distribution");
        if (dist == NULL || dist->type != JJ_ARR || dist->count < 1 || dist->count > 4) {
            unsupported("branch_count in", tn);
            goto fail;
        }
        for (int i = 0; i < dist->count; ++i) {
            t->cherry_vals[i] = (int)jj_double(jj_get(dist->items[i], "data"));
            t->cherry_weights[i] = (int)jj_double(jj_get(dist->items[i], "weight"));
            t->cherry_total += t->cherry_weights[i];
        }
        t->cherry_n = dist->count;
        t->cherry_h_len = jf_parse_int_provider(jj_get(tp, "branch_horizontal_length"));
        const JJson *st = jj_get(tp, "branch_start_offset_from_top");
        t->cherry_start.kind = 1;
        t->cherry_start.a = (int)jj_double(jj_get(st, "min_inclusive"));
        t->cherry_start.b = (int)jj_double(jj_get(st, "max_inclusive"));
        t->cherry_start2 = t->cherry_start;
        t->cherry_start2.b = t->cherry_start.b - 1;
        t->cherry_end = jf_parse_int_provider(jj_get(tp, "branch_end_offset_from_top"));
        if (t->cherry_h_len.kind < 0 || t->cherry_end.kind < 0) {
            unsupported("int provider in", tn);
            goto fail;
        }
    } else if (strcmp(tn, "bending_trunk_placer") == 0) {
        t->trunk_kind = TRUNK_BENDING;
        t->bend_length = jf_parse_int_provider(jj_get(tp, "bend_length"));
        t->min_height_for_leaves = jj_get(tp, "min_height_for_leaves") != NULL ? (int)jj_double(jj_get(tp, "min_height_for_leaves")) : 1;
        if (t->bend_length.kind < 0) {
            unsupported("int provider in", tn);
            goto fail;
        }
    } else {
        unsupported("trunk placer", tn);
        goto fail;
    }

    const JJson *fp = jj_get(config, "foliage_placer");
    const char *fn = jf_local_name(jf_jstr(jj_get(fp, "type")));
    t->radius = jf_parse_int_provider(jj_get(fp, "radius"));
    t->offset = jf_parse_int_provider(jj_get(fp, "offset"));
    if (strcmp(fn, "blob_foliage_placer") == 0 || strcmp(fn, "bush_foliage_placer") == 0) {
        t->foliage_kind = fn[0] == 'b' && fn[1] == 'l' ? FOL_BLOB : FOL_BUSH;
        t->blob_height = (int)jj_double(jj_get(fp, "height"));
    } else if (strcmp(fn, "spruce_foliage_placer") == 0) {
        t->foliage_kind = FOL_SPRUCE;
        t->spruce_trunk_h = jf_parse_int_provider(jj_get(fp, "trunk_height"));
    } else if (strcmp(fn, "pine_foliage_placer") == 0) {
        t->foliage_kind = FOL_PINE;
        t->pine_height = jf_parse_int_provider(jj_get(fp, "height"));
    } else if (strcmp(fn, "acacia_foliage_placer") == 0) {
        t->foliage_kind = FOL_ACACIA;
    } else if (strcmp(fn, "mega_pine_foliage_placer") == 0) {
        t->foliage_kind = FOL_MEGA_PINE;
        t->pine_height = jf_parse_int_provider(jj_get(fp, "crown_height"));
    } else if (strcmp(fn, "jungle_foliage_placer") == 0) {
        t->foliage_kind = FOL_JUNGLE;
        t->blob_height = (int)jj_double(jj_get(fp, "height"));
    } else if (strcmp(fn, "dark_oak_foliage_placer") == 0) {
        t->foliage_kind = FOL_DARK_OAK;
    } else if (strcmp(fn, "cherry_foliage_placer") == 0) {
        t->foliage_kind = FOL_CHERRY;
        t->pine_height = jf_parse_int_provider(jj_get(fp, "height"));
        t->hole_wide = jj_float(jj_get(fp, "wide_bottom_layer_hole_chance"));
        t->hole_corner = jj_float(jj_get(fp, "corner_hole_chance"));
        t->hang_chance = jj_float(jj_get(fp, "hanging_leaves_chance"));
        t->hang_ext = jj_float(jj_get(fp, "hanging_leaves_extension_chance"));
    } else if (strcmp(fn, "random_spread_foliage_placer") == 0) {
        t->foliage_kind = FOL_RANDOM_SPREAD;
        t->pine_height = jf_parse_int_provider(jj_get(fp, "foliage_height"));
        t->spread_attempts = (int)jj_double(jj_get(fp, "leaf_placement_attempts"));
    } else if (strcmp(fn, "fancy_foliage_placer") == 0) {
        t->foliage_kind = FOL_FANCY;
        t->blob_height = (int)jj_double(jj_get(fp, "height"));
    } else {
        unsupported("foliage placer", fn);
        goto fail;
    }
    if (t->radius.kind < 0 || t->offset.kind < 0) {
        unsupported("int provider in foliage placer", fn);
        goto fail;
    }

    const JJson *ms = jj_get(config, "minimum_size");
    const char *sn = jf_local_name(jf_jstr(jj_get(ms, "type")));
    t->size_limit = (int)jj_double(jj_get(ms, "limit"));
    t->size_lower = (int)jj_double(jj_get(ms, "lower_size"));
    t->size_upper = (int)jj_double(jj_get(ms, "upper_size"));
    t->min_clipped = jj_get(ms, "min_clipped_height") != NULL ? (int)jj_double(jj_get(ms, "min_clipped_height")) : -1;
    if (strcmp(sn, "two_layers_feature_size") == 0) {
        t->size_kind = 0;
    } else if (strcmp(sn, "three_layers_feature_size") == 0) {
        t->size_kind = 1;
        t->size_middle = (int)jj_double(jj_get(ms, "middle_size"));
        t->size_upper_limit = (int)jj_double(jj_get(ms, "upper_limit"));
    } else {
        unsupported("feature size", sn);
        goto fail;
    }

    t->trunk_state = parse_state_name(jj_get(config, "trunk_provider"));
    int dirt = parse_state_name(jj_get(config, "dirt_provider"));
    t->foliage = jf_parse_provider(jj_get(config, "foliage_provider"));
    if (t->trunk_state < 0 || dirt < 0 || t->foliage.kind < 0) {
        unsupported("state provider", "in tree configuration");
        goto fail;
    }
    t->dirt.kind = 0;
    t->dirt.states[0] = dirt;
    t->dirt.count = 1;
    t->force_dirt = jj_get(config, "force_dirt") != NULL && jj_get(config, "force_dirt")->boolean;
    t->ignore_vines = jj_get(config, "ignore_vines") != NULL && jj_get(config, "ignore_vines")->boolean;

    const JJson *decorators = jj_get(config, "decorators");
    if (decorators != NULL && decorators->type == JJ_ARR) {
        for (int i = 0; i < decorators->count; ++i) {
            const JJson *d = decorators->items[i];
            const char *dn = jf_local_name(jf_jstr(jj_get(d, "type")));
            Deco *deco = &t->decos[t->deco_count];
            if (t->deco_count >= MAX_DECO) {
                unsupported("number of tree decorators", dn);
                goto fail;
            }
            if (strcmp(dn, "beehive") == 0) {
                deco->kind = DECO_BEEHIVE;
                deco->probability = jj_float(jj_get(d, "probability"));
                jfill_block_register("minecraft:bee_nest");
            } else if (strcmp(dn, "trunk_vine") == 0 || strcmp(dn, "leave_vine") == 0) {
                deco->kind = dn[0] == 't' ? DECO_TRUNK_VINE : DECO_LEAVE_VINE;
                deco->probability = jj_float(jj_get(d, "probability"));
                jblock_set_no_collision(jfill_block_register("minecraft:vine"));
            } else if (strcmp(dn, "cocoa") == 0) {
                deco->kind = DECO_COCOA;
                deco->probability = jj_float(jj_get(d, "probability"));
                jfill_block_register("minecraft:cocoa");
            } else if (strcmp(dn, "alter_ground") == 0) {
                deco->kind = DECO_ALTER_GROUND;
                deco->provider = jf_parse_provider(jj_get(d, "provider"));
                if (deco->provider.kind < 0) {
                    unsupported("state provider in", dn);
                    goto fail;
                }
            } else if (strcmp(dn, "place_on_ground") == 0) {
                deco->kind = DECO_PLACE_ON_GROUND;
                deco->tries = jj_get(d, "tries") != NULL ? (int)jj_double(jj_get(d, "tries")) : 128;
                deco->radius = jj_get(d, "radius") != NULL ? (int)jj_double(jj_get(d, "radius")) : 2;
                deco->height = jj_get(d, "height") != NULL ? (int)jj_double(jj_get(d, "height")) : 1;
                deco->provider = jf_parse_provider(jj_get(d, "block_state_provider"));
                if (deco->provider.kind < 0) {
                    unsupported("state provider in", dn);
                    goto fail;
                }
                for (int k = 0; k < deco->provider.count; ++k) {
                    jblock_set_no_collision(deco->provider.states[k]);
                }
            } else {
                unsupported("tree decorator", dn);
                goto fail;
            }
            ++t->deco_count;
        }
    }
    if (jj_get(config, "root_placer") != NULL) {
        unsupported("root placer", "");
        goto fail;
    }
    return t;
fail:
    free(t);
    return NULL;
}

void jtree_free(JTreeCfg *t)
{
    free(t);
}

/* ---- Set<BlockPos>: java.util.HashSet iteration order ------------------------------------------------ */

typedef struct PsEntry {
    int x, y, z;
    unsigned hash; /* HashMap.hash(): h ^ (h >>> 16) */
    int next;      /* index of the next entry in the bucket, -1 at the end */
} PsEntry;

typedef struct PosSet {
    PsEntry *e;
    int size, cap_entries;
    int *tab; /* bucket heads, power-of-two sized */
    int tab_cap;
    int threshold;
} PosSet;

static unsigned pos_hash(int x, int y, int z)
{
    unsigned h = (unsigned)((y + z * 31) * 31 + x);
    return h ^ (h >> 16);
}

static void ps_rebuild(PosSet *s, int new_cap)
{
    /* Java's resize splits each bucket into lo/hi lists keeping their relative order. */
    int *old = s->tab;
    int old_cap = s->tab_cap;
    int *tab = (int *)malloc((size_t)new_cap * sizeof(int));
    for (int i = 0; i < new_cap; ++i) {
        tab[i] = -1;
    }
    if (old != NULL) {
        for (int j = 0; j < old_cap; ++j) {
            int tail_lo = -1, tail_hi = -1;
            for (int i = old[j]; i >= 0;) {
                int next = s->e[i].next;
                s->e[i].next = -1;
                if ((s->e[i].hash & (unsigned)old_cap) == 0) {
                    if (tail_lo < 0) {
                        tab[j] = i;
                    } else {
                        s->e[tail_lo].next = i;
                    }
                    tail_lo = i;
                } else {
                    if (tail_hi < 0) {
                        tab[j + old_cap] = i;
                    } else {
                        s->e[tail_hi].next = i;
                    }
                    tail_hi = i;
                }
                i = next;
            }
        }
        free(old);
    }
    s->tab = tab;
    s->tab_cap = new_cap;
    s->threshold = new_cap * 3 / 4;
}

static bool ps_contains(const PosSet *s, int x, int y, int z)
{
    if (s->tab == NULL) {
        return false;
    }
    unsigned h = pos_hash(x, y, z);
    for (int i = s->tab[h & (unsigned)(s->tab_cap - 1)]; i >= 0; i = s->e[i].next) {
        if (s->e[i].x == x && s->e[i].y == y && s->e[i].z == z) {
            return true;
        }
    }
    return false;
}

static void ps_add(PosSet *s, int x, int y, int z)
{
    if (s->tab == NULL) {
        ps_rebuild(s, 16);
    }
    unsigned h = pos_hash(x, y, z);
    int idx = (int)(h & (unsigned)(s->tab_cap - 1));
    int last = -1;
    for (int i = s->tab[idx]; i >= 0; i = s->e[i].next) {
        if (s->e[i].x == x && s->e[i].y == y && s->e[i].z == z) {
            return;
        }
        last = i;
    }
    if (s->size == s->cap_entries) {
        s->cap_entries = s->cap_entries != 0 ? s->cap_entries * 2 : 256;
        s->e = (PsEntry *)realloc(s->e, (size_t)s->cap_entries * sizeof(PsEntry));
    }
    int n = s->size++;
    s->e[n].x = x;
    s->e[n].y = y;
    s->e[n].z = z;
    s->e[n].hash = h;
    s->e[n].next = -1;
    if (last < 0) {
        s->tab[idx] = n;
    } else {
        s->e[last].next = n;
    }
    if (s->size > s->threshold) {
        ps_rebuild(s, s->tab_cap * 2);
    }
}

static void ps_free(PosSet *s)
{
    free(s->e);
    free(s->tab);
    memset(s, 0, sizeof(*s));
}

/* ---- placement context ------------------------------------------------------------------------------ */

typedef struct Attach {
    int x, y, z;
    int radius_offset;
    bool double_trunk;
} Attach;

#define MAX_ATTACH 128

typedef struct TCtx {
    Level *l;
    const JTreeCfg *t;
    JrRandom *r;
    const JTreeTags *tags;
    PosSet roots, trunks, foliage, deco;
} TCtx;

static bool valid_tree_pos(const TCtx *c, int x, int y, int z)
{
    int b = jf_level_get(c->l, x, y, z);
    return jblock_is_air(b) || c->tags->replaceable_by_trees[b & 255];
}

static void set_trunk(TCtx *c, int x, int y, int z, int state)
{
    ps_add(&c->trunks, x, y, z);
    jf_level_set(c->l, x, y, z, state);
}

static void set_foliage(TCtx *c, int x, int y, int z, int state)
{
    ps_add(&c->foliage, x, y, z);
    jf_level_set(c->l, x, y, z, state);
}

/* TrunkPlacer.isFree: a valid tree position or a log */
static bool trunk_is_free(const TCtx *c, int x, int y, int z)
{
    if (valid_tree_pos(c, x, y, z)) {
        return true;
    }
    return c->tags->logs[jf_level_get(c->l, x, y, z) & 255];
}

/* TrunkPlacer.setDirtAt */
static void set_dirt_at(TCtx *c, int x, int y, int z)
{
    int b = jf_level_get(c->l, x, y, z);
    int grass = JB_GRASS_BLOCK, mycelium = JB_MYCELIUM;
    bool is_dirt = c->tags->dirt[b & 255] && b != grass && b != mycelium;
    if (c->t->force_dirt || !is_dirt) {
        set_trunk(c, x, y, z, jf_provider_state(&c->t->dirt, c->r));
    }
}

/* TrunkPlacer.placeLog */
static bool place_log(TCtx *c, int x, int y, int z)
{
    if (valid_tree_pos(c, x, y, z)) {
        set_trunk(c, x, y, z, c->t->trunk_state);
        return true;
    }
    return false;
}

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

static int imin(int a, int b)
{
    return a < b ? a : b;
}

static int imax(int a, int b)
{
    return a > b ? a : b;
}

/* Direction.Plane.HORIZONTAL: NORTH, EAST, SOUTH, WEST */
static const int HSTEP[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

static int random_horizontal(JrRandom *r)
{
    return jr_next_int_bound(r, 4);
}

/* ---- trunk placers ----------------------------------------------------------------------------------- */

static int place_trunk_straight(TCtx *c, int height, int ox, int oy, int oz, Attach *out)
{
    set_dirt_at(c, ox, oy - 1, oz);
    for (int i = 0; i < height; ++i) {
        place_log(c, ox, oy + i, oz);
    }
    out[0] = (Attach){ox, oy + height, oz, 0, false};
    return 1;
}

static int place_trunk_forking(TCtx *c, int height, int ox, int oy, int oz, Attach *out)
{
    set_dirt_at(c, ox, oy - 1, oz);
    int n = 0;
    int dir = random_horizontal(c->r);
    int i = height - jr_next_int_bound(c->r, 4) - 1;
    int j = 3 - jr_next_int_bound(c->r, 3);
    int k = ox, l = oz;
    int top = 0; /* OptionalInt: 0 = empty */
    bool have_top = false;
    for (int y = 0; y < height; ++y) {
        int yy = oy + y;
        if (y >= i && j > 0) {
            k += HSTEP[dir][0];
            l += HSTEP[dir][1];
            --j;
        }
        if (place_log(c, k, yy, l)) {
            top = yy + 1;
            have_top = true;
        }
    }
    if (have_top) {
        out[n++] = (Attach){k, top, l, 1, false};
    }
    k = ox;
    l = oz;
    int dir2 = random_horizontal(c->r);
    if (dir2 != dir) {
        int y2 = i - jr_next_int_bound(c->r, 2) - 1;
        int len = 1 + jr_next_int_bound(c->r, 3);
        have_top = false;
        for (int y = y2; y < height && len > 0; ++y, --len) {
            if (y >= 1) {
                int yy = oy + y;
                k += HSTEP[dir2][0];
                l += HSTEP[dir2][1];
                if (place_log(c, k, yy, l)) {
                    top = yy + 1;
                    have_top = true;
                }
            }
        }
        if (have_top) {
            out[n++] = (Attach){k, top, l, 0, false};
        }
    }
    return n;
}

static bool is_air_or_leaves(const TCtx *c, int x, int y, int z)
{
    int b = jf_level_get(c->l, x, y, z);
    return jblock_is_air(b) || c->tags->leaves[b & 255];
}

/* TrunkPlacer.placeLogIfFree */
static void place_log_if_free(TCtx *c, int x, int y, int z)
{
    if (trunk_is_free(c, x, y, z)) {
        place_log(c, x, y, z);
    }
}

static int place_trunk_giant(TCtx *c, int height, int ox, int oy, int oz, Attach *out)
{
    set_dirt_at(c, ox, oy - 1, oz);
    set_dirt_at(c, ox + 1, oy - 1, oz);
    set_dirt_at(c, ox, oy - 1, oz + 1);
    set_dirt_at(c, ox + 1, oy - 1, oz + 1);
    for (int i = 0; i < height; ++i) {
        place_log_if_free(c, ox, oy + i, oz);
        if (i < height - 1) {
            place_log_if_free(c, ox + 1, oy + i, oz);
            place_log_if_free(c, ox + 1, oy + i, oz + 1);
            place_log_if_free(c, ox, oy + i, oz + 1);
        }
    }
    out[0] = (Attach){ox, oy + height, oz, 0, true};
    return 1;
}

static int place_trunk_mega_jungle(TCtx *c, int height, int ox, int oy, int oz, Attach *out)
{
    int n = place_trunk_giant(c, height, ox, oy, oz, out);
    for (int i = height - 2 - jr_next_int_bound(c->r, 4); i > height / 2; i -= 2 + jr_next_int_bound(c->r, 4)) {
        float f = jr_next_float(c->r) * 6.2831855f;
        int j = 0, k = 0;
        for (int l = 0; l < 5; ++l) {
            j = (int)(1.5f + jf_mth_cos((double)f) * (float)l);
            k = (int)(1.5f + jf_mth_sin((double)f) * (float)l);
            place_log(c, ox + j, oy + i - 3 + l / 2, oz + k);
        }
        out[n++] = (Attach){ox + j, oy + i, oz + k, -2, false};
    }
    return n;
}

static int place_trunk_dark_oak(TCtx *c, int height, int ox, int oy, int oz, Attach *out)
{
    int n = 0;
    set_dirt_at(c, ox, oy - 1, oz);
    set_dirt_at(c, ox + 1, oy - 1, oz);
    set_dirt_at(c, ox, oy - 1, oz + 1);
    set_dirt_at(c, ox + 1, oy - 1, oz + 1);
    int dir = random_horizontal(c->r);
    int i = height - jr_next_int_bound(c->r, 4);
    int j = 2 - jr_next_int_bound(c->r, 3);
    int k = ox, l = oy, m = oz;
    int px = k, pz = m;
    int top = l + height - 1;
    for (int q = 0; q < height; ++q) {
        if (q >= i && j > 0) {
            px += HSTEP[dir][0];
            pz += HSTEP[dir][1];
            --j;
        }
        int yy = l + q;
        if (is_air_or_leaves(c, px, yy, pz)) {
            place_log(c, px, yy, pz);
            place_log(c, px + 1, yy, pz);
            place_log(c, px, yy, pz + 1);
            place_log(c, px + 1, yy, pz + 1);
        }
    }
    out[n++] = (Attach){px, top, pz, 0, true};
    for (int q = -1; q <= 2; ++q) {
        for (int r = -1; r <= 2; ++r) {
            if ((q < 0 || q > 1 || r < 0 || r > 1) && jr_next_int_bound(c->r, 3) <= 0) {
                int s = jr_next_int_bound(c->r, 3) + 2;
                for (int t = 0; t < s; ++t) {
                    place_log(c, k + q, top - t - 1, m + r);
                }
                out[n++] = (Attach){k + q, top, m + r, 0, false};
            }
        }
    }
    return n;
}

static int place_trunk_bending(TCtx *c, int height, int ox, int oy, int oz, Attach *out)
{
    int dir = random_horizontal(c->r);
    int top = height - 1;
    int mx = ox, my = oy, mz = oz;
    set_dirt_at(c, mx, my - 1, mz);
    int n = 0;
    for (int j = 0; j <= top; ++j) {
        if (j + 1 >= top + jr_next_int_bound(c->r, 2)) {
            mx += HSTEP[dir][0];
            mz += HSTEP[dir][1];
        }
        if (valid_tree_pos(c, mx, my, mz)) {
            place_log(c, mx, my, mz);
        }
        if (j >= c->t->min_height_for_leaves && n < MAX_ATTACH) {
            out[n++] = (Attach){mx, my, mz, 0, false};
        }
        ++my;
    }
    int len = jf_sample_int(&c->t->bend_length, c->r);
    for (int l = 0; l <= len; ++l) {
        if (valid_tree_pos(c, mx, my, mz)) {
            place_log(c, mx, my, mz);
        }
        if (n < MAX_ATTACH) {
            out[n++] = (Attach){mx, my, mz, 0, false};
        }
        mx += HSTEP[dir][0];
        mz += HSTEP[dir][1];
    }
    return n;
}

/* CherryTrunkPlacer.generateBranch; `diagonal_flag` is the `boolean` argument of the vanilla method */
static Attach cherry_branch(TCtx *c, int height, int ox, int oy, int oz, int dir, int offset_from_top, bool flag)
{
    const JTreeCfg *t = c->t;
    int mx = ox, my = oy + offset_from_top, mz = oz;
    int i = height - 1 + jf_sample_int(&t->cherry_end, c->r);
    bool bl = flag || i < offset_from_top;
    int j = jf_sample_int(&t->cherry_h_len, c->r) + (bl ? 1 : 0);
    int ex = ox + HSTEP[dir][0] * j, ey = oy + i, ez = oz + HSTEP[dir][1] * j;
    int k = bl ? 2 : 1;
    for (int n = 0; n < k; ++n) {
        mx += HSTEP[dir][0];
        mz += HSTEP[dir][1];
        place_log(c, mx, my, mz);
    }
    int vert_up = ey > my; /* Direction.UP : DOWN */
    for (;;) {
        int dist = iabs(mx - ex) + iabs(my - ey) + iabs(mz - ez);
        if (dist == 0) {
            return (Attach){ex, ey + 1, ez, 0, false};
        }
        float f = (float)iabs(ey - my) / (float)dist;
        bool bl3 = jr_next_float(c->r) < f;
        if (bl3) {
            my += vert_up ? 1 : -1;
        } else {
            mx += HSTEP[dir][0];
            mz += HSTEP[dir][1];
        }
        place_log(c, mx, my, mz);
    }
}

static int place_trunk_cherry(TCtx *c, int height, int ox, int oy, int oz, Attach *out)
{
    const JTreeCfg *t = c->t;
    set_dirt_at(c, ox, oy - 1, oz);
    int first = imax(0, height - 1 + jf_sample_int(&t->cherry_start, c->r));
    int second = imax(0, height - 1 + jf_sample_int(&t->cherry_start2, c->r));
    if (second >= first) {
        ++second;
    }
    int rnd = jr_next_int_bound(c->r, t->cherry_total);
    int branch_count = t->cherry_vals[t->cherry_n - 1];
    for (int i = 0; i < t->cherry_n; ++i) {
        rnd -= t->cherry_weights[i];
        if (rnd < 0) {
            branch_count = t->cherry_vals[i];
            break;
        }
    }
    bool three = branch_count == 3;
    bool two = branch_count >= 2;
    int trunk_height;
    if (three) {
        trunk_height = height;
    } else if (two) {
        trunk_height = imax(first, second) + 1;
    } else {
        trunk_height = first + 1;
    }
    for (int i = 0; i < trunk_height; ++i) {
        place_log(c, ox, oy + i, oz);
    }
    int n = 0;
    if (three) {
        out[n++] = (Attach){ox, oy + trunk_height, oz, 0, false};
    }
    int dir = random_horizontal(c->r);
    out[n++] = cherry_branch(c, height, ox, oy, oz, dir, first, first < trunk_height - 1);
    if (two) {
        out[n++] = cherry_branch(c, height, ox, oy, oz, (dir + 2) & 3, second, second < trunk_height - 1);
    }
    return n;
}

/* Mth.floor(float) */
static int mth_floor_f(float f)
{
    if (f != f) {
        return 0;
    }
    int i = (int)f;
    return f < (float)i ? i - 1 : i;
}

static int mth_floor_d(double v)
{
    int i = (int)v;
    return v < (double)i ? i - 1 : i;
}

/* FancyTrunkPlacer */
typedef struct FancyCoords {
    int x, y, z;
    int branch_base;
} FancyCoords;

static float fancy_tree_shape(int height, int y)
{
    if ((float)y < (float)height * 0.3f) {
        return -1.0f;
    }
    float f = (float)height / 2.0f;
    float g = f - (float)y;
    float h = (float)sqrt((double)(f * f - g * g));
    if (g == 0.0f) {
        h = f;
    } else if (fabsf(g) >= f) {
        return 0.0f;
    }
    return h * 0.5f;
}

static bool fancy_trim_branches(int height, int local_y)
{
    return (double)local_y >= (double)height * 0.2;
}

static bool fancy_make_limb(TCtx *c, int sx, int sy, int sz, int ex, int ey, int ez, bool place_logs)
{
    if (!place_logs && sx == ex && sy == ey && sz == ez) {
        return true;
    }
    int dx = ex - sx, dy = ey - sy, dz = ez - sz;
    int steps = imax(iabs(dx), imax(iabs(dy), iabs(dz)));
    float fx = (float)dx / (float)steps;
    float fy = (float)dy / (float)steps;
    float fz = (float)dz / (float)steps;
    for (int j = 0; j <= steps; ++j) {
        int px = sx + mth_floor_f(0.5f + (float)j * fx);
        int py = sy + mth_floor_f(0.5f + (float)j * fy);
        int pz = sz + mth_floor_f(0.5f + (float)j * fz);
        if (place_logs) {
            place_log(c, px, py, pz);
        } else if (!trunk_is_free(c, px, py, pz)) {
            return false;
        }
    }
    return true;
}

static int place_trunk_fancy(TCtx *c, int height, int ox, int oy, int oz, Attach *out)
{
    int i = height + 2;
    int j = mth_floor_d((double)i * 0.618);
    set_dirt_at(c, ox, oy - 1, oz);
    double dd = 1.0 * (double)i / 13.0;
    int k = imin(1, mth_floor_d(1.382 + dd * dd));
    int l = oy + j;
    int m = i - 5;
    FancyCoords coords[MAX_ATTACH];
    int nc = 0;
    coords[nc++] = (FancyCoords){ox, oy + m, oz, l};
    for (; m >= 0; --m) {
        float f = fancy_tree_shape(i, m);
        if (f < 0.0f) {
            continue;
        }
        for (int n = 0; n < k; ++n) {
            double g = 1.0 * (double)f * ((double)jr_next_float(c->r) + 0.328);
            double h = (double)(jr_next_float(c->r) * 2.0f) * 3.141592653589793;
            double o = g * sin(h) + 0.5;
            double p = g * cos(h) + 0.5;
            int bx = ox + mth_floor_d(o), by = oy + m - 1, bz = oz + mth_floor_d(p);
            int b2y = by + 5;
            if (fancy_make_limb(c, bx, by, bz, bx, b2y, bz, false)) {
                int q = ox - bx;
                int r = oz - bz;
                double sdist = (double)by - sqrt((double)(q * q + r * r)) * 0.381;
                int t = sdist > (double)l ? l : (int)sdist;
                if (fancy_make_limb(c, ox, t, oz, bx, by, bz, false) && nc < MAX_ATTACH) {
                    coords[nc++] = (FancyCoords){bx, by, bz, t};
                }
            }
        }
    }
    fancy_make_limb(c, ox, oy, oz, ox, oy + j, oz, true);
    /* makeBranches */
    for (int e = 0; e < nc; ++e) {
        int base = coords[e].branch_base;
        if (!(ox == coords[e].x && base == coords[e].y && oz == coords[e].z) && fancy_trim_branches(i, base - oy)) {
            fancy_make_limb(c, ox, base, oz, coords[e].x, coords[e].y, coords[e].z, true);
        }
    }
    int n = 0;
    for (int e = 0; e < nc; ++e) {
        if (fancy_trim_branches(i, coords[e].branch_base - oy)) {
            out[n++] = (Attach){coords[e].x, coords[e].y, coords[e].z, 0, false};
        }
    }
    return n;
}

/* ---- foliage placers ---------------------------------------------------------------------------------- */

/* FoliagePlacer.tryPlaceLeaf */
static bool try_place_leaf(TCtx *c, int x, int y, int z)
{
    if (!valid_tree_pos(c, x, y, z)) {
        return false;
    }
    set_foliage(c, x, y, z, jf_provider_state(&c->t->foliage, c->r));
    return true;
}

/* shouldSkipLocation of the concrete foliage placer */
static bool should_skip(TCtx *c, int dx, int y, int dz, int range, bool large)
{
    (void)large;
    switch (c->t->foliage_kind) {
    case FOL_BLOB:
        return dx == range && dz == range && (jr_next_int_bound(c->r, 2) == 0 || y == 0);
    case FOL_BUSH:
        return dx == range && dz == range && jr_next_int_bound(c->r, 2) == 0;
    case FOL_SPRUCE:
    case FOL_PINE:
        return dx == range && dz == range && range > 0;
    case FOL_ACACIA:
        if (y == 0) {
            return (dx > 1 || dz > 1) && dx != 0 && dz != 0;
        }
        return dx == range && dz == range && range > 0;
    case FOL_RANDOM_SPREAD:
        return false;
    case FOL_CHERRY: {
        if (y == -1 && (dx == range || dz == range) && jr_next_float(c->r) < c->t->hole_wide) {
            return true;
        }
        bool corner = dx == range && dz == range;
        bool wide = range > 2;
        if (wide) {
            return corner || (dx + dz > range * 2 - 2 && jr_next_float(c->r) < c->t->hole_corner);
        }
        return corner && jr_next_float(c->r) < c->t->hole_corner;
    }
    case FOL_MEGA_PINE:
    case FOL_JUNGLE:
        if (dx + dz >= 7) {
            return true;
        }
        return dx * dx + dz * dz > range * range;
    case FOL_DARK_OAK:
        if (y == -1 && !large) {
            return dx == range && dz == range;
        }
        if (y == 1) {
            return dx + dz > range * 2 - 2;
        }
        return false;
    case FOL_FANCY: {
        float a = (float)dx + 0.5f;
        float b = (float)dz + 0.5f;
        return a * a + b * b > (float)(range * range);
    }
    }
    return false;
}

static bool should_skip_signed(TCtx *c, int dx, int y, int dz, int range, bool large)
{
    if (c->t->foliage_kind == FOL_DARK_OAK && y == 0 && large && (dx == -range || dx >= range) && (dz == -range || dz >= range)) {
        return true;
    }
    int ax, az;
    if (large) {
        ax = imin(iabs(dx), iabs(dx - 1));
        az = imin(iabs(dz), iabs(dz - 1));
    } else {
        ax = iabs(dx);
        az = iabs(dz);
    }
    return should_skip(c, ax, y, az, range, large);
}

static bool try_place_extension(TCtx *c, float chance, int bx, int by, int bz, int mx, int my, int mz)
{
    if (iabs(mx - bx) + iabs(my - by) + iabs(mz - bz) >= 7) {
        return false;
    }
    if (jr_next_float(c->r) > chance) {
        return false;
    }
    return try_place_leaf(c, mx, my, mz);
}

/* FoliagePlacer.placeLeavesRow */
static void place_leaves_row(TCtx *c, int px, int py, int pz, int range, int y, bool large)
{
    int i = large ? 1 : 0;
    for (int dx = -range; dx <= range + i; ++dx) {
        for (int dz = -range; dz <= range + i; ++dz) {
            if (!should_skip_signed(c, dx, y, dz, range, large)) {
                try_place_leaf(c, px + dx, py + y, pz + dz);
            }
        }
    }
}

/* FoliagePlacer.placeLeavesRowWithHangingLeavesBelow */
static void place_leaves_row_hanging(TCtx *c, int px, int py, int pz, int range, int y, bool large, float hang, float ext)
{
    place_leaves_row(c, px, py, pz, range, y, large);
    int i = large ? 1 : 0;
    int bx = px, by = py - 1, bz = pz;
    for (int d = 0; d < 4; ++d) {
        int cw = (d + 1) & 3; /* Direction.getClockWise: N -> E -> S -> W */
        bool cw_positive = HSTEP[cw][0] + HSTEP[cw][1] > 0;
        int j = cw_positive ? range + i : range;
        int mx = px + HSTEP[cw][0] * j - HSTEP[d][0] * range;
        int my = py + y - 1;
        int mz = pz + HSTEP[cw][1] * j - HSTEP[d][1] * range;
        for (int k = -range; k < range + i; ++k) {
            bool bl = ps_contains(&c->foliage, mx, my + 1, mz);
            if (bl && try_place_extension(c, hang, bx, by, bz, mx, my, mz)) {
                try_place_extension(c, ext, bx, by, bz, mx, my - 1, mz);
            }
            mx += HSTEP[d][0];
            mz += HSTEP[d][1];
        }
    }
}

static int foliage_height(TCtx *c, int tree_height)
{
    const JTreeCfg *t = c->t;
    switch (t->foliage_kind) {
    case FOL_BLOB:
    case FOL_BUSH:
    case FOL_JUNGLE:
    case FOL_FANCY:
        return t->blob_height;
    case FOL_DARK_OAK:
        return 4;
    case FOL_MEGA_PINE:
    case FOL_RANDOM_SPREAD:
    case FOL_CHERRY:
        return jf_sample_int(&t->pine_height, c->r);
    case FOL_SPRUCE:
        return imax(4, tree_height - jf_sample_int(&t->spruce_trunk_h, c->r));
    case FOL_PINE:
        return jf_sample_int(&t->pine_height, c->r);
    case FOL_ACACIA:
        return 0;
    }
    return 0;
}

static int foliage_radius(TCtx *c, int trunk_height)
{
    int r = jf_sample_int(&c->t->radius, c->r);
    if (c->t->foliage_kind == FOL_PINE) {
        r += jr_next_int_bound(c->r, imax(trunk_height + 1, 1));
    }
    return r;
}

/* FoliagePlacer.createFoliage: offset is sampled first, then the placer's doCreateFoliage runs */
static void create_foliage(TCtx *c, int tree_height, const Attach *a, int foliage_h, int leaf_r)
{
    (void)tree_height;
    int offset = jf_sample_int(&c->t->offset, c->r);
    switch (c->t->foliage_kind) {
    case FOL_BLOB:
        for (int y = offset; y >= offset - foliage_h; --y) {
            int r = imax(leaf_r + a->radius_offset - 1 - y / 2, 0);
            place_leaves_row(c, a->x, a->y, a->z, r, y, a->double_trunk);
        }
        break;
    case FOL_BUSH:
        for (int y = offset; y >= offset - foliage_h; --y) {
            int r = leaf_r + a->radius_offset - 1 - y;
            place_leaves_row(c, a->x, a->y, a->z, r, y, a->double_trunk);
        }
        break;
    case FOL_SPRUCE: {
        int i = jr_next_int_bound(c->r, 2);
        int j = 1;
        int k = 0;
        for (int y = offset; y >= -foliage_h; --y) {
            place_leaves_row(c, a->x, a->y, a->z, i, y, a->double_trunk);
            if (i >= j) {
                i = k;
                k = 1;
                j = imin(j + 1, leaf_r + a->radius_offset);
            } else {
                ++i;
            }
        }
        break;
    }
    case FOL_PINE: {
        int i = 0;
        for (int y = offset; y >= offset - foliage_h; --y) {
            place_leaves_row(c, a->x, a->y, a->z, i, y, a->double_trunk);
            if (i >= 1 && y == offset - foliage_h + 1) {
                --i;
            } else if (i < leaf_r + a->radius_offset) {
                ++i;
            }
        }
        break;
    }
    case FOL_MEGA_PINE: {
        int prev = 0;
        for (int j = a->y - foliage_h + offset; j <= a->y + offset; ++j) {
            int k = a->y - j;
            int l = leaf_r + a->radius_offset + mth_floor_f((float)k / (float)foliage_h * 3.5f);
            int m = (k > 0 && l == prev && (j & 1) == 0) ? l + 1 : l;
            place_leaves_row(c, a->x, j, a->z, m, 0, a->double_trunk);
            prev = l;
        }
        break;
    }
    case FOL_JUNGLE: {
        int i = a->double_trunk ? foliage_h : 1 + jr_next_int_bound(c->r, 2);
        for (int j = offset; j >= offset - i; --j) {
            int k = leaf_r + a->radius_offset + 1 - j;
            place_leaves_row(c, a->x, a->y, a->z, k, j, a->double_trunk);
        }
        break;
    }
    case FOL_DARK_OAK: {
        int py = a->y + offset;
        bool large = a->double_trunk;
        if (large) {
            place_leaves_row(c, a->x, py, a->z, leaf_r + 2, -1, large);
            place_leaves_row(c, a->x, py, a->z, leaf_r + 3, 0, large);
            place_leaves_row(c, a->x, py, a->z, leaf_r + 2, 1, large);
            if (jr_next_boolean(c->r)) {
                place_leaves_row(c, a->x, py, a->z, leaf_r, 2, large);
            }
        } else {
            place_leaves_row(c, a->x, py, a->z, leaf_r + 2, -1, large);
            place_leaves_row(c, a->x, py, a->z, leaf_r + 1, 0, large);
        }
        break;
    }
    case FOL_CHERRY: {
        bool large = a->double_trunk;
        int py = a->y + offset;
        int r = leaf_r + a->radius_offset - 1;
        place_leaves_row(c, a->x, py, a->z, r - 2, foliage_h - 3, large);
        place_leaves_row(c, a->x, py, a->z, r - 1, foliage_h - 4, large);
        for (int yy = foliage_h - 5; yy >= 0; --yy) {
            place_leaves_row(c, a->x, py, a->z, r, yy, large);
        }
        place_leaves_row_hanging(c, a->x, py, a->z, r, -1, large, c->t->hang_chance, c->t->hang_ext);
        place_leaves_row_hanging(c, a->x, py, a->z, r - 1, -2, large, c->t->hang_chance, c->t->hang_ext);
        break;
    }
    case FOL_RANDOM_SPREAD:
        for (int i = 0; i < c->t->spread_attempts; ++i) {
            int dx = jr_next_int_bound(c->r, leaf_r);
            dx -= jr_next_int_bound(c->r, leaf_r);
            int dy = jr_next_int_bound(c->r, foliage_h);
            dy -= jr_next_int_bound(c->r, foliage_h);
            int dz = jr_next_int_bound(c->r, leaf_r);
            dz -= jr_next_int_bound(c->r, leaf_r);
            try_place_leaf(c, a->x + dx, a->y + dy, a->z + dz);
        }
        break;
    case FOL_FANCY:
        for (int y = offset; y >= offset - foliage_h; --y) {
            int r = leaf_r + ((y != offset && y != offset - foliage_h) ? 1 : 0);
            place_leaves_row(c, a->x, a->y, a->z, r, y, a->double_trunk);
        }
        break;
    case FOL_ACACIA: {
        bool large = a->double_trunk;
        int py = a->y + offset;
        place_leaves_row(c, a->x, py, a->z, leaf_r + a->radius_offset, -1 - foliage_h, large);
        place_leaves_row(c, a->x, py, a->z, leaf_r - 1, -foliage_h, large);
        place_leaves_row(c, a->x, py, a->z, leaf_r + a->radius_offset - 1, 0, large);
        break;
    }
    }
}

/* ---- tree decorators ---------------------------------------------------------------------------------------- */

typedef struct PList {
    int *x, *y, *z;
    int n;
} PList;

typedef struct SortKey {
    int y, idx;
} SortKey;

static int sort_key_cmp(const void *a, const void *b)
{
    const SortKey *p = (const SortKey *)a, *q = (const SortKey *)b;
    if (p->y != q->y) {
        return p->y < q->y ? -1 : 1;
    }
    return p->idx < q->idx ? -1 : (p->idx > q->idx ? 1 : 0);
}

/* new ObjectArrayList(set) sorted (stable) by Y: the HashSet iteration order breaks ties */
static void plist_from_set(const PosSet *s, PList *out)
{
    out->n = s->size;
    out->x = (int *)malloc(((size_t)s->size + 1) * sizeof(int));
    out->y = (int *)malloc(((size_t)s->size + 1) * sizeof(int));
    out->z = (int *)malloc(((size_t)s->size + 1) * sizeof(int));
    SortKey *keys = (SortKey *)malloc(((size_t)s->size + 1) * sizeof(SortKey));
    int n = 0;
    for (int b = 0; b < s->tab_cap; ++b) {
        for (int i = s->tab[b]; i >= 0; i = s->e[i].next) {
            out->x[n] = s->e[i].x;
            out->y[n] = s->e[i].y;
            out->z[n] = s->e[i].z;
            keys[n].y = s->e[i].y;
            keys[n].idx = n;
            ++n;
        }
    }
    qsort(keys, (size_t)n, sizeof(SortKey), sort_key_cmp);
    int *tx = (int *)malloc(((size_t)n + 1) * sizeof(int));
    int *tz = (int *)malloc(((size_t)n + 1) * sizeof(int));
    int *ty = (int *)malloc(((size_t)n + 1) * sizeof(int));
    for (int i = 0; i < n; ++i) {
        tx[i] = out->x[keys[i].idx];
        ty[i] = out->y[keys[i].idx];
        tz[i] = out->z[keys[i].idx];
    }
    free(out->x);
    free(out->y);
    free(out->z);
    out->x = tx;
    out->y = ty;
    out->z = tz;
    free(keys);
}

static void plist_free(PList *l)
{
    free(l->x);
    free(l->y);
    free(l->z);
}

static void deco_set(TCtx *c, int x, int y, int z, int state)
{
    ps_add(&c->deco, x, y, z);
    jf_level_set(c->l, x, y, z, state);
}

static bool deco_is_air(const TCtx *c, int x, int y, int z)
{
    return jblock_is_air(jf_level_get(c->l, x, y, z));
}

/* TreeFeature.getLowestTrunkOrRootOfTree */
static void lowest_trunk_or_root(const PList *roots, const PList *logs, PList *out)
{
    int n = 0;
    out->x = (int *)malloc(((size_t)roots->n + (size_t)logs->n + 1) * sizeof(int));
    out->y = (int *)malloc(((size_t)roots->n + (size_t)logs->n + 1) * sizeof(int));
    out->z = (int *)malloc(((size_t)roots->n + (size_t)logs->n + 1) * sizeof(int));
    const PList *first = NULL, *second = NULL;
    if (roots->n == 0) {
        first = logs;
    } else if (logs->n > 0 && roots->y[0] == logs->y[0]) {
        first = logs;
        second = roots;
    } else {
        first = roots;
    }
    for (int pass = 0; pass < 2; ++pass) {
        const PList *src = pass == 0 ? first : second;
        for (int i = 0; src != NULL && i < src->n; ++i) {
            out->x[n] = src->x[i];
            out->y[n] = src->y[i];
            out->z[n] = src->z[i];
            ++n;
        }
    }
    out->n = n;
}

/* BlockStateBase.isSolidRender for the blocks that occur around a tree */
static bool solid_render(const TCtx *c, int b)
{
    if (jblock_is_air(b) || jblock_is_fluid(b) || !jblock_blocks_motion(b) || c->tags->leaves[b & 255]) {
        return false;
    }
    const char *n = jfill_block_name((JFillBlock)b);
    return strcmp(n, "minecraft:ice") != 0 && strcmp(n, "minecraft:powder_snow") != 0 && strcmp(n, "minecraft:snow") != 0;
}

static void place_on_ground_above(TCtx *c, const Deco *d, int x, int y, int z)
{
    int above = jf_level_get(c->l, x, y + 1, z);
    int vine = jfill_block_from_name("minecraft:vine");
    if ((jblock_is_air(above) || above == vine) && solid_render(c, jf_level_get(c->l, x, y, z)) &&
        jf_level_height(c->l, 3, x, z) <= y + 1) {
        deco_set(c, x, y + 1, z, jf_provider_state(&d->provider, c->r));
    }
}

/* AlterGroundDecorator.placeBlockAt: the first grass/dirt block between y + 2 and y - 3 becomes the provider's state */
static void alter_block_at(TCtx *c, const Deco *d, int x, int y, int z)
{
    for (int i = 2; i >= -3; --i) {
        int b = jf_level_get(c->l, x, y + i, z);
        if (c->tags->dirt[b & 255]) {
            deco_set(c, x, y + i, z, jf_provider_state(&d->provider, c->r));
            break;
        }
        if (!jblock_is_air(b) && i < 0) {
            break;
        }
    }
}

static void alter_circle(TCtx *c, const Deco *d, int x, int y, int z)
{
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            if (iabs(i) != 2 || iabs(j) != 2) {
                alter_block_at(c, d, x + i, y, z + j);
            }
        }
    }
}

static void run_decorator(TCtx *c, const Deco *d, const PList *roots, const PList *logs, const PList *leaves)
{
    JrRandom *r = c->r;
    switch (d->kind) {
    case DECO_BEEHIVE: {
        if (logs->n == 0) {
            return;
        }
        if (jr_next_float(r) >= d->probability) {
            return;
        }
        int target;
        if (leaves->n > 0) {
            target = imax(leaves->y[0] - 1, logs->y[0] + 1);
        } else {
            target = imin(logs->y[0] + 1 + jr_next_int_bound(r, 3), logs->y[logs->n - 1]);
        }
        /* SPAWN_DIRECTIONS: EAST, SOUTH, WEST (every horizontal direction except the one opposite the nest facing) */
        static const int SPAWN[3][2] = {{1, 0}, {0, 1}, {-1, 0}};
        int *cx = (int *)malloc(((size_t)logs->n * 3 + 1) * sizeof(int));
        int *cy = (int *)malloc(((size_t)logs->n * 3 + 1) * sizeof(int));
        int *cz = (int *)malloc(((size_t)logs->n * 3 + 1) * sizeof(int));
        int n = 0;
        for (int i = 0; i < logs->n; ++i) {
            if (logs->y[i] != target) {
                continue;
            }
            for (int k = 0; k < 3; ++k) {
                cx[n] = logs->x[i] + SPAWN[k][0];
                cy[n] = logs->y[i];
                cz[n] = logs->z[i] + SPAWN[k][1];
                ++n;
            }
        }
        if (n > 0) {
            /* Util.shuffle */
            for (int j = n; j > 1; --j) {
                int k = jr_next_int_bound(r, j);
                int tx = cx[j - 1], ty = cy[j - 1], tz = cz[j - 1];
                cx[j - 1] = cx[k];
                cy[j - 1] = cy[k];
                cz[j - 1] = cz[k];
                cx[k] = tx;
                cy[k] = ty;
                cz[k] = tz;
            }
            for (int i = 0; i < n; ++i) {
                if (deco_is_air(c, cx[i], cy[i], cz[i]) && deco_is_air(c, cx[i], cy[i], cz[i] + 1)) {
                    deco_set(c, cx[i], cy[i], cz[i], jfill_block_register("minecraft:bee_nest"));
                    /* the bee nest block entity stores 2 + nextInt(2) bees, each with nextInt(599) ticks in the hive */
                    int bees = 2 + jr_next_int_bound(r, 2);
                    for (int b = 0; b < bees; ++b) {
                        (void)jr_next_int_bound(r, 599);
                    }
                    break;
                }
            }
        }
        free(cx);
        free(cy);
        free(cz);
        break;
    }
    case DECO_TRUNK_VINE: {
        int vine = jfill_block_register("minecraft:vine");
        for (int i = 0; i < logs->n; ++i) {
            int x = logs->x[i], y = logs->y[i], z = logs->z[i];
            if (jr_next_int_bound(r, 3) > 0 && deco_is_air(c, x - 1, y, z)) {
                deco_set(c, x - 1, y, z, vine);
            }
            if (jr_next_int_bound(r, 3) > 0 && deco_is_air(c, x + 1, y, z)) {
                deco_set(c, x + 1, y, z, vine);
            }
            if (jr_next_int_bound(r, 3) > 0 && deco_is_air(c, x, y, z - 1)) {
                deco_set(c, x, y, z - 1, vine);
            }
            if (jr_next_int_bound(r, 3) > 0 && deco_is_air(c, x, y, z + 1)) {
                deco_set(c, x, y, z + 1, vine);
            }
        }
        break;
    }
    case DECO_LEAVE_VINE: {
        int vine = jfill_block_register("minecraft:vine");
        static const int SIDE[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (int i = 0; i < leaves->n; ++i) {
            for (int k = 0; k < 4; ++k) {
                int x = leaves->x[i] + SIDE[k][0], y = leaves->y[i], z = leaves->z[i] + SIDE[k][1];
                if (jr_next_float(r) < d->probability && deco_is_air(c, x, y, z)) {
                    /* addHangingVine */
                    deco_set(c, x, y, z, vine);
                    int left = 4;
                    --y;
                    while (deco_is_air(c, x, y, z) && left > 0) {
                        deco_set(c, x, y, z, vine);
                        --y;
                        --left;
                    }
                }
            }
        }
        break;
    }
    case DECO_COCOA: {
        if (jr_next_float(r) >= d->probability) {
            return;
        }
        if (logs->n == 0) {
            return;
        }
        int base = logs->y[0];
        int cocoa = jfill_block_register("minecraft:cocoa");
        for (int i = 0; i < logs->n; ++i) {
            if (logs->y[i] - base > 2) {
                continue;
            }
            for (int dir = 0; dir < 4; ++dir) {
                if (jr_next_float(r) <= 0.25f) {
                    /* opposite of the direction: placed on the side the cocoa faces away from */
                    int x = logs->x[i] - HSTEP[dir][0], z = logs->z[i] - HSTEP[dir][1];
                    if (deco_is_air(c, x, logs->y[i], z)) {
                        (void)jr_next_int_bound(r, 3); /* age */
                        deco_set(c, x, logs->y[i], z, cocoa);
                    }
                }
            }
        }
        break;
    }
    case DECO_ALTER_GROUND: {
        PList low;
        lowest_trunk_or_root(roots, logs, &low);
        if (low.n > 0) {
            int y = low.y[0];
            for (int e = 0; e < low.n; ++e) {
                if (low.y[e] != y) {
                    continue;
                }
                int px = low.x[e], pz = low.z[e];
                alter_circle(c, d, px - 1, y, pz - 1);
                alter_circle(c, d, px + 2, y, pz - 1);
                alter_circle(c, d, px - 1, y, pz + 2);
                alter_circle(c, d, px + 2, y, pz + 2);
                for (int i = 0; i < 5; ++i) {
                    int j = jr_next_int_bound(r, 64);
                    int k = j % 8;
                    int l = j / 8;
                    if (k == 0 || k == 7 || l == 0 || l == 7) {
                        alter_circle(c, d, px - 3 + k, y, pz - 3 + l);
                    }
                }
            }
        }
        free(low.x);
        free(low.y);
        free(low.z);
        break;
    }
    case DECO_PLACE_ON_GROUND: {
        PList low;
        lowest_trunk_or_root(roots, logs, &low);
        if (low.n > 0) {
            int y = low.y[0];
            int min_x = low.x[0], max_x = low.x[0], min_z = low.z[0], max_z = low.z[0];
            for (int i = 0; i < low.n; ++i) {
                if (low.y[i] == y) {
                    min_x = imin(min_x, low.x[i]);
                    max_x = imax(max_x, low.x[i]);
                    min_z = imin(min_z, low.z[i]);
                    max_z = imax(max_z, low.z[i]);
                }
            }
            int bx0 = min_x - d->radius, bx1 = max_x + d->radius;
            int by0 = y - d->height, by1 = y + d->height;
            int bz0 = min_z - d->radius, bz1 = max_z + d->radius;
            for (int i = 0; i < d->tries; ++i) {
                int px = jr_next_int_bound(r, bx1 - bx0 + 1) + bx0;
                int py = jr_next_int_bound(r, by1 - by0 + 1) + by0;
                int pz = jr_next_int_bound(r, bz1 - bz0 + 1) + bz0;
                place_on_ground_above(c, d, px, py, pz);
            }
        }
        free(low.x);
        free(low.y);
        free(low.z);
        break;
    }
    }
}

/* ---- TreeFeature ----------------------------------------------------------------------------------------- */

static int size_at_height(const JTreeCfg *t, int tree_height, int y)
{
    if (t->size_kind == 0) {
        return y < t->size_limit ? t->size_lower : t->size_upper;
    }
    if (y < t->size_limit) {
        return t->size_lower;
    }
    return y >= tree_height - t->size_upper_limit ? t->size_upper : t->size_middle;
}

/* TreeFeature.getMaxFreeTreeHeight */
static int max_free_tree_height(TCtx *c, int max_height, int ox, int oy, int oz)
{
    int vine = jfill_block_from_name("minecraft:vine");
    for (int y = 0; y <= max_height + 1; ++y) {
        int r = size_at_height(c->t, max_height, y);
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                int px = ox + dx, py = oy + y, pz = oz + dz;
                if (!trunk_is_free(c, px, py, pz) ||
                    (!c->t->ignore_vines && vine >= 0 && jf_level_get(c->l, px, py, pz) == vine)) {
                    return y - 2;
                }
            }
        }
    }
    return max_height;
}

static bool do_place(TCtx *c, int ox, int oy, int oz)
{
    const JTreeCfg *t = c->t;
    int tree_height = t->base_height + jr_next_int_bound(c->r, t->rand_a + 1);
    tree_height += jr_next_int_bound(c->r, t->rand_b + 1);
    int foliage_h = foliage_height(c, tree_height);
    int trunk_len = tree_height - foliage_h;
    int leaf_r = foliage_radius(c, trunk_len);
    int min_y = oy, max_y = oy + tree_height + 1;
    if (min_y < JF_MIN_Y + 1 || max_y > JF_MIN_Y + JF_HEIGHT - 1 + 1) {
        return false;
    }
    int checked = max_free_tree_height(c, tree_height, ox, oy, oz);
    if (checked < tree_height && (t->min_clipped < 0 || checked < t->min_clipped)) {
        return false;
    }
    Attach attach[MAX_ATTACH];
    int n = 0;
    switch (t->trunk_kind) {
    case TRUNK_STRAIGHT:
        n = place_trunk_straight(c, checked, ox, oy, oz, attach);
        break;
    case TRUNK_FORKING:
        n = place_trunk_forking(c, checked, ox, oy, oz, attach);
        break;
    case TRUNK_GIANT:
        n = place_trunk_giant(c, checked, ox, oy, oz, attach);
        break;
    case TRUNK_MEGA_JUNGLE:
        n = place_trunk_mega_jungle(c, checked, ox, oy, oz, attach);
        break;
    case TRUNK_DARK_OAK:
        n = place_trunk_dark_oak(c, checked, ox, oy, oz, attach);
        break;
    case TRUNK_FANCY:
        n = place_trunk_fancy(c, checked, ox, oy, oz, attach);
        break;
    case TRUNK_BENDING:
        n = place_trunk_bending(c, checked, ox, oy, oz, attach);
        break;
    case TRUNK_CHERRY:
        n = place_trunk_cherry(c, checked, ox, oy, oz, attach);
        break;
    }
    for (int i = 0; i < n; ++i) {
        create_foliage(c, checked, &attach[i], foliage_h, leaf_r);
    }
    return true;
}

bool jtree_place(Level *l, const JTreeCfg *t, int x, int y, int z)
{
    if (t == NULL) {
        return false;
    }
    TCtx c;
    memset(&c, 0, sizeof(c));
    c.l = l;
    c.t = t;
    c.r = l->rnd;
    c.tags = jf_tree_tags(l);
    bool placed = do_place(&c, x, y, z);
    bool result = placed && (c.trunks.size > 0 || c.foliage.size > 0);
    if (result && t->deco_count > 0) {
        PList roots, logs, leaves;
        plist_from_set(&c.roots, &roots);
        plist_from_set(&c.trunks, &logs);
        plist_from_set(&c.foliage, &leaves);
        for (int i = 0; i < t->deco_count; ++i) {
            run_decorator(&c, &t->decos[i], &roots, &logs, &leaves);
        }
        plist_free(&roots);
        plist_free(&logs);
        plist_free(&leaves);
    }
    ps_free(&c.roots);
    ps_free(&c.trunks);
    ps_free(&c.foliage);
    ps_free(&c.deco);
    return result;
}
