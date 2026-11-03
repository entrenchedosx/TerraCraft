#include "world/jcarver.h"

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

#define MIN_GEN_Y (-64)
#define GEN_DEPTH 384
#define CARVER_RANGE 4                       /* WorldCarver.getRange() */
#define CARVE_TOP_MARGIN 7                   /* top rows left alone (not upgrading) */
#define CAVE_BOUND 15                        /* CaveWorldCarver.getCaveBound() */
#define TWO_PI_F 6.2831855f
#define PI_F 3.1415927f
#define HALF_PI_F 1.5707964f

/* ---- Mth ------------------------------------------------------------------- */

static float g_sin[65536];
static bool g_sin_ready;

static void sin_init(void)
{
    if (g_sin_ready) {
        return;
    }
    for (int i = 0; i < 65536; ++i) {
        g_sin[i] = (float)sin((double)i * 3.14159265358979323846 * 2.0 / 65536.0);
    }
    g_sin_ready = true;
}

/* Mth.sin(double) / Mth.cos(double). */
static float mth_sin(double d)
{
    return g_sin[(int)((long long)(d * 10430.378350470453) & 65535)];
}

static float mth_cos(double d)
{
    return g_sin[(int)((long long)(d * 10430.378350470453 + 16384.0) & 65535)];
}

static int mth_floor(double v)
{
    int i = (int)v;
    return v < (double)i ? i - 1 : i;
}

/* ---- configuration ------------------------------------------------------------ */

typedef enum FpKind { FP_CONST, FP_UNIFORM, FP_TRAPEZOID } FpKind;

typedef struct FloatProvider {
    FpKind kind;
    float a, b, c; /* const: a. uniform: min=a, max=b. trapezoid: min=a, max=b, plateau=c. */
} FloatProvider;

typedef struct Anchor {
    int above_bottom; /* 1: above_bottom, 0: absolute. */
    int value;
} Anchor;

typedef enum CarverKind { CARVER_CAVE, CARVER_CANYON } CarverKind;

typedef struct CarverCfg {
    CarverKind kind;
    float probability;
    Anchor y_min, y_max; /* UniformHeight */
    FloatProvider y_scale;
    Anchor lava_level;
    /* cave */
    FloatProvider hr_mult, vr_mult, floor_level;
    /* canyon */
    FloatProvider vertical_rotation, distance_factor, thickness, hr_factor;
    int width_smoothness;
    float vr_default, vr_center;
} CarverCfg;

struct JCarvers {
    int count;
    CarverCfg cfg[JCARVER_MAX];
    bool replaceable[256];
};

static bool parse_fp(const JJson *j, FloatProvider *out)
{
    if (j == NULL) {
        return false;
    }
    if (j->type == JJ_NUM) {
        out->kind = FP_CONST;
        out->a = jj_float(j);
        return true;
    }
    const JJson *type = jj_get(j, "type");
    if (type == NULL || type->text == NULL) {
        return false;
    }
    if (strcmp(type->text, "minecraft:uniform") == 0) {
        const JJson *lo = jj_get(j, "min_inclusive");
        const JJson *hi = jj_get(j, "max_exclusive");
        if (lo == NULL || hi == NULL) {
            return false;
        }
        out->kind = FP_UNIFORM;
        out->a = jj_float(lo);
        out->b = jj_float(hi);
        return true;
    }
    if (strcmp(type->text, "minecraft:trapezoid") == 0) {
        const JJson *lo = jj_get(j, "min");
        const JJson *hi = jj_get(j, "max");
        const JJson *pl = jj_get(j, "plateau");
        if (lo == NULL || hi == NULL || pl == NULL) {
            return false;
        }
        out->kind = FP_TRAPEZOID;
        out->a = jj_float(lo);
        out->b = jj_float(hi);
        out->c = jj_float(pl);
        return true;
    }
    if (strcmp(type->text, "minecraft:constant") == 0) {
        const JJson *v = jj_get(j, "value");
        if (v == NULL) {
            return false;
        }
        out->kind = FP_CONST;
        out->a = jj_float(v);
        return true;
    }
    return false;
}

