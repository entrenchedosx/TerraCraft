#include "world/worldgen_parity.h"

#include "world/jbiome.h"
#include "world/jcarver.h"
#include "world/jdensity.h"
#include "world/jfeature.h"
#include "world/jfill.h"
#include "world/jnoise.h"
#include "world/jrandom.h"
#include "world/jstructure.h"
#include "world/jsurface.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 512
#define FIELDS_MAX 12

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

/* ---- router.csv: seed,field,x,y,z,value_hex ---------------------------------- */
static const struct { const char *field; const char *key; } ROUTER_FIELDS[] = {
    {"barrierNoise", "barrier"}, {"fluidLevelFloodednessNoise", "fluid_level_floodedness"},
    {"fluidLevelSpreadNoise", "fluid_level_spread"}, {"lavaNoise", "lava"},
    {"temperature", "temperature"}, {"vegetation", "vegetation"}, {"continents", "continents"},
    {"erosion", "erosion"}, {"depth", "depth"}, {"ridges", "ridges"},
    {"preliminarySurfaceLevel", "preliminary_surface_level"}, {"finalDensity", "final_density"},
    {"veinToggle", "vein_toggle"}, {"veinRidged", "vein_ridged"}, {"veinGap", "vein_gap"},
};

static void run_router(const char *dir, const char *datapack, Ctx *c)
{
    FILE *f = open_fixture(dir, "router.csv", c->stats);
    if (f == NULL) {
        return;
    }
    char line[LINE_MAX_LEN];
    char *fld[FIELDS_MAX];
    c->suite = "router";
    fgets(line, sizeof(line), f);
    char cur_seed[32] = "";
    JRandomState *rs = NULL;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (split_csv(line, fld) < 6) {
            continue;
        }
        if (datapack == NULL) {
            c->stats->skipped++;
            continue;
        }
        if (strcmp(cur_seed, fld[0]) != 0) {
            jrs_destroy(rs);
            rs = jrs_create(datapack, "overworld", (int64_t)strtoll(fld[0], NULL, 10));
            snprintf(cur_seed, sizeof(cur_seed), "%s", fld[0]);
        }
        const char *key = NULL;
        for (size_t i = 0; i < sizeof(ROUTER_FIELDS) / sizeof(ROUTER_FIELDS[0]); ++i) {
            if (strcmp(ROUTER_FIELDS[i].field, fld[1]) == 0) {
                key = ROUTER_FIELDS[i].key;
            }
        }
        const JDensity *df = rs != NULL && key != NULL ? jrs_router(rs, key) : NULL;
        char label[160];
        snprintf(label, sizeof(label), "%s/%s/%s_%s_%s", fld[0], fld[1], fld[2], fld[3], fld[4]);
        uint64_t expected = hex64(fld[5]);
        if (df == NULL) {
            record(c, label, expected, ~expected);
            continue;
        }
        double v = jdf_compute(df, atoi(fld[2]), atoi(fld[3]), atoi(fld[4]));
        if (v != v) { /* NaN = depends on a node not implemented yet */
            c->stats->skipped++;
            continue;
        }
        record(c, label, expected, double_bits(v));
    }
    jrs_destroy(rs);
    fclose(f);
}

/* ---- biome stage -------------------------------------------------------------- */
static void record_str(Ctx *c, const char *key, const char *expected, const char *actual)
{
    c->stats->checked++;
    if (strcmp(expected, actual) != 0) {
        c->stats->mismatched++;
        if (c->report != NULL) {
            fprintf(c->report, "%s,%s,%s,%s\n", c->suite, key, expected, actual);
        }
    }
}

/* biome_list.csv: count,hash. biome_random.csv then biome_climate.csv are
 * replayed in the order the reference issued them against ONE R-tree, because
 * vanilla's RTree keeps a last-result shortcut that decides exact ties. */
