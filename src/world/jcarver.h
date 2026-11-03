#pragma once

/* Vanilla 1.21.11 overworld carvers (CaveWorldCarver and CanyonWorldCarver) and
 * the carving loop of NoiseBasedChunkGenerator.applyCarvers, for the blocks
 * produced by the fill stage (jfill). Configuration is read at run time from
 * the local vanilla data pack:
 *   data/minecraft/worldgen/configured_carver/*.json
 *   data/minecraft/worldgen/biome/plains.json            (carver list)
 *   data/minecraft/tags/block/overworld_carver_replaceables.json
 * Nothing from the data pack is compiled into or committed with TerraCraft.
 *
 * All 54 overworld biomes list the same carvers (cave, cave_extra_underground,
 * canyon, in that order), so the carver list is taken from one biome; the
 * reference dump asks the real biome source for every neighbour chunk and so
 * checks that assumption.
 */

#include "world/jdensity.h"
#include "world/jfill.h"

#include <stdbool.h>
#include <stdint.h>

#define JCARVER_MAX 4

typedef struct JCarvers JCarvers;

/* Load carver configurations. NULL if required files are missing or malformed. */
JCarvers *jcarvers_load(const char *datapack_root);
void jcarvers_free(JCarvers *cv);
int jcarvers_count(const JCarvers *cv);

/* Optional surface context for carving a chunk that already went through the
 * surface stage: carved blocks update the heightmaps, and a carved grass or
 * mycelium column re-runs the surface rules for the dirt below it (the
 * CarvingContext.topMaterial swap). Pass NULL to carve a raw fill chunk. */
typedef struct JCarveSurface {
    JHeightmaps *heightmaps;                                            /* may be NULL */
    int (*top_material)(void *user, int x, int y, int z, bool has_fluid); /* may be NULL */
    void *user;
} JCarveSurface;

/* applyCarvers for one chunk whose fill result is blocks[16 * 16 * JFILL_HEIGHT]
 * (JFILL_INDEX layout, JFillBlock ids); modified in place. mask (same layout,
 * may be NULL) receives 1 for every position a carver visited (CarvingMask).
 * starts (may be NULL) receives, per carver index, the number of neighbour
 * chunks whose carver started. */
bool jchunk_carve(JChunk *chunk, const JCarvers *cv, JAquifer *aquifer, uint8_t *blocks, uint8_t *mask,
                  int starts[JCARVER_MAX], const JCarveSurface *surface);
