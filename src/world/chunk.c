#include "world/chunk.h"
#include "world/block.h"

#include <stdlib.h>
#include <string.h>

/* Allocate + initialise a heap chunk.
 *
 * Args:
 *   cx, cz: chunk coords.
 *
 * Returns: owned Chunk or NULL.
 */
Chunk *chunk_create(int cx, int cz)
{
    Chunk *c = (Chunk *)calloc(1, sizeof(Chunk));
    if (c == NULL) {
        return NULL;
    }
    chunk_init(c, cx, cz);
    return c;
}

/* Free a heap chunk.
 *
 * Args:
 *   c: chunk to free.
 */
void chunk_destroy(Chunk *c)
{
    free(c);
}

/* Initialise a chunk.
 *
 * Args:
 *   c: chunk to init.
 *   cx, cz: chunk coords.
 */
void chunk_init(Chunk *c, int cx, int cz)
{
    if (c == NULL) {
        return;
    }
    c->cx = cx;
    c->cz = cz;
    for (size_t i = 0; i < CHUNK_VOLUME; ++i) {
        c->blocks[i] = BLOCK_AIR;
    }
    c->dirty = true;
    c->save_dirty = false;
    /* Cache the full-column AABB for frustum culling (M2). */
    c->aabb_min[0] = (float)(cx * CHUNK_X);
    c->aabb_min[1] = 0.0f;
    c->aabb_min[2] = (float)(cz * CHUNK_Z);
    c->aabb_max[0] = (float)(cx * CHUNK_X + CHUNK_X);
    c->aabb_max[1] = (float)CHUNK_Y;
    c->aabb_max[2] = (float)(cz * CHUNK_Z + CHUNK_Z);
}

/* Linear index (unchecked): (y * 256) + (z * 16) + x.
 *
 * Args:
 *   x, y, z: local coords.
 *
 * Returns: index.
 */
size_t chunk_index(int x, int y, int z)
{
    return ((size_t)y * (size_t)CHUNK_XZ_AREA) + ((size_t)z * (size_t)CHUNK_X) + (size_t)x;
}

/* Bounds check.
 *
 * Args:
 *   x, y, z: local coords.
 *
 * Returns: true if in range.
 */
bool chunk_in_bounds(int x, int y, int z)
{
    return x >= 0 && x < CHUNK_X && y >= 0 && y < CHUNK_Y && z >= 0 && z < CHUNK_Z;
}

/* Get block (AIR when out of bounds).
 *
 * Args:
 *   c: chunk.
 *   x, y, z: local coords.
 *
 * Returns: block ID.
 */
uint16_t chunk_get_block(const Chunk *c, int x, int y, int z)
{
    if (c == NULL || !chunk_in_bounds(x, y, z)) {
        return BLOCK_AIR;
    }
    return c->blocks[chunk_index(x, y, z)];
}

/* Set block. Marks dirty on change.
 *
 * Args:
 *   c: chunk.
 *   x, y, z: local coords.
 *   id: block ID.
 */
void chunk_set_block(Chunk *c, int x, int y, int z, uint16_t id)
{
    if (c == NULL || !chunk_in_bounds(x, y, z)) {
        return;
    }
    size_t i = chunk_index(x, y, z);
    if (c->blocks[i] != id) {
        c->blocks[i] = id;
        c->dirty = true;
        /* Any content change needs disk persistence; generation clears this
         * after filling (terrain regenerates exactly from the seed). */
        c->save_dirty = true;
    }
}

/* Legacy 8-bit get wrapper.
 *
 * Args:
 *   c: chunk.
 *   x, y, z: local coords.
 *
 * Returns: block ID truncated to 8 bits.
 */
uint8_t chunk_get(const Chunk *c, int x, int y, int z)
{
    return (uint8_t)chunk_get_block(c, x, y, z);
}

/* Legacy 8-bit set wrapper.
 *
 * Args:
 *   c: chunk.
 *   x, y, z: local coords.
 *   type: block ID.
 *
 * Returns: true if written.
 */
bool chunk_set(Chunk *c, int x, int y, int z, uint8_t type)
{
    if (c == NULL || !chunk_in_bounds(x, y, z)) {
        return false;
    }
    chunk_set_block(c, x, y, z, (uint16_t)type);
    return true;
}

/* Fill chunk.
 *
 * Args:
 *   c: chunk.
 *   type: block ID.
 */
void chunk_fill(Chunk *c, uint16_t type)
{
    if (c == NULL) {
        return;
    }
    for (size_t i = 0; i < CHUNK_VOLUME; ++i) {
        c->blocks[i] = type;
    }
    c->dirty = true;
    c->save_dirty = true;
}

/* Check emptiness.
 *
 * Args:
 *   c: chunk.
 *
 * Returns: true if all AIR.
 */
bool chunk_is_empty(const Chunk *c)
{
    if (c == NULL) {
        return false;
    }
    for (size_t i = 0; i < CHUNK_VOLUME; ++i) {
        if (c->blocks[i] != BLOCK_AIR) {
            return false;
        }
    }
    return true;
}

/* Highest solid block in a column.
 *
 * Args:
 *   c: chunk.
 *   x, z: local column.
 *
 * Returns: highest solid y or -1.
 */
int chunk_top_solid(const Chunk *c, int x, int z)
{
    if (c == NULL || x < 0 || x >= CHUNK_X || z < 0 || z >= CHUNK_Z) {
        return -1;
    }
    for (int y = CHUNK_Y - 1; y >= 0; --y) {
        if (block_is_solid(c->blocks[chunk_index(x, y, z)])) {
            return y;
        }
    }
    return -1;
}
