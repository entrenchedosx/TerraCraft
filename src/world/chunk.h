#pragma once

/* Chunk storage (M1): fixed 16 x 256 x 16 column of block IDs.
 * Flat array, index = (y * 16 * 16) + (z * 16) + x  (y-major).
 * Storage is uint16_t to allow future block IDs beyond 255.
 * Chunks are heap-allocated (128 KiB each); use chunk_create/destroy.
 * Bounds-checked accessors. Meshing lives in render/mesher.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHUNK_X 16
#define CHUNK_Y 256
#define CHUNK_Z 16
#define CHUNK_VOLUME (CHUNK_X * CHUNK_Y * CHUNK_Z)
#define CHUNK_XZ_AREA (CHUNK_X * CHUNK_Z)

/* Single chunk: origin in chunk coords (cx, cz) + block data.
 * The cached AABB (world-space) is used for frustum culling; it spans the
 * full column: min=(cx*16,0,cz*16), max=(cx*16+16,256,cz*16+16).
 * `dirty` flags GPU remesh; `save_dirty` flags disk persistence (set only
 * by player edits, never by generation, so untouched chunks cost no I/O).
 */
typedef struct Chunk {
    int cx; /* Chunk X coordinate (in chunks). */
    int cz; /* Chunk Z coordinate (in chunks). */
    uint16_t blocks[CHUNK_VOLUME]; /* Block IDs, see BlockType. */
    bool dirty; /* True when blocks changed since last mesh. */
    bool save_dirty; /* True when edited since last disk save. */
    float aabb_min[3]; /* Cached world-space AABB minimum. */
    float aabb_max[3]; /* Cached world-space AABB maximum. */
} Chunk;

/* Allocate + initialise a chunk to all AIR.
 *
 * Args:
 *   cx, cz: chunk coordinates.
 *
 * Returns: owned Chunk on success, NULL on OOM.
 */
Chunk *chunk_create(int cx, int cz);

/* Free a heap chunk. NULL-safe.
 *
 * Args:
 *   c: chunk to free (may be NULL).
 */
void chunk_destroy(Chunk *c);

/* Initialise a chunk to all AIR with the given chunk coords.
 * Works for stack, static, or heap chunks.
 *
 * Args:
 *   c: chunk to init (must not be NULL).
 *   cx, cz: chunk coordinates.
 */
void chunk_init(Chunk *c, int cx, int cz);

/* Linear index for (x,y,z). No bounds check.
 * Formula: (y * 16 * 16) + (z * 16) + x.
 *
 * Args:
 *   x: 0..15, y: 0..255, z: 0..15.
 *
 * Returns: index into blocks[].
 */
size_t chunk_index(int x, int y, int z);

/* Check whether (x,y,z) is inside the chunk.
 *
 * Args:
 *   x, y, z: local coords.
 *
 * Returns: true if in range.
 */
bool chunk_in_bounds(int x, int y, int z);

/* Get a block ID (M1 primary, 16-bit). Out-of-bounds returns BLOCK_AIR.
 *
 * Args:
 *   c: chunk (must not be NULL).
 *   x, y, z: local coords.
 *
 * Returns: block ID.
 */
uint16_t chunk_get_block(const Chunk *c, int x, int y, int z);

/* Set a block ID (M1 primary). Out-of-bounds is ignored.
 * Marks the chunk dirty + save-dirty when a value actually changes
 * (generation clears save_dirty once per chunk after filling).
 *
 * Args:
 *   c: chunk (must not be NULL).
 *   x, y, z: local coords.
 *   id: block ID.
 */
void chunk_set_block(Chunk *c, int x, int y, int z, uint16_t id);

/* Legacy 8-bit accessors (M0 compat wrappers around the 16-bit core).
 * New code should prefer chunk_get_block / chunk_set_block.
 */
uint8_t chunk_get(const Chunk *c, int x, int y, int z);
bool chunk_set(Chunk *c, int x, int y, int z, uint8_t type);

/* Fill the whole chunk with one block ID (marks dirty).
 *
 * Args:
 *   c: chunk (must not be NULL).
 *   type: block ID.
 */
void chunk_fill(Chunk *c, uint16_t type);

/* Check whether every block is AIR.
 *
 * Args:
 *   c: chunk (must not be NULL).
 *
 * Returns: true if all air (false on NULL).
 */
bool chunk_is_empty(const Chunk *c);

/* Highest solid block in a column, for skylight scans (M4).
 * Scans from the top down; transparent non-solids (water, air) are skipped.
 *
 * Args:
 *   c: chunk (must not be NULL).
 *   x, z: local column coords (out-of-range reads as -1).
 *
 * Returns: highest solid y, or -1 when the column is empty of solids.
 */
int chunk_top_solid(const Chunk *c, int x, int z);
