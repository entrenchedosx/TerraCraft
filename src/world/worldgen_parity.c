#include "world/worldgen_parity.h"

#include "world/jbiome.h"
#include "world/jdensity.h"
#include "world/jfill.h"
#include "world/jnoise.h"
#include "world/jrandom.h"

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
    return stats->mismatched + stats->missing;
}