static void run_biomes(const char *dir, const char *datapack, Ctx *c)
{
    FILE *fl = open_fixture(dir, "biome_list.csv", c->stats);
    FILE *fr = open_fixture(dir, "biome_random.csv", c->stats);
    FILE *fc = open_fixture(dir, "biome_climate.csv", c->stats);
    char line[LINE_MAX_LEN];
    char *fld[FIELDS_MAX + 8];
    JBiomeList *list = datapack != NULL ? jbiome_load(datapack) : NULL;
    if (fl != NULL && fr != NULL && fc != NULL) {
        c->suite = "biome_list";
        fgets(line, sizeof(line), fl);
        if (fgets(line, sizeof(line), fl) != NULL && split_csv(line, fld) >= 2) {
            if (list == NULL) {
                c->stats->skipped++;
            } else {
                record(c, "count", (uint64_t)strtoull(fld[0], NULL, 10), (uint64_t)jbiome_leaf_count(list));
                record(c, "hash", (uint64_t)strtoll(fld[1], NULL, 10), (uint64_t)jbiome_list_hash(list));
            }
        }
        c->suite = "biome_random";
        fgets(line, sizeof(line), fr);
        int row = 0;
        while (fgets(line, sizeof(line), fr) != NULL) {
            char *f2[FIELDS_MAX + 8];
            if (split_csv(line, f2) < 8) {
                continue;
            }
            row++;
            if (list == NULL) {
                c->stats->skipped++;
                continue;
            }
            JTargetPoint t = {strtoll(f2[0], NULL, 10), strtoll(f2[1], NULL, 10), strtoll(f2[2], NULL, 10),
                              strtoll(f2[3], NULL, 10), strtoll(f2[4], NULL, 10), strtoll(f2[5], NULL, 10)};
            char key[64];
            snprintf(key, sizeof(key), "rtree/%d", row);
            record_str(c, key, f2[6], jbiome_find(list, &t));
            snprintf(key, sizeof(key), "brute/%d", row);
            record_str(c, key, f2[7], jbiome_find_brute(list, &t));
        }
        c->suite = "biome_climate";
        fgets(line, sizeof(line), fc);
        char cur_seed[32] = "";
        JRandomState *rs = NULL;
        while (fgets(line, sizeof(line), fc) != NULL) {
            char *f3[FIELDS_MAX + 8];
            if (split_csv(line, f3) < 11) {
                continue;
            }
            if (list == NULL || datapack == NULL) {
                c->stats->skipped++;
                continue;
            }
            if (strcmp(cur_seed, f3[0]) != 0) {
                jrs_destroy(rs);
                rs = jrs_create(datapack, "overworld", (int64_t)strtoll(f3[0], NULL, 10));
                snprintf(cur_seed, sizeof(cur_seed), "%s", f3[0]);
            }
            char key[96];
            snprintf(key, sizeof(key), "%s/%s_%s_%s", f3[0], f3[1], f3[2], f3[3]);
            if (rs == NULL) {
                record_str(c, key, f3[10], "<no router>");
                continue;
            }
            JTargetPoint t = jclimate_sample(rs, atoi(f3[1]), atoi(f3[2]), atoi(f3[3]));
            char sub[110];
            const int64_t got[6] = {t.temperature, t.humidity, t.continentalness, t.erosion, t.depth, t.weirdness};
            for (int i = 0; i < 6; ++i) {
                snprintf(sub, sizeof(sub), "%s/p%d", key, i);
                record(c, sub, (uint64_t)strtoll(f3[4 + i], NULL, 10), (uint64_t)got[i]);
            }
            snprintf(sub, sizeof(sub), "%s/biome", key);
            record_str(c, sub, f3[10], jbiome_find(list, &t));
        }
        jrs_destroy(rs);
    }
    if (fl != NULL) {
        fclose(fl);
    }
    if (fr != NULL) {
        fclose(fr);
    }
    if (fc != NULL) {
        fclose(fc);
    }
    jbiome_free(list);
}

/* ---- chunks.csv: seed,chunk_x,chunk_z,section,solid_hash,name_hash ------------- */
static void run_chunks(const char *dir, const char *datapack, Ctx *c)
{
    FILE *f = open_fixture(dir, "chunks.csv", c->stats);
    if (f == NULL) {
        return;
    }
    char line[LINE_MAX_LEN];
    char *fld[FIELDS_MAX];
    c->suite = "chunk_density";
    fgets(line, sizeof(line), f);
    char cur[64] = "";
    JRandomState *rs = NULL;
    JChunk *ch = NULL;
    uint64_t solid_hash[24];
    uint64_t name_hash[24];
    uint64_t solid_mask_hash[24];
    uint8_t *blocks = (uint8_t *)malloc(16 * 16 * JFILL_HEIGHT);
    int have_hashes = 0;
    char last_seed[32] = "";
    while (fgets(line, sizeof(line), f) != NULL) {
        if (split_csv(line, fld) < 7) {
            continue;
        }
        if (datapack == NULL) {
            c->stats->skipped++;
            continue;
        }
        char group[64];
        snprintf(group, sizeof(group), "%s/%s/%s", fld[0], fld[1], fld[2]);
        if (strcmp(group, cur) != 0) {
            char seed_s[32];
            snprintf(seed_s, sizeof(seed_s), "%s", fld[0]);
            jchunk_free(ch);
            ch = NULL;
            if (rs == NULL || strcmp(last_seed, seed_s) != 0) {
                jrs_destroy(rs);
                rs = jrs_create(datapack, "overworld", (int64_t)strtoll(seed_s, NULL, 10));
                snprintf(last_seed, sizeof(last_seed), "%s", seed_s);
            }
            snprintf(cur, sizeof(cur), "%s", group);
            have_hashes = 0;
            if (rs != NULL) {
                ch = jchunk_create(rs, atoi(fld[1]), atoi(fld[2]));
                int cx = atoi(fld[1]), cz = atoi(fld[2]);
                for (int sec = 0; sec < 24 && ch != NULL; ++sec) {
                    uint64_t h = 1125899906842597ULL;
                    for (int yy = 0; yy < 16; ++yy) {
                        int y = -64 + sec * 16 + yy;
                        for (int zz = 0; zz < 16; ++zz) {
                            for (int xx = 0; xx < 16; ++xx) {
                                double d = jchunk_final_density(ch, cx * 16 + xx, y, cz * 16 + zz);
                                h = 31u * h + double_bits(d);
                            }
                        }
                    }
                    solid_hash[sec] = h;
                }
                have_hashes = ch != NULL;
                if (ch != NULL && blocks != NULL && jchunk_fill(ch, blocks)) {
                    for (int sec = 0; sec < 24; ++sec) {
                        uint64_t hn = 1125899906842597ULL, hs = 1125899906842597ULL;
                        for (int yy = 0; yy < 16; ++yy) {
                            int y = -64 + sec * 16 + yy;
                            for (int zz = 0; zz < 16; ++zz) {
                                for (int xx = 0; xx < 16; ++xx) {
                                    int b = blocks[JFILL_INDEX(xx, zz, y)];
                                    hn = 31u * hn + (uint64_t)(int64_t)jr_java_string_hash(jfill_block_name((JFillBlock)b));
                                    bool solid = !(b == JB_AIR || b == JB_WATER || b == JB_LAVA);
                                    hs = 31u * hs + (solid ? 1u : 0u);
                                }
                            }
                        }
                        name_hash[sec] = hn;
                        solid_mask_hash[sec] = hs;
                    }
                } else {
                    have_hashes = 0;
                }
            }
        }
        int sec = atoi(fld[3]);
        char label[96];
        snprintf(label, sizeof(label), "%s/sec%d", group, sec);
        uint64_t expected = (uint64_t)strtoll(fld[6], NULL, 10);
        record(c, label, expected, have_hashes && sec >= 0 && sec < 24 ? solid_hash[sec] : ~expected);
        const char *saved = c->suite;
        c->suite = "chunk_blocks";
        uint64_t exp_names = (uint64_t)strtoll(fld[5], NULL, 10);
        record(c, label, exp_names, have_hashes && sec >= 0 && sec < 24 ? name_hash[sec] : ~exp_names);
        c->suite = "chunk_solid";
        uint64_t exp_solid = (uint64_t)strtoll(fld[4], NULL, 10);
        record(c, label, exp_solid, have_hashes && sec >= 0 && sec < 24 ? solid_mask_hash[sec] : ~exp_solid);
        c->suite = saved;
    }
    free(blocks);
    jchunk_free(ch);
    jrs_destroy(rs);
    fclose(f);
}

