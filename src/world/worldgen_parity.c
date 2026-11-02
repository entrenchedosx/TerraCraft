#include "world/worldgen_parity.h"

#include "world/jnoise.h"
#include "world/jrandom.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 512
#define FIELDS_MAX 8

typedef struct Ctx {
    FILE *report;
    WorldgenParityStats *stats;
    const char *suite;
} Ctx;

/* Split a CSV line in place, keeping empty fields. Returns the field count. */
static int split_csv(char *line, char *fields[FIELDS_MAX])
{
    int n = 0;
    char *p = line;
    while (n < FIELDS_MAX) {
        fields[n++] = p;
        char *c = strchr(p, ',');
        if (c == NULL) {
            break;
        }
        *c = '\0';
        p = c + 1;
    }
    size_t len = strlen(fields[n - 1]);
    while (len > 0 && (fields[n - 1][len - 1] == '\n' || fields[n - 1][len - 1] == '\r')) {
        fields[n - 1][--len] = '\0';
    }
    return n;
}

static uint64_t hex64(const char *s)
{
    return (uint64_t)strtoull(s, NULL, 16);
}

static double hex_double(const char *s)
{
    uint64_t bits = hex64(s);
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

static uint64_t double_bits(double d)
{
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

static uint32_t float_bits(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static void record(Ctx *c, const char *key, uint64_t expected, uint64_t actual)
{
    c->stats->checked++;
    if (expected != actual) {
        c->stats->mismatched++;
        if (c->report != NULL) {
            fprintf(c->report, "%s,%s,%" PRIx64 ",%" PRIx64 "\n", c->suite, key, expected, actual);
        }
    }
}

static FILE *open_fixture(const char *dir, const char *name, WorldgenParityStats *stats)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        stats->missing++;
    }
    return f;
}

static void make_rng(JrRandom *r, const char *gen, int64_t seed)
{
    if (strcmp(gen, "legacy") == 0) {
        jr_legacy_init(r, seed);
    } else {
        jr_xoroshiro_init(r, seed);
    }
}

/* ---- rng.csv: gen,seed,op,index,value_hex ---------------------------------
 * Rows arrive in the order the reference consumed them, so each (gen, seed)
 * group is replayed with one generator instance. */
static void run_rng(const char *dir, Ctx *c)
{
    FILE *f = open_fixture(dir, "rng.csv", c->stats);
    if (f == NULL) {
        return;
    }
    char line[LINE_MAX_LEN];
    char *fld[FIELDS_MAX];
    c->suite = "rng";
    JrRandom r;
    char cur_gen[16] = "";
    char cur_seed[64] = "";
    bool have = false;
    fgets(line, sizeof(line), f); /* header */
    while (fgets(line, sizeof(line), f) != NULL) {
        if (split_csv(line, fld) < 5) {
            continue;
        }
        const char *gen = fld[0];
        const char *seed_s = fld[1];
        const char *op = fld[2];
        const char *idx = fld[3];
        uint64_t expected = hex64(fld[4]);
        if (strcmp(gen, "xoroshiro128") == 0) {
            /* seed field is "lo_hi"; rows are nextLong of that raw state. */
            int64_t lo, hi;
            if (strcmp(cur_seed, seed_s) != 0 || strcmp(cur_gen, gen) != 0) {
                sscanf(seed_s, "%" SCNd64 "_%" SCNd64, &lo, &hi);
                jr_xoroshiro_init128(&r, lo, hi);
                snprintf(cur_gen, sizeof(cur_gen), "%s", gen);
                snprintf(cur_seed, sizeof(cur_seed), "%s", seed_s);
            }
        } else if (!have || strcmp(cur_seed, seed_s) != 0 || strcmp(cur_gen, gen) != 0) {
            make_rng(&r, gen, (int64_t)strtoll(seed_s, NULL, 10));
            snprintf(cur_gen, sizeof(cur_gen), "%s", gen);
            snprintf(cur_seed, sizeof(cur_seed), "%s", seed_s);
            have = true;
        }
        char key[96];
        snprintf(key, sizeof(key), "%s/%s/%s/%s", gen, seed_s, op, idx);
        uint64_t actual = 0;
        if (strcmp(op, "nextInt") == 0) {
            actual = (uint32_t)jr_next_int(&r);
        } else if (strncmp(op, "nextIntB", 8) == 0) {
            actual = (uint32_t)jr_next_int_bound(&r, atoi(op + 8));
        } else if (strcmp(op, "nextLong") == 0) {
            actual = (uint64_t)jr_next_long(&r);
        } else if (strcmp(op, "nextBoolean") == 0) {
            actual = jr_next_boolean(&r) ? 1u : 0u;
            expected = (uint64_t)strtoull(fld[4], NULL, 10);
        } else if (strcmp(op, "nextFloat") == 0) {
            actual = float_bits(jr_next_float(&r));
        } else if (strcmp(op, "nextDouble") == 0) {
            actual = double_bits(jr_next_double(&r));
        } else if (strcmp(op, "nextGaussian") == 0) {
            actual = double_bits(jr_next_gaussian(&r));
        } else {
            continue;
        }
        record(c, key, expected, actual);
    }
    fclose(f);
}

/* ---- positional.csv: gen,seed,kind,key,index,value_hex -------------------- */
static void run_positional(const char *dir, Ctx *c)
{
    FILE *f = open_fixture(dir, "positional.csv", c->stats);
    if (f == NULL) {
        return;
    }
    char line[LINE_MAX_LEN];
    char *fld[FIELDS_MAX];
    c->suite = "positional";
    fgets(line, sizeof(line), f);
    char cur[64] = "";
    JrPositional fac;
    JrRandom r;
    char cur_kind[16] = "";
    char cur_key[96] = "";
    while (fgets(line, sizeof(line), f) != NULL) {
        if (split_csv(line, fld) < 6) {
            continue;
        }
        const char *gen = fld[0];
        const char *seed_s = fld[1];
        const char *kind = fld[2];
        const char *key = fld[3];
        char group[64];
        snprintf(group, sizeof(group), "%s/%s", gen, seed_s);
        if (strcmp(group, cur) != 0) {
            JrRandom base;
            make_rng(&base, gen, (int64_t)strtoll(seed_s, NULL, 10));
            fac = jr_fork_positional(&base);
            snprintf(cur, sizeof(cur), "%s", group);
            cur_kind[0] = '\0';
        }
        if (strcmp(kind, cur_kind) != 0 || strcmp(key, cur_key) != 0) {
            if (strcmp(kind, "at") == 0) {
                int32_t x, y, z;
                sscanf(key, "%" SCNd32 "_%" SCNd32 "_%" SCNd32, &x, &y, &z);
                r = jr_pos_at(&fac, x, y, z);
            } else if (strcmp(kind, "fromHashOf") == 0) {
                r = jr_pos_from_hash_of(&fac, key);
            } else {
                r = jr_pos_from_seed(&fac, (int64_t)strtoll(seed_s, NULL, 10) ^ 0x5555LL);
            }
            snprintf(cur_kind, sizeof(cur_kind), "%s", kind);
            snprintf(cur_key, sizeof(cur_key), "%s", key);
        }
        char label[160];
        snprintf(label, sizeof(label), "%s/%s/%s/%s/%s", gen, seed_s, kind, key, fld[4]);
        record(c, label, hex64(fld[5]), (uint64_t)jr_next_long(&r));
    }
    fclose(f);
}

/* ---- improved_noise.csv: seed,x,y,z,yScale,yFudge,value_hex ---------------- */
static void run_improved(const char *dir, Ctx *c)
{
    FILE *f = open_fixture(dir, "improved_noise.csv", c->stats);
    if (f == NULL) {
        return;
    }
    char line[LINE_MAX_LEN];
    char *fld[FIELDS_MAX];
    c->suite = "improved_noise";
    fgets(line, sizeof(line), f);
    char cur_seed[32] = "";
    JImprovedNoise n;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (split_csv(line, fld) < 7) {
            continue;
        }
        if (strcmp(cur_seed, fld[0]) != 0) {
            JrRandom r;
            jr_xoroshiro_init(&r, (int64_t)strtoll(fld[0], NULL, 10));
            jn_improved_init(&n, &r);
            snprintf(cur_seed, sizeof(cur_seed), "%s", fld[0]);
        }
        double x = hex_double(fld[1]), y = hex_double(fld[2]), z = hex_double(fld[3]);
        /* yScale/yFudge are "0" or hex doubles. */
        double ys = strcmp(fld[4], "0") == 0 ? 0.0 : hex_double(fld[4]);
        double yf = strcmp(fld[5], "0") == 0 ? 0.0 : hex_double(fld[5]);
        char label[160];
        snprintf(label, sizeof(label), "%s/%s_%s_%s/%s_%s", fld[0], fld[1], fld[2], fld[3], fld[4], fld[5]);
        record(c, label, hex64(fld[6]), double_bits(jn_improved_noise_fudge(&n, x, y, z, ys, yf)));
    }
    fclose(f);
}

