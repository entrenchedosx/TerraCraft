#include "world/jsurface.h"

#include "world/jbiome.h"
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

#define MIN_Y (-64)
#define MAX_Y 319
#define WATER_UNSET INT32_MIN

/* ---- Mth --------------------------------------------------------------------- */

static int mth_floor(double v)
{
    int i = (int)v;
    return v < (double)i ? i - 1 : i;
}

static double mth_lerp(double d, double s, double e)
{
    return s + d * (e - s);
}

static double mth_lerp2(double dx, double dy, double v00, double v10, double v01, double v11)
{
    return mth_lerp(dy, mth_lerp(dx, v00, v10), mth_lerp(dx, v01, v11));
}

static double mth_map(double v, double a1, double a2, double b1, double b2)
{
    double t = (v - a1) / (a2 - a1);
    return mth_lerp(t, b1, b2);
}

/* Java Math.round(double) -> long, for the small values used here. */
static long long java_round(double d)
{
    return (long long)floor(d + 0.5);
}

/* ---- SHA-256 (BiomeManager.obfuscateSeed uses Guava's sha256().hashLong) ------ */

static uint32_t rotr32(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void sha256_8bytes(const uint8_t msg[8], uint8_t out[32])
{
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block[64];
    memset(block, 0, sizeof(block));
    memcpy(block, msg, 8);
    block[8] = 0x80;
    block[63] = 64; /* message length in bits */
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)h[i];
    }
}

/* BiomeManager.obfuscateSeed: Hashing.sha256().hashLong(seed).asLong(). */
static int64_t obfuscate_seed(int64_t seed)
{
    uint8_t msg[8], dig[32];
    uint64_t u = (uint64_t)seed;
    for (int i = 0; i < 8; ++i) {
        msg[i] = (uint8_t)(u >> (8 * i));
    }
    sha256_8bytes(msg, dig);
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i) {
        r |= (uint64_t)dig[i] << (8 * i);
    }
    return (int64_t)r;
}

/* ---- SimplexNoise / PerlinSimplexNoise (2-D path used by Biome) --------------- */

static const int SIMPLEX_GRAD[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                                        {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};

typedef struct Simplex {
    int p[512];
    double xo, yo, zo;
} Simplex;

static void simplex_init(Simplex *s, JrRandom *r)
{
    s->xo = jr_next_double(r) * 256.0;
    s->yo = jr_next_double(r) * 256.0;
    s->zo = jr_next_double(r) * 256.0;
    for (int i = 0; i < 256; ++i) {
        s->p[i] = i;
    }
    for (int i = 0; i < 256; ++i) {
        int j = jr_next_int_bound(r, 256 - i);
        int tmp = s->p[i];
        s->p[i] = s->p[j + i];
        s->p[j + i] = tmp;
    }
}

static int simplex_p(const Simplex *s, int i)
{
    return s->p[i & 255];
}

static double simplex_corner(const Simplex *s, int gi, double x, double y, double z, double base)
{
    double d = base - x * x - y * y - z * z;
    if (d < 0.0) {
        return 0.0;
    }
    (void)s;
    d = d * d;
    const int *g = SIMPLEX_GRAD[gi];
    double dot = (double)g[0] * x + (double)g[1] * y + (double)g[2] * z;
    return d * d * dot;
}

static double simplex_value2(const Simplex *s, double xin, double yin)
{
    const double SQRT3 = sqrt(3.0);
    const double F2 = 0.5 * (SQRT3 - 1.0);
    const double G2 = (3.0 - SQRT3) / 6.0;
    double t0 = (xin + yin) * F2;
    int i = mth_floor(xin + t0);
    int j = mth_floor(yin + t0);
    double t = (double)(i + j) * G2;
    double x0 = xin - ((double)i - t);
    double y0 = yin - ((double)j - t);
    int i1, j1;
    if (x0 > y0) {
        i1 = 1;
        j1 = 0;
    } else {
        i1 = 0;
        j1 = 1;
    }
    double x1 = x0 - (double)i1 + G2;
    double y1 = y0 - (double)j1 + G2;
    double x2 = x0 - 1.0 + 2.0 * G2;
    double y2 = y0 - 1.0 + 2.0 * G2;
    int ii = i & 255;
    int jj = j & 255;
    int gi0 = simplex_p(s, ii + simplex_p(s, jj)) % 12;
    int gi1 = simplex_p(s, ii + i1 + simplex_p(s, jj + j1)) % 12;
    int gi2 = simplex_p(s, ii + 1 + simplex_p(s, jj + 1)) % 12;
    double n0 = simplex_corner(s, gi0, x0, y0, 0.0, 0.5);
    double n1 = simplex_corner(s, gi1, x1, y1, 0.0, 0.5);
    double n2 = simplex_corner(s, gi2, x2, y2, 0.0, 0.5);
    return 70.0 * (n0 + n1 + n2);
}

#define PSN_MAX 3
typedef struct PerlinSimplex {
    Simplex levels[PSN_MAX];
    int count;
    double input_factor;  /* 2^lastOctave */
    double value_factor;  /* 1 / (2^count - 1) */
} PerlinSimplex;

/* PerlinSimplexNoise(new WorldgenRandom(new LegacyRandomSource(seed)), octaves) for
 * octave lists that are consecutive and end at 0 (the three Biome noises). */
static void psn_init(PerlinSimplex *n, int64_t seed, int octave_count)
{
    JrRandom r;
    jr_legacy_init(&r, seed);
    n->count = octave_count;
    for (int i = 0; i < octave_count; ++i) {
        simplex_init(&n->levels[i], &r);
    }
    n->input_factor = 1.0;
    n->value_factor = 1.0 / (pow(2.0, (double)octave_count) - 1.0);
}

static double psn_value(const PerlinSimplex *n, double x, double y)
{
    double sum = 0.0;
    double e = n->input_factor;
    double f = n->value_factor;
    for (int i = 0; i < n->count; ++i) {
        sum = sum + simplex_value2(&n->levels[i], x * e + 0.0, y * e + 0.0) * f;
        e = e / 2.0;
        f = f * 2.0;
    }
    return sum;
}

static PerlinSimplex g_temperature_noise; /* seed 1234, octaves [0] */
static PerlinSimplex g_frozen_noise;      /* seed 3456, octaves [-2, -1, 0] */
static PerlinSimplex g_info_noise;        /* seed 2345, octaves [0] */
static bool g_simplex_ready;

static void simplex_statics(void)
{
    if (!g_simplex_ready) {
        psn_init(&g_temperature_noise, 1234, 1);
        psn_init(&g_frozen_noise, 3456, 3);
        psn_init(&g_info_noise, 2345, 1);
        g_simplex_ready = true;
    }
}

double jsurface_simplex_noise(int which, double x, double y)
{
    simplex_statics();
    const PerlinSimplex *n = which == 0 ? &g_temperature_noise : (which == 1 ? &g_frozen_noise : &g_info_noise);
    return psn_value(n, x, y);
}

/* ---- surface rule tree --------------------------------------------------------- */

typedef enum CondKind {
    C_BIOME,
    C_NOISE,
    C_STONE_DEPTH,
    C_WATER,
    C_Y_ABOVE,
    C_STEEP,
    C_HOLE,
    C_ABOVE_PRELIM,
    C_TEMPERATURE,
    C_NOT,
    C_GRADIENT
} CondKind;

typedef struct Cond {
    CondKind kind;
    /* biome */
    char **biomes;
    int biome_count;
    /* noise_threshold */
    const JNormalNoise *noise;
    double min_threshold, max_threshold;
    /* stone_depth / water / y_above */
    int offset;
    bool add_surface_depth;  /* stone_depth */
    bool add_stone_depth;    /* water / y_above */
    int secondary_range;
    bool ceiling;
    int multiplier;          /* surface_depth_multiplier */
    int anchor_y;            /* resolved y_above anchor / gradient true_at */
    int anchor_y2;           /* gradient false_at */
    JrPositional gradient;
    struct Cond *child;      /* not */
    /* lazy cache */
    int64_t last_update;
    bool result;
} Cond;

typedef enum RuleKind { R_SEQUENCE, R_TEST, R_BLOCK, R_BANDLANDS } RuleKind;

typedef struct Rule {
    RuleKind kind;
    struct Rule **children;
    int child_count;
    Cond *cond;
    struct Rule *followup;
    int block;
} Rule;

typedef struct BiomeInfo {
    char *name;
    float temperature;
    bool frozen;
} BiomeInfo;

/* ChunkAccess biome storage: ChunkAccess.fillBiomesFromNoise writes every quart of the
 * chunk (24 sections x 4 x 4 x 4) once, sections bottom to top, quarts in x, y, z order.
 * BiomeManager then only reads the stored values, which removes any dependence of the
 * Climate R-tree's last-result shortcut on the order of later lookups. */
#define BIOME_QUART_Y 96  /* quart Y -16 .. 79 */

typedef struct BiomeChunk {
    int cx, cz;
    const char *q[16 * BIOME_QUART_Y];  /* index (qy + 16) * 16 + lqz * 4 + lqx */
} BiomeChunk;

struct JSurface {
    JRandomState *rs;
    JBiomeList *biomes;
    int64_t zoom_seed;
    int sea_level;
    int default_block;
    JrPositional noise_random;       /* RandomState.random */
    const JNormalNoise *surface_noise, *surface_secondary, *clay_offset;
    const JNormalNoise *badlands_pillar, *badlands_roof, *badlands_surface;
    const JNormalNoise *iceberg_pillar, *iceberg_roof, *iceberg_surface;
    int clay_bands[192];
    Rule *rule;
    Rule **all_rules;
    int rule_count, rule_cap;
    Cond **all_conds;
    int cond_count, cond_cap;
    BiomeInfo *infos;
    int info_count, info_cap;
    BiomeChunk **stored;
    int stored_count, stored_cap;
    BiomeChunk *last_stored;
    int *stored_hash;                /* open addressing over stored[]: index + 1, 0 = empty */
    int stored_hash_cap;
    int64_t epoch_xz, epoch_y;       /* SurfaceRules.Context.lastUpdateXZ / lastUpdateY */
    char root[1024];
};

/* ---- noise settings: rule parsing ------------------------------------------------ */

static bool g_parse_failed;

static char *dup_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d != NULL) {
        memcpy(d, s, n);
    }
    return d;
}

