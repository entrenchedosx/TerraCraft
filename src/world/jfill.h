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
    /* Blocks that only appear after the fill stage (surface rules, carvers). */
    JB_BEDROCK,
    JB_DEEPSLATE,
    JB_CALCITE,
    JB_COARSE_DIRT,
    JB_DIRT,
    JB_GRASS_BLOCK,
    JB_GRAVEL,
    JB_ICE,
    JB_MUD,
    JB_MYCELIUM,
    JB_ORANGE_TERRACOTTA,
    JB_PACKED_ICE,
    JB_PODZOL,
    JB_POWDER_SNOW,
    JB_RED_SAND,
    JB_RED_SANDSTONE,
    JB_SAND,
    JB_SANDSTONE,
    JB_SNOW_BLOCK,
    JB_TERRACOTTA,
    JB_WHITE_TERRACOTTA,
    JB_YELLOW_TERRACOTTA,
    JB_BROWN_TERRACOTTA,
    JB_RED_TERRACOTTA,
    JB_LIGHT_GRAY_TERRACOTTA,
    JB_COUNT
} JFillBlock;

#define JFILL_HEIGHT 384
#define JFILL_MIN_Y (-64)
/* Index of block (x, y, z) inside a chunk: x, z in 0..15, y in -64..319. */
#define JFILL_INDEX(x, z, y) ((((x) * 16) + (z)) * JFILL_HEIGHT + ((y) - JFILL_MIN_Y))

/* Vanilla registry name of a block ("minecraft:stone"). */
const char *jfill_block_name(JFillBlock b);
/* JFillBlock for a registry name, or -1 if unknown. */
int jfill_block_from_name(const char *name);
/* BlockState.isAir / getFluidState().isEmpty() / blocksMotion for a block id. */
bool jblock_is_air(int b);
bool jblock_is_fluid(int b);
bool jblock_blocks_motion(int b);

/* ChunkAccess heightmaps used during generation: Heightmap.Types.WORLD_SURFACE_WG
 * (first non-air block) and OCEAN_FLOOR_WG (first motion-blocking block). Values
 * are Heightmap.getFirstAvailable: the Y above the top matching block, or
 * JFILL_MIN_Y for an empty column. */
typedef struct JHeightmaps {
    int world_surface[256];  /* index lz * 16 + lx */
    int ocean_floor[256];
} JHeightmaps;
/* Heightmaps as doFill leaves them for a freshly filled chunk. */
void jheightmaps_prime(JHeightmaps *hm, const uint8_t *blocks);
/* Heightmap.update for both types after block (lx, y, lz) became `state`; the block
 * array must already hold the new state. */
void jheightmaps_update(JHeightmaps *hm, const uint8_t *blocks, int lx, int y, int lz, int state);
/* ChunkAccess.getHeight(WORLD_SURFACE_WG / OCEAN_FLOOR_WG, x, z): top block Y. */
int jheightmaps_world_surface_height(const JHeightmaps *hm, int lx, int lz);
int jheightmaps_ocean_floor_height(const JHeightmaps *hm, int lx, int lz);

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
