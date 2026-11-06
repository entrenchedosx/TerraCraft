/* Times the vanilla-exact world generator stage by stage for a square of chunks.
 *
 *   vgen_bench <datapack_root> [seed] [radius]
 *
 * Each chunk of the square goes through fill, surface and carvers once (the
 * "base" stage); the 3 x 3 decoration stage then runs once per chunk whose
 * neighbours are all in the square. Prints milliseconds per chunk.
 */

#include "core/time.h"
#include "world/jcarver.h"
#include "world/jdensity.h"
#include "world/jfeature.h"
#include "world/jfill.h"
#include "world/jsurface.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCKS_BYTES ((size_t)16 * 16 * JFILL_HEIGHT)

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: vgen_bench <datapack_root> [seed] [radius]\n");
        return 2;
    }
    int64_t seed = argc > 2 ? (int64_t)strtoll(argv[2], NULL, 10) : 12345;
    int radius = argc > 3 ? atoi(argv[3]) : 3;
    int side = radius * 2 + 1;

    double t0 = time_now_seconds();
    JRandomState *rs = jrs_create(argv[1], "overworld", seed);
    JSurface *surf = rs != NULL ? jsurface_create(rs) : NULL;
    JFeatures *feats = surf != NULL ? jfeatures_load(argv[1], surf) : NULL;
    JCarvers *cv = jcarvers_load(argv[1]);
    if (feats == NULL || cv == NULL) {
        fprintf(stderr, "cannot set up the generator from %s\n", argv[1]);
        return 1;
    }
    printf("setup: %.0f ms\n", (time_now_seconds() - t0) * 1000.0);

    int n = side * side;
    uint8_t **blocks = (uint8_t **)calloc((size_t)n, sizeof(uint8_t *));
    JHeightmaps *hms = (JHeightmaps *)calloc((size_t)n, sizeof(JHeightmaps));
    double t_fill = 0, t_surf = 0, t_carve = 0, t_bio = 0;
    for (int i = 0; i < n; ++i) {
        int cx = i % side - radius, cz = i / side - radius;
        blocks[i] = (uint8_t *)malloc(BLOCKS_BYTES);
        double a = time_now_seconds();
        JChunk *ch = jchunk_create(rs, cx, cz);
        jchunk_fill(ch, blocks[i]);
        jheightmaps_prime(&hms[i], blocks[i]);
        double b = time_now_seconds();
        jsurface_build(surf, ch, blocks[i], &hms[i]);
        double c = time_now_seconds();
        JAquifer *aq = jaquifer_create(ch);
        JSurfaceTopUser top = {surf, ch, blocks[i], &hms[i]};
        JCarveSurface cs = {&hms[i], jsurface_top_material, &top};
        jchunk_carve(ch, cv, aq, blocks[i], NULL, NULL, &cs);
        jaquifer_free(aq);
        jchunk_free(ch);
        double d = time_now_seconds();
        t_fill += b - a;
        t_surf += c - b;
        t_carve += d - c;
    }
    double tb = time_now_seconds();
    for (int cz = -radius; cz <= radius; ++cz) {
        for (int cx = -radius; cx <= radius; ++cx) {
            jsurface_prepare_chunk(surf, cx, cz);
        }
    }
    t_bio = time_now_seconds() - tb;

    double t_deco = 0;
    int deco_count = 0;
    int changed_total = 0;
    for (int tz = -radius + 1; tz <= radius - 1; ++tz) {
        for (int tx = -radius + 1; tx <= radius - 1; ++tx) {
            JRegion region;
            memset(&region, 0, sizeof(region));
            uint8_t *copy[9];
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int slot = (dz + 1) * 3 + (dx + 1);
                    int src = (tz + dz + radius) * side + (tx + dx + radius);
                    copy[slot] = (uint8_t *)malloc(BLOCKS_BYTES);
                    memcpy(copy[slot], blocks[src], BLOCKS_BYTES);
                    region.blocks[slot] = copy[slot];
                    region.heightmaps[slot] = &hms[src];
                }
            }
            region.target_x = tx;
            region.target_z = tz;
            const char *types[] = {"minecraft:ore", "minecraft:random_patch", "minecraft:flower",
                                   "minecraft:simple_block", "minecraft:tree"};
            double a = time_now_seconds();
            jfeatures_decorate(feats, surf, seed, &region, types, 5, NULL, 0);
            t_deco += time_now_seconds() - a;
            changed_total += jfeatures_last_changed(feats);
            ++deco_count;
            for (int i = 0; i < 9; ++i) {
                free(copy[i]);
            }
        }
    }
    printf("chunks %d: fill %.1f ms, surface %.1f ms, carve %.1f ms per chunk; biomes %.1f ms per chunk\n", n,
           t_fill / n * 1000.0, t_surf / n * 1000.0, t_carve / n * 1000.0, t_bio / n * 1000.0);
    printf("decoration: %.1f ms per chunk (%d runs, %d blocks changed on average)\n",
           deco_count > 0 ? t_deco / deco_count * 1000.0 : 0.0, deco_count,
           deco_count > 0 ? changed_total / deco_count : 0);
    return 0;
}