static const char *jstr(const JJson *j)
{
    return (j != NULL && j->type == JJ_STR) ? j->text : "";
}

/* VerticalAnchor.resolveY for the overworld generation context (minY -64, depth 384). */
static int anchor_resolve(const JJson *a)
{
    const JJson *v;
    if ((v = jj_get(a, "absolute")) != NULL) {
        return (int)jj_double(v);
    }
    if ((v = jj_get(a, "above_bottom")) != NULL) {
        return MIN_Y + (int)jj_double(v);
    }
    if ((v = jj_get(a, "below_top")) != NULL) {
        return MIN_Y + 384 - 1 - (int)jj_double(v);
    }
    g_parse_failed = true;
    return 0;
}

static bool jbool(const JJson *j)
{
    return j != NULL && j->type == JJ_BOOL && j->boolean;
}

static Cond *cond_new(JSurface *s, CondKind kind)
{
    Cond *c = (Cond *)calloc(1, sizeof(Cond));
    if (c == NULL) {
        g_parse_failed = true;
        return NULL;
    }
    c->kind = kind;
    c->last_update = -1;
    if (s->cond_count == s->cond_cap) {
        s->cond_cap = s->cond_cap != 0 ? s->cond_cap * 2 : 64;
        s->all_conds = (Cond **)realloc(s->all_conds, (size_t)s->cond_cap * sizeof(Cond *));
    }
    s->all_conds[s->cond_count++] = c;
    return c;
}

