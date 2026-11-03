#pragma once

/* Vanilla 1.21.11 overworld surface stage: SurfaceSystem.buildSurface with the
 * SurfaceRules interpreter (bedrock floor, deepslate transition, grass/dirt/
 * sand/gravel/badlands/ice/snow top layers, eroded-badlands pillars and
 * frozen-ocean icebergs), BiomeManager.getBiome (the hashed 8-corner biome
 * zoom) and the Biome temperature helpers the rules use.
 *
 * Rules are read at run time from the local vanilla data pack:
 *   data/minecraft/worldgen/noise_settings/overworld.json   (surface_rule, sea level)
 *   data/minecraft/worldgen/biome/<biome>.json              (temperature, modifier)
 *   data/minecraft/worldgen/noise/*.json                    (surface noises)
 * plus the biome parameter report used by jbiome. Nothing from the data pack
 * is compiled into or committed with TerraCraft.
 */

#include "world/jdensity.h"
#include "world/jfill.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct JSurface JSurface;

/* Build the surface system for a seeded RandomState (RandomState.surfaceSystem()).
 * NULL if a data file is missing or a rule type is not supported. */
JSurface *jsurface_create(JRandomState *rs);
void jsurface_free(JSurface *s);

/* BiomeManager.getBiome(blockX, blockY, blockZ): registry id of the biome. */
const char *jsurface_biome_at(JSurface *s, int x, int y, int z);
/* Fill the stored biomes (ChunkAccess.fillBiomesFromNoise) of a chunk and its 8
 * neighbours, in the canonical order; BiomeManager reads only stored biomes. Called
 * automatically by jsurface_build; exposed so callers can pin the fill order. */
void jsurface_prepare_biomes(JSurface *s, int chunk_x, int chunk_z);
/* Fill the stored biomes of exactly one chunk (no neighbours) if absent. */
void jsurface_prepare_chunk(JSurface *s, int chunk_x, int chunk_z);
/* Distinct stored biomes of a chunk (writes up to cap names, returns the count). */
int jsurface_chunk_biomes(JSurface *s, int chunk_x, int chunk_z, const char **out, int cap);
/* Distinct biome ids of the surface system's parameter list, in possibleBiomes() order. */
int jsurface_possible_biome_count(const JSurface *s);
const char *jsurface_possible_biome(const JSurface *s, int i);
/* MultiNoiseBiomeSource.getNoiseBiome at a quart position (y clamped to the
 * overworld column the way ChunkAccess.getNoiseBiome does). */
const char *jsurface_noise_biome(JSurface *s, int qx, int qy, int qz);

/* Biome.getTemperature(pos, seaLevel) for a biome id. */
float jsurface_biome_temperature(JSurface *s, const char *biome, int x, int y, int z);

/* PerlinSimplexNoise.getValue(x, y, false) of Biome.TEMPERATURE_NOISE (0),
 * FROZEN_TEMPERATURE_NOISE (1) and BIOME_INFO_NOISE (2). */
double jsurface_simplex_noise(int which, double x, double y);

/* SurfaceSystem.buildSurface for one chunk whose fill result is blocks (JFILL_INDEX
 * layout); heightmaps must be primed from the fill and are kept up to date. */
bool jsurface_build(JSurface *s, JChunk *chunk, uint8_t *blocks, JHeightmaps *hm);

/* CarvingContext.topMaterial / SurfaceSystem.topMaterial: the block the surface
 * rules would place at (x, y, z) of the chunk being carved; -1 if no rule
 * applies. The signature matches JCarveSurface.top_material (user = JSurfaceTopUser). */
typedef struct JSurfaceTopUser {
    JSurface *surface;
    JChunk *chunk;
    uint8_t *blocks;
    const JHeightmaps *heightmaps;
} JSurfaceTopUser;
int jsurface_top_material(void *user, int x, int y, int z, bool has_fluid);
