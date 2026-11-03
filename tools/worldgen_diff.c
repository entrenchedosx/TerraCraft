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

int main(int argc, char **argv)
{
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