static Cond *parse_cond(JSurface *s, const JJson *j)
{
    const char *type = jstr(jj_get(j, "type"));
    if (strncmp(type, "minecraft:", 10) == 0) {
        type += 10;
    }
    Cond *c = NULL;
    if (strcmp(type, "biome") == 0) {
        c = cond_new(s, C_BIOME);
        const JJson *list = jj_get(j, "biome_is");
        if (c != NULL && list != NULL && list->type == JJ_ARR) {
            c->biomes = (char **)calloc((size_t)list->count, sizeof(char *));
            c->biome_count = list->count;
            for (int i = 0; i < list->count; ++i) {
                c->biomes[i] = dup_string(jstr(list->items[i]));
            }
        } else {
            g_parse_failed = true;
        }
    } else if (strcmp(type, "noise_threshold") == 0) {
        c = cond_new(s, C_NOISE);
        if (c != NULL) {
            c->noise = jrs_noise(s->rs, jstr(jj_get(j, "noise")));
            c->min_threshold = jj_double(jj_get(j, "min_threshold"));
            c->max_threshold = jj_double(jj_get(j, "max_threshold"));
            if (c->noise == NULL) {
                g_parse_failed = true;
            }
        }
    } else if (strcmp(type, "stone_depth") == 0) {
        c = cond_new(s, C_STONE_DEPTH);
        if (c != NULL) {
            c->offset = (int)jj_double(jj_get(j, "offset"));
            c->add_surface_depth = jbool(jj_get(j, "add_surface_depth"));
            c->secondary_range = (int)jj_double(jj_get(j, "secondary_depth_range"));
            c->ceiling = strcmp(jstr(jj_get(j, "surface_type")), "ceiling") == 0;
        }
    } else if (strcmp(type, "water") == 0) {
        c = cond_new(s, C_WATER);
        if (c != NULL) {
            c->offset = (int)jj_double(jj_get(j, "offset"));
            c->multiplier = (int)jj_double(jj_get(j, "surface_depth_multiplier"));
            c->add_stone_depth = jbool(jj_get(j, "add_stone_depth"));
        }
    } else if (strcmp(type, "y_above") == 0) {
        c = cond_new(s, C_Y_ABOVE);
        if (c != NULL) {
            c->anchor_y = anchor_resolve(jj_get(j, "anchor"));
            c->multiplier = (int)jj_double(jj_get(j, "surface_depth_multiplier"));
            c->add_stone_depth = jbool(jj_get(j, "add_stone_depth"));
        }
    } else if (strcmp(type, "steep") == 0) {
        c = cond_new(s, C_STEEP);
    } else if (strcmp(type, "hole") == 0) {
        c = cond_new(s, C_HOLE);
    } else if (strcmp(type, "above_preliminary_surface") == 0) {
        c = cond_new(s, C_ABOVE_PRELIM);
    } else if (strcmp(type, "temperature") == 0) {
        c = cond_new(s, C_TEMPERATURE);
    } else if (strcmp(type, "not") == 0) {
        c = cond_new(s, C_NOT);
        if (c != NULL) {
            c->child = parse_cond(s, jj_get(j, "invert"));
        }
    } else if (strcmp(type, "vertical_gradient") == 0) {
        c = cond_new(s, C_GRADIENT);
        if (c != NULL) {
            c->anchor_y = anchor_resolve(jj_get(j, "true_at_and_below"));
            c->anchor_y2 = anchor_resolve(jj_get(j, "false_at_and_above"));
            /* RandomState.getOrCreateRandomFactory(id): random.fromHashOf(id).forkPositional() */
            JrRandom r = jr_pos_from_hash_of(&s->noise_random, jstr(jj_get(j, "random_name")));
            c->gradient = jr_fork_positional(&r);
        }
    } else {
        fprintf(stderr, "jsurface: unsupported condition '%s'\n", type);
        g_parse_failed = true;
    }
    return c;
}

static Rule *rule_new(JSurface *s, RuleKind kind)
{
    Rule *r = (Rule *)calloc(1, sizeof(Rule));
    if (r == NULL) {
        g_parse_failed = true;
        return NULL;
    }
    r->kind = kind;
    r->block = -1;
    if (s->rule_count == s->rule_cap) {
        s->rule_cap = s->rule_cap != 0 ? s->rule_cap * 2 : 64;
        s->all_rules = (Rule **)realloc(s->all_rules, (size_t)s->rule_cap * sizeof(Rule *));
    }
    s->all_rules[s->rule_count++] = r;
    return r;
}

static Rule *parse_rule(JSurface *s, const JJson *j)
{
    const char *type = jstr(jj_get(j, "type"));
    if (strncmp(type, "minecraft:", 10) == 0) {
        type += 10;
    }
    Rule *r = NULL;
    if (strcmp(type, "sequence") == 0) {
        r = rule_new(s, R_SEQUENCE);
        const JJson *list = jj_get(j, "sequence");
        if (r != NULL && list != NULL && list->type == JJ_ARR) {
            r->children = (Rule **)calloc((size_t)list->count, sizeof(Rule *));
            r->child_count = list->count;
            for (int i = 0; i < list->count; ++i) {
                r->children[i] = parse_rule(s, list->items[i]);
            }
        } else {
            g_parse_failed = true;
        }
    } else if (strcmp(type, "condition") == 0) {
        r = rule_new(s, R_TEST);
        if (r != NULL) {
            r->cond = parse_cond(s, jj_get(j, "if_true"));
            r->followup = parse_rule(s, jj_get(j, "then_run"));
        }
    } else if (strcmp(type, "block") == 0) {
        r = rule_new(s, R_BLOCK);
        if (r != NULL) {
            const char *name = jstr(jj_get(jj_get(j, "result_state"), "Name"));
            r->block = jfill_block_from_name(name);
            if (r->block < 0) {
                fprintf(stderr, "jsurface: unknown block '%s'\n", name);
                g_parse_failed = true;
            }
        }
    } else if (strcmp(type, "bandlands") == 0) {
        r = rule_new(s, R_BANDLANDS);
    } else {
        fprintf(stderr, "jsurface: unsupported rule '%s'\n", type);
        g_parse_failed = true;
    }
    return r;
}

/* ---- SurfaceSystem clay bands -------------------------------------------------------- */

static int next_int_between_inclusive(JrRandom *r, int lo, int hi)
{
    return jr_next_int_bound(r, hi - lo + 1) + lo;
}

static void make_bands(JrRandom *r, int *bands, int min_thickness, int block)
{
    int count = next_int_between_inclusive(r, 6, 15);
    for (int i = 0; i < count; ++i) {
        int size = min_thickness + jr_next_int_bound(r, 3);
        int start = jr_next_int_bound(r, 192);
        for (int p = 0; start + p < 192 && p < size; ++p) {
            bands[start + p] = block;
        }
    }
}

