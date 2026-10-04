#pragma once

/* Noise-based terrain generation (M5): layered continental/hill/detail
 * heightmap over seven climate biomes, with 3D-noise caves, clustered
 * ores, order-independent trees, and surface vegetation. Deterministic
 * per seed (no RNG anywhere). Pure CPU, headless-testable.
 *
 * The M1 sine `world_generate_stub` is frozen for tests; the streamer uses
 * this module for all runtime chunks.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration (full type in world.h). */
typedef struct World World;

/* Column height for world (x,z): continental base + mountain lift +
 * hills + detail. Typical range roughly [25..155]; clamped to [4..200].
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *
 * Returns: clamped column height.
 */
int world_gen_height(long seed, int wx, int wz);

/* Surface block for a column of height h at (wx,wz): biome-driven
 * (ocean floor, sand, snow, rock, grass).
 *
 * Args:
 *   seed: world seed (slope estimate reuses world_gen_height).
 *   wx, wz: world coords.
 *   h: column height from world_gen_height.
 *
 * Returns: surface block ID.
 */
uint16_t world_gen_surface(long seed, int wx, int wz, int h);

/* Tree decision for a trunk column (pure function of world coords:
 * height, biome, density lottery, trunk height 4..6). Order-independent.
 *
 * Args:
 *   seed: world seed.
 *   tx, tz: trunk column world coords.
 *   out_trunk: receives trunk height 4..6 on success (may be NULL).
 *
 * Returns: true when a tree grows here.
 */
bool world_gen_tree(long seed, int tx, int tz, int *out_trunk);

/* Vegetation for a surface column (pure function): grass plant, flower,
 * or AIR. Only above sea level in vegetated biomes.
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *   h: column height from world_gen_height.
 *
 * Returns: block ID to place at h+1 (or AIR for none).
 */
uint16_t world_gen_vegetation(long seed, int wx, int wz, int h);

/* Fill one chunk's columns: bedrock at y=0, stone core with ore sprinkle,
 * dirt subsurface (3 deep), surface block at h, water fill (h,SEA] when
 * below sea level. Marks the chunk dirty.
 *
 * Args:
 *   w: world holding the seed (must not be NULL; w->seed is used).
 *   cx, cz: chunk coords to generate (must be absent from w).
 *
 * Returns: 0 on success, non-zero when present/full/OOM.
 */
int world_generate_chunk(World *w, int cx, int cz);
