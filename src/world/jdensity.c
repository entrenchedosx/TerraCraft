#include "world/jdensity.h"

#include "world/jjson.h"
#include "world/jnoise.h"
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

typedef enum Kind {
    K_CONST, K_ADD, K_MUL, K_MIN, K_MAX, K_ADD_C, K_MUL_C, K_MIN_C, K_MAX_C,
    K_ABS, K_SQUARE, K_CUBE, K_HALF_NEG, K_QUARTER_NEG, K_SQUEEZE, K_INVERT,
    K_CLAMP, K_YGRAD, K_NOISE, K_SHIFTED, K_SHIFT_A, K_SHIFT_B,
    K_RANGE, K_WEIRD, K_SPLINE, K_PASS, K_BLEND_ALPHA, K_BLEND_OFFSET, K_BLENDED, K_FIND_TOP, K_FLATCACHE, K_INTERP, K_UNSUPPORTED
} Kind;

typedef struct Blended {
    JPerlinNoise min_limit, max_limit, main;
    double xz_multiplier, y_multiplier, xz_factor, y_factor, smear;
} Blended;

typedef struct Spline Spline;
struct Spline {
    bool is_const;
    float value;           /* Constant splines. */
    JDensity *coord;       /* Coordinate function (cast to float). */
    int n;
    float *loc;
    float *deriv;
    Spline **val;          /* Per-point value splines (constants or nested). */
};

struct JDensity {
    Kind kind;
    double c1, c2, c3, c4; /* Constants / scales / ranges (meaning per kind). */
    JDensity *a, *b, *c, *d, *e;
    const JNormalNoise *noise;
    Spline *spline;
    int mapper;            /* Weird sampler: 1 = type_1, 2 = type_2. */
    int ay, by;            /* Y gradient endpoints (find_top_surface: lower bound, cell height). */
    Blended *blended;
    int cache_id;          /* K_FLATCACHE / K_INTERP index within the router. */
};

typedef struct NoiseEntry {
    char *id;
    JNormalNoise noise;
} NoiseEntry;

typedef struct RefEntry {
    char *id;
    JDensity *df;
} RefEntry;

struct JRandomState {
    char *root;
    JrPositional random;
    NoiseEntry **noises;
    int noise_count, noise_cap;
    RefEntry *refs;
    int ref_count, ref_cap;
    JDensity **pool;       /* All allocated nodes (for freeing). */
    int pool_count, pool_cap;
    Spline **splines;
    int spline_count, spline_cap;
    JDensity *router[15];
    const char *router_keys[15];
    int unsupported;
    bool failed;
    int flat_count, interp_count;
    JrPositional aquifer_rand, ore_rand;
    int sea_level;
    int64_t seed;
    JDensity *interp_nodes[16];
    JDensity *flat_nodes[64];
};

static const char *ROUTER_KEYS[15] = {
    "barrier", "fluid_level_floodedness", "fluid_level_spread", "lava", "temperature",
    "vegetation", "continents", "erosion", "depth", "ridges", "preliminary_surface_level",
    "final_density", "vein_toggle", "vein_ridged", "vein_gap",
};

/* ---- allocation --------------------------------------------------------- */

static JDensity *new_node(JRandomState *rs, Kind k)
{
    JDensity *n = (JDensity *)calloc(1, sizeof(JDensity));
    if (n == NULL) {
        rs->failed = true;
        return NULL;
    }
    n->kind = k;
    if (rs->pool_count == rs->pool_cap) {
        rs->pool_cap = rs->pool_cap ? rs->pool_cap * 2 : 256;
        rs->pool = (JDensity **)realloc(rs->pool, (size_t)rs->pool_cap * sizeof(JDensity *));
    }
    rs->pool[rs->pool_count++] = n;
    return n;
}

static JDensity *const_node(JRandomState *rs, double v)
{
    JDensity *n = new_node(rs, K_CONST);
    if (n != NULL) {
        n->c1 = v;
    }
    return n;
}

/* ---- noise instances ------------------------------------------------------ */

