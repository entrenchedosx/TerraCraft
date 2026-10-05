#pragma once

/* Naive face mesher (M5): one quad per visible block face, textured,
 * split into opaque and blended-transparent passes.
 * CPU-only (no GL) so unit tests run headless.
 * Right-handed, Y-up. World-space positions (chunk origin baked).
 *
 * Vertex format (interleaved, 9 floats = VoxelVertex):
 *   pos xyz | normal xyz | uv uv | ao
 * UVs address the 256x256 texture atlas tile for (block, face).
 * AO holds the combined per-vertex light-occlusion multiplier
 * (geometry AO level x sun-visibility factor); the shader multiplies it
 * into the lit color. Cross-chunk neighbors are resolved via the World so
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

/* Textured voxel vertex (9 floats, tightly packed). */
typedef struct VoxelVertex {
    float pos[3];  /* World-space position. */
    float norm[3]; /* Outward face normal (unit axis). */
    float uv[2];   /* Atlas UVs (u right, v up). */
    float ao;      /* Combined AO x sun multiplier (0.0..1.0). */
} VoxelVertex;

/* Number of floats per vertex (sizeof(VoxelVertex)/sizeof(float)). */
#define MESHER_FLOATS_PER_VERTEX 9

/* AO brightness table indexed by AO level 0..3 (darkest -> brightest).
 * Level 1 is reserved by the M4 heuristic (see mesher_vertex_ao). */
#define MESHER_AO_LEVELS 4

/* Sun-visibility multipliers folded into the AO channel per face. */
#define MESHER_SUN_FULL 1.0f
#define MESHER_SUN_SHADE 0.45f

/* Skylight probe height (cells above the face): covers trees and roofs;
 * anything past it reads as open sky for gameplay shading (bounding the
 * per-face column scan — a full 256-cell scan per face costs ~1M
 * wasted lookups per open-terrain chunk).
 */
#define MESHER_SKY_PROBE 12

/* CPU mesh: caller owns vertices/indices, frees via mesher_free(). */
typedef struct MeshData {
    float *vertices; /* Interleaved VoxelVertex data, vertex_count*9 floats. */
    unsigned int *indices; /* Triangles, index_count entries. */
    size_t vertex_count; /* Number of vertices (not floats). */
    size_t index_count; /* Number of indices. */
    size_t vertex_cap; /* Allocated vertex capacity (internal). */
    size_t index_cap; /* Allocated index capacity (internal). */
} MeshData;

/* AO level from side/corner occupancy (M4 heuristic):
 * side1||side2 solid -> 0 (darkest); else corner solid -> 2; else 3.
 * Level 1 is reserved (unused) to keep the 4-entry brightness table stable
 * for future refinement.
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

/* Free a mesh and its arrays. NULL-safe.
 *
 * Args:
 *   m: mesh to free (may be NULL).
 */
void mesher_free(MeshData *m);
