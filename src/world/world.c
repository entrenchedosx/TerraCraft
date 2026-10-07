#include "world/world.h"
#include "core/log.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/water.h"
#include "world/world_save.h"

#include <string.h>

#include <math.h>
#include <stdlib.h>

/* Floor division for chunk mapping (C truncates toward zero; we need floor
 * so world x=-1 maps to chunk -1, not 0).
 */
static int floor_div_16(int v)
{
    if (v >= 0) {
        return v / 16;
    }
    return -((-v + 15) / 16);
}

/* Positive modulo 16. */
static int mod_16(int v)
{
    int r = v % 16;
    if (r < 0) {
        r += 16;
    }
    return r;
}

/* Create an empty world.
 *
 * Returns: owned World or NULL.
 */
World *world_create(void)
{
    World *w = (World *)calloc(1, sizeof(World));
    if (w == NULL) {
        LOG_ERROR("world_create: out of memory");
        return NULL;
    }
    w->count = 0;
    w->seed = 1337;
    w->terrain_version = 1;
    w->water_updates = (WorldWaterUpdate *)calloc(WORLD_WATER_QUEUE_CAP, sizeof(*w->water_updates));
    if (w->water_updates == NULL) {
        free(w);
        return NULL;
    }
    w->name[0] = '\0';
    w->mode = 0; /* WORLD_MODE_SURVIVAL without pulling world_meta.h here. */
    if (hashmap_init(&w->chunk_map, WORLD_MAX_CHUNKS) != 0) {
        LOG_ERROR("world_create: chunk map init failed");
        free(w->water_updates);
        free(w);
        return NULL;
    }
    return w;
}

/* Destroy a world and its chunks.
 *
 * Args:
 *   w: world to destroy.
 */
void world_destroy(World *w)
{
    if (w == NULL) {
        return;
    }
    for (size_t i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        chunk_destroy(w->chunks[i]);
        w->chunks[i] = NULL;
    }
    hashmap_free(&w->chunk_map);
    free(w->water_updates);
    w->water_updates = NULL;
    w->count = 0;
    free(w);
}

/* Count live chunks.
 *
 * Args:
 *   w: world.
 *
 * Returns: count.
 */
size_t world_chunk_count(const World *w)
{
    if (w == NULL) {
        return 0;
    }
    return w->count;
}

/* Find a chunk by coords (hashmap lookup).
 *
 * Args:
 *   w: world.
 *   cx, cz: chunk coords.
 *
 * Returns: chunk or NULL.
 */
Chunk *world_get_chunk(const World *w, int cx, int cz)
{
    if (w == NULL) {
        return NULL;
    }
    return (Chunk *)hashmap_get(&w->chunk_map, hashmap_chunk_key(cx, cz));
}

/* Adopt a heap chunk.
 *
 * Args:
 *   w: world.
 *   c: chunk to adopt.
 *
 * Returns: 0 on success.
 */
int world_add_chunk(World *w, Chunk *c)
{
    if (w == NULL || c == NULL) {
        return -1;
    }
    if (world_get_chunk(w, c->cx, c->cz) != NULL) {
        return -2;
    }
    for (size_t i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        if (w->chunks[i] == NULL) {
            if (hashmap_put(&w->chunk_map, hashmap_chunk_key(c->cx, c->cz), c) != 0) {
                return -4;
            }
            w->chunks[i] = c;
            w->count++;
            world_water_seed_chunk(w, c->cx, c->cz);
            world_water_seed_chunk_edges(w, c->cx, c->cz);
            return 0;
        }
    }
    return -3;
}

/* Remove and free a chunk (flushing save-dirty edits first).
 *
 * Args:
 *   w: world.
 *   cx, cz: chunk coords.
 *
 * Returns: true when a chunk was freed.
 */
bool world_remove_chunk(World *w, int cx, int cz)
{
    if (w == NULL) {
        return false;
    }
    Chunk *c = world_get_chunk(w, cx, cz);
    if (c == NULL) {
        return false;
    }
    if (c->save_dirty && w->save_dir[0] != '\0') {
        if (world_save_write_chunk(w->save_dir, c) == 0) {
            c->save_dirty = false;
        } else {
            LOG_ERROR("world_remove_chunk: dropping unsaved edits (%d,%d)", cx, cz);
        }
    }
    hashmap_remove(&w->chunk_map, hashmap_chunk_key(cx, cz));
    for (size_t i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        if (w->chunks[i] == c) {
            w->chunks[i] = NULL;
            break;
        }
    }
    chunk_destroy(c);
    if (w->count > 0) {
        w->count--;
    }
    return true;
}

/* Point the world at a save directory ("" disables persistence).
 *
 * Args:
 *   w: world.
 *   dir: world directory path.
 */
void world_set_save_dir(World *w, const char *dir)
{
    if (w == NULL) {
        return;
    }
    if (dir == NULL || dir[0] == '\0') {
        w->save_dir[0] = '\0';
        return;
    }
    size_t n = strlen(dir);
    if (n >= sizeof(w->save_dir)) {
        n = sizeof(w->save_dir) - 1;
    }
    memcpy(w->save_dir, dir, n);
    w->save_dir[n] = '\0';
}

