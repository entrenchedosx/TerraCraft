#pragma once

/* Vanilla 1.21.11 NoiseBasedChunkGenerator.doFill for one overworld chunk:
 * for every block, the NoiseChunk material rule list
 * [Aquifer.computeSubstance(final density), OreVeinifier] with the default
 * block (stone) as fallback. Runs before surface rules, carvers and
 * features, exactly like the first generation stage in vanilla.
 */

#include "world/jdensity.h"

#include <stdbool.h>
#include <stdint.h>

/* Block ids produced by the fill stage. */
typedef enum JFillBlock {
    JB_AIR = 0,
    JB_STONE,
    JB_WATER,
    JB_LAVA,
    JB_COPPER_ORE,
    JB_RAW_COPPER_BLOCK,
    JB_GRANITE,
    JB_DEEPSLATE_IRON_ORE,
    JB_RAW_IRON_BLOCK,
    JB_TUFF,
    JB_COUNT
} JFillBlock;

#define JFILL_HEIGHT 384
#define JFILL_MIN_Y (-64)
/* Index of block (x, y, z) inside a chunk: x, z in 0..15, y in -64..319. */
#define JFILL_INDEX(x, z, y) ((((x) * 16) + (z)) * JFILL_HEIGHT + ((y) - JFILL_MIN_Y))

/* Vanilla registry name of a fill block ("minecraft:stone"). */
const char *jfill_block_name(JFillBlock b);

/* NoiseChunk.aquifer(): kept alive after the fill so carvers can ask it for
 * the fluid in a carved block (Aquifer.computeSubstance). */
typedef struct Aquifer JAquifer;
JAquifer *jaquifer_create(JChunk *chunk);
void jaquifer_free(JAquifer *a);
/* Aquifer.computeSubstance(pos, density): a JFillBlock (air/water/lava) or
 * -1 for "null" (keep the existing block). */
int jaquifer_substance(JAquifer *a, int x, int y, int z, double density);

/* Fill out[16 * 16 * JFILL_HEIGHT]. Returns false on bad arguments. */
bool jchunk_fill(JChunk *chunk, uint8_t *out);