static bool parse_anchor(const JJson *j, Anchor *out)
{
    const JJson *v = jj_get(j, "absolute");
    if (v != NULL) {
        out->above_bottom = 0;
        out->value = (int)jj_double(v);
        return true;
    }
    v = jj_get(j, "above_bottom");
    if (v != NULL) {
        out->above_bottom = 1;
        out->value = (int)jj_double(v);
        return true;
    }
    return false; /* below_top is not used by any vanilla carver */
}

static int anchor_y(const Anchor *a)
{
    return a->above_bottom ? MIN_GEN_Y + a->value : a->value;
}

static bool parse_carver(const JJson *root, CarverCfg *c)
{
    const JJson *type = jj_get(root, "type");
    const JJson *cfg = jj_get(root, "config");
    if (type == NULL || type->text == NULL || cfg == NULL) {
        return false;
    }
    memset(c, 0, sizeof(*c));
    if (strcmp(type->text, "minecraft:cave") == 0) {
        c->kind = CARVER_CAVE;
    } else if (strcmp(type->text, "minecraft:canyon") == 0) {
        c->kind = CARVER_CANYON;
    } else {
        return false;
    }
    const JJson *p = jj_get(cfg, "probability");
    const JJson *y = jj_get(cfg, "y");
    const JJson *yt = jj_get(y, "type");
    if (p == NULL || y == NULL || yt == NULL || yt->text == NULL || strcmp(yt->text, "minecraft:uniform") != 0) {
        return false;
    }
    c->probability = jj_float(p);
    if (!parse_anchor(jj_get(y, "min_inclusive"), &c->y_min) || !parse_anchor(jj_get(y, "max_inclusive"), &c->y_max) ||
        !parse_fp(jj_get(cfg, "yScale"), &c->y_scale) || !parse_anchor(jj_get(cfg, "lava_level"), &c->lava_level)) {
        return false;
    }
    if (c->kind == CARVER_CAVE) {
        return parse_fp(jj_get(cfg, "horizontal_radius_multiplier"), &c->hr_mult) &&
               parse_fp(jj_get(cfg, "vertical_radius_multiplier"), &c->vr_mult) &&
               parse_fp(jj_get(cfg, "floor_level"), &c->floor_level);
    }
    const JJson *shape = jj_get(cfg, "shape");
    const JJson *ws = jj_get(shape, "width_smoothness");
    const JJson *vd = jj_get(shape, "vertical_radius_default_factor");
    const JJson *vc = jj_get(shape, "vertical_radius_center_factor");
    if (shape == NULL || ws == NULL || vd == NULL || vc == NULL) {
        return false;
    }
    c->width_smoothness = (int)jj_double(ws);
    c->vr_default = jj_float(vd);
    c->vr_center = jj_float(vc);
    return parse_fp(jj_get(cfg, "vertical_rotation"), &c->vertical_rotation) &&
           parse_fp(jj_get(shape, "distance_factor"), &c->distance_factor) &&
           parse_fp(jj_get(shape, "thickness"), &c->thickness) &&
           parse_fp(jj_get(shape, "horizontal_radius_factor"), &c->hr_factor);
}

/* ---- block tag resolution ------------------------------------------------------- */

typedef struct NameSet {
    char **names;
    int count, cap;
} NameSet;

static void nameset_add(NameSet *s, const char *name)
{
    if (s->count == s->cap) {
        s->cap = s->cap != 0 ? s->cap * 2 : 32;
        s->names = (char **)realloc(s->names, (size_t)s->cap * sizeof(char *));
    }
    size_t n = strlen(name) + 1;
    s->names[s->count] = (char *)malloc(n);
    memcpy(s->names[s->count], name, n);
    s->count++;
}

