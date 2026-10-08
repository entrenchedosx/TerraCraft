#pragma once
#include "math/mmath.h"
#include <stdint.h>
#include <stdbool.h>
typedef struct World World;
typedef struct Chunk Chunk;
/* Profile 4 is an original density pipeline, not Java seed compatibility.
 * These pure sampling APIs also drive the offline maps and regression tests. */
typedef struct GenClimate {
    float continentalness, erosion, temperature, humidity, weirdness;
    float height, mountain, river;
    int biome;
} GenClimate;
typedef struct GenDensity {
    float terrain, cheese, spaghetti, noodle, final_density;
    float vein, vein_ridge, richness, filler_gap;
} GenDensity;
typedef struct GenAquifer {
    int level;
    bool wet, barrier, lava;
} GenAquifer;
GenClimate worldgen_climate(long seed, int x, int z);
GenDensity worldgen_density(long seed, int x, int y, int z);
GenAquifer worldgen_aquifer(long seed, int x, int y, int z);
int worldgen_height(long seed, int x, int z);
uint16_t worldgen_surface(long seed, int x, int z, int height);
uint16_t worldgen_base_block(long seed, int x, int y, int z);
void worldgen_fill(Chunk *chunk, long seed);
/* Evaluates generated blocks, not the height estimate. No terrain edits. */
bool worldgen_spawn_valid(const World *world, int x, int z, Vec3 *out, float *score);
int worldgen_find_spawn(World *world, Vec3 *out);
/* Diagnostic search budget (0..128 rings), also exercises rescue behavior. */
int worldgen_find_spawn_with_budget(World *world, Vec3 *out, int rings);
