#pragma once

/* Deterministic climate biomes (M5): temperature + humidity noise fields
 * plus terrain height select one of seven biomes per column. Pure functions
 * of world (x,z) — no chunk borders, no RNG, fully order-independent.
 * Original design (not Minecraft's algorithm); names are generic.
 */

#include <stdbool.h>
#include <stdint.h>

/* Biome types. */
typedef enum BiomeType {
    BIOME_OCEAN = 0,    /* Deep water basins. */
    BIOME_BEACH,        /* Sandy shoreline fringe. */
    BIOME_PLAINS,       /* Temperate grassland, sparse trees. */
    BIOME_FOREST,       /* Humid grassland, dense trees. */
    BIOME_DESERT,       /* Hot dry sand, no trees. */
    BIOME_SNOW,         /* Cold snowfields, sparse trees. */
    BIOME_MOUNTAINS,    /* High rock with snow caps. */
    BIOME_COUNT         /* Sentinel. */
} BiomeType;

/* Human-readable biome name (never NULL; "unknown" for invalid values).
 *
 * Args:
 *   b: biome value.
 *
 * Returns: static name string.
 */
const char *biome_name(int b);

/* Climate temperature in [-1,1] (low-frequency fBm, seed-derived).
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *
 * Returns: temperature value.
 */
float biome_temperature(long seed, int wx, int wz);

/* Climate humidity in [-1,1] (low-frequency fBm, independent stream).
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *
 * Returns: humidity value.
 */
float biome_humidity(long seed, int wx, int wz);

/* Mountain mask in [0,1] (ridged fBm gated to highlands).
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *
 * Returns: mountain intensity.
 */
float biome_mountains(long seed, int wx, int wz);

/* Select the biome for a column of height h.
 * Priority: ocean (deep) -> beach (shore) -> mountains (mask/altitude) ->
 * snow (cold) -> desert (hot+dry) -> forest (humid) -> plains.
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *   h: column height from world_gen_height.
 *
 * Returns: BiomeType value.
 */
int biome_at(long seed, int wx, int wz, int h);

/* Tree density for a biome: expected trees per column (0 = none).
 * Forest is dense, plains/snow sparse, others barren.
 *
 * Args:
 *   b: BiomeType value.
 *
 * Returns: density (0.0 when treeless).
 */
float biome_tree_density(int b);

/* True when vegetation (plants/flowers) may sprout in a biome.
 *
 * Args:
 *   b: BiomeType value.
 *
 * Returns: true for plains/forest (and snow sparsely — handled by caller).
 */
bool biome_has_vegetation(int b);