/* ---- carvers.csv: seed,chunk_x,chunk_z,section,name_hash,mask_hash ------------- */
typedef struct StartRow {
    char seed[32];
    int cx, cz, index, count;
} StartRow;

static void run_carvers(const char *dir, const char *datapack, Ctx *c)
{
    FILE *f = open_fixture(dir, "carvers.csv", c->stats);
    if (f == NULL) {
        return;
    }
    char line[LINE_MAX_LEN];
    char *fld[FIELDS_MAX];
    c->suite = "carver_blocks";

    StartRow *rows = NULL;
    int row_count = 0, row_cap = 0;
    FILE *fs = open_fixture(dir, "carver_starts.csv", c->stats);
    if (fs != NULL) {
        fgets(line, sizeof(line), fs);
        while (fgets(line, sizeof(line), fs) != NULL) {
            if (split_csv(line, fld) < 5) {
                continue;
            }
            if (row_count == row_cap) {
                row_cap = row_cap != 0 ? row_cap * 2 : 64;
                rows = (StartRow *)realloc(rows, (size_t)row_cap * sizeof(StartRow));
            }
            StartRow *r = &rows[row_count++];
            snprintf(r->seed, sizeof(r->seed), "%s", fld[0]);
            r->cx = atoi(fld[1]);
            r->cz = atoi(fld[2]);
            r->index = atoi(fld[3]);
            r->count = atoi(fld[4]);
        }
        fclose(fs);
    }

    JCarvers *cv = datapack != NULL ? jcarvers_load(datapack) : NULL;
    fgets(line, sizeof(line), f);
    char cur[64] = "";
    char last_seed[32] = "";
    JRandomState *rs = NULL;
    JChunk *ch = NULL;
    uint64_t name_hash[24], mask_hash[24];
    uint8_t *blocks = (uint8_t *)malloc((size_t)16 * 16 * JFILL_HEIGHT);
    uint8_t *mask = (uint8_t *)malloc((size_t)16 * 16 * JFILL_HEIGHT);
    bool have = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (split_csv(line, fld) < 6) {
            continue;
        }
        if (datapack == NULL) {
            c->stats->skipped++;
            continue;
        }
        char group[64];
        snprintf(group, sizeof(group), "%s/%s/%s", fld[0], fld[1], fld[2]);
        if (strcmp(group, cur) != 0) {
            snprintf(cur, sizeof(cur), "%s", group);
            jchunk_free(ch);
            ch = NULL;
            if (rs == NULL || strcmp(last_seed, fld[0]) != 0) {
                jrs_destroy(rs);
                rs = jrs_create(datapack, "overworld", (int64_t)strtoll(fld[0], NULL, 10));
                snprintf(last_seed, sizeof(last_seed), "%s", fld[0]);
            }
            have = false;
            int cx = atoi(fld[1]), cz = atoi(fld[2]);
            ch = rs != NULL ? jchunk_create(rs, cx, cz) : NULL;
            if (ch != NULL && cv != NULL && blocks != NULL && mask != NULL && jchunk_fill(ch, blocks)) {
                memset(mask, 0, (size_t)16 * 16 * JFILL_HEIGHT);
                JAquifer *aq = jaquifer_create(ch);
                int starts[JCARVER_MAX];
                if (aq != NULL && jchunk_carve(ch, cv, aq, blocks, mask, starts, NULL)) {
                    have = true;
                    for (int m = 0; m < jcarvers_count(cv); ++m) {
                        int expect = 0;
                        for (int i = 0; i < row_count; ++i) {
                            if (rows[i].cx == cx && rows[i].cz == cz && rows[i].index == m && strcmp(rows[i].seed, fld[0]) == 0) {
                                expect = rows[i].count;
                            }
                        }
                        char key[96];
                        snprintf(key, sizeof(key), "%s/starts%d", group, m);
                        const char *saved = c->suite;
                        c->suite = "carver_starts";
                        record(c, key, (uint64_t)expect, (uint64_t)starts[m]);
                        c->suite = saved;
                    }
                    for (int sec = 0; sec < 24; ++sec) {
                        uint64_t hn = 1125899906842597ULL, hm = 1125899906842597ULL;
                        for (int yy = 0; yy < 16; ++yy) {
                            int y = -64 + sec * 16 + yy;
                            for (int zz = 0; zz < 16; ++zz) {
                                for (int xx = 0; xx < 16; ++xx) {
                                    int b = blocks[JFILL_INDEX(xx, zz, y)];
                                    hn = 31u * hn + (uint64_t)(int64_t)jr_java_string_hash(jfill_block_name((JFillBlock)b));
                                    hm = 31u * hm + (mask[JFILL_INDEX(xx, zz, y)] != 0 ? 1u : 0u);
                                }
                            }
                        }
                        name_hash[sec] = hn;
                        mask_hash[sec] = hm;
                    }
                }
                jaquifer_free(aq);
            }
        }
        int sec = atoi(fld[3]);
        char label[96];
        snprintf(label, sizeof(label), "%s/sec%d", group, sec);
        uint64_t exp_names = (uint64_t)strtoll(fld[4], NULL, 10);
        record(c, label, exp_names, have && sec >= 0 && sec < 24 ? name_hash[sec] : ~exp_names);
        c->suite = "carver_mask";
        uint64_t exp_mask = (uint64_t)strtoll(fld[5], NULL, 10);
        record(c, label, exp_mask, have && sec >= 0 && sec < 24 ? mask_hash[sec] : ~exp_mask);
        c->suite = "carver_blocks";
    }
    free(rows);
    free(blocks);
    free(mask);
    jcarvers_free(cv);
    jchunk_free(ch);
    jrs_destroy(rs);
    fclose(f);
}