static void generate_bands(JrRandom *r, int *bands)
{
    for (int i = 0; i < 192; ++i) {
        bands[i] = JB_TERRACOTTA;
    }
    for (int i = 0; i < 192;) {
        i += jr_next_int_bound(r, 5) + 1;
        if (i < 192) {
            bands[i] = JB_ORANGE_TERRACOTTA;
        }
        ++i;
    }
    make_bands(r, bands, 1, JB_YELLOW_TERRACOTTA);
    make_bands(r, bands, 2, JB_BROWN_TERRACOTTA);
    make_bands(r, bands, 1, JB_RED_TERRACOTTA);
    int band_count = next_int_between_inclusive(r, 9, 15);
    for (int j = 0, k = 0; j < band_count && k < 192; ++j, k += jr_next_int_bound(r, 16) + 4) {
        bands[k] = JB_WHITE_TERRACOTTA;
        if (k - 1 > 0 && jr_next_boolean(r)) {
            bands[k - 1] = JB_LIGHT_GRAY_TERRACOTTA;
        }
        if (k + 1 < 192 && jr_next_boolean(r)) {
            bands[k + 1] = JB_LIGHT_GRAY_TERRACOTTA;
        }
    }
}

static int get_band(const JSurface *s, int x, int y, int z)
{
    int offset = (int)java_round(jn_normal_value(s->clay_offset, (double)x, 0.0, (double)z) * 4.0);
    return s->clay_bands[(y + offset + 192) % 192];
}

/* ---- biomes -------------------------------------------------------------------------------- */

static const BiomeInfo *biome_info(JSurface *s, const char *name)
{
    for (int i = 0; i < s->info_count; ++i) {
        if (strcmp(s->infos[i].name, name) == 0) {
            return &s->infos[i];
        }
    }
    char path[1100];
    const char *local = strchr(name, ':');
    snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/biome/%s.json", s->root, local != NULL ? local + 1 : name);
    JJson *j = jj_parse_file(path);
    BiomeInfo info;
    info.name = dup_string(name);
    info.temperature = 0.8f;
    info.frozen = false;
    if (j != NULL) {
        const JJson *t = jj_get(j, "temperature");
        if (t != NULL) {
            info.temperature = jj_float(t);
        }
        info.frozen = strcmp(jstr(jj_get(j, "temperature_modifier")), "frozen") == 0;
        jj_free(j);
    }
    if (s->info_count == s->info_cap) {
        s->info_cap = s->info_cap != 0 ? s->info_cap * 2 : 64;
        s->infos = (BiomeInfo *)realloc(s->infos, (size_t)s->info_cap * sizeof(BiomeInfo));
    }
    s->infos[s->info_count] = info;
    return &s->infos[s->info_count++];
}

/* Biome.getHeightAdjustedTemperature(pos, seaLevel) (no cache; it is a pure function). */
static float biome_temperature(JSurface *s, const BiomeInfo *b, int x, int y, int z)
{
    simplex_statics();
    float base = b->temperature;
    if (b->frozen) {
        double d = psn_value(&g_frozen_noise, (double)x * 0.05, (double)z * 0.05) * 7.0;
        double e = psn_value(&g_info_noise, (double)x * 0.2, (double)z * 0.2);
        double f = d + e;
        if (f < 0.3) {
            double g = psn_value(&g_info_noise, (double)x * 0.09, (double)z * 0.09);
            if (g < 0.8) {
                base = 0.2f;
            }
        }
    }
    int snow_level = s->sea_level + 17;
    if (y > snow_level) {
        float f = (float)(psn_value(&g_temperature_noise, (double)((float)x / 8.0f), (double)((float)z / 8.0f)) * 8.0);
        return base - (f + (float)y - (float)snow_level) * 0.05f / 40.0f;
    }
    return base;
}

float jsurface_biome_temperature(JSurface *s, const char *biome, int x, int y, int z)
{
    return biome_temperature(s, biome_info(s, biome), x, y, z);
}

/* A long-running game keeps generating new chunks, so the stored biomes are a bounded cache: when it is
 * full the older half is dropped (and recomputed from the same noise if it is asked for again). */
#define BIOME_STORE_MAX 1536

static unsigned biome_hash_pos(int cx, int cz, int cap)
{
    uint32_t h = (uint32_t)cx * 0x9E3779B1u ^ ((uint32_t)cz + 0x7F4A7C15u) * 0x85EBCA6Bu;
    h ^= h >> 15;
    return (unsigned)(h % (uint32_t)cap);
}

static void biome_hash_rebuild(JSurface *s)
{
    int cap = 4096;
    int *tab = (int *)calloc((size_t)cap, sizeof(int));
    if (tab == NULL) {
        free(s->stored_hash);
        s->stored_hash = NULL;
        s->stored_hash_cap = 0;
        return;
    }
    for (int i = 0; i < s->stored_count; ++i) {
        unsigned p = biome_hash_pos(s->stored[i]->cx, s->stored[i]->cz, cap);
        while (tab[p] != 0) {
            p = (p + 1u) % (unsigned)cap;
        }
        tab[p] = i + 1;
    }
    free(s->stored_hash);
    s->stored_hash = tab;
    s->stored_hash_cap = cap;
}

static BiomeChunk *biome_chunk(JSurface *s, int cx, int cz)
{
    if (s->last_stored != NULL && s->last_stored->cx == cx && s->last_stored->cz == cz) {
        return s->last_stored;
    }
    if (s->stored_hash != NULL) {
        unsigned p = biome_hash_pos(cx, cz, s->stored_hash_cap);
        while (s->stored_hash[p] != 0) {
            BiomeChunk *e = s->stored[s->stored_hash[p] - 1];
            if (e->cx == cx && e->cz == cz) {
                s->last_stored = e;
                return e;
            }
            p = (p + 1u) % (unsigned)s->stored_hash_cap;
        }
    }
    if (s->stored_count >= BIOME_STORE_MAX) {
        int drop = s->stored_count / 2;
        for (int i = 0; i < drop; ++i) {
            free(s->stored[i]);
        }
        memmove(s->stored, s->stored + drop, (size_t)(s->stored_count - drop) * sizeof(BiomeChunk *));
        s->stored_count -= drop;
        s->last_stored = NULL;
        biome_hash_rebuild(s);
    }
    BiomeChunk *bc = (BiomeChunk *)calloc(1, sizeof(BiomeChunk));
    if (bc == NULL) {
        return NULL;
    }
    bc->cx = cx;
    bc->cz = cz;
    /* LevelChunkSection.fillBiomesFromNoise for sections 0..23. */
    for (int sec = 0; sec < 24; ++sec) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                for (int k = 0; k < 4; ++k) {
                    int qx = cx * 4 + i, qy = -16 + sec * 4 + j, qz = cz * 4 + k;
                    JTargetPoint t = jclimate_sample(s->rs, qx, qy, qz);
                    bc->q[(qy + 16) * 16 + k * 4 + i] = jbiome_find(s->biomes, &t);
                }
            }
        }
    }
    if (s->stored_count == s->stored_cap) {
        s->stored_cap = s->stored_cap != 0 ? s->stored_cap * 2 : 64;
        s->stored = (BiomeChunk **)realloc(s->stored, (size_t)s->stored_cap * sizeof(BiomeChunk *));
    }
    s->stored[s->stored_count++] = bc;
    s->last_stored = bc;
    if (s->stored_hash == NULL) {
        biome_hash_rebuild(s);
    } else {
        unsigned p = biome_hash_pos(cx, cz, s->stored_hash_cap);
        while (s->stored_hash[p] != 0) {
            p = (p + 1u) % (unsigned)s->stored_hash_cap;
        }
        s->stored_hash[p] = s->stored_count;
    }
    return bc;
}

