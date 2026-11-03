#pragma once

#include "math/mmath.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct World World;

/* Default Java overworld colors sampled from the owner's 1.21.11 colormaps.
 * The render cache never changes biome selection or saved block data. */
typedef struct VisualTintGrid {
    Vec3 grass[16 * 16];
    Vec3 foliage[16 * 16];
} VisualTintGrid;

void visual_build_tints(const World *world, int chunk_x, int chunk_z, VisualTintGrid *out);
Vec3 visual_tile_tint(const VisualTintGrid *grid, int x, int z, int tile);
Vec3 visual_world_tint(const World *world, int x, int z, int tile);
Vec3 visual_biome_color(int biome, bool foliage);
Vec3 visual_sky_color(const World *world, int x, int z, float daylight);

/* 16 x 16 RGBA lightmap, block level on X and sky level on Y. The formula
 * follows 1.21.11 lightmap.fsh; ordinary overworld, without status effects. */
void visual_lightmap(unsigned char out[16 * 16 * 4], float sky_brightness, float gamma);
void visual_fog_range(int chunk_radius, bool underwater, float *start, float *end);