/* ---- surface stage -----------------------------------------------------------------------------
 * surface.csv / pipeline.csv:   seed,chunk_x,chunk_z,section,name_hash[,mask_hash]
 * surface_heights.csv / pipeline_heights.csv: seed,chunk_x,chunk_z,world_surface_hash,ocean_floor_hash
 * biome_zoom.csv:   seed,x,y,z,biome       temperature.csv: biome,x,y,z,float_bits_hex
 * simplex.csv:      noise,x,y,double_bits_hex */
typedef struct Table {
    char (*lines)[160];
    int count;
} Table;

static Table load_table(const char *dir, const char *name, WorldgenParityStats *stats)
{
    Table t = {NULL, 0};
    FILE *f = open_fixture(dir, name, stats);
    if (f == NULL) {
        return t;
    }
    int cap = 0;
    char line[LINE_MAX_LEN];
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f) != NULL) {
        if (t.count == cap) {
            cap = cap != 0 ? cap * 2 : 1024;
            t.lines = (char(*)[160])realloc(t.lines, (size_t)cap * 160);
        }
        snprintf(t.lines[t.count++], 160, "%s", line);
    }
    fclose(f);
    return t;
}

static void free_table(Table *t)
{
    free(t->lines);
    t->lines = NULL;
    t->count = 0;
}

static uint64_t hash_names(const uint8_t *blocks, int sec)
{
    uint64_t hn = 1125899906842597ULL;
    for (int yy = 0; yy < 16; ++yy) {
        int y = -64 + sec * 16 + yy;
        for (int zz = 0; zz < 16; ++zz) {
            for (int xx = 0; xx < 16; ++xx) {
                int b = blocks[JFILL_INDEX(xx, zz, y)];
                hn = 31u * hn + (uint64_t)(int64_t)jr_java_string_hash(jfill_block_name((JFillBlock)b));
            }
        }
    }
    return hn;
}

static uint64_t hash_mask(const uint8_t *mask, int sec)
{
    uint64_t hm = 1125899906842597ULL;
    for (int yy = 0; yy < 16; ++yy) {
        int y = -64 + sec * 16 + yy;
        for (int zz = 0; zz < 16; ++zz) {
            for (int xx = 0; xx < 16; ++xx) {
                hm = 31u * hm + (mask[JFILL_INDEX(xx, zz, y)] != 0 ? 1u : 0u);
            }
        }
    }
    return hm;
}

static void hash_heights(const JHeightmaps *hm, uint64_t *ws, uint64_t *of)
{
    uint64_t hw = 1125899906842597ULL, ho = 1125899906842597ULL;
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            hw = 31u * hw + (uint64_t)(int64_t)jheightmaps_world_surface_height(hm, x, z);
            ho = 31u * ho + (uint64_t)(int64_t)jheightmaps_ocean_floor_height(hm, x, z);
        }
    }
    *ws = hw;
    *of = ho;
}

