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
