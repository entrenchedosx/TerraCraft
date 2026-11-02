#pragma once

/* Vanilla 1.21.11 overworld noise router, loaded at run time from a local
 * data pack directory (the output of the vanilla data generator, or an
 * extracted data pack: <root>/data/minecraft/worldgen/...). Nothing from the
 * data pack is compiled into TerraCraft.
 *
 * Mirrors RandomState + NoiseRouter: noises are instantiated from the world
 * seed exactly as RandomState does (Xoroshiro positional fork, fromHashOf of
 * the noise id), and density functions are evaluated at single points like
 * DensityFunction.SinglePointContext. Cache/interpolation markers are
 * identity at point evaluation, as in the reference.
 */

#include "world/jrandom.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct JDensity JDensity;           /* One density-function node. */
typedef struct JRandomState JRandomState;   /* Seeded router. */

/* Create the router for <settings> ("overworld") from <datapack_root>/data.
 * Returns NULL if required data files are missing or malformed. */
JRandomState *jrs_create(const char *datapack_root, const char *settings, int64_t seed);
void jrs_destroy(JRandomState *rs);

/* Router output by data-pack key: barrier, fluid_level_floodedness,
 * fluid_level_spread, lava, temperature, vegetation, continents, erosion,
 * depth, ridges, preliminary_surface_level, final_density, vein_toggle,
 * vein_ridged, vein_gap. NULL if absent. */
const JDensity *jrs_router(const JRandomState *rs, const char *key);

/* DensityFunction.compute(SinglePointContext(x, y, z)). */
double jdf_compute(const JDensity *df, int x, int y, int z);

/* Number of density-function node kinds that could not be evaluated exactly
 * yet (reported, not hidden): >0 means some outputs are incomplete. */
int jrs_unsupported_count(const JRandomState *rs);

/* ---- NoiseChunk environment ------------------------------------------------
 * One overworld chunk's wrapping of the router: FlatCache grids sampled on the
 * 4-block quart lattice and trilinear cell interpolation (4x8x4 blocks) of the
 * `interpolated` nodes, exactly as NoiseChunk does. */
typedef struct JChunk JChunk;
JChunk *jchunk_create(const JRandomState *rs, int chunk_x, int chunk_z);
void jchunk_free(JChunk *c);
/* Density the block loop sees at block (x, y, z) of this chunk (y -64..319). */
double jchunk_final_density(JChunk *c, int x, int y, int z);
/* Any router output in chunk context. chain_lerp selects the plain
 * NoiseInterpolator Y,X,Z lerp chain (used by ore-vein noises) instead of the
 * cell-cache Mth.lerp3 form. */
double jchunk_eval_router(JChunk *c, const char *key, int x, int y, int z, bool chain_lerp);

/* RandomState extras used by the fill stage. */
int jrs_sea_level(const JRandomState *rs);
const JrPositional *jrs_aquifer_random(const JRandomState *rs);
const JrPositional *jrs_ore_random(const JRandomState *rs);
const JRandomState *jchunk_state(const JChunk *c);
int jchunk_min_x(const JChunk *c);
int jchunk_min_z(const JChunk *c);