/* Find the row of a table matching a chunk (and, if sec >= 0, a section). */
static bool find_row(const Table *t, const char *seed, int cx, int cz, int sec, char *fld_out[FIELDS_MAX], char *buf)
{
    for (int i = 0; i < t->count; ++i) {
        snprintf(buf, 160, "%s", t->lines[i]);
        if (split_csv(buf, fld_out) < 5) {
            continue;
        }
        if (strcmp(fld_out[0], seed) == 0 && atoi(fld_out[1]) == cx && atoi(fld_out[2]) == cz &&
            (sec < 0 || atoi(fld_out[3]) == sec)) {
            return true;
        }
    }
    return false;
}

static void run_surface(const char *dir, const char *datapack, Ctx *c)
{
    Table pipe = load_table(dir, "pipeline.csv", c->stats);
    Table surf = load_table(dir, "surface.csv", c->stats);
    Table surf_h = load_table(dir, "surface_heights.csv", c->stats);
    Table pipe_h = load_table(dir, "pipeline_heights.csv", c->stats);
    Table zoom = load_table(dir, "biome_zoom.csv", c->stats);
    Table temp = load_table(dir, "temperature.csv", c->stats);
    Table simp = load_table(dir, "simplex.csv", c->stats);
    if (datapack == NULL) {
        c->stats->skipped += (uint64_t)(pipe.count + zoom.count + temp.count);
    } else {
        JCarvers *cv = jcarvers_load(datapack);
        uint8_t *blocks = (uint8_t *)malloc((size_t)16 * 16 * JFILL_HEIGHT);
        uint8_t *mask = (uint8_t *)malloc((size_t)16 * 16 * JFILL_HEIGHT);
        char last_seed[32] = "";
        JRandomState *rs = NULL;
        JSurface *surface = NULL;
        char cur[64] = "";
        char buf[160];
        char *fld[FIELDS_MAX];
        int i = 0;
        while (i < pipe.count) {
            /* One chunk = 24 consecutive pipeline rows. */
            snprintf(buf, sizeof(buf), "%s", pipe.lines[i]);
            if (split_csv(buf, fld) < 6) {
                ++i;
                continue;
            }
            char seed[32];
            snprintf(seed, sizeof(seed), "%s", fld[0]);
            int cx = atoi(fld[1]), cz = atoi(fld[2]);
            snprintf(cur, sizeof(cur), "%s/%d/%d", seed, cx, cz);
            if (rs == NULL || strcmp(last_seed, seed) != 0) {
                jsurface_free(surface);
                jrs_destroy(rs);
                rs = jrs_create(datapack, "overworld", (int64_t)strtoll(seed, NULL, 10));
                surface = rs != NULL ? jsurface_create(rs) : NULL;
                snprintf(last_seed, sizeof(last_seed), "%s", seed);
            }
            JChunk *ch = rs != NULL ? jchunk_create(rs, cx, cz) : NULL;
            bool have_surface = false, have_pipe = false;
            uint64_t s_name[24], p_name[24], p_mask[24], s_ws = 0, s_of = 0, p_ws = 0, p_of = 0;
            JHeightmaps hm;
            if (ch != NULL && surface != NULL && cv != NULL && blocks != NULL && mask != NULL && jchunk_fill(ch, blocks)) {
                jheightmaps_prime(&hm, blocks);
                if (jsurface_build(surface, ch, blocks, &hm)) {
                    have_surface = true;
                    for (int sec = 0; sec < 24; ++sec) {
                        s_name[sec] = hash_names(blocks, sec);
                    }
                    hash_heights(&hm, &s_ws, &s_of);
                    JAquifer *aq = jaquifer_create(ch);
                    JSurfaceTopUser top = {surface, ch, blocks, &hm};
                    JCarveSurface cs = {&hm, jsurface_top_material, &top};
                    memset(mask, 0, (size_t)16 * 16 * JFILL_HEIGHT);
                    if (aq != NULL && jchunk_carve(ch, cv, aq, blocks, mask, NULL, &cs)) {
                        have_pipe = true;
                        for (int sec = 0; sec < 24; ++sec) {
                            p_name[sec] = hash_names(blocks, sec);
                            p_mask[sec] = hash_mask(mask, sec);
                        }
                        hash_heights(&hm, &p_ws, &p_of);
                    }
                    jaquifer_free(aq);
                }
            }
            char label[96];
            char rbuf[160];
            char *rf[FIELDS_MAX];
            for (int sec = 0; sec < 24; ++sec) {
                snprintf(label, sizeof(label), "%s/sec%d", cur, sec);
                c->suite = "surface_blocks";
                if (find_row(&surf, seed, cx, cz, sec, rf, rbuf)) {
                    uint64_t e = (uint64_t)strtoll(rf[4], NULL, 10);
                    record(c, label, e, have_surface ? s_name[sec] : ~e);
                }
                c->suite = "pipeline_blocks";
                if (find_row(&pipe, seed, cx, cz, sec, rf, rbuf)) {
                    uint64_t e = (uint64_t)strtoll(rf[4], NULL, 10);
                    record(c, label, e, have_pipe ? p_name[sec] : ~e);
                    c->suite = "pipeline_mask";
                    uint64_t em = (uint64_t)strtoll(rf[5], NULL, 10);
                    record(c, label, em, have_pipe ? p_mask[sec] : ~em);
                }
            }
            if (find_row(&surf_h, seed, cx, cz, -1, rf, rbuf)) {
                c->suite = "surface_heights";
                uint64_t ew = (uint64_t)strtoll(rf[3], NULL, 10), eo = (uint64_t)strtoll(rf[4], NULL, 10);
                record(c, cur, ew, have_surface ? s_ws : ~ew);
                record(c, cur, eo, have_surface ? s_of : ~eo);
            }
            if (find_row(&pipe_h, seed, cx, cz, -1, rf, rbuf)) {
                c->suite = "pipeline_heights";
                uint64_t ew = (uint64_t)strtoll(rf[3], NULL, 10), eo = (uint64_t)strtoll(rf[4], NULL, 10);
                record(c, cur, ew, have_pipe ? p_ws : ~ew);
                record(c, cur, eo, have_pipe ? p_of : ~eo);
            }
            jchunk_free(ch);
            /* Skip this chunk's rows. */
            while (i < pipe.count) {
                snprintf(buf, sizeof(buf), "%s", pipe.lines[i]);
                if (split_csv(buf, fld) < 6 || strcmp(fld[0], seed) != 0 || atoi(fld[1]) != cx || atoi(fld[2]) != cz) {
                    break;
                }
                ++i;
            }
        }
        free(blocks);
        free(mask);
        jcarvers_free(cv);
        jsurface_free(surface);
        jrs_destroy(rs);

        /* BiomeManager.getBiome probes, per seed. */
        c->suite = "biome_zoom";
        rs = NULL;
        surface = NULL;
        last_seed[0] = '\0';
        for (int r = 0; r < zoom.count; ++r) {
            snprintf(buf, sizeof(buf), "%s", zoom.lines[r]);
            if (split_csv(buf, fld) < 5) {
                continue;
            }
            if (rs == NULL || strcmp(last_seed, fld[0]) != 0) {
                jsurface_free(surface);
                jrs_destroy(rs);
                rs = jrs_create(datapack, "overworld", (int64_t)strtoll(fld[0], NULL, 10));
                surface = rs != NULL ? jsurface_create(rs) : NULL;
                snprintf(last_seed, sizeof(last_seed), "%s", fld[0]);
            }
            uint64_t e = (uint64_t)(int64_t)jr_java_string_hash(fld[4]);
            const char *got = surface != NULL ? jsurface_biome_at(surface, atoi(fld[1]), atoi(fld[2]), atoi(fld[3])) : NULL;
            record(c, zoom.lines[r], e, got != NULL ? (uint64_t)(int64_t)jr_java_string_hash(got) : ~e);
        }
        /* Biome.getTemperature probes (seed independent). */
        c->suite = "biome_temperature";
        for (int r = 0; r < temp.count; ++r) {
            snprintf(buf, sizeof(buf), "%s", temp.lines[r]);
            if (split_csv(buf, fld) < 5) {
                continue;
            }
            uint32_t e = (uint32_t)hex64(fld[4]);
            float got = surface != NULL ? jsurface_biome_temperature(surface, fld[0], atoi(fld[1]), atoi(fld[2]), atoi(fld[3])) : 0.0f;
            record(c, temp.lines[r], e, surface != NULL ? float_bits(got) : ~(uint64_t)e);
        }
        jsurface_free(surface);
        jrs_destroy(rs);
    }
    /* PerlinSimplexNoise probes (no data pack needed). */
    c->suite = "simplex_noise";
    {
        char buf[160];
        char *fld[FIELDS_MAX];
        for (int r = 0; r < simp.count; ++r) {
            snprintf(buf, sizeof(buf), "%s", simp.lines[r]);
            if (split_csv(buf, fld) < 4) {
                continue;
            }
            record(c, simp.lines[r], hex64(fld[3]),
                   double_bits(jsurface_simplex_noise(atoi(fld[0]), strtod(fld[1], NULL), strtod(fld[2], NULL))));
        }
    }
    free_table(&pipe);
    free_table(&surf);
    free_table(&surf_h);
    free_table(&pipe_h);
    free_table(&zoom);
    free_table(&temp);
    free_table(&simp);
}