/* Make sure the biome storage of a chunk and its 8 neighbours exists, in the canonical
 * order dz = -1..1, dx = -1..1 (what chunk generation would have done before the surface
 * stage reads them). */
void jsurface_prepare_biomes(JSurface *s, int cx, int cz)
{
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            biome_chunk(s, cx + dx, cz + dz);
        }
    }
}

void jsurface_prepare_chunk(JSurface *s, int chunk_x, int chunk_z)
{
    biome_chunk(s, chunk_x, chunk_z);
}

int jsurface_chunk_biomes(JSurface *s, int chunk_x, int chunk_z, const char **out, int cap)
{
    BiomeChunk *bc = biome_chunk(s, chunk_x, chunk_z);
    int n = 0;
    for (int i = 0; bc != NULL && i < 16 * BIOME_QUART_Y; ++i) {
        bool seen = false;
        for (int k = 0; k < n; ++k) {
            if (out[k] == bc->q[i] || strcmp(out[k], bc->q[i]) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen && n < cap) {
            out[n++] = bc->q[i];
        }
    }
    return n;
}

int jsurface_possible_biome_count(const JSurface *s)
{
    return jbiome_name_count(s->biomes);
}

const char *jsurface_possible_biome(const JSurface *s, int i)
{
    return jbiome_name(s->biomes, i);
}

const char *jsurface_noise_biome(JSurface *s, int qx, int qy, int qz)
{
    if (qy < -16) {
        qy = -16;
    } else if (qy > 79) {
        qy = 79;
    }
    BiomeChunk *bc = biome_chunk(s, qx >> 2, qz >> 2);
    return bc != NULL ? bc->q[(qy + 16) * 16 + (qz & 3) * 4 + (qx & 3)] : "minecraft:plains";
}

/* LinearCongruentialGenerator.next */
static int64_t lcg_next(int64_t seed, int64_t increment)
{
    uint64_t s = (uint64_t)seed;
    return (int64_t)(s * (s * 6364136223846793005ULL + 1442695040888963407ULL) + (uint64_t)increment);
}

static double fiddle(int64_t l)
{
    int m = (int)((l >> 24) & 1023);
    return ((double)m / 1024.0 - 0.5) * 0.9;
}

static double fiddled_distance(int64_t seed, int x, int y, int z, double xf, double yf, double zf)
{
    int64_t l = lcg_next(seed, x);
    l = lcg_next(l, y);
    l = lcg_next(l, z);
    l = lcg_next(l, x);
    l = lcg_next(l, y);
    l = lcg_next(l, z);
    double d = fiddle(l);
    l = lcg_next(l, seed);
    double e = fiddle(l);
    l = lcg_next(l, seed);
    double f = fiddle(l);
    double a = zf + f;
    double b = yf + e;
    double c = xf + d;
    return a * a + b * b + c * c;
}

const char *jsurface_biome_at(JSurface *s, int x, int y, int z)
{
    int i = x - 2, j = y - 2, k = z - 2;
    int l = i >> 2, m = j >> 2, n = k >> 2;
    double d = (double)(i & 3) / 4.0;
    double e = (double)(j & 3) / 4.0;
    double f = (double)(k & 3) / 4.0;
    int best = 0;
    double best_d = INFINITY;
    for (int o = 0; o < 8; ++o) {
        bool bx = (o & 4) == 0, by = (o & 2) == 0, bz = (o & 1) == 0;
        int p = bx ? l : l + 1;
        int q = by ? m : m + 1;
        int r = bz ? n : n + 1;
        double g = bx ? d : d - 1.0;
        double h = by ? e : e - 1.0;
        double t = bz ? f : f - 1.0;
        double u = fiddled_distance(s->zoom_seed, p, q, r, g, h, t);
        if (best_d > u) {
            best = o;
            best_d = u;
        }
    }
    int qx = (best & 4) == 0 ? l : l + 1;
    int qy = (best & 2) == 0 ? m : m + 1;
    int qz = (best & 1) == 0 ? n : n + 1;
    return jsurface_noise_biome(s, qx, qy, qz);
}

/* ---- SurfaceRules.Context -------------------------------------------------------------------- */

typedef struct Ctx {
    JSurface *s;
    JChunk *chunk;
    uint8_t *blocks;
    JHeightmaps *hm;
    int chunk_min_x, chunk_min_z;
    /* updateXZ */
    int block_x, block_z, surface_depth;
    int64_t xz_epoch;      /* value of s->epoch_xz after the last updateXZ */
    int64_t secondary_epoch;
    double surface_secondary;
    int64_t min_level_epoch;
    int min_surface_level;
    int64_t prelim_cell_key;
    bool prelim_valid;
    int prelim[4];
    /* updateY */
    int block_y, water_height, stone_below, stone_above;
    int64_t y_epoch;
    const char *biome;
    bool biome_valid;
    /* shared context conditions */
    Cond steep, hole, above, temperature;
} Ctx;

static int ctx_block(const Ctx *c, int x, int y, int z)
{
    if (y < MIN_Y || y > MAX_Y) {
        return JB_AIR;
    }
    return c->blocks[JFILL_INDEX(x - c->chunk_min_x, z - c->chunk_min_z, y)];
}

static void ctx_set_block(Ctx *c, int x, int y, int z, int state)
{
    if (y < MIN_Y || y > MAX_Y) {
        return;
    }
    int lx = x - c->chunk_min_x, lz = z - c->chunk_min_z;
    c->blocks[JFILL_INDEX(lx, lz, y)] = (uint8_t)state;
    if (c->hm != NULL) {
        jheightmaps_update(c->hm, c->blocks, lx, y, lz, state);
    }
}

static int surface_depth_at(JSurface *s, int x, int z)
{
    double noise = jn_normal_value(s->surface_noise, (double)x, 0.0, (double)z);
    JrRandom r = jr_pos_at(&s->noise_random, x, 0, z);
    return (int)(noise * 2.75 + 3.0 + jr_next_double(&r) * 0.25);
}

static void ctx_update_xz(Ctx *c, int x, int z)
{
    c->s->epoch_xz++;
    c->s->epoch_y++;
    c->xz_epoch = c->s->epoch_xz;
    c->block_x = x;
    c->block_z = z;
    c->surface_depth = surface_depth_at(c->s, x, z);
}

static void ctx_update_y(Ctx *c, int stone_above, int stone_below, int water_height, int x, int y, int z)
{
    c->s->epoch_y++;
    c->y_epoch = c->s->epoch_y;
    c->biome_valid = false;
    c->block_x = x;
    c->block_y = y;
    c->block_z = z;
    c->water_height = water_height;
    c->stone_below = stone_below;
    c->stone_above = stone_above;
}

static double ctx_surface_secondary(Ctx *c)
{
    if (c->secondary_epoch != c->xz_epoch) {
        c->secondary_epoch = c->xz_epoch;
        c->surface_secondary = jn_normal_value(c->s->surface_secondary, (double)c->block_x, 0.0, (double)c->block_z);
    }
    return c->surface_secondary;
}

static int prelim_surface(Ctx *c, int cell_x, int cell_z)
{
    int x = cell_x << 4, z = cell_z << 4;
    double v = jchunk_eval_router(c->chunk, "preliminary_surface_level", x, 0, z, false);
    return mth_floor(v);
}

static int ctx_min_surface_level(Ctx *c)
{
    if (c->min_level_epoch != c->xz_epoch) {
        c->min_level_epoch = c->xz_epoch;
        int cx = c->block_x >> 4, cz = c->block_z >> 4;
        int64_t key = ((int64_t)cx << 32) ^ (int64_t)(uint32_t)cz;
        if (!c->prelim_valid || c->prelim_cell_key != key) {
            c->prelim_valid = true;
            c->prelim_cell_key = key;
            c->prelim[0] = prelim_surface(c, cx, cz);
            c->prelim[1] = prelim_surface(c, cx + 1, cz);
            c->prelim[2] = prelim_surface(c, cx, cz + 1);
            c->prelim[3] = prelim_surface(c, cx + 1, cz + 1);
        }
        double dx = (double)((float)(c->block_x & 15) / 16.0f);
        double dz = (double)((float)(c->block_z & 15) / 16.0f);
        int f = mth_floor(mth_lerp2(dx, dz, (double)c->prelim[0], (double)c->prelim[1], (double)c->prelim[2],
                                    (double)c->prelim[3]));
        c->min_surface_level = f + c->surface_depth - 8;
    }
    return c->min_surface_level;
}

static const char *ctx_biome(Ctx *c)
{
    if (!c->biome_valid) {
        c->biome = jsurface_biome_at(c->s, c->block_x, c->block_y, c->block_z);
        c->biome_valid = true;
    }
    return c->biome;
}

static int ctx_height(const Ctx *c, int lx, int lz)
{
    return jheightmaps_world_surface_height(c->hm, lx, lz);
}

static bool steep_compute(Ctx *c)
{
    int x = c->block_x & 15, z = c->block_z & 15;
    int z0 = z - 1 > 0 ? z - 1 : 0;
    int z1 = z + 1 < 15 ? z + 1 : 15;
    int h0 = ctx_height(c, x, z0);
    int h1 = ctx_height(c, x, z1);
    if (h1 >= h0 + 4) {
        return true;
    }
    int x0 = x - 1 > 0 ? x - 1 : 0;
    int x1 = x + 1 < 15 ? x + 1 : 15;
    int h2 = ctx_height(c, x0, z);
    int h3 = ctx_height(c, x1, z);
    return h2 >= h3 + 4;
}

static bool cond_test(Ctx *c, Cond *cd);

static bool cond_compute(Ctx *c, Cond *cd)
{
    switch (cd->kind) {
    case C_BIOME: {
        const char *b = ctx_biome(c);
        for (int i = 0; i < cd->biome_count; ++i) {
            if (strcmp(cd->biomes[i], b) == 0) {
                return true;
            }
        }
        return false;
    }
    case C_NOISE: {
        double v = jn_normal_value(cd->noise, (double)c->block_x, 0.0, (double)c->block_z);
        return v >= cd->min_threshold && v <= cd->max_threshold;
    }
    case C_STONE_DEPTH: {
        int depth = cd->ceiling ? c->stone_below : c->stone_above;
        int surf = cd->add_surface_depth ? c->surface_depth : 0;
        int secondary = cd->secondary_range == 0
                            ? 0
                            : (int)mth_map(ctx_surface_secondary(c), -1.0, 1.0, 0.0, (double)cd->secondary_range);
        return depth <= 1 + cd->offset + surf + secondary;
    }
    case C_WATER:
        if (c->water_height == WATER_UNSET) {
            return true;
        }
        return c->block_y + (cd->add_stone_depth ? c->stone_above : 0) >=
               c->water_height + cd->offset + c->surface_depth * cd->multiplier;
    case C_Y_ABOVE:
        return c->block_y + (cd->add_stone_depth ? c->stone_above : 0) >=
               cd->anchor_y + c->surface_depth * cd->multiplier;
    case C_STEEP:
        return steep_compute(c);
    case C_HOLE:
        return c->surface_depth <= 0;
    case C_ABOVE_PRELIM:
        return c->block_y >= ctx_min_surface_level(c);
    case C_TEMPERATURE: {
        const BiomeInfo *b = biome_info(c->s, ctx_biome(c));
        return biome_temperature(c->s, b, c->block_x, c->block_y, c->block_z) < 0.15f;
    }
    case C_NOT:
        return !cond_test(c, cd->child);
    case C_GRADIENT: {
        int y = c->block_y;
        if (y <= cd->anchor_y) {
            return true;
        }
        if (y >= cd->anchor_y2) {
            return false;
        }
        double t = mth_map((double)y, (double)cd->anchor_y, (double)cd->anchor_y2, 1.0, 0.0);
        JrRandom r = jr_pos_at(&cd->gradient, c->block_x, y, c->block_z);
        return (double)jr_next_float(&r) < t;
    }
    }
    return false;
}

/* Condition.test(): XZ-lazy, Y-lazy or uncached, exactly as SurfaceRules wires them. */
static bool cond_test(Ctx *c, Cond *cd)
{
    switch (cd->kind) {
    case C_NOISE:
    case C_STEEP:
    case C_HOLE: {
        Cond *slot = cd;
        if (cd->kind == C_STEEP) {
            slot = &c->steep;
        } else if (cd->kind == C_HOLE) {
            slot = &c->hole;
        }
        if (slot->last_update == c->s->epoch_xz) {
            return slot->result;
        }
        slot->last_update = c->s->epoch_xz;
        slot->result = cond_compute(c, cd);
        return slot->result;
    }
    case C_BIOME:
    case C_STONE_DEPTH:
    case C_WATER:
    case C_Y_ABOVE:
    case C_TEMPERATURE:
    case C_GRADIENT: {
        Cond *slot = cd->kind == C_TEMPERATURE ? &c->temperature : cd;
        if (slot->last_update == c->s->epoch_y) {
            return slot->result;
        }
        slot->last_update = c->s->epoch_y;
        slot->result = cond_compute(c, cd);
        return slot->result;
    }
    case C_ABOVE_PRELIM:
    case C_NOT:
        return cond_compute(c, cd);
    }
    return false;
}

static int rule_apply(Ctx *c, const Rule *r, int x, int y, int z)
{
    switch (r->kind) {
    case R_SEQUENCE:
        for (int i = 0; i < r->child_count; ++i) {
            int v = rule_apply(c, r->children[i], x, y, z);
            if (v >= 0) {
                return v;
            }
        }
        return -1;
    case R_TEST:
        return cond_test(c, r->cond) ? rule_apply(c, r->followup, x, y, z) : -1;
    case R_BLOCK:
        return r->block;
    case R_BANDLANDS:
        return get_band(c->s, x, y, z);
    }
    return -1;
}

static void ctx_init(Ctx *c, JSurface *s, JChunk *chunk, uint8_t *blocks, JHeightmaps *hm)
{
    memset(c, 0, sizeof(*c));
    c->s = s;
    c->chunk = chunk;
    c->blocks = blocks;
    c->hm = hm;
    c->chunk_min_x = jchunk_min_x(chunk);
    c->chunk_min_z = jchunk_min_z(chunk);
    c->steep.last_update = c->hole.last_update = c->temperature.last_update = -1;
    c->xz_epoch = c->secondary_epoch = c->min_level_epoch = -1;
    c->water_height = WATER_UNSET;
}

/* ---- SurfaceSystem.buildSurface --------------------------------------------------------------------- */

static bool is_stone(int b)
{
    return !jblock_is_air(b) && !jblock_is_fluid(b);
}

static void eroded_badlands_extension(Ctx *c, int x, int z, int surface_y)
{
    JSurface *s = c->s;
    double e = fmin(fabs(jn_normal_value(s->badlands_surface, (double)x, 0.0, (double)z) * 8.25),
                    jn_normal_value(s->badlands_pillar, (double)x * 0.2, 0.0, (double)z * 0.2) * 15.0);
    if (e <= 0.0) {
        return;
    }
    double h = fabs(jn_normal_value(s->badlands_roof, (double)x * 0.75, 0.0, (double)z * 0.75) * 1.5);
    double i = 64.0 + fmin(e * e * 2.5, ceil(h * 50.0) + 24.0);
    int j = mth_floor(i);
    if (surface_y > j) {
        return;
    }
    for (int k = j; k >= MIN_Y; --k) {
        int b = ctx_block(c, x, k, z);
        if (b == s->default_block) {
            break;
        }
        if (b == JB_WATER) {
            return;
        }
    }
    for (int k = j; k >= MIN_Y; --k) {
        if (!jblock_is_air(ctx_block(c, x, k, z))) {
            break;
        }
        ctx_set_block(c, x, k, z, s->default_block);
    }
}

static void frozen_ocean_extension(Ctx *c, int min_surface_level, const char *biome, int x, int z, int surface_y)
{
    JSurface *s = c->s;
    double e = fmin(fabs(jn_normal_value(s->iceberg_surface, (double)x, 0.0, (double)z) * 8.25),
                    jn_normal_value(s->iceberg_pillar, (double)x * 1.28, 0.0, (double)z * 1.28) * 15.0);
    if (e <= 1.8) {
        return;
    }
    double h = fabs(jn_normal_value(s->iceberg_roof, (double)x * 1.17, 0.0, (double)z * 1.17) * 1.5);
    double i = fmin(e * e * 1.2, ceil(h * 40.0) + 14.0);
    const BiomeInfo *bi = biome_info(s, biome);
    if (biome_temperature(s, bi, x, s->sea_level, z) > 0.1f) {
        i = i - 2.0;
    }
    double j, top;
    if (i > 2.0) {
        j = (double)s->sea_level - i - 7.0;
        i = i + (double)s->sea_level;
    } else {
        i = 0.0;
        j = 0.0;
    }
    top = i;
    JrRandom rnd = jr_pos_at(&s->noise_random, x, 0, z);
    int m = 2 + jr_next_int_bound(&rnd, 4);
    int n = s->sea_level + 18 + jr_next_int_bound(&rnd, 10);
    int o = 0;
    int start = (int)top + 1;
    if (surface_y > start) {
        start = surface_y;
    }
    for (int p = start; p >= min_surface_level; --p) {
        int b = ctx_block(c, x, p, z);
        bool melt_air = jblock_is_air(b) && p < (int)top && jr_next_double(&rnd) > 0.01;
        bool melt_water = false;
        if (!melt_air) {
            melt_water = b == JB_WATER && p > (int)j && p < s->sea_level && j != 0.0 && jr_next_double(&rnd) > 0.15;
        }
        if (melt_air || melt_water) {
            if (o <= m && p > n) {
                ctx_set_block(c, x, p, z, JB_SNOW_BLOCK);
                ++o;
            } else {
                ctx_set_block(c, x, p, z, JB_PACKED_ICE);
            }
        }
    }
}

bool jsurface_build(JSurface *s, JChunk *chunk, uint8_t *blocks, JHeightmaps *hm)
{
    if (s == NULL || chunk == NULL || blocks == NULL || hm == NULL) {
        return false;
    }
    Ctx c;
    ctx_init(&c, s, chunk, blocks, hm);
    int min_x = c.chunk_min_x, min_z = c.chunk_min_z;
    jsurface_prepare_biomes(s, min_x >> 4, min_z >> 4);
    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            int x = min_x + lx, z = min_z + lz;
            int surface_y = jheightmaps_world_surface_height(hm, lx, lz) + 1;
            const char *biome = jsurface_biome_at(s, x, surface_y, z);
            if (strcmp(biome, "minecraft:eroded_badlands") == 0) {
                eroded_badlands_extension(&c, x, z, surface_y);
            }
            int top = jheightmaps_world_surface_height(hm, lx, lz) + 1;
            ctx_update_xz(&c, x, z);
            int stone_above = 0;
            int water_height = WATER_UNSET;
            int floor_y = INT32_MAX;
            for (int y = top; y >= MIN_Y; --y) {
                int b = ctx_block(&c, x, y, z);
                if (jblock_is_air(b)) {
                    stone_above = 0;
                    water_height = WATER_UNSET;
                    continue;
                }
                if (jblock_is_fluid(b)) {
                    if (water_height == WATER_UNSET) {
                        water_height = y + 1;
                    }
                    continue;
                }
                if (floor_y >= y) {
                    floor_y = MIN_Y - 1;
                    for (int q = y - 1; q >= MIN_Y - 1; --q) {
                        if (!is_stone(ctx_block(&c, x, q, z))) {
                            floor_y = q + 1;
                            break;
                        }
                    }
                }
                ++stone_above;
                int stone_below = y - floor_y + 1;
                ctx_update_y(&c, stone_above, stone_below, water_height, x, y, z);
                if (b != s->default_block) {
                    continue;
                }
                int r = rule_apply(&c, s->rule, x, y, z);
                if (r >= 0) {
                    ctx_set_block(&c, x, y, z, r);
                }
            }
            if (strcmp(biome, "minecraft:frozen_ocean") == 0 || strcmp(biome, "minecraft:deep_frozen_ocean") == 0) {
                frozen_ocean_extension(&c, ctx_min_surface_level(&c), biome, x, z, surface_y);
            }
        }
    }
    return true;
}

