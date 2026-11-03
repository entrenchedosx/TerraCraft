#pragma once

/* Naive face mesher (M5): one quad per visible block face, textured,
 * split into opaque and blended-transparent passes.
 * CPU-only (no GL) so unit tests run headless.
 * Right-handed, Y-up. World-space positions (chunk origin baked).
 *
 * Vertex format (interleaved, 14 floats = VoxelVertex):
 *   pos xyz | normal xyz | uv uv | ao | skylight | block light | tint rgb
 * UVs address the 256x256 texture atlas tile for (block, face).
 * Geometry AO and the two propagated light channels remain independent.
 * Cross-chunk neighbors are resolved via the World so
 * chunk borders shade seamlessly.
 *
 * Pass rule: WATER and GLASS faces go to the transparent mesh (blended);
 * everything else — including cutout leaves/vegetation — goes to the
 * opaque mesh (alpha discard). See block_is_blended().
 */

#include <stdbool.h>
#include <stddef.h>

/* Forward declarations (full types in world headers). */
typedef struct Chunk Chunk;
typedef struct World World;
typedef struct VoxelLightBake VoxelLightBake;

/* Textured voxel vertex (14 floats, tightly packed). */
typedef struct VoxelVertex {
    float pos[3];  /* World-space position. */
    float norm[3]; /* Outward face normal (unit axis). */
    float uv[2];   /* Atlas UVs (u right, v up). */
    float ao;      /* Geometry ambient occlusion (0.0..1.0). */
    float sky;     /* Propagated skylight (0.0..1.0). */
    float block;   /* Propagated emissive light (0.0..1.0). */
    float tint[3]; /* Blended biome color multiplier. */
} VoxelVertex;

/* Number of floats per vertex (sizeof(VoxelVertex)/sizeof(float)). */
#define MESHER_FLOATS_PER_VERTEX 14

/* AO brightness table indexed by AO level 0..3 (darkest -> brightest).
 * All four levels are used by the corner occupancy rule. */
#define MESHER_AO_LEVELS 4

/* CPU mesh: caller owns vertices/indices, frees via mesher_free(). */
typedef struct MeshData {
    float *vertices;       /* Interleaved VoxelVertex data, vertex_count*14 floats. */
    unsigned int *indices; /* Triangles, index_count entries. */
    size_t vertex_count; /* Number of vertices (not floats). */
    size_t index_count; /* Number of indices. */
    size_t vertex_cap; /* Allocated vertex capacity (internal). */
    size_t index_cap; /* Allocated index capacity (internal). */
} MeshData;

/* AO level from the three corner occluders. Two adjacent filled sides
 * fully occlude; otherwise every filled neighbor removes one level.
 *
 * Args:
 *   side1, side2, corner: occupancy of the three occluder cells.
 *
 * Returns: AO level 0..3.
 */
int mesher_ao_level(bool side1, bool side2, bool corner);

/* Brightness factor for an AO level: table [0.4, 0.6, 0.8, 1.0].
 * Out-of-range levels clamp to the nearest end.
 *
 * Args:
 *   level: AO level 0..3.
 *
 * Returns: brightness multiplier.
 */
float mesher_ao_factor(int level);

/* Build the OPAQUE mesh for chunk `c` (all non-blended faces).
 * `w` may be NULL (neighbors outside the chunk read as AIR).
 * Empty chunks yield an empty (non-NULL, zero-count) mesh.
 * Logs build time at DEBUG level.
 *
 * Args:
 *   c: chunk to mesh (must not be NULL).
 *   w: world for neighbor lookups (may be NULL).
 *
 * Returns: owned MeshData on success, NULL on bad args/OOM.
 */
MeshData *mesher_build_chunk_mesh(const Chunk *c, const World *w);

/* Build the TRANSPARENT (blended) mesh for chunk `c`: WATER and GLASS
 * faces only. Same ownership/logging rules as the opaque builder.
 *
 * Args:
 *   c: chunk to mesh (must not be NULL).
 *   w: world for neighbor lookups (may be NULL).
 *
 * Returns: owned MeshData on success, NULL on bad args/OOM.
 */
MeshData *mesher_build_transparent_mesh(const Chunk *c, const World *w);

/* Renderer shares one bake between opaque and transparent passes. */
MeshData *mesher_build_lit_mesh(const Chunk *c, const World *w,
                               const VoxelLightBake *light, bool transparent);

/* Free a mesh and its arrays. NULL-safe.
 *
 * Args:
 *   m: mesh to free (may be NULL).
 */
void mesher_free(MeshData *m);