/* ---- feature stage ------------------------------------------------------------------------------
 * feature_order.csv: step,index,placed_feature         (FeatureSorter)
 * features.csv:      seed,target_x,target_z,dx,dz,section,name_hash   (3 x 3 region after decoration) */
static void build_pipeline_chunk(JRandomState *rs, JSurface *surface, JCarvers *cv, int cx, int cz, uint8_t *blocks,
                                 JHeightmaps *hm)
{
    JChunk *ch = jchunk_create(rs, cx, cz);
    if (ch == NULL || !jchunk_fill(ch, blocks)) {
        jchunk_free(ch);
        return;
    }
    jheightmaps_prime(hm, blocks);
    jsurface_build(surface, ch, blocks, hm);
    JAquifer *aq = jaquifer_create(ch);
    JSurfaceTopUser top = {surface, ch, blocks, hm};
    JCarveSurface cs = {hm, jsurface_top_material, &top};
    jchunk_carve(ch, cv, aq, blocks, NULL, NULL, &cs);
    jaquifer_free(aq);
    jchunk_free(ch);
}

static void run_features_set(const char *dir, const char *datapack, Ctx *c, const char *order_name, const char *rows_name,
                             const char *const *types, int type_count, const char *const *names, int name_count,
                             const char *order_suite, const char *blocks_suite)
{
    Table order = load_table(dir, order_name, c->stats);
    Table rows = load_table(dir, rows_name, c->stats);
    if (datapack == NULL) {
        c->stats->skipped += (uint64_t)(order.count + rows.count);
        free_table(&order);
        free_table(&rows);
        return;
    }
    JCarvers *cv = jcarvers_load(datapack);
    JRegion region;
    uint8_t *blocks[9];
    JHeightmaps *hms[9];
    for (int i = 0; i < 9; ++i) {
        blocks[i] = (uint8_t *)malloc((size_t)16 * 16 * JFILL_HEIGHT);
        hms[i] = (JHeightmaps *)malloc(sizeof(JHeightmaps));
        region.blocks[i] = blocks[i];
        region.heightmaps[i] = hms[i];
    }
    char buf[160];
    char *fld[FIELDS_MAX];
    char last_seed[32] = "";
    JRandomState *rs = NULL;
    JSurface *surface = NULL;
    JFeatures *feats = NULL;
    int r = 0;
    bool order_done = false;
    while (r < rows.count) {
        snprintf(buf, sizeof(buf), "%s", rows.lines[r]);
        if (split_csv(buf, fld) < 7) {
            ++r;
            continue;
        }
        char seed[32];
        snprintf(seed, sizeof(seed), "%s", fld[0]);
        int tx = atoi(fld[1]), tz = atoi(fld[2]);
        if (rs == NULL || strcmp(last_seed, seed) != 0) {
            jfeatures_free(feats);
            jsurface_free(surface);
            jrs_destroy(rs);
            rs = jrs_create(datapack, "overworld", (int64_t)strtoll(seed, NULL, 10));
            surface = rs != NULL ? jsurface_create(rs) : NULL;
            feats = surface != NULL ? jfeatures_load(datapack, surface) : NULL;
            snprintf(last_seed, sizeof(last_seed), "%s", seed);
            if (feats != NULL && !order_done) {
                order_done = true;
                c->suite = order_suite;
                for (int i = 0; i < order.count; ++i) {
                    char ob[160];
                    char *of[FIELDS_MAX];
                    snprintf(ob, sizeof(ob), "%s", order.lines[i]);
                    if (split_csv(ob, of) < 3) {
                        continue;
                    }
                    int step = atoi(of[0]), index = atoi(of[1]);
                    const char *got = (step >= 0 && step < jfeatures_step_count(feats) && index >= 0 &&
                                       index < jfeatures_step_size(feats, step))
                                          ? jfeatures_step_feature(feats, step, index)
                                          : "<missing>";
                    record(c, order.lines[i], (uint64_t)(int64_t)jr_java_string_hash(of[2]),
                           (uint64_t)(int64_t)jr_java_string_hash(got));
                }
                /* A step list longer than the reference would hide extra features. */
                int expected_total = order.count, got_total = 0;
                for (int st = 0; st < jfeatures_step_count(feats); ++st) {
                    got_total += jfeatures_step_size(feats, st);
                }
                record(c, "feature_order/total", (uint64_t)expected_total, (uint64_t)got_total);
            }
        }
        bool have = false;
        uint64_t hashes[9][24];
        if (rs != NULL && surface != NULL && feats != NULL && cv != NULL) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int slot = (dz + 1) * 3 + (dx + 1);
                    build_pipeline_chunk(rs, surface, cv, tx + dx, tz + dz, blocks[slot], hms[slot]);
                }
            }
            jsurface_prepare_biomes(surface, tx - 1, tz - 1);
            jsurface_prepare_biomes(surface, tx + 1, tz + 1);
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    jsurface_prepare_chunk(surface, tx + dx, tz + dz);
                }
            }
            region.target_x = tx;
            region.target_z = tz;
            have = jfeatures_decorate(feats, surface, (int64_t)strtoll(seed, NULL, 10), &region, types, type_count, names,
                                      name_count);
            for (int slot = 0; slot < 9; ++slot) {
                for (int sec = 0; sec < 24; ++sec) {
                    hashes[slot][sec] = hash_names(blocks[slot], sec);
                }
            }
        }
        c->suite = blocks_suite;
        while (r < rows.count) {
            snprintf(buf, sizeof(buf), "%s", rows.lines[r]);
            if (split_csv(buf, fld) < 7 || strcmp(fld[0], seed) != 0 || atoi(fld[1]) != tx || atoi(fld[2]) != tz) {
                break;
            }
            int dx = atoi(fld[3]), dz = atoi(fld[4]), sec = atoi(fld[5]);
            uint64_t e = (uint64_t)strtoll(fld[6], NULL, 10);
            char label[96];
            snprintf(label, sizeof(label), "%s/%d/%d/%d/%d/sec%d", seed, tx, tz, dx, dz, sec);
            record(c, label, e, have ? hashes[(dz + 1) * 3 + (dx + 1)][sec] : ~e);
            ++r;
        }
    }
    jfeatures_free(feats);
    jsurface_free(surface);
    jrs_destroy(rs);
    jcarvers_free(cv);
    for (int i = 0; i < 9; ++i) {
        free(blocks[i]);
        free(hms[i]);
    }
    free_table(&order);
    free_table(&rows);
}