static bool nameset_has(const NameSet *s, const char *name)
{
    for (int i = 0; i < s->count; ++i) {
        if (strcmp(s->names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static void nameset_free(NameSet *s)
{
    for (int i = 0; i < s->count; ++i) {
        free(s->names[i]);
    }
    free(s->names);
}

static bool resolve_tag(const char *root, const char *tag, NameSet *out, int depth)
{
    if (depth > 8 || strncmp(tag, "minecraft:", 10) != 0) {
        return false;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/data/minecraft/tags/block/%s.json", root, tag + 10);
    JJson *j = jj_parse_file(path);
    const JJson *values = jj_get(j, "values");
    if (values == NULL || values->type != JJ_ARR) {
        jj_free(j);
        return false;
    }
    bool ok = true;
    for (int i = 0; i < values->count && ok; ++i) {
        const JJson *v = values->items[i];
        if (v->type != JJ_STR || v->text == NULL) {
            ok = false;
        } else if (v->text[0] == '#') {
            ok = resolve_tag(root, v->text + 1, out, depth + 1);
        } else {
            nameset_add(out, v->text);
        }
    }
    jj_free(j);
    return ok;
}

JCarvers *jcarvers_load(const char *datapack_root)
{
    if (datapack_root == NULL) {
        return NULL;
    }
    sin_init();
    char path[1024];
    snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/biome/plains.json", datapack_root);
    JJson *biome = jj_parse_file(path);
    const JJson *list = jj_get(biome, "carvers");
    if (list == NULL || list->type != JJ_ARR || list->count < 1 || list->count > JCARVER_MAX) {
        jj_free(biome);
        return NULL;
    }
    JCarvers *cv = (JCarvers *)calloc(1, sizeof(JCarvers));
    if (cv == NULL) {
        jj_free(biome);
        return NULL;
    }
    bool ok = true;
    for (int i = 0; i < list->count && ok; ++i) {
        const JJson *id = list->items[i];
        if (id->type != JJ_STR || id->text == NULL || strncmp(id->text, "minecraft:", 10) != 0) {
            ok = false;
            break;
        }
        snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/configured_carver/%s.json", datapack_root, id->text + 10);
        JJson *j = jj_parse_file(path);
        ok = j != NULL && parse_carver(j, &cv->cfg[i]);
        jj_free(j);
        cv->count++;
    }
    jj_free(biome);
    NameSet tags = {0};
    if (ok) {
        ok = resolve_tag(datapack_root, "minecraft:overworld_carver_replaceables", &tags, 0);
    }
    if (ok) {
        for (int b = 0; b < jfill_block_count(); ++b) {
            cv->replaceable[b] = nameset_has(&tags, jfill_block_name((JFillBlock)b));
        }
    }
    nameset_free(&tags);
    if (!ok) {
        free(cv);
        return NULL;
    }
    return cv;
}

void jcarvers_free(JCarvers *cv)
{
    free(cv);
}

int jcarvers_count(const JCarvers *cv)
{
    return cv != NULL ? cv->count : 0;
}

/* ---- providers ------------------------------------------------------------------ */

static float fp_sample(const FloatProvider *p, JrRandom *r)
{
    switch (p->kind) {
    case FP_UNIFORM: {
        float f = jr_next_float(r);
        return f * (p->b - p->a) + p->a; /* Mth.randomBetween */
    }
    case FP_TRAPEZOID: {
        float range = p->b - p->a;
        float edge = (range - p->c) / 2.0f;
        float mid = range - edge;
        float f1 = jr_next_float(r);
        float f2 = jr_next_float(r);
        return p->a + f1 * mid + f2 * edge;
    }
    default:
        return p->a;
    }
}

/* UniformHeight.sample */
static int height_sample(const CarverCfg *c, JrRandom *r)
{
    int lo = anchor_y(&c->y_min);
    int hi = anchor_y(&c->y_max);
    if (lo > hi) {
        return lo;
    }
    return jr_next_int_bound(r, hi - lo + 1) + lo;
}

/* ---- carving ----------------------------------------------------------------------- */

typedef struct Skip {
    CarverKind kind;
    double floor_level;     /* cave */
    const float *width;     /* canyon: GEN_DEPTH width factors */
} Skip;

typedef struct Ctx {
    const CarverCfg *cfg;
    const bool *replaceable;
    JAquifer *aquifer;
    uint8_t *blocks;
    uint8_t *mask;
    int min_x, min_z;
    const JCarveSurface *surface;
    bool top_flag;  /* the MutableBoolean carveEllipsoid creates per (x, z) column */
} Ctx;

static bool skip_block(const Skip *s, double dx, double dy, double dz, int y)
{
    if (s->kind == CARVER_CAVE) {
        if (dy <= s->floor_level) {
            return true;
        }
        return dx * dx + dy * dy + dz * dz >= 1.0;
    }
    int k = y - MIN_GEN_Y;
    return (dx * dx + dz * dz) * (double)s->width[k - 1] + dy * dy / 6.0 >= 1.0;
}

/* ChunkAccess.setBlockState: block array plus both generation heightmaps. */
static void chunk_set(Ctx *c, int lx, int y, int lz, int state)
{
    c->blocks[JFILL_INDEX(lx, lz, y)] = (uint8_t)state;
    if (c->surface != NULL && c->surface->heightmaps != NULL) {
        jheightmaps_update(c->surface->heightmaps, c->blocks, lx, y, lz, state);
    }
}

/* WorldCarver.carveBlock. With a surface context, a carved grass/mycelium column
 * re-runs the surface rules for the dirt below (CarvingContext.topMaterial). */
static bool carve_block(Ctx *c, int lx, int y, int lz, int x, int z)
{
    int idx = JFILL_INDEX(lx, lz, y);
    int cur = c->blocks[idx];
    if (cur == JB_GRASS_BLOCK || cur == JB_MYCELIUM) {
        c->top_flag = true;
    }
    if (!c->replaceable[cur]) {
        return false;
    }
    int state;
    if (y <= anchor_y(&c->cfg->lava_level)) {
        state = JB_LAVA;
    } else {
        state = jaquifer_substance(c->aquifer, x, y, z, 0.0);
        if (state < 0) {
            return false;
        }
    }
    chunk_set(c, lx, y, lz, state);
    if (c->top_flag && c->surface != NULL && c->surface->top_material != NULL && y - 1 >= MIN_GEN_Y &&
        c->blocks[JFILL_INDEX(lx, lz, y - 1)] == JB_DIRT) {
        int top = c->surface->top_material(c->surface->user, x, y - 1, z, jblock_is_fluid(state));
        if (top >= 0) {
            chunk_set(c, lx, y - 1, lz, top);
        }
    }
    return true;
}

/* WorldCarver.carveEllipsoid */
static bool carve_ellipsoid(Ctx *c, double x, double y, double z, double hr, double vr, const Skip *skip)
{
    double mid_x = (double)(c->min_x + 8);
    double mid_z = (double)(c->min_z + 8);
    double reach = 16.0 + hr * 2.0;
    if (fabs(x - mid_x) > reach || fabs(z - mid_z) > reach) {
        return false;
    }
    int x0 = mth_floor(x - hr) - c->min_x - 1;
    if (x0 < 0) {
        x0 = 0;
    }
    int x1 = mth_floor(x + hr) - c->min_x;
    if (x1 > 15) {
        x1 = 15;
    }
    int y0 = mth_floor(y - vr) - 1;
    if (y0 < MIN_GEN_Y + 1) {
        y0 = MIN_GEN_Y + 1;
    }
    int y1 = mth_floor(y + vr) + 1;
    if (y1 > MIN_GEN_Y + GEN_DEPTH - 1 - CARVE_TOP_MARGIN) {
        y1 = MIN_GEN_Y + GEN_DEPTH - 1 - CARVE_TOP_MARGIN;
    }
    int z0 = mth_floor(z - hr) - c->min_z - 1;
    if (z0 < 0) {
        z0 = 0;
    }
    int z1 = mth_floor(z + hr) - c->min_z;
    if (z1 > 15) {
        z1 = 15;
    }
    bool carved = false;
    for (int q = x0; q <= x1; ++q) {
        int bx = c->min_x + q;
        double g = ((double)bx + 0.5 - x) / hr;
        for (int s = z0; s <= z1; ++s) {
            int bz = c->min_z + s;
            double h = ((double)bz + 0.5 - z) / hr;
            if (g * g + h * h >= 1.0) {
                continue;
            }
            c->top_flag = false;
            for (int u = y1; u > y0; --u) {
                double v = ((double)u - 0.5 - y) / vr;
                if (skip_block(skip, g, v, h, u)) {
                    continue;
                }
                int idx = JFILL_INDEX(q, s, u);
                if (c->mask[idx] != 0) {
                    continue;
                }
                c->mask[idx] = 1;
                carved |= carve_block(c, q, u, s, bx, bz);
            }
        }
    }
    return carved;
}

/* WorldCarver.canReach */
static bool can_reach(const Ctx *c, double x, double z, int index, int count, float width)
{
    double dx = x - (double)(c->min_x + 8);
    double dz = z - (double)(c->min_z + 8);
    double left = (double)(count - index);
    double limit = (double)(width + 2.0f + 16.0f);
    return dx * dx + dz * dz - left * left <= limit * limit;
}

static void cave_tunnel(Ctx *c, int64_t seed, double x, double y, double z, double hrm, double vrm, float thickness,
                        float yaw, float pitch, int step, int steps, double y_scale, const Skip *skip)
{
    JrRandom rnd;
    jr_legacy_init(&rnd, seed);
    int branch_point = jr_next_int_bound(&rnd, steps / 2) + steps / 4;
    bool steep = jr_next_int_bound(&rnd, 6) == 0;
    float s30 = 0.0f; /* yaw acceleration */
    float s31 = 0.0f; /* pitch acceleration */
    for (int k = step; k < steps; ++k) {
        float arg = (PI_F * (float)k) / (float)steps;
        double hr = 1.5 + (double)(mth_sin((double)arg) * thickness);
        double vr = hr * y_scale;
        float cp = mth_cos((double)pitch);
        x += (double)(mth_cos((double)yaw) * cp);
        y += (double)mth_sin((double)pitch);
        z += (double)(mth_sin((double)yaw) * cp);
        pitch *= steep ? 0.92f : 0.7f;
        pitch += s31 * 0.1f;
        yaw += s30 * 0.1f;
        s31 *= 0.9f;
        s30 *= 0.75f;
        float a = jr_next_float(&rnd);
        float b = jr_next_float(&rnd);
        float d = jr_next_float(&rnd);
        s31 += (a - b) * d * 2.0f;
        a = jr_next_float(&rnd);
        b = jr_next_float(&rnd);
        d = jr_next_float(&rnd);
        s30 += (a - b) * d * 4.0f;
        if (k == branch_point && thickness > 1.0f) {
            int64_t sa = jr_next_long(&rnd);
            float ta = jr_next_float(&rnd) * 0.5f + 0.5f;
            cave_tunnel(c, sa, x, y, z, hrm, vrm, ta, yaw - HALF_PI_F, pitch / 3.0f, k, steps, 1.0, skip);
            int64_t sb = jr_next_long(&rnd);
            float tb = jr_next_float(&rnd) * 0.5f + 0.5f;
            cave_tunnel(c, sb, x, y, z, hrm, vrm, tb, yaw + HALF_PI_F, pitch / 3.0f, k, steps, 1.0, skip);
            return;
        }
        if (jr_next_int_bound(&rnd, 4) == 0) {
            continue;
        }
        if (!can_reach(c, x, z, k, steps, thickness)) {
            return;
        }
        carve_ellipsoid(c, x, y, z, hr * hrm, vr * vrm, skip);
    }
}

/* CaveWorldCarver.carve. (start_x, start_z) is the block origin of the start chunk (the
 * ChunkPos argument); c->min_x / min_z is the chunk being carved (ChunkAccess.getPos()). */
static void cave_carve(Ctx *c, JrRandom *r, int start_x, int start_z)
{
    const CarverCfg *cfg = c->cfg;
    int range_blocks = (CARVER_RANGE * 2 - 1) * 16;
    int n1 = jr_next_int_bound(r, CAVE_BOUND) + 1;
    int n2 = jr_next_int_bound(r, n1) + 1;
    int count = jr_next_int_bound(r, n2);
    for (int i = 0; i < count; ++i) {
        double x = (double)(start_x + jr_next_int_bound(r, 16));
        double y = (double)height_sample(cfg, r);
        double z = (double)(start_z + jr_next_int_bound(r, 16));
        double hrm = (double)fp_sample(&cfg->hr_mult, r);
        double vrm = (double)fp_sample(&cfg->vr_mult, r);
        double floor_level = (double)fp_sample(&cfg->floor_level, r);
        Skip skip = {CARVER_CAVE, floor_level, NULL};
        int tunnels = 1;
        if (jr_next_int_bound(r, 4) == 0) {
            double y_scale = (double)fp_sample(&cfg->y_scale, r);
            float thickness = 1.0f + jr_next_float(r) * 6.0f;
            /* createRoom */
            double hr = 1.5 + (double)(mth_sin(1.5707963705062866) * thickness);
            double vr = hr * y_scale;
            carve_ellipsoid(c, x + 1.0, y, z, hr, vr, &skip);
            tunnels += jr_next_int_bound(r, 4);
        }
        for (int t = 0; t < tunnels; ++t) {
            float yaw = jr_next_float(r) * TWO_PI_F;
            float pitch = (jr_next_float(r) - 0.5f) / 4.0f;
            /* getThickness */
            float th = jr_next_float(r) * 2.0f;
            th = th + jr_next_float(r);
            if (jr_next_int_bound(r, 10) == 0) {
                float f1 = jr_next_float(r);
                float f2 = jr_next_float(r);
                th = th * (f1 * f2 * 3.0f + 1.0f);
            }
            int steps = range_blocks - jr_next_int_bound(r, range_blocks / 4);
            int64_t tseed = jr_next_long(r);
            cave_tunnel(c, tseed, x, y, z, hrm, vrm, th, yaw, pitch, 0, steps, 1.0, &skip);
        }
    }
}

/* CanyonWorldCarver.doCarve */
static void canyon_do_carve(Ctx *c, int64_t seed, double x, double y, double z, float thickness, float yaw, float pitch,
                            int step, int steps, double y_scale)
{
    const CarverCfg *cfg = c->cfg;
    JrRandom rnd;
    jr_legacy_init(&rnd, seed);
    float width[GEN_DEPTH];
    float f = 1.0f;
    for (int i = 0; i < GEN_DEPTH; ++i) {
        if (i == 0 || jr_next_int_bound(&rnd, cfg->width_smoothness) == 0) {
            float a = jr_next_float(&rnd);
            float b = jr_next_float(&rnd);
            f = 1.0f + a * b;
        }
        width[i] = f * f;
    }
    Skip skip = {CARVER_CANYON, 0.0, width};
    float s24 = 0.0f; /* yaw acceleration */
    float s25 = 0.0f; /* pitch acceleration */
    for (int k = step; k < steps; ++k) {
        float arg = ((float)k * PI_F) / (float)steps;
        double hr = 1.5 + (double)(mth_sin((double)arg) * thickness);
        double vr = hr * y_scale;
        hr = hr * (double)fp_sample(&cfg->hr_factor, &rnd);
        /* updateVerticalRadius */
        float edge = 1.0f - fabsf(0.5f - (float)k / (float)steps) * 2.0f;
        float vf = cfg->vr_default + cfg->vr_center * edge;
        float rb = jr_next_float(&rnd) * (1.0f - 0.75f) + 0.75f;
        vr = (double)vf * vr * (double)rb;
        float cp = mth_cos((double)pitch);
        float sp = mth_sin((double)pitch);
        x += (double)(mth_cos((double)yaw) * cp);
        y += (double)sp;
        z += (double)(mth_sin((double)yaw) * cp);
        pitch *= 0.7f;
        pitch += s25 * 0.05f;
        yaw += s24 * 0.05f;
        s25 *= 0.8f;
        s24 *= 0.5f;
        float a = jr_next_float(&rnd);
        float b = jr_next_float(&rnd);
        float d = jr_next_float(&rnd);
        s25 += (a - b) * d * 2.0f;
        a = jr_next_float(&rnd);
        b = jr_next_float(&rnd);
        d = jr_next_float(&rnd);
        s24 += (a - b) * d * 4.0f;
        if (jr_next_int_bound(&rnd, 4) == 0) {
            continue;
        }
        if (!can_reach(c, x, z, k, steps, thickness)) {
            return;
        }
        carve_ellipsoid(c, x, y, z, hr, vr, &skip);
    }
}

/* CanyonWorldCarver.carve (same position convention as cave_carve). */
static void canyon_carve(Ctx *c, JrRandom *r, int start_x, int start_z)
{
    const CarverCfg *cfg = c->cfg;
    int range_blocks = (CARVER_RANGE * 2 - 1) * 16;
    double x = (double)(start_x + jr_next_int_bound(r, 16));
    int y = height_sample(cfg, r);
    double z = (double)(start_z + jr_next_int_bound(r, 16));
    float yaw = jr_next_float(r) * TWO_PI_F;
    float pitch = fp_sample(&cfg->vertical_rotation, r);
    double y_scale = (double)fp_sample(&cfg->y_scale, r);
    float thickness = fp_sample(&cfg->thickness, r);
    int steps = (int)((float)range_blocks * fp_sample(&cfg->distance_factor, r));
    int64_t seed = jr_next_long(r);
    canyon_do_carve(c, seed, x, (double)y, z, thickness, yaw, pitch, 0, steps, y_scale);
}

/* WorldgenRandom.setLargeFeatureSeed */
static void set_large_feature_seed(JrRandom *r, int64_t seed, int cx, int cz)
{
    jr_legacy_init(r, seed);
    int64_t a = jr_next_long(r);
    int64_t b = jr_next_long(r);
    uint64_t mix = ((uint64_t)(int64_t)cx * (uint64_t)a) ^ ((uint64_t)(int64_t)cz * (uint64_t)b) ^ (uint64_t)seed;
    jr_legacy_init(r, (int64_t)mix);
}

bool jchunk_carve(JChunk *chunk, const JCarvers *cv, JAquifer *aquifer, uint8_t *blocks, uint8_t *mask,
                  int starts[JCARVER_MAX], const JCarveSurface *surface)
{
    if (chunk == NULL || cv == NULL || aquifer == NULL || blocks == NULL) {
        return false;
    }
    sin_init();
    size_t total = (size_t)16 * 16 * JFILL_HEIGHT;
    uint8_t *own_mask = NULL;
    if (mask == NULL) {
        own_mask = (uint8_t *)calloc(total, 1);
        if (own_mask == NULL) {
            return false;
        }
        mask = own_mask;
    }
    if (starts != NULL) {
        memset(starts, 0, sizeof(int) * JCARVER_MAX);
    }
    int64_t seed = jrs_seed(jchunk_state(chunk));
    int chunk_x = jchunk_min_x(chunk) >> 4;
    int chunk_z = jchunk_min_z(chunk) >> 4;
    for (int j = -8; j <= 8; ++j) {
        for (int k = -8; k <= 8; ++k) {
            int sx = chunk_x + j, sz = chunk_z + k;
            for (int m = 0; m < cv->count; ++m) {
                const CarverCfg *cfg = &cv->cfg[m];
                JrRandom rnd;
                set_large_feature_seed(&rnd, seed + (int64_t)m, sx, sz);
                if (!(jr_next_float(&rnd) <= cfg->probability)) {
                    continue;
                }
                if (starts != NULL) {
                    starts[m]++;
                }
                Ctx ctx = {cfg, cv->replaceable, aquifer, blocks, mask, jchunk_min_x(chunk), jchunk_min_z(chunk), surface, false};
                if (cfg->kind == CARVER_CAVE) {
                    cave_carve(&ctx, &rnd, sx * 16, sz * 16);
                } else {
                    canyon_carve(&ctx, &rnd, sx * 16, sz * 16);
                }
            }
        }
    }
    free(own_mask);
    return true;
}