static char *dup_str(const char *s)
{
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

static void id_to_path(const char *root, const char *folder, const char *id, char *out, size_t cap)
{
    const char *colon = strchr(id, ':');
    const char *ns = "minecraft";
    char nsbuf[64];
    const char *path = id;
    if (colon != NULL) {
        size_t n = (size_t)(colon - id);
        if (n >= sizeof(nsbuf)) {
            n = sizeof(nsbuf) - 1;
        }
        memcpy(nsbuf, id, n);
        nsbuf[n] = '\0';
        ns = nsbuf;
        path = colon + 1;
    }
    snprintf(out, cap, "%s/data/%s/worldgen/%s/%s.json", root, ns, folder, path);
}

static const JNormalNoise *get_noise(JRandomState *rs, const char *id)
{
    for (int i = 0; i < rs->noise_count; ++i) {
        if (strcmp(rs->noises[i]->id, id) == 0) {
            return &rs->noises[i]->noise;
        }
    }
    char path[1024];
    id_to_path(rs->root, "noise", id, path, sizeof(path));
    JJson *j = jj_parse_file(path);
    const JJson *fo = jj_get(j, "firstOctave");
    const JJson *amps = jj_get(j, "amplitudes");
    if (j == NULL || fo == NULL || amps == NULL || amps->type != JJ_ARR || amps->count < 1 ||
        amps->count > JN_MAX_OCTAVES) {
        jj_free(j);
        rs->failed = true;
        return NULL;
    }
    double a[JN_MAX_OCTAVES];
    for (int i = 0; i < amps->count; ++i) {
        a[i] = jj_double(amps->items[i]);
    }
    int first = (int)jj_double(fo);
    if (rs->noise_count == rs->noise_cap) {
        rs->noise_cap = rs->noise_cap ? rs->noise_cap * 2 : 32;
        rs->noises = (NoiseEntry **)realloc(rs->noises, (size_t)rs->noise_cap * sizeof(NoiseEntry *));
    }
    NoiseEntry *e = (NoiseEntry *)calloc(1, sizeof(NoiseEntry));
    if (e == NULL) {
        jj_free(j);
        rs->failed = true;
        return NULL;
    }
    rs->noises[rs->noise_count++] = e;
    e->id = dup_str(id);
    /* RandomState.getOrCreateNoise -> Noises.instantiate:
     * NormalNoise.create(positional.fromHashOf(id), parameters). */
    JrRandom r = jr_pos_from_hash_of(&rs->random, id);
    if (!jn_normal_init(&e->noise, &r, first, a, amps->count)) {
        rs->failed = true;
    }
    jj_free(j);
    return &e->noise;
}

/* ---- compilation ----------------------------------------------------------- */

static JDensity *compile(JRandomState *rs, const JJson *j);

static JDensity *compile_ref(JRandomState *rs, const char *id)
{
    for (int i = 0; i < rs->ref_count; ++i) {
        if (strcmp(rs->refs[i].id, id) == 0) {
            return rs->refs[i].df;
        }
    }
    char path[1024];
    id_to_path(rs->root, "density_function", id, path, sizeof(path));
    JJson *j = jj_parse_file(path);
    if (j == NULL) {
        rs->failed = true;
        return NULL;
    }
    JDensity *df = compile(rs, j);
    jj_free(j);
    if (rs->ref_count == rs->ref_cap) {
        rs->ref_cap = rs->ref_cap ? rs->ref_cap * 2 : 64;
        rs->refs = (RefEntry *)realloc(rs->refs, (size_t)rs->ref_cap * sizeof(RefEntry));
    }
    rs->refs[rs->ref_count].id = dup_str(id);
    rs->refs[rs->ref_count].df = df;
    rs->ref_count++;
    return df;
}

static JDensity *compile_member(JRandomState *rs, const JJson *obj, const char *key)
{
    const JJson *m = jj_get(obj, key);
    if (m == NULL) {
        rs->failed = true;
        return NULL;
    }
    return compile(rs, m);
}

static const char *strip_ns(const char *t)
{
    return strncmp(t, "minecraft:", 10) == 0 ? t + 10 : t;
}

static Spline *compile_spline(JRandomState *rs, const JJson *j)
{
    Spline *s = (Spline *)calloc(1, sizeof(Spline));
    if (s == NULL) {
        rs->failed = true;
        return NULL;
    }
    if (rs->spline_count == rs->spline_cap) {
        rs->spline_cap = rs->spline_cap ? rs->spline_cap * 2 : 64;
        rs->splines = (Spline **)realloc(rs->splines, (size_t)rs->spline_cap * sizeof(Spline *));
    }
    rs->splines[rs->spline_count++] = s;
    if (j->type == JJ_NUM) {
        s->is_const = true;
        s->value = jj_float(j);
        return s;
    }
    const JJson *coord = jj_get(j, "coordinate");
    const JJson *pts = jj_get(j, "points");
    if (coord == NULL || pts == NULL || pts->type != JJ_ARR || pts->count < 1) {
        rs->failed = true;
        return s;
    }
    s->coord = compile(rs, coord);
    s->n = pts->count;
    s->loc = (float *)malloc((size_t)s->n * sizeof(float));
    s->deriv = (float *)malloc((size_t)s->n * sizeof(float));
    s->val = (Spline **)calloc((size_t)s->n, sizeof(Spline *));
    for (int i = 0; i < s->n; ++i) {
        const JJson *p = pts->items[i];
        s->loc[i] = jj_float(jj_get(p, "location"));
        s->deriv[i] = jj_float(jj_get(p, "derivative"));
        const JJson *v = jj_get(p, "value");
        s->val[i] = v != NULL ? compile_spline(rs, v) : NULL;
        if (s->val[i] == NULL) {
            rs->failed = true;
        }
    }
    return s;
}

static JDensity *compile(JRandomState *rs, const JJson *j)
{
    if (j == NULL || rs->failed) {
        rs->failed = true;
        return NULL;
    }
    if (j->type == JJ_NUM) {
        return const_node(rs, jj_double(j));
    }
    if (j->type == JJ_STR) {
        return compile_ref(rs, j->text);
    }
    const JJson *tj = jj_get(j, "type");
    if (j->type != JJ_OBJ || tj == NULL || tj->type != JJ_STR) {
        rs->failed = true;
        return NULL;
    }
    const char *t = strip_ns(tj->text);
    JDensity *n = NULL;

    if (strcmp(t, "constant") == 0) {
        return const_node(rs, jj_double(jj_get(j, "argument")));
    }
    if (strcmp(t, "add") == 0 || strcmp(t, "mul") == 0 || strcmp(t, "min") == 0 || strcmp(t, "max") == 0) {
        JDensity *x = compile_member(rs, j, "argument1");
        JDensity *y = compile_member(rs, j, "argument2");
        if (x == NULL || y == NULL) {
            return NULL;
        }
        int op = t[1] == 'd' ? 0 : (t[1] == 'u' ? 1 : (t[1] == 'i' ? 2 : 3)); /* add mul min max */
        if (x->kind == K_CONST && y->kind == K_CONST) {
            double a = x->c1, b = y->c1;
            double r = op == 0 ? a + b : (op == 1 ? a * b : (op == 2 ? fmin(a, b) : fmax(a, b)));
            return const_node(rs, r);
        }
        /* DensityFunctions.TwoArgumentSimpleFunction.create: a constant operand
         * turns into MulOrAdd / MinOrMax with the other side as input. */
        if (x->kind == K_CONST || y->kind == K_CONST) {
            JDensity *in = x->kind == K_CONST ? y : x;
            double cv = x->kind == K_CONST ? x->c1 : y->c1;
            n = new_node(rs, (Kind)(K_ADD_C + op));
            if (n != NULL) {
                n->a = in;
                n->c1 = cv;
            }
            return n;
        }
        n = new_node(rs, (Kind)(K_ADD + op));
        if (n != NULL) {
            n->a = x;
            n->b = y;
        }
        return n;
    }
    static const struct { const char *name; Kind k; } UNARY[] = {
        {"abs", K_ABS}, {"square", K_SQUARE}, {"cube", K_CUBE}, {"half_negative", K_HALF_NEG},
        {"quarter_negative", K_QUARTER_NEG}, {"squeeze", K_SQUEEZE}, {"invert", K_INVERT},
    };
    for (size_t i = 0; i < sizeof(UNARY) / sizeof(UNARY[0]); ++i) {
        if (strcmp(t, UNARY[i].name) == 0) {
            JDensity *x = compile_member(rs, j, "argument");
            n = new_node(rs, UNARY[i].k);
            if (n != NULL) {
                n->a = x;
            }
            return n;
        }
    }
    if (strcmp(t, "interpolated") == 0 || strcmp(t, "flat_cache") == 0) {
        bool interp = t[0] == 'i';
        JDensity *x = compile_member(rs, j, "argument");
        n = new_node(rs, interp ? K_INTERP : K_FLATCACHE);
        if (n != NULL && x != NULL) {
            n->a = x;
            if (interp && rs->interp_count < 16) {
                n->cache_id = rs->interp_count;
                rs->interp_nodes[rs->interp_count++] = n;
            } else if (!interp && rs->flat_count < 64) {
                n->cache_id = rs->flat_count;
                rs->flat_nodes[rs->flat_count++] = n;
            } else {
                rs->failed = true;
            }
        }
        return n;
    }
    if (strcmp(t, "cache_2d") == 0 || strcmp(t, "cache_once") == 0 || strcmp(t, "cache_all_in_cell") == 0 ||
        strcmp(t, "blend_density") == 0) {
        return compile_member(rs, j, "argument");
    }
    if (strcmp(t, "blend_alpha") == 0) {
        return new_node(rs, K_BLEND_ALPHA);
    }
    if (strcmp(t, "blend_offset") == 0) {
        return new_node(rs, K_BLEND_OFFSET);
    }
    if (strcmp(t, "clamp") == 0) {
        n = new_node(rs, K_CLAMP);
        if (n != NULL) {
            n->a = compile_member(rs, j, "input");
            n->c1 = jj_double(jj_get(j, "min"));
            n->c2 = jj_double(jj_get(j, "max"));
        }
        return n;
    }
    if (strcmp(t, "y_clamped_gradient") == 0) {
        n = new_node(rs, K_YGRAD);
        if (n != NULL) {
            n->ay = (int)jj_double(jj_get(j, "from_y"));
            n->by = (int)jj_double(jj_get(j, "to_y"));
            n->c1 = jj_double(jj_get(j, "from_value"));
            n->c2 = jj_double(jj_get(j, "to_value"));
        }
        return n;
    }
    if (strcmp(t, "noise") == 0) {
        n = new_node(rs, K_NOISE);
        const JJson *nz = jj_get(j, "noise");
        if (n != NULL && nz != NULL && nz->type == JJ_STR) {
            n->noise = get_noise(rs, nz->text);
            n->c1 = jj_double(jj_get(j, "xz_scale"));
            n->c2 = jj_double(jj_get(j, "y_scale"));
        } else {
            rs->failed = true;
        }
        return n;
    }
    if (strcmp(t, "shifted_noise") == 0) {
        n = new_node(rs, K_SHIFTED);
        const JJson *nz = jj_get(j, "noise");
        if (n != NULL && nz != NULL && nz->type == JJ_STR) {
            n->noise = get_noise(rs, nz->text);
            n->a = compile_member(rs, j, "shift_x");
            n->b = compile_member(rs, j, "shift_y");
            n->c = compile_member(rs, j, "shift_z");
            n->c1 = jj_double(jj_get(j, "xz_scale"));
            n->c2 = jj_double(jj_get(j, "y_scale"));
        } else {
            rs->failed = true;
        }
        return n;
    }
    if (strcmp(t, "shift_a") == 0 || strcmp(t, "shift_b") == 0) {
        n = new_node(rs, t[6] == 'a' ? K_SHIFT_A : K_SHIFT_B);
        const JJson *nz = jj_get(j, "argument");
        if (n != NULL && nz != NULL && nz->type == JJ_STR) {
            n->noise = get_noise(rs, nz->text);
        } else {
            rs->failed = true;
        }
        return n;
    }
    if (strcmp(t, "range_choice") == 0) {
        n = new_node(rs, K_RANGE);
        if (n != NULL) {
            n->a = compile_member(rs, j, "input");
            n->c1 = jj_double(jj_get(j, "min_inclusive"));
            n->c2 = jj_double(jj_get(j, "max_exclusive"));
            n->b = compile_member(rs, j, "when_in_range");
            n->c = compile_member(rs, j, "when_out_of_range");
        }
        return n;
    }
    if (strcmp(t, "weird_scaled_sampler") == 0) {
        n = new_node(rs, K_WEIRD);
        const JJson *nz = jj_get(j, "noise");
        const JJson *rm = jj_get(j, "rarity_value_mapper");
        if (n != NULL && nz != NULL && rm != NULL && nz->type == JJ_STR && rm->type == JJ_STR) {
            n->a = compile_member(rs, j, "input");
            n->noise = get_noise(rs, nz->text);
            n->mapper = strcmp(strip_ns(rm->text), "type_1") == 0 ? 1 : 2;
        } else {
            rs->failed = true;
        }
        return n;
    }
    if (strcmp(t, "spline") == 0) {
        n = new_node(rs, K_SPLINE);
        const JJson *sp = jj_get(j, "spline");
        if (n != NULL && sp != NULL) {
            n->spline = compile_spline(rs, sp);
        } else {
            rs->failed = true;
        }
        return n;
    }
    if (strcmp(t, "old_blended_noise") == 0) {
        n = new_node(rs, K_BLENDED);
        Blended *b = (Blended *)calloc(1, sizeof(Blended));
        if (n == NULL || b == NULL) {
            free(b);
            rs->failed = true;
            return n;
        }
        n->blended = b;
        double xz_scale = jj_double(jj_get(j, "xz_scale"));
        double y_scale = jj_double(jj_get(j, "y_scale"));
        b->xz_factor = jj_double(jj_get(j, "xz_factor"));
        b->y_factor = jj_double(jj_get(j, "y_factor"));
        b->smear = jj_double(jj_get(j, "smear_scale_multiplier"));
        b->xz_multiplier = 684.412 * xz_scale;
        b->y_multiplier = 684.412 * y_scale;
        /* NoiseWiringHelper.wrapNew: withNewRandom(positional.fromHashOf("terrain")). */
        JrRandom r = jr_pos_from_hash_of(&rs->random, "minecraft:terrain");
        double ones[16];
        for (int i = 0; i < 16; ++i) {
            ones[i] = 1.0;
        }
        if (!jn_perlin_init_legacy(&b->min_limit, &r, -15, ones, 16) ||
            !jn_perlin_init_legacy(&b->max_limit, &r, -15, ones, 16) ||
            !jn_perlin_init_legacy(&b->main, &r, -7, ones, 8)) {
            rs->failed = true;
        }
        return n;
    }
    if (strcmp(t, "find_top_surface") == 0) {
        n = new_node(rs, K_FIND_TOP);
        if (n != NULL) {
            n->a = compile_member(rs, j, "density");
            n->b = compile_member(rs, j, "upper_bound");
            n->ay = (int)jj_double(jj_get(j, "lower_bound"));
            n->by = (int)jj_double(jj_get(j, "cell_height"));
        }
        return n;
    }
    /* end_islands, beardifier ... */
    n = new_node(rs, K_UNSUPPORTED);
    rs->unsupported++;
    return n;
}

/* ---- evaluation --------------------------------------------------------------- */

static double java_min(double a, double b)
{
    if (a != a) {
        return a;
    }
    if (a == 0.0 && b == 0.0) {
        return signbit(a) ? a : b;
    }
    return a <= b ? a : b;
}

static double java_max(double a, double b)
{
    if (a != a) {
        return a;
    }
    if (a == 0.0 && b == 0.0) {
        return signbit(a) ? b : a;
    }
    return a >= b ? a : b;
}

static double mth_clamp(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float spline_lerp(float d, float s, float e)
{
    return s + d * (e - s);
}

typedef struct JChunk JChunk;
static double df_eval(const JDensity *n, int x, int y, int z, const JChunk *ch, bool cc);

static float spline_eval(const Spline *s, int x, int y, int z, const JChunk *ch, bool cc)
{
    if (s == NULL) {
        return 0.0f;
    }
    if (s->is_const) {
        return s->value;
    }
    float f = (float)df_eval(s->coord, x, y, z, ch, cc);
    /* findIntervalStart: first index where f < location, minus one. */
    int lo = 0, hi = s->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (f < s->loc[mid]) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    int i = lo - 1;
    int last = s->n - 1;
    if (i < 0 || i == last) {
        int k = i < 0 ? 0 : last;
        float v = spline_eval(s->val[k], x, y, z, ch, cc);
        float g = s->deriv[k];
        return g == 0.0f ? v : v + g * (f - s->loc[k]);
    }
    float g = s->loc[i];
    float h = s->loc[i + 1];
    float k = (f - g) / (h - g);
    float l = s->deriv[i];
    float m = s->deriv[i + 1];
    float nn = spline_eval(s->val[i], x, y, z, ch, cc);
    float o = spline_eval(s->val[i + 1], x, y, z, ch, cc);
    float p = l * (h - g) - (o - nn);
    float q = -m * (h - g) + (o - nn);
    return spline_lerp(k, nn, o) + k * (1.0f - k) * spline_lerp(k, p, q);
}

static double rarity(int mapper, double d)
{
    if (mapper == 1) { /* TYPE1 -> getSpaghettiRarity3D, max 2.0 */
        return d < -0.5 ? 0.75 : (d < 0.0 ? 1.0 : (d < 0.5 ? 1.5 : 2.0));
    }
    return d < -0.75 ? 0.5 : (d < -0.5 ? 0.75 : (d < 0.5 ? 1.0 : (d < 0.75 ? 2.0 : 3.0)));
}


/* ---- NoiseChunk environment ------------------------------------------------------ */

#define CHUNK_CELL_W 4
#define CHUNK_CELL_H 8
#define CHUNK_MIN_Y (-64)
#define CHUNK_CELLS_XZ 4
#define CHUNK_CELLS_Y 48
#define CHUNK_CORNERS (5 * 5 * 49)
#define FLAT_N 5

struct JChunk {
    const JRandomState *rs;
    int chunk_x, chunk_z;
    int min_x, min_z;
    int lerp_mode;                 /* 0: Mth.lerp3 (CacheAllInCell fill), 1: Y,X,Z chain (plain interpolator). */
    double *corner[16];            /* Interpolator corner values [cx][cz][cy]. */
    unsigned char *corner_have[16];
    double *flat[64];              /* FlatCache grids [5][5]. */
    unsigned char *flat_have[64];
};

static double mth_lerp_d(double d, double s, double e)
{
    return s + d * (e - s);
}

static double chunk_corner(const JChunk *chc, const JDensity *n, int cx, int cz, int cy)
{
    JChunk *c = (JChunk *)chc;
    int id = n->cache_id;
    size_t idx = ((size_t)cx * 5 + (size_t)cz) * 49 + (size_t)cy;
    if (!c->corner_have[id][idx]) {
        int bx = c->min_x + cx * CHUNK_CELL_W;
        int bz = c->min_z + cz * CHUNK_CELL_W;
        int by = (cy - 8) * CHUNK_CELL_H;
        c->corner[id][idx] = df_eval(n->a, bx, by, bz, chc, false);
        c->corner_have[id][idx] = 1;
    }
    return c->corner[id][idx];
}

static double interp_eval(const JDensity *n, int x, int y, int z, const JChunk *ch)
{
    int ix = x - ch->min_x;
    int iz = z - ch->min_z;
    int iy = y - CHUNK_MIN_Y;
    if (ix < 0 || ix >= 16 || iz < 0 || iz >= 16 || iy < 0 || iy >= CHUNK_CELLS_Y * CHUNK_CELL_H) {
        return df_eval(n->a, x, y, z, ch, false);
    }
    int cx = ix / CHUNK_CELL_W, cz = iz / CHUNK_CELL_W, cy = iy / CHUNK_CELL_H;
    double tx = (double)(ix % CHUNK_CELL_W) / (double)CHUNK_CELL_W;
    double ty = (double)(iy % CHUNK_CELL_H) / (double)CHUNK_CELL_H;
    double tz = (double)(iz % CHUNK_CELL_W) / (double)CHUNK_CELL_W;
    double d000 = chunk_corner(ch, n, cx, cz, cy);
    double d100 = chunk_corner(ch, n, cx + 1, cz, cy);
    double d010 = chunk_corner(ch, n, cx, cz, cy + 1);
    double d110 = chunk_corner(ch, n, cx + 1, cz, cy + 1);
    double d001 = chunk_corner(ch, n, cx, cz + 1, cy);
    double d101 = chunk_corner(ch, n, cx + 1, cz + 1, cy);
    double d011 = chunk_corner(ch, n, cx, cz + 1, cy + 1);
    double d111 = chunk_corner(ch, n, cx + 1, cz + 1, cy + 1);
    if (ch->lerp_mode == 0) {
        /* Mth.lerp3(tx, ty, tz, 000, 100, 010, 110, 001, 101, 011, 111) */
        double a0 = mth_lerp_d(ty, mth_lerp_d(tx, d000, d100), mth_lerp_d(tx, d010, d110));
        double a1 = mth_lerp_d(ty, mth_lerp_d(tx, d001, d101), mth_lerp_d(tx, d011, d111));
        return mth_lerp_d(tz, a0, a1);
    }
    /* NoiseInterpolator.updateForY / X / Z chain. */
    double p = mth_lerp_d(ty, d000, d010);
    double q = mth_lerp_d(ty, d100, d110);
    double r = mth_lerp_d(ty, d001, d011);
    double s = mth_lerp_d(ty, d101, d111);
    double t0 = mth_lerp_d(tx, p, q);
    double u0 = mth_lerp_d(tx, r, s);
    return mth_lerp_d(tz, t0, u0);
}

static double flat_eval(const JDensity *n, int x, int y, int z, const JChunk *chc)
{
    JChunk *ch = (JChunk *)chc;
    int qx = (x >> 2) - (ch->min_x >> 2);
    int qz = (z >> 2) - (ch->min_z >> 2);
    if (qx < 0 || qz < 0 || qx >= FLAT_N || qz >= FLAT_N) {
        return df_eval(n->a, x, y, z, chc, false);
    }
    int id = n->cache_id;
    size_t idx = (size_t)qx * FLAT_N + (size_t)qz;
    if (!ch->flat_have[id][idx]) {
        int bx = ((ch->min_x >> 2) + qx) * 4;
        int bz = ((ch->min_z >> 2) + qz) * 4;
        ch->flat[id][idx] = df_eval(n->a, bx, 0, bz, chc, false);
        ch->flat_have[id][idx] = 1;
    }
    return ch->flat[id][idx];
}

static double blended_eval(const Blended *b, int bx, int by, int bz)
{
    double d = (double)bx * b->xz_multiplier;
    double e = (double)by * b->y_multiplier;
    double f = (double)bz * b->xz_multiplier;
    double g = d / b->xz_factor;
    double h = e / b->y_factor;
    double i = f / b->xz_factor;
    double j = b->y_multiplier * b->smear;
    double k = j / b->y_factor;
    double n = 0.0, o = 0.0, p = 0.0;
    double q = 1.0;
    for (int r = 0; r < 8; ++r) {
        const JImprovedNoise *in = jn_perlin_octave(&b->main, r);
        if (in != NULL) {
            p += jn_improved_noise_fudge(in, jn_perlin_wrap(g * q), jn_perlin_wrap(h * q), jn_perlin_wrap(i * q),
                                         k * q, h * q) / q;
        }
        q /= 2.0;
    }
    double s = (p / 10.0 + 1.0) / 2.0;
    bool hi = s >= 1.0;
    bool lo = s <= 0.0;
    q = 1.0;
    for (int t = 0; t < 16; ++t) {
        double u = jn_perlin_wrap(d * q);
        double v = jn_perlin_wrap(e * q);
        double w = jn_perlin_wrap(f * q);
        double xx = j * q;
        if (!hi) {
            const JImprovedNoise *in2 = jn_perlin_octave(&b->min_limit, t);
            if (in2 != NULL) {
                n += jn_improved_noise_fudge(in2, u, v, w, xx, e * q) / q;
            }
        }
        if (!lo) {
            const JImprovedNoise *in3 = jn_perlin_octave(&b->max_limit, t);
            if (in3 != NULL) {
                o += jn_improved_noise_fudge(in3, u, v, w, xx, e * q) / q;
            }
        }
        q /= 2.0;
    }
    double a = n / 512.0;
    double c = o / 512.0;
    /* Mth.clampedLerp(delta, start, end). */
    double r = s < 0.0 ? a : (s > 1.0 ? c : a + s * (c - a));
    return r / 128.0;
}

static double df_eval(const JDensity *n, int x, int y, int z, const JChunk *ch, bool cc)
{
    switch (n->kind) {
    case K_CONST: return n->c1;
    case K_ADD: return df_eval(n->a, x, y, z, ch, cc) + df_eval(n->b, x, y, z, ch, cc);
    case K_MUL: {
        double d = df_eval(n->a, x, y, z, ch, cc);
        return d == 0.0 ? 0.0 : d * df_eval(n->b, x, y, z, ch, cc);
    }
    case K_MIN: return java_min(df_eval(n->a, x, y, z, ch, cc), df_eval(n->b, x, y, z, ch, cc));
    case K_MAX: return java_max(df_eval(n->a, x, y, z, ch, cc), df_eval(n->b, x, y, z, ch, cc));
    case K_ADD_C: return df_eval(n->a, x, y, z, ch, cc) + n->c1;
    case K_MUL_C: return df_eval(n->a, x, y, z, ch, cc) * n->c1;
    case K_MIN_C: {
        double d = df_eval(n->a, x, y, z, ch, cc);
        return d < n->c1 ? d : n->c1;
    }
    case K_MAX_C: {
        double d = df_eval(n->a, x, y, z, ch, cc);
        return d > n->c1 ? d : n->c1;
    }
    case K_ABS: return fabs(df_eval(n->a, x, y, z, ch, cc));
    case K_SQUARE: {
        double d = df_eval(n->a, x, y, z, ch, cc);
        return d * d;
    }
    case K_CUBE: {
        double d = df_eval(n->a, x, y, z, ch, cc);
        return d * d * d;
    }
    case K_HALF_NEG: {
        double d = df_eval(n->a, x, y, z, ch, cc);
        return d > 0.0 ? d : d * 0.5;
    }
    case K_QUARTER_NEG: {
        double d = df_eval(n->a, x, y, z, ch, cc);
        return d > 0.0 ? d : d * 0.25;
    }
    case K_SQUEEZE: {
        double c = mth_clamp(df_eval(n->a, x, y, z, ch, cc), -1.0, 1.0);
        return c / 2.0 - c * c * c / 24.0;
    }
    case K_INVERT: return 1.0 / df_eval(n->a, x, y, z, ch, cc);
    case K_CLAMP: return mth_clamp(df_eval(n->a, x, y, z, ch, cc), n->c1, n->c2);
    case K_YGRAD: {
        double delta = ((double)y - (double)n->ay) / ((double)n->by - (double)n->ay);
        if (delta < 0.0) {
            return n->c1;
        }
        if (delta > 1.0) {
            return n->c2;
        }
        return n->c1 + delta * (n->c2 - n->c1);
    }
    case K_NOISE:
        return jn_normal_value(n->noise, (double)x * n->c1, (double)y * n->c2, (double)z * n->c1);
    case K_SHIFTED: {
        double d = (double)x * n->c1 + df_eval(n->a, x, y, z, ch, cc);
        double e = (double)y * n->c2 + df_eval(n->b, x, y, z, ch, cc);
        double f = (double)z * n->c1 + df_eval(n->c, x, y, z, ch, cc);
        return jn_normal_value(n->noise, d, e, f);
    }
    case K_SHIFT_A:
        return jn_normal_value(n->noise, (double)x * 0.25, 0.0 * 0.25, (double)z * 0.25) * 4.0;
    case K_SHIFT_B:
        return jn_normal_value(n->noise, (double)z * 0.25, (double)x * 0.25, 0.0 * 0.25) * 4.0;
    case K_RANGE: {
        double d = df_eval(n->a, x, y, z, ch, cc);
        return (d >= n->c1 && d < n->c2) ? df_eval(n->b, x, y, z, ch, cc) : df_eval(n->c, x, y, z, ch, cc);
    }
    case K_WEIRD: {
        double d = rarity(n->mapper, df_eval(n->a, x, y, z, ch, cc));
        return d * fabs(jn_normal_value(n->noise, (double)x / d, (double)y / d, (double)z / d));
    }
    case K_SPLINE: return (double)spline_eval(n->spline, x, y, z, ch, cc);
    case K_PASS: return df_eval(n->a, x, y, z, ch, cc);
    case K_BLEND_ALPHA: return 1.0;
    case K_BLEND_OFFSET: return 0.0;
    case K_BLENDED: return blended_eval(n->blended, x, y, z);
    case K_FIND_TOP: {
        int cell = n->by;
        int lower = n->ay;
        double ub = df_eval(n->b, x, y, z, ch, cc) / (double)cell;
        int i = (int)ub;
        if (ub < (double)i) {
            i--;
        }
        i *= cell;
        if (i <= lower) {
            return (double)lower;
        }
        for (int yy = i; yy >= lower; yy -= cell) {
            if (df_eval(n->a, x, yy, z, ch, false) > 0.0) {
                return (double)yy;
            }
        }
        return (double)lower;
    }
    case K_FLATCACHE:
        return ch != NULL ? flat_eval(n, x, y, z, ch) : df_eval(n->a, x, y, z, ch, cc);
    case K_INTERP:
        return (ch != NULL && cc) ? interp_eval(n, x, y, z, ch) : df_eval(n->a, x, y, z, ch, cc);
    case K_UNSUPPORTED: return NAN;
    }
    return NAN;
}

double jdf_compute(const JDensity *df, int x, int y, int z)
{
    return df != NULL ? df_eval(df, x, y, z, NULL, false) : NAN;
}

/* ---- router ----------------------------------------------------------------------- */

int64_t jrs_seed(const JRandomState *rs)
{
    return rs != NULL ? rs->seed : 0;
}

JRandomState *jrs_create(const char *datapack_root, const char *settings, int64_t seed)
{
    if (datapack_root == NULL || settings == NULL) {
        return NULL;
    }
    JRandomState *rs = (JRandomState *)calloc(1, sizeof(JRandomState));
    if (rs == NULL) {
        return NULL;
    }
    rs->root = dup_str(datapack_root);
    rs->seed = seed;
    char path[1024];
    snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/noise_settings/%s.json", datapack_root, settings);
    JJson *ns = jj_parse_file(path);
    const JJson *router = jj_get(ns, "noise_router");
    const JJson *legacy = jj_get(ns, "legacy_random_source");
    if (router == NULL || (legacy != NULL && legacy->type == JJ_BOOL && legacy->boolean)) {
        /* Legacy-random-source settings (Nether-style) are not supported yet. */
        jj_free(ns);
        jrs_destroy(rs);
        return NULL;
    }
    /* RandomState: settings.getRandomSource().newInstance(seed).forkPositional() */
    JrRandom base;
    jr_xoroshiro_init(&base, seed);
    rs->random = jr_fork_positional(&base);
    {
        /* RandomState: aquiferRandom / oreRandom = random.fromHashOf("aquifer"/"ore").forkPositional() */
        JrRandom ra = jr_pos_from_hash_of(&rs->random, "minecraft:aquifer");
        rs->aquifer_rand = jr_fork_positional(&ra);
        JrRandom ro = jr_pos_from_hash_of(&rs->random, "minecraft:ore");
        rs->ore_rand = jr_fork_positional(&ro);
        const JJson *sl = jj_get(ns, "sea_level");
        rs->sea_level = sl != NULL ? (int)jj_double(sl) : 63;
    }
    for (int i = 0; i < 15 && !rs->failed; ++i) {
        rs->router_keys[i] = ROUTER_KEYS[i];
        const JJson *m = jj_get(router, ROUTER_KEYS[i]);
        rs->router[i] = m != NULL ? compile(rs, m) : NULL;
    }
    jj_free(ns);
    if (rs->failed) {
        jrs_destroy(rs);
        return NULL;
    }
    return rs;
}


JChunk *jchunk_create(const JRandomState *rs, int chunk_x, int chunk_z)
{
    if (rs == NULL) {
        return NULL;
    }
    JChunk *c = (JChunk *)calloc(1, sizeof(JChunk));
    if (c == NULL) {
        return NULL;
    }
    c->rs = rs;
    c->chunk_x = chunk_x;
    c->chunk_z = chunk_z;
    c->min_x = chunk_x * 16;
    c->min_z = chunk_z * 16;
    for (int i = 0; i < rs->interp_count; ++i) {
        c->corner[i] = (double *)calloc(CHUNK_CORNERS, sizeof(double));
        c->corner_have[i] = (unsigned char *)calloc(CHUNK_CORNERS, 1);
    }
    for (int i = 0; i < rs->flat_count; ++i) {
        c->flat[i] = (double *)calloc(FLAT_N * FLAT_N, sizeof(double));
        c->flat_have[i] = (unsigned char *)calloc(FLAT_N * FLAT_N, 1);
    }
    return c;
}

void jchunk_free(JChunk *c)
{
    if (c == NULL) {
        return;
    }
    for (int i = 0; i < 16; ++i) {
        free(c->corner[i]);
        free(c->corner_have[i]);
    }
    for (int i = 0; i < 64; ++i) {
        free(c->flat[i]);
        free(c->flat_have[i]);
    }
    free(c);
}

double jchunk_final_density(JChunk *c, int x, int y, int z)
{
    const JDensity *fd = jrs_router(c->rs, "final_density");
    c->lerp_mode = 0; /* CacheAllInCell(add(final_density, beardifier)) fills with Mth.lerp3 */
    double d = fd != NULL ? df_eval(fd, x, y, z, c, true) : NAN;
    return d + 0.0; /* + BeardifierMarker (0.0 without structures) */
}

double jchunk_eval_router(JChunk *c, const char *key, int x, int y, int z, bool chain_lerp)
{
    const JDensity *df = jrs_router(c->rs, key);
    c->lerp_mode = chain_lerp ? 1 : 0;
    return df != NULL ? df_eval(df, x, y, z, c, true) : NAN;
}

const JDensity *jrs_router(const JRandomState *rs, const char *key)
{
    if (rs == NULL || key == NULL) {
        return NULL;
    }
    for (int i = 0; i < 15; ++i) {
        if (strcmp(ROUTER_KEYS[i], key) == 0) {
            return rs->router[i];
        }
    }
    return NULL;
}

int jrs_unsupported_count(const JRandomState *rs)
{
    return rs != NULL ? rs->unsupported : 0;
}

void jrs_destroy(JRandomState *rs)
{
    if (rs == NULL) {
        return;
    }
    for (int i = 0; i < rs->pool_count; ++i) {
        free(rs->pool[i]->blended);
        free(rs->pool[i]);
    }
    free(rs->pool);
    for (int i = 0; i < rs->spline_count; ++i) {
        Spline *s = rs->splines[i];
        free(s->loc);
        free(s->deriv);
        free(s->val);
        free(s);
    }
    free(rs->splines);
    for (int i = 0; i < rs->noise_count; ++i) {
        free(rs->noises[i]->id);
        free(rs->noises[i]);
    }
    free(rs->noises);
    for (int i = 0; i < rs->ref_count; ++i) {
        free(rs->refs[i].id);
    }
    free(rs->refs);
    free(rs->root);
    free(rs);
}

int jrs_sea_level(const JRandomState *rs)
{
    return rs != NULL ? rs->sea_level : 63;
}

const JrPositional *jrs_random(const JRandomState *rs)
{
    return &rs->random;
}

const char *jrs_root(const JRandomState *rs)
{
    return rs->root;
}

const struct JNormalNoise *jrs_noise(JRandomState *rs, const char *id)
{
    return get_noise(rs, id);
}

const JrPositional *jrs_aquifer_random(const JRandomState *rs)
{
    return &rs->aquifer_rand;
}

const JrPositional *jrs_ore_random(const JRandomState *rs)
{
    return &rs->ore_rand;
}

const JRandomState *jchunk_state(const JChunk *c)
{
    return c->rs;
}

int jchunk_min_x(const JChunk *c)
{
    return c->min_x;
}

int jchunk_min_z(const JChunk *c)
{
    return c->min_z;
}