/* Get a block by world coords (cross-chunk).
 *
 * Args:
 *   w: world.
 *   wx, wy, wz: world coords.
 *
 * Returns: block ID (AIR when missing/OOB).
 */
uint16_t world_get_block(const World *w, int wx, int wy, int wz)
{
    if (w == NULL) {
        return BLOCK_AIR;
    }
    if (wy < 0 || wy >= CHUNK_Y) {
        return BLOCK_AIR;
    }
    int cx = floor_div_16(wx);
    int cz = floor_div_16(wz);
    Chunk *c = world_get_chunk(w, cx, cz);
    if (c == NULL) {
        return BLOCK_AIR;
    }
    return chunk_get_block(c, mod_16(wx), wy, mod_16(wz));
}

/* Write a block by world coordinates and invalidate both sides of chunk
 * seams, since neighboring meshes query across those borders. */
bool world_set_block(World *w, int wx, int wy, int wz, uint16_t id)
{
    if (w == NULL || wy < 0 || wy >= CHUNK_Y) {
        return false;
    }
    int cx = floor_div_16(wx);
    int cz = floor_div_16(wz);
    Chunk *c = world_get_chunk(w, cx, cz);
    if (c == NULL) {
        return false;
    }
    int lx = mod_16(wx);
    int lz = mod_16(wz);
    uint16_t old = chunk_get_block(c, lx, wy, lz);
    if (old == id) {
        return false;
    }
    chunk_set_block(c, lx, wy, lz, id);
    if (lx == 0) {
        Chunk *neighbor = world_get_chunk(w, cx - 1, cz);
        if (neighbor != NULL) {
            neighbor->dirty = true;
        }
    } else if (lx == CHUNK_X - 1) {
        Chunk *neighbor = world_get_chunk(w, cx + 1, cz);
        if (neighbor != NULL) {
            neighbor->dirty = true;
        }
    }
    if (lz == 0) {
        Chunk *neighbor = world_get_chunk(w, cx, cz - 1);
        if (neighbor != NULL) {
            neighbor->dirty = true;
        }
    } else if (lz == CHUNK_Z - 1) {
        Chunk *neighbor = world_get_chunk(w, cx, cz + 1);
        if (neighbor != NULL) {
            neighbor->dirty = true;
        }
    }
    world_water_notify_block_changed(w, wx, wy, wz);
    if (w->block_change_callback != NULL) {
        w->block_change_callback(w->block_change_context, wx, wy, wz, id);
    }
    return true;
}

void world_set_block_change_callback(World *w, WorldBlockChangeCallback callback, void *context)
{
    if (w == NULL) {
        return;
    }
    w->block_change_callback = callback;
    w->block_change_context = callback != NULL ? context : NULL;
}

/* Legacy deterministic column height (frozen for tests).
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *
 * Returns: clamped height.
 */
int world_height_at(long seed, int wx, int wz)
{
    double phase = (double)(seed % 1000) * 0.01;
    double fx = ((double)wx * 0.1) + phase;
    double fz = ((double)wz * 0.1) - phase;
    double h = 64.0 + sin(fx) * cos(fz) * 8.0;
    int hi = (int)floor(h);
    if (hi < 1) {
        hi = 1;
    }
    if (hi > CHUNK_Y - 2) {
        hi = CHUNK_Y - 2;
    }
    return hi;
}

/* Legacy 3x3 generator (frozen for tests).
 *
 * Args:
 *   w: world.
 *   seed: generation seed.
 */
void world_generate_stub(World *w, long seed)
{
    if (w == NULL) {
        return;
    }
    w->seed = seed;
    for (int cx = -1; cx <= 1; ++cx) {
        for (int cz = -1; cz <= 1; ++cz) {
            Chunk *c = world_get_chunk(w, cx, cz);
            if (c == NULL) {
                c = chunk_create(cx, cz);
                if (c == NULL) {
                    LOG_ERROR("world_generate_stub: OOM creating chunk (%d,%d)", cx, cz);
                    continue;
                }
                if (world_add_chunk(w, c) != 0) {
                    LOG_ERROR("world_generate_stub: world full, dropping chunk (%d,%d)", cx, cz);
                    chunk_destroy(c);
                    continue;
                }
            }
            /* Fill column by column. */
            for (int lx = 0; lx < CHUNK_X; ++lx) {
                for (int lz = 0; lz < CHUNK_Z; ++lz) {
                    int wx = cx * CHUNK_X + lx;
                    int wz = cz * CHUNK_Z + lz;
                    int h = world_height_at(seed, wx, wz);
                    for (int y = 0; y <= h; ++y) {
                        uint16_t id = BLOCK_STONE;
                        if (y == 0) {
                            id = BLOCK_BEDROCK;
                        } else if (y == h) {
                            id = BLOCK_GRASS;
                        } else if (y >= h - 3) {
                            id = BLOCK_DIRT;
                        } else {
                            id = BLOCK_STONE;
                        }
                        chunk_set_block(c, lx, y, lz, id);
                    }
                }
            }
            c->dirty = true;
            c->save_dirty = false; /* Stub terrain is deterministic; no save. */
        }
    }
    LOG_INFO("world_generate_stub: seed=%ld, chunks=%zu (3x3 around origin)", seed, w->count);
}