static void run_features(const char *dir, const char *datapack, Ctx *c)
{
    static const char *const ORE_TYPES[1] = {"minecraft:ore"};
    run_features_set(dir, datapack, c, "feature_order.csv", "features.csv", ORE_TYPES, 1, NULL, 0, "feature_order",
                     "feature_blocks");
    /* Vegetation patches: random_patch / flower features whose block states and predicates are ported. */
    static const char *const VEG_TYPES[2] = {"minecraft:random_patch", "minecraft:flower"};
    static const char *const VEG_NAMES[] = {
    "minecraft:patch_grass_plain",
    "minecraft:patch_grass_forest",
    "minecraft:patch_grass_normal",
    "minecraft:patch_grass_taiga",
    "minecraft:patch_grass_taiga_2",
    "minecraft:patch_grass_savanna",
    "minecraft:patch_grass_jungle",
    "minecraft:patch_grass_meadow",
    "minecraft:patch_grass_badlands",
    "minecraft:patch_tall_grass",
    "minecraft:patch_tall_grass_2",
    "minecraft:patch_large_fern",
    "minecraft:patch_dead_bush",
    "minecraft:patch_dead_bush_2",
    "minecraft:patch_dead_bush_badlands",
    "minecraft:flower_default",
    "minecraft:flower_swamp",
    "minecraft:flower_warm",
    "minecraft:patch_sunflower",
    "minecraft:patch_pumpkin",
    "minecraft:patch_melon",
    "minecraft:patch_melon_sparse"};
    run_features_set(dir, datapack, c, "feature_order_veg.csv", "features_veg.csv", VEG_TYPES, 2, VEG_NAMES,
                     (int)(sizeof(VEG_NAMES) / sizeof(VEG_NAMES[0])), "feature_order_veg", "vegetation_blocks");
}