int jsurface_top_material(void *user, int x, int y, int z, bool has_fluid)
{
    JSurfaceTopUser *u = (JSurfaceTopUser *)user;
    Ctx c;
    ctx_init(&c, u->surface, u->chunk, u->blocks, (JHeightmaps *)u->heightmaps);
    jsurface_prepare_biomes(u->surface, c.chunk_min_x >> 4, c.chunk_min_z >> 4);
    ctx_update_xz(&c, x, z);
    ctx_update_y(&c, 1, 1, has_fluid ? y + 1 : WATER_UNSET, x, y, z);
    return rule_apply(&c, u->surface->rule, x, y, z);
}

/* ---- construction ------------------------------------------------------------------------------------------ */

void jsurface_free(JSurface *s)
{
    if (s == NULL) {
        return;
    }
    for (int i = 0; i < s->cond_count; ++i) {
        Cond *c = s->all_conds[i];
        for (int k = 0; k < c->biome_count; ++k) {
            free(c->biomes[k]);
        }
        free(c->biomes);
        free(c);
    }
    free(s->all_conds);
    for (int i = 0; i < s->rule_count; ++i) {
        free(s->all_rules[i]->children);
        free(s->all_rules[i]);
    }
    free(s->all_rules);
    for (int i = 0; i < s->info_count; ++i) {
        free(s->infos[i].name);
    }
    free(s->infos);
    for (int i = 0; i < s->stored_count; ++i) {
        free(s->stored[i]);
    }
    free(s->stored);
    free(s->stored_hash);
    jbiome_free(s->biomes);
    free(s);
}

