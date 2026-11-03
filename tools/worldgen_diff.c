/* worldgen_diff: compare TerraCraft's Java-compatible world-generation
 * primitives with fixtures produced by the vanilla 1.21.11 server jar.
 *
 *   worldgen_diff <fixture_dir> [mismatch_report.csv|-] [datapack_root]
 *   worldgen_diff --chunk <datapack_root> <seed> <cx> <cz> <full_dump.csv>
 *
 * The first form exits 0 only when every compared value is bit-identical and
 * every fixture file was found. Mismatches are written as CSV
 * (suite,key,expected_hex,actual_hex) to the report path ("-" or omitted: no
 * file). The optional datapack_root (the vanilla data generator output dir)
 * enables the stages that need data-pack content.
 *
 * The second form is a debugging aid: it lists every block whose
 * solid/non-solid state differs from a full vanilla chunk dump
 * (WorldgenChunkDump with a 4th argument "<seed>_<cx>_<cz>").
 */

#include "world/jdensity.h"
#include "world/jfill.h"
#include "world/jsurface.h"
#include "world/jfeature.h"
#include "world/jcarver.h"
#include "world/worldgen_parity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int chunk_debug(char **argv)
{
    JRandomState *rs = jrs_create(argv[2], "overworld", (int64_t)strtoll(argv[3], NULL, 10));
    if (rs == NULL) {
        fprintf(stderr, "cannot load data pack\n");
        return 2;
    }
    int cx = atoi(argv[4]), cz = atoi(argv[5]);
    JChunk *ch = jchunk_create(rs, cx, cz);
    FILE *f = fopen(argv[6], "r");
    if (f == NULL || ch == NULL) {
        fprintf(stderr, "cannot open dump\n");
        return 2;
    }
    char line[256];
    fgets(line, sizeof(line), f);
    int diffs = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        int x, z, y;
        char name[128];
        if (sscanf(line, "%d,%d,%d,%127s", &x, &z, &y, name) != 4) {
            continue;
        }
        int solid = !(strstr(name, "air") || strstr(name, "water") || strstr(name, "lava"));
        double d = jchunk_final_density(ch, cx * 16 + x, y, cz * 16 + z);
        if ((d > 0.0) != (solid != 0)) {
            if (diffs++ < 40) {
                printf("x=%d y=%d z=%d vanilla=%s density=%.17g point=%.17g\n", cx * 16 + x, y, cz * 16 + z, name, d,
                       jdf_compute(jrs_router(rs, "final_density"), cx * 16 + x, y, cz * 16 + z));
            }
        }
    }
    printf("total differing blocks: %d\n", diffs);
    fclose(f);
    jchunk_free(ch);
    jrs_destroy(rs);
    return diffs == 0 ? 0 : 1;
}

/* --surface <datapack> <seed> <chunk_x> <chunk_z> <dump.csv>: compare the C surface stage
 * block by block against a SURF_FULL dump (x,z,y,block rows) of the Java reference. */
static int surface_debug(char **argv)
{
    JRandomState *rs = jrs_create(argv[2], "overworld", (int64_t)strtoll(argv[3], NULL, 10));
    JSurface *surf = rs != NULL ? jsurface_create(rs) : NULL;
    int cx = atoi(argv[4]), cz = atoi(argv[5]);
    JChunk *ch = surf != NULL ? jchunk_create(rs, cx, cz) : NULL;
    uint8_t *blocks = (uint8_t *)malloc((size_t)16 * 16 * JFILL_HEIGHT);
    FILE *f = fopen(argv[6], "r");
    if (f == NULL || ch == NULL || blocks == NULL || !jchunk_fill(ch, blocks)) {
        fprintf(stderr, "cannot set up surface debug\n");
        return 2;
    }
    JHeightmaps hm;
    jheightmaps_prime(&hm, blocks);
    jsurface_build(surf, ch, blocks, &hm);
    char line[256];
    fgets(line, sizeof(line), f);
    int diffs = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        int x, z, y;
        char name[128];
        if (sscanf(line, "%d,%d,%d,%127s", &x, &z, &y, name) != 4) {
            continue;
        }
        const char *mine = jfill_block_name((JFillBlock)blocks[JFILL_INDEX(x, z, y)]);
        if (strcmp(mine, name) != 0 && diffs++ < 60) {
            printf("x=%d y=%d z=%d vanilla=%s c=%s biome=%s\n", cx * 16 + x, y, cz * 16 + z, name, mine,
                   jsurface_biome_at(surf, cx * 16 + x, y, cz * 16 + z));
        }
    }
    printf("total differing blocks: %d\n", diffs);
    fclose(f);
    free(blocks);
    jchunk_free(ch);
    jsurface_free(surf);
    jrs_destroy(rs);
    return diffs == 0 ? 0 : 1;
}