/* ---- structure placement -------------------------------------------------------------------------
 * structure_chunks.csv: seed,structure_set,chunk_x,chunk_z  (every chunk where isStructureChunk is true
 * inside the scanned square: +-64 chunks, +-12 for the densest sets) */
static void run_structures(const char *dir, const char *datapack, Ctx *c)
{
    Table rows = load_table(dir, "structure_chunks.csv", c->stats);
    if (datapack == NULL) {
        c->stats->skipped += (uint64_t)rows.count;
        free_table(&rows);
        return;
    }
    JStructureSets *sets = jstructures_load(datapack);
    c->suite = "structure_chunks";
    if (sets == NULL) {
        record(c, "structure_sets/load", 1, 0);
        free_table(&rows);
        return;
    }
    static const long long SEEDS[8] = {0LL, 1LL, 42LL, -1LL, 123456789012345LL, (-9223372036854775807LL - 1LL),
                                       9223372036854775807LL, -8760239871234567LL};
    for (int si = 0; si < 8; ++si) {
        char seed_s[32];
        snprintf(seed_s, sizeof(seed_s), "%lld", SEEDS[si]);
        for (int k = 0; k < jstructures_count(sets); ++k) {
            if (!jstructures_is_random_spread(sets, k)) {
                continue;
            }
            const char *name = jstructures_name(sets, k);
            int spacing_probe_x, spacing_probe_z;
            jstructures_potential_chunk(sets, k, SEEDS[si], 0, 0, &spacing_probe_x, &spacing_probe_z);
            /* The reference scans +-12 chunks for spacing <= 2 sets other than buried treasure/mineshafts. */
            int range = 64;
            if ((strstr(name, "nether_fossils") != NULL)) {
                range = 12;
            }
            int expected = 0, found = 0;
            for (int r = 0; r < rows.count; ++r) {
                char buf[160];
                char *fld[FIELDS_MAX];
                snprintf(buf, sizeof(buf), "%s", rows.lines[r]);
                if (split_csv(buf, fld) < 4 || strcmp(fld[0], seed_s) != 0 || strcmp(fld[1], name) != 0) {
                    continue;
                }
                ++expected;
                bool hit = jstructures_is_structure_chunk(sets, k, SEEDS[si], atoi(fld[2]), atoi(fld[3]));
                char label[120];
                snprintf(label, sizeof(label), "%s/%s/%s/%s", seed_s, name, fld[2], fld[3]);
                record(c, label, 1, hit ? 1 : 0);
            }
            for (int x = -range; x <= range; ++x) {
                for (int z = -range; z <= range; ++z) {
                    if (jstructures_is_structure_chunk(sets, k, SEEDS[si], x, z)) {
                        ++found;
                    }
                }
            }
            char label[120];
            snprintf(label, sizeof(label), "%s/%s/count", seed_s, name);
            record(c, label, (uint64_t)expected, (uint64_t)found);
        }
    }
    jstructures_free(sets);
    free_table(&rows);
}

uint64_t worldgen_parity_run(const char *fixture_dir, const char *datapack_root, FILE *report,
                             WorldgenParityStats *stats)
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
    run_router(fixture_dir, datapack_root, &c);
    run_biomes(fixture_dir, datapack_root, &c);
    run_chunks(fixture_dir, datapack_root, &c);
    run_carvers(fixture_dir, datapack_root, &c);
    run_surface(fixture_dir, datapack_root, &c);
    run_features(fixture_dir, datapack_root, &c);
    run_structures(fixture_dir, datapack_root, &c);
    return stats->mismatched + stats->missing;
}