/* ---- perlin_normal.csv: kind,seed,noise,x,y,z,value_hex --------------------- */
typedef struct NoiseDef {
    const char *name;
    int first;
    int count;
    double amps[9];
} NoiseDef;

static const NoiseDef NOISE_DEFS[] = {
    {"continentalness", -9, 9, {1, 1, 2, 2, 2, 1, 1, 1, 1}},
    {"temperature", -10, 6, {1.5, 0, 1, 0, 0, 0}},
    {"erosion", -9, 5, {1, 1, 0, 1, 1}},
    {"single", 0, 1, {1}},
    {"sparse", -4, 5, {0, 1, 0, 0, 2}},
};

static void run_perlin_normal(const char *dir, Ctx *c)
{
    FILE *f = open_fixture(dir, "perlin_normal.csv", c->stats);
    if (f == NULL) {
        return;
    }
    char line[LINE_MAX_LEN];
    char *fld[FIELDS_MAX];
    c->suite = "perlin_normal";
    fgets(line, sizeof(line), f);
    char cur[64] = "";
    JPerlinNoise perlin;
    JNormalNoise normal;
    bool ok = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (split_csv(line, fld) < 7) {
            continue;
        }
        char group[64];
        snprintf(group, sizeof(group), "%s/%s", fld[1], fld[2]);
        if (strcmp(group, cur) != 0) {
            const NoiseDef *def = NULL;
            for (size_t i = 0; i < sizeof(NOISE_DEFS) / sizeof(NOISE_DEFS[0]); ++i) {
                if (strcmp(NOISE_DEFS[i].name, fld[2]) == 0) {
                    def = &NOISE_DEFS[i];
                }
            }
            ok = def != NULL;
            if (ok) {
                int64_t seed = (int64_t)strtoll(fld[1], NULL, 10);
                JrRandom r1, r2;
                jr_xoroshiro_init(&r1, seed);
                jr_xoroshiro_init(&r2, seed);
                ok = jn_perlin_init(&perlin, &r1, def->first, def->amps, def->count) &&
                     jn_normal_init(&normal, &r2, def->first, def->amps, def->count);
            }
            snprintf(cur, sizeof(cur), "%s", group);
        }
        char label[200];
        snprintf(label, sizeof(label), "%s/%s/%s/%s_%s_%s", fld[0], fld[1], fld[2], fld[3], fld[4], fld[5]);
        uint64_t expected = hex64(fld[6]);
        if (!ok) {
            record(c, label, expected, ~expected);
            continue;
        }
        double x = hex_double(fld[3]), y = hex_double(fld[4]), z = hex_double(fld[5]);
        double v;
        if (strcmp(fld[0], "perlin_max") == 0) {
            v = jn_perlin_max_value(&perlin);
        } else if (strcmp(fld[0], "normal_max") == 0) {
            v = jn_normal_max_value(&normal);
        } else if (strcmp(fld[0], "perlin") == 0) {
            v = jn_perlin_value(&perlin, x, y, z);
        } else {
            v = jn_normal_value(&normal, x, y, z);
        }
        record(c, label, expected, double_bits(v));
    }
    fclose(f);
}

uint64_t worldgen_parity_run(const char *fixture_dir, FILE *report, WorldgenParityStats *stats)
{
    WorldgenParityStats local;
    if (stats == NULL) {
        stats = &local;
    }
    memset(stats, 0, sizeof(*stats));
    if (fixture_dir == NULL) {
        stats->missing = 1;
        return 1;
    }
    Ctx c = {report, stats, ""};
    if (report != NULL) {
        fprintf(report, "suite,key,expected_hex,actual_hex\n");
    }
    run_rng(fixture_dir, &c);
    run_positional(fixture_dir, &c);
    run_improved(fixture_dir, &c);
    run_perlin_normal(fixture_dir, &c);
    return stats->mismatched + stats->missing;
}
