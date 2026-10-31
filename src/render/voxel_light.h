#pragma once

#include <stdint.h>

typedef struct Chunk Chunk;
typedef struct World World;
typedef struct VoxelLightBake VoxelLightBake;

/* Derived render data only: no lighting is written into world saves.
 * A full-height 15-cell halo makes independent neighboring bakes agree. */
VoxelLightBake *voxel_light_bake(const Chunk *chunk, const World *world);
void voxel_light_free(VoxelLightBake *bake);
void voxel_light_sample(const VoxelLightBake *bake, int x, int y, int z,
                        float *sky, float *block);
/* Copy the central chunk to a caller-owned CHUNK_VOLUME-byte cache.
 * Low nibble = sky, high nibble = block light, both 0..15. */
void voxel_light_copy_chunk(const VoxelLightBake *bake, uint8_t *destination);
/* Conservative direct-column fallback for entities awaiting their first bake. */
float voxel_light_direct_sky(const World *world, int x, int y, int z);