/* --feature <datapack> <seed> <tx> <tz> <dump.csv>: list the blocks the C decoration changes
 * and compare with a FEAT_FULL dump (x,y,z,block of every block the reference changed). */
static int feature_debug(char **argv)
{
    int64_t seed = (int64_t)strtoll(argv[3], NULL, 10);
    int tx = atoi(argv[4]), tz = atoi(argv[5]);
    JRandomState *rs = jrs_create(argv[2], "overworld", seed);
    JSurface *surf = rs != NULL ? jsurface_create(rs) : NULL;
    JFeatures *feats = surf != NULL ? jfeatures_load(argv[2], surf) : NULL;
    JCarvers *cv = jcarvers_load(argv[2]);
    if (feats == NULL || cv == NULL) {
        fprintf(stderr, "cannot set up feature debug\n");
        return 2;
    }
    JRegion region;
    uint8_t *before[9];
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            int slot = (dz + 1) * 3 + (dx + 1);
            region.blocks[slot] = (uint8_t *)malloc((size_t)16 * 16 * JFILL_HEIGHT);
            before[slot] = (uint8_t *)malloc((size_t)16 * 16 * JFILL_HEIGHT);
            region.heightmaps[slot] = (JHeightmaps *)malloc(sizeof(JHeightmaps));
            JChunk *ch = jchunk_create(rs, tx + dx, tz + dz);
            jchunk_fill(ch, region.blocks[slot]);
            jheightmaps_prime(region.heightmaps[slot], region.blocks[slot]);
            jsurface_build(surf, ch, region.blocks[slot], region.heightmaps[slot]);
            JAquifer *aq = jaquifer_create(ch);
            JSurfaceTopUser top = {surf, ch, region.blocks[slot], region.heightmaps[slot]};
            JCarveSurface cs = {region.heightmaps[slot], jsurface_top_material, &top};
            jchunk_carve(ch, cv, aq, region.blocks[slot], NULL, NULL, &cs);
            jaquifer_free(aq);
            jchunk_free(ch);
        }
    }
    jsurface_prepare_biomes(surf, tx - 1, tz - 1);
    jsurface_prepare_biomes(surf, tx + 1, tz + 1);
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            jsurface_prepare_chunk(surf, tx + dx, tz + dz);
        }
    }
    for (int i = 0; i < 9; ++i) {
        memcpy(before[i], region.blocks[i], (size_t)16 * 16 * JFILL_HEIGHT);
    }
    region.target_x = tx;
    region.target_z = tz;
    const char *types[2] = {"minecraft:ore", NULL};
    int ntypes = 1;
    const char *names[64];
    int nnames = 0;
    char *name_buf = NULL;
    if (getenv("FEATURE_NAMES") != NULL) {
        /* Vegetation debugging: FEATURE_NAMES=a,b,c selects random_patch / flower placed features. */
        types[0] = "minecraft:random_patch";
        types[1] = "minecraft:flower";
        ntypes = 2;
        name_buf = _strdup(getenv("FEATURE_NAMES"));
        for (char *tok = strtok(name_buf, ","); tok != NULL && nnames < 64; tok = strtok(NULL, ",")) {
            names[nnames++] = tok;
        }
    }
    jfeatures_decorate(feats, surf, seed, &region, types, ntypes, names, nnames);
    free(name_buf);
    /* Load the reference's changed blocks. */
    FILE *f = fopen(argv[6], "r");
    if (f == NULL) {
        return 2;
    }
    char line[256];
    fgets(line, sizeof(line), f);
    int ref_changed = 0, missing = 0, wrong = 0, extra = 0;
    uint8_t *seen[9];
    for (int i = 0; i < 9; ++i) {
        seen[i] = (uint8_t *)calloc((size_t)16 * 16 * JFILL_HEIGHT, 1);
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        int x, y, z;
        char name[128];
        if (sscanf(line, "%d,%d,%d,%127s", &x, &y, &z, name) != 4) {
            continue;
        }
        ++ref_changed;
        int slot = ((z >> 4) - (tz - 1)) * 3 + ((x >> 4) - (tx - 1));
        int idx = JFILL_INDEX(x & 15, z & 15, y);
        seen[slot][idx] = 1;
        const char *mine = jfill_block_name((JFillBlock)region.blocks[slot][idx]);
        if (strcmp(mine, name) != 0) {
            if (region.blocks[slot][idx] == before[slot][idx]) {
                if (missing++ < 12) printf("missing x=%d y=%d z=%d vanilla=%s c=%s\n", x, y, z, name, mine);
            } else if (wrong++ < 12) {
                printf("wrong   x=%d y=%d z=%d vanilla=%s c=%s\n", x, y, z, name, mine);
            }
        }
    }
    for (int slot = 0; slot < 9; ++slot) {
        for (int i = 0; i < 16 * 16 * JFILL_HEIGHT; ++i) {
            if (region.blocks[slot][i] != before[slot][i] && !seen[slot][i]) {
                if (extra++ < 12) {
                    int y = i % JFILL_HEIGHT + JFILL_MIN_Y, zz = (i / JFILL_HEIGHT) % 16, xx = i / JFILL_HEIGHT / 16;
                    printf("extra   x=%d y=%d z=%d c=%s\n", (tx - 1 + slot % 3) * 16 + xx, y, (tz - 1 + slot / 3) * 16 + zz,
                           jfill_block_name((JFillBlock)region.blocks[slot][i]));
                }
            }
        }
    }
    printf("reference changed %d, C changed %d: missing %d wrong %d extra %d\n", ref_changed, jfeatures_last_changed(feats),
           missing, wrong, extra);
    return missing + wrong + extra == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 7 && strcmp(argv[1], "--feature") == 0) {
        return feature_debug(argv);
    }
    if (argc >= 7 && strcmp(argv[1], "--surface") == 0) {
        return surface_debug(argv);
    }
    if (argc >= 7 && strcmp(argv[1], "--chunk") == 0) {
        return chunk_debug(argv);
    }
    if (argc < 2) {
        fprintf(stderr, "usage: %s <fixture_dir> [mismatch_report.csv|-] [datapack_root]\n", argv[0]);
        return 2;
    }
    FILE *report = NULL;
    if (argc >= 3 && strcmp(argv[2], "-") != 0) {
        report = fopen(argv[2], "w");
        if (report == NULL) {
            fprintf(stderr, "cannot open report file %s\n", argv[2]);
            return 2;
        }
    }
    WorldgenParityStats st;
    const char *datapack = argc >= 4 ? argv[3] : NULL;
    uint64_t bad = worldgen_parity_run(argv[1], datapack, report, &st);
    if (report != NULL) {
        fclose(report);
    }
    printf("checked=%llu mismatched=%llu missing_files=%llu skipped=%llu\n", (unsigned long long)st.checked,
           (unsigned long long)st.mismatched, (unsigned long long)st.missing, (unsigned long long)st.skipped);
    return bad == 0 ? 0 : 1;
}
