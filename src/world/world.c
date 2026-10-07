#include "world/world.h"
#include "core/log.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/water.h"
#include "world/world_save.h"

#include <string.h>
#include <limits.h>

#include <math.h>
#include <stdlib.h>

/* Floor division for chunk mapping (C truncates toward zero; we need floor
 * so world x=-1 maps to chunk -1, not 0).
 */
static int floor_div_16(int v)
{
    int q = v / 16;
    if (v % 16 < 0) {
        --q;
    }
    return q;
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

/* AO samples diagonal cells around chunk corners as well as cardinal seams. */
static void world_dirty_chunk_neighborhood(World *w, int cx, int cz)
{
    if (w == NULL) {
        return;
    }
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            Chunk *neighbor = world_get_chunk(w, cx + dx, cz + dz);
            if (neighbor != NULL) {
                neighbor->dirty = true;
            }
        }
    }
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
    w->gravity_replicate_changes = true;
    w->water_updates = (WorldWaterUpdate *)calloc(WORLD_WATER_QUEUE_CAP, sizeof(*w->water_updates));
    if (w->water_updates == NULL) {
        free(w);
        return NULL;
    }
    w->gravity_updates = (WorldGravityUpdate *)calloc(WORLD_GRAVITY_QUEUE_CAP, sizeof(*w->gravity_updates));
    w->falling_blocks = (WorldFallingBlock *)calloc(WORLD_FALLING_BLOCK_CAP, sizeof(*w->falling_blocks));
    if (w->gravity_updates == NULL || w->falling_blocks == NULL) {
        free(w->falling_blocks);
        free(w->gravity_updates);
        free(w->water_updates);
        free(w);
        return NULL;
    }
    w->name[0] = '\0';
    w->mode = 0; /* WORLD_MODE_SURVIVAL without pulling world_meta.h here. */
    if (hashmap_init(&w->chunk_map, WORLD_MAX_CHUNKS) != 0) {
        LOG_ERROR("world_create: chunk map init failed");
        free(w->falling_blocks);
        free(w->gravity_updates);
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
    free(w->gravity_updates);
    w->gravity_updates = NULL;
    free(w->falling_blocks);
    w->falling_blocks = NULL;
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
    /* Keep room for neighboring-block arithmetic and ensure each chunk's
     * complete 16-cell world-space extent fits in signed block coordinates. */
    if (w == NULL || c == NULL || c->cx <= INT_MIN / CHUNK_X ||
        c->cx >= (INT_MAX - (CHUNK_X - 1)) / CHUNK_X || c->cz <= INT_MIN / CHUNK_Z ||
        c->cz >= (INT_MAX - (CHUNK_Z - 1)) / CHUNK_Z) {
        return -1;
    }
    if (world_get_chunk(w, c->cx, c->cz) != NULL) {
        return -2;
    }
    if (w->count >= WORLD_MAX_CHUNKS || w->chunks[w->count] != NULL) {
        return -3;
    }
    if (hashmap_put(&w->chunk_map, hashmap_chunk_key(c->cx, c->cz), c) != 0) {
        return -4;
    }
    w->chunks[w->count++] = c;
    world_water_seed_chunk(w, c->cx, c->cz);
    world_water_seed_chunk_edges(w, c->cx, c->cz);
    world_gravity_seed_chunk(w, c->cx, c->cz);

    /* Faces and AO can sample any of the eight chunks around a corner. */
    world_dirty_chunk_neighborhood(w, c->cx, c->cz);
    if (w->water_rescan_needed) {
        w->water_scan_chunk = 0;
        w->water_scan_cell = 0;
        w->water_rescan_repeat = true;
    }
    if (w->gravity_rescan_needed) {
        w->gravity_scan_chunk = 0;
        w->gravity_scan_cell = 0;
        w->gravity_rescan_repeat = true;
    }
    return 0;
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
    size_t i = 0;
    for (; i < w->count; ++i) {
        if (w->chunks[i] == c) {
            break;
        }
    }
    if (i >= w->count) {
        LOG_ERROR("world_remove_chunk: hashmap/iteration array disagree for (%d,%d)", cx, cz);
        return false;
    }
    if (!world_gravity_settle_chunk(w, cx, cz, w->gravity_replicate_changes)) {
        LOG_ERROR("world_remove_chunk: could not safely settle falling blocks in (%d,%d)", cx, cz);
        return false;
    }
    if (c->save_dirty && w->save_dir[0] != '\0') {
        if (world_save_write_chunk(w->save_dir, c) == 0) {
            c->save_dirty = false;
        } else {
            LOG_ERROR("world_remove_chunk: preserving chunk after save failure (%d,%d)", cx, cz);
            return false;
        }
    }
    hashmap_remove(&w->chunk_map, hashmap_chunk_key(cx, cz));
    size_t last = w->count - 1;
    w->chunks[i] = w->chunks[last];
    w->chunks[last] = NULL;
    chunk_destroy(c);
    if (w->count > 0) {
        w->count--;
    }
    world_dirty_chunk_neighborhood(w, cx, cz);
    if (w->water_rescan_needed) {
        w->water_scan_chunk = 0;
        w->water_scan_cell = 0;
        w->water_rescan_repeat = true;
    }
    if (w->gravity_rescan_needed) {
        w->gravity_scan_chunk = 0;
        w->gravity_scan_cell = 0;
        w->gravity_rescan_repeat = true;
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
static bool world_set_block_internal(World *w, int wx, int wy, int wz, uint16_t id, bool replicate)
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
    if ((lx == 0 || lx == CHUNK_X - 1) && (lz == 0 || lz == CHUNK_Z - 1)) {
        int dcx = lx == 0 ? -1 : 1;
        int dcz = lz == 0 ? -1 : 1;
        Chunk *diagonal = world_get_chunk(w, cx + dcx, cz + dcz);
        if (diagonal != NULL) {
            diagonal->dirty = true;
        }
    }
    world_water_notify_block_changed(w, wx, wy, wz);
    world_gravity_notify_block_changed(w, wx, wy, wz);
    if (replicate && w->block_change_callback != NULL) {
        w->block_change_callback(w->block_change_context, wx, wy, wz, id);
    }
    return true;
}

bool world_set_block(World *w, int wx, int wy, int wz, uint16_t id)
{
    return world_set_block_internal(w, wx, wy, wz, id, true);
}

bool world_set_block_unreplicated(World *w, int wx, int wy, int wz, uint16_t id)
{
    return world_set_block_internal(w, wx, wy, wz, id, false);
}

void world_set_block_change_callback(World *w, WorldBlockChangeCallback callback, void *context)
{
    if (w == NULL) {
        return;
    }
    w->block_change_callback = callback;
    w->block_change_context = callback != NULL ? context : NULL;
}

void world_set_gravity_event_callback(World *w, WorldGravityEventCallback callback, void *context)
{
    if (w == NULL) {
        return;
    }
    w->gravity_event_callback = callback;
    w->gravity_event_context = callback != NULL ? context : NULL;
}

void world_set_gravity_replication(World *w, bool enabled)
{
    if (w != NULL) {
        w->gravity_replicate_changes = enabled;
    }
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