JSurface *jsurface_create(JRandomState *rs)
{
    if (rs == NULL) {
        return NULL;
    }
    JSurface *s = (JSurface *)calloc(1, sizeof(JSurface));
    if (s == NULL) {
        return NULL;
    }
    s->rs = rs;
    snprintf(s->root, sizeof(s->root), "%s", jrs_root(rs));
    s->zoom_seed = obfuscate_seed(jrs_seed(rs));
    s->sea_level = jrs_sea_level(rs);
    s->noise_random = *jrs_random(rs);
    s->default_block = JB_STONE;
    s->biomes = jbiome_load(s->root);
    s->surface_noise = jrs_noise(rs, "minecraft:surface");
    s->surface_secondary = jrs_noise(rs, "minecraft:surface_secondary");
    s->clay_offset = jrs_noise(rs, "minecraft:clay_bands_offset");
    s->badlands_pillar = jrs_noise(rs, "minecraft:badlands_pillar");
    s->badlands_roof = jrs_noise(rs, "minecraft:badlands_pillar_roof");
    s->badlands_surface = jrs_noise(rs, "minecraft:badlands_surface");
    s->iceberg_pillar = jrs_noise(rs, "minecraft:iceberg_pillar");
    s->iceberg_roof = jrs_noise(rs, "minecraft:iceberg_pillar_roof");
    s->iceberg_surface = jrs_noise(rs, "minecraft:iceberg_surface");
    JrRandom bands = jr_pos_from_hash_of(&s->noise_random, "minecraft:clay_bands");
    generate_bands(&bands, s->clay_bands);

    char path[1100];
    snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/noise_settings/overworld.json", s->root);
    JJson *ns = jj_parse_file(path);
    g_parse_failed = false;
    if (ns != NULL) {
        s->rule = parse_rule(s, jj_get(ns, "surface_rule"));
        jj_free(ns);
    }
    simplex_statics();
    if (s->biomes == NULL || s->rule == NULL || g_parse_failed || s->surface_noise == NULL ||
        s->surface_secondary == NULL || s->clay_offset == NULL || s->badlands_pillar == NULL ||
        s->badlands_roof == NULL || s->badlands_surface == NULL || s->iceberg_pillar == NULL ||
        s->iceberg_roof == NULL || s->iceberg_surface == NULL) {
        jsurface_free(s);
        return NULL;
    }
    return s;
}
