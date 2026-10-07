#include "world/water.h"

#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>

#define WATER_TICK_SECONDS 0.25f
#define WATER_TICK_BUDGET 1024u
#define WATER_RESCAN_BUDGET 4096u

static const int WATER_SIDE_X[4] = {1, -1, 0, 0};
static const int WATER_SIDE_Z[4] = {0, 0, 1, -1};

static int floor_div_16(int v)
{
    int64_t wide = (int64_t)v;
    return wide >= 0 ? (int)(wide / 16) : (int)-((-wide + 15) / 16);
}

static int mod_16(int v)
{
    int r = v % 16;
    return r < 0 ? r + 16 : r;
}

/* Add a one-cell/chunk offset without invoking signed overflow. */
static bool water_add_offset(int value, int offset, int *out)
{
    if (out == NULL) {
        return false;
    }
    int64_t result = (int64_t)value + (int64_t)offset;
    if (result < INT_MIN || result > INT_MAX) {
        return false;
    }
    *out = (int)result;
    return true;
}

/* Convert chunk coordinates to a world-cell coordinate only when it fits. */
static bool water_chunk_cell(int chunk, int local, int chunk_size, int *out)
{
    if (out == NULL || local < 0 || local >= chunk_size) {
        return false;
    }
    int64_t result = (int64_t)chunk * (int64_t)chunk_size + (int64_t)local;
    if (result < INT_MIN || result > INT_MAX) {
        return false;
    }
    *out = (int)result;
    return true;
}

/* Safe block lookup local to the fluid solver. This keeps its chunk mapping
 * defined even for the minimum signed coordinate. */
static uint16_t water_get_block(const World *w, int x, int y, int z)
{
    if (w == NULL || y < 0 || y >= CHUNK_Y || x == INT_MIN || z == INT_MIN) {
        return BLOCK_AIR;
    }
    int cx = floor_div_16(x);
    int cz = floor_div_16(z);
    Chunk *c = world_get_chunk(w, cx, cz);
    return c != NULL ? chunk_get_block(c, mod_16(x), y, mod_16(z)) : BLOCK_AIR;
}

/* world_set_block currently uses signed negation in its floor division.
 * Reject the one unrepresentable negation here rather than passing it down. */
static bool water_set_block(World *w, int x, int y, int z, uint16_t id, bool replicate)
{
    if (x == INT_MIN || z == INT_MIN) {
        return false;
    }
    return replicate ? world_set_block(w, x, y, z, id) : world_set_block_unreplicated(w, x, y, z, id);
}

static bool water_replaceable(uint16_t id)
{
    return id == BLOCK_AIR || block_is_cross(id);
}

typedef enum WaterEnqueueResult {
    WATER_ENQUEUE_UNAVAILABLE = 0,
    WATER_ENQUEUE_ALREADY_QUEUED,
    WATER_ENQUEUE_ADDED,
    WATER_ENQUEUE_FULL
} WaterEnqueueResult;

static void water_request_rescan(World *w)
{
    if (w->water_rescan_needed) {
        w->water_rescan_repeat = true;
    } else {
        w->water_scan_chunk = 0;
        w->water_scan_cell = 0;
        w->water_rescan_needed = true;
    }
}

static WaterEnqueueResult water_enqueue(World *w, int x, int y, int z)
{
    if (w == NULL || w->water_updates == NULL || y < 0 || y >= CHUNK_Y || x == INT_MIN || z == INT_MIN) {
        return WATER_ENQUEUE_UNAVAILABLE;
    }
    int cx = floor_div_16(x);
    int cz = floor_div_16(z);
    Chunk *c = world_get_chunk(w, cx, cz);
    if (c == NULL) {
        return WATER_ENQUEUE_UNAVAILABLE; /* Never simulate into an unloaded chunk. */
    }
    size_t idx = chunk_index(mod_16(x), y, mod_16(z));
    uint8_t bit = (uint8_t)(1u << (idx & 7u));
    uint8_t *slot = &c->water_queued[idx >> 3];
    if ((*slot & bit) != 0) {
        return WATER_ENQUEUE_ALREADY_QUEUED;
    }
    /* Check dedupe before capacity: duplicate notifications are already
     * represented and must not trigger a recovery scan. */
    if (w->water_count >= WORLD_WATER_QUEUE_CAP) {
        uint8_t *deferred = &c->water_deferred[idx >> 3];
        if ((*deferred & bit) == 0) {
            *deferred |= bit;
            c->water_deferred_count++;
        }
        water_request_rescan(w);
        return WATER_ENQUEUE_FULL;
    }
    size_t tail = (w->water_head + w->water_count) % WORLD_WATER_QUEUE_CAP;
    w->water_updates[tail] = (WorldWaterUpdate){x, y, z};
    w->water_count++;
    *slot |= bit;
    return WATER_ENQUEUE_ADDED;
}

void world_water_notify_block_changed(World *w, int x, int y, int z)
{
    if (w == NULL) {
        return;
    }
    if (block_is_water(water_get_block(w, x, y, z))) {
        (void)water_enqueue(w, x, y, z);
    }
    for (int i = 0; i < 4; ++i) {
        int nx;
        int nz;
        if (!water_add_offset(x, WATER_SIDE_X[i], &nx) || !water_add_offset(z, WATER_SIDE_Z[i], &nz)) {
            continue;
        }
        if (block_is_water(water_get_block(w, nx, y, nz))) {
            (void)water_enqueue(w, nx, y, nz);
        }
    }
    if (y > 0 && block_is_water(water_get_block(w, x, y - 1, z))) {
        (void)water_enqueue(w, x, y - 1, z);
    }
    if (y + 1 < CHUNK_Y && block_is_water(water_get_block(w, x, y + 1, z))) {
        (void)water_enqueue(w, x, y + 1, z);
    }
}

void world_water_seed_chunk(World *w, int cx, int cz)
{
    Chunk *c = world_get_chunk(w, cx, cz);
    if (w == NULL || c == NULL) {
        return;
    }
    int ox;
    int oz;
    if (!water_chunk_cell(cx, 0, CHUNK_X, &ox) || !water_chunk_cell(cz, 0, CHUNK_Z, &oz)) {
        return;
    }
    for (int y = 0; y < CHUNK_Y; ++y) {
        for (int z = 0; z < CHUNK_Z; ++z) {
            for (int x = 0; x < CHUNK_X; ++x) {
                if (!block_is_water(chunk_get_block(c, x, y, z))) {
                    continue;
                }
                int wx;
                int wz;
                if (!water_add_offset(ox, x, &wx) || !water_add_offset(oz, z, &wz)) {
                    continue;
                }
                bool exposed = y > 0 && water_replaceable(water_get_block(w, wx, y - 1, wz));
                for (int i = 0; !exposed && i < 4; ++i) {
                    int nx;
                    int nz;
                    if (water_add_offset(wx, WATER_SIDE_X[i], &nx) &&
                        water_add_offset(wz, WATER_SIDE_Z[i], &nz)) {
                        exposed = water_replaceable(water_get_block(w, nx, y, nz));
                    }
                }
                if (exposed) {
                    (void)water_enqueue(w, wx, y, wz);
                }
            }
        }
    }
}

void world_water_seed_chunk_edges(World *w, int cx, int cz)
{
    if (w == NULL) {
        return;
    }
    static const int dcx[4] = {-1, 1, 0, 0};
    static const int dcz[4] = {0, 0, -1, 1};
    for (int side = 0; side < 4; ++side) {
        int ncx;
        int ncz;
        if (!water_add_offset(cx, dcx[side], &ncx) || !water_add_offset(cz, dcz[side], &ncz)) {
            continue;
        }
        Chunk *neighbor = world_get_chunk(w, ncx, ncz);
        if (neighbor == NULL) {
            continue;
        }
        int neighbor_ox;
        int neighbor_oz;
        if (!water_chunk_cell(ncx, 0, CHUNK_X, &neighbor_ox) ||
            !water_chunk_cell(ncz, 0, CHUNK_Z, &neighbor_oz)) {
            continue;
        }
        int edge = side == 0 ? CHUNK_X - 1 : (side == 1 ? 0 : (side == 2 ? CHUNK_Z - 1 : 0));
        for (int y = 0; y < CHUNK_Y; ++y) {
            for (int lane = 0; lane < CHUNK_X; ++lane) {
                int lx = (side < 2) ? edge : lane;
                int lz = (side < 2) ? lane : edge;
                if (!block_is_water(chunk_get_block(neighbor, lx, y, lz))) {
                    continue;
                }
                int wx;
                int wz;
                if (!water_add_offset(neighbor_ox, lx, &wx) ||
                    !water_add_offset(neighbor_oz, lz, &wz)) {
                    continue;
                }
                (void)water_enqueue(w, wx, y, wz);
            }
        }
    }
}

static int water_min_horizontal_parent(const World *w, int x, int y, int z)
{
    int best = 8;
    for (int i = 0; i < 4; ++i) {
        int nx;
        int nz;
        if (!water_add_offset(x, WATER_SIDE_X[i], &nx) || !water_add_offset(z, WATER_SIDE_Z[i], &nz)) {
            continue;
        }
        uint16_t n = water_get_block(w, nx, y, nz);
        if (!block_is_water(n)) {
            continue;
        }
        int level = block_water_is_falling(n) ? 0 : block_water_level(n);
        if (level >= 0 && level < best) {
            best = level;
        }
    }
    return best;
}

static void water_spread_side(World *w, int x, int y, int z, int level, bool replicate)
{
    if (level < 1 || level > 7) {
        return;
    }
    uint16_t candidate = block_water_flowing(level, false);
    for (int i = 0; i < 4; ++i) {
        int nx;
        int nz;
        if (!water_add_offset(x, WATER_SIDE_X[i], &nx) || !water_add_offset(z, WATER_SIDE_Z[i], &nz)) {
            continue;
        }
        uint16_t target = water_get_block(w, nx, y, nz);
        if (water_replaceable(target)) {
            (void)water_set_block(w, nx, y, nz, candidate, replicate);
        } else if (block_is_water(target) && !block_water_is_falling(target) &&
                   block_water_level(target) > level) {
            (void)water_set_block(w, nx, y, nz, candidate, replicate);
        }
    }
}

static void water_step(World *w, int x, int y, int z, bool replicate)
{
    uint16_t id = water_get_block(w, x, y, z);
    if (!block_is_water(id)) {
        return;
    }
    bool source = id == BLOCK_WATER;
    bool falling = block_water_is_falling(id);
    int level = block_water_level(id);

    if (falling && !block_is_water(water_get_block(w, x, y + 1, z))) {
        int parent = water_min_horizontal_parent(w, x, y, z);
        if (parent < 7) {
            (void)water_set_block(w, x, y, z, block_water_flowing(parent + 1, false), replicate);
        } else {
            (void)water_set_block(w, x, y, z, BLOCK_AIR, replicate);
        }
        return;
    }

    /* Horizontal flow loses one level per block. Removing orphaned levels
     * here lets a stream retract after its source is blocked or removed. */
    if (!source && !falling) {
        int parent = water_min_horizontal_parent(w, x, y, z);
        if (parent >= 7 || parent + 1 > level) {
            (void)water_set_block(w, x, y, z, BLOCK_AIR, replicate);
            return;
        }
        if (parent + 1 < level) {
            level = parent + 1;
            (void)water_set_block(w, x, y, z, block_water_flowing(level, false), replicate);
        }
    }

    uint16_t below = y > 0 ? water_get_block(w, x, y - 1, z) : BLOCK_BEDROCK;
    if (water_replaceable(below)) {
        if (water_set_block(w, x, y - 1, z, BLOCK_WATER_FALLING, replicate)) {
            return; /* Fall first; the lower cell continues the stream. */
        }
    }

    /* Minecraft's infinite-water rule: two source neighbors over a solid
     * floor can refill the middle cell. */
    if (!source && !falling && level <= 1 && block_is_solid(below)) {
        int sources = 0;
        for (int i = 0; i < 4; ++i) {
            int nx;
            int nz;
            if (water_add_offset(x, WATER_SIDE_X[i], &nx) &&
                water_add_offset(z, WATER_SIDE_Z[i], &nz) &&
                water_get_block(w, nx, y, nz) == BLOCK_WATER) {
                ++sources;
            }
        }
        if (sources >= 2) {
            (void)water_set_block(w, x, y, z, BLOCK_WATER, replicate);
            return;
        }
    }

    int flow_level = source || falling ? 1 : level + 1;
    water_spread_side(w, x, y, z, flow_level, replicate);
}

/* Queue saturation must delay fluid work, not permanently lose it. Overflow
 * cells are recorded in a per-chunk bitset, so recovery scans only deferred
 * update bits instead of revisiting every block in every loaded chunk. */
static void water_rescan_loaded(World *w, size_t budget)
{
    while (budget > 0 && w->water_rescan_needed) {
        if (w->water_scan_chunk >= w->count) {
            w->water_scan_chunk = 0;
            w->water_scan_cell = 0;
            if (w->water_rescan_repeat) {
                w->water_rescan_repeat = false;
            } else {
                bool any_deferred = false;
                for (size_t i = 0; i < w->count; ++i) {
                    if (w->chunks[i] != NULL && w->chunks[i]->water_deferred_count > 0) {
                        any_deferred = true;
                        break;
                    }
                }
                w->water_rescan_needed = any_deferred;
            }
            return;
        }
        Chunk *c = w->chunks[w->water_scan_chunk];
        if (c == NULL || c->water_deferred_count == 0) {
            w->water_scan_chunk++;
            w->water_scan_cell = 0;
            continue;
        }
        if (w->water_scan_cell >= (CHUNK_VOLUME + 7u) / 8u) {
            w->water_scan_chunk++;
            w->water_scan_cell = 0;
            continue;
        }
        size_t byte_index = w->water_scan_cell;
        uint8_t bits = c->water_deferred[byte_index];
        if (bits == 0) {
            w->water_scan_cell++;
            budget--;
            continue;
        }
        for (unsigned bit_index = 0; bit_index < 8 && budget > 0; ++bit_index) {
            uint8_t bit = (uint8_t)(1u << bit_index);
            if ((bits & bit) == 0) {
                continue;
            }
            size_t idx = byte_index * 8u + bit_index;
            budget--;
            if (idx >= CHUNK_VOLUME || !block_is_water(c->blocks[idx])) {
                c->water_deferred[byte_index] &= (uint8_t)~bit;
                c->water_deferred_count--;
                continue;
            }
            if ((c->water_queued[byte_index] & bit) != 0) {
                c->water_deferred[byte_index] &= (uint8_t)~bit;
                c->water_deferred_count--;
                continue;
            }
            if (w->water_count >= WORLD_WATER_QUEUE_CAP) {
                return; /* Keep the unqueued deferred bit for the next tick. */
            }
            int x = (int)(idx % CHUNK_X);
            int z = (int)((idx / CHUNK_X) % CHUNK_Z);
            int y = (int)(idx / CHUNK_XZ_AREA);
            int ox;
            int oz;
            int wx;
            int wz;
            if (water_chunk_cell(c->cx, 0, CHUNK_X, &ox) &&
                water_chunk_cell(c->cz, 0, CHUNK_Z, &oz) &&
                water_add_offset(ox, x, &wx) && water_add_offset(oz, z, &wz) &&
                water_enqueue(w, wx, y, wz) == WATER_ENQUEUE_ADDED) {
                c->water_deferred[byte_index] &= (uint8_t)~bit;
                c->water_deferred_count--;
            } else if ((c->water_queued[byte_index] & bit) != 0) {
                c->water_deferred[byte_index] &= (uint8_t)~bit;
                c->water_deferred_count--;
            }
        }
        if (c->water_deferred[byte_index] == 0) {
            w->water_scan_cell++;
        }
    }
}

void world_water_tick_mode(World *w, float dt, bool replicate_changes)
{
    if (w == NULL || w->water_updates == NULL || !(dt > 0.0f) || !isfinite(dt)) {
        return;
    }
    w->water_accumulator += dt;
    if (w->water_accumulator > 2.0f) {
        w->water_accumulator = 2.0f;
    }
    while (w->water_accumulator >= WATER_TICK_SECONDS) {
        w->water_accumulator -= WATER_TICK_SECONDS;
        unsigned budget = WATER_TICK_BUDGET;
        while (budget-- > 0 && w->water_count > 0) {
            WorldWaterUpdate update = w->water_updates[w->water_head];
            w->water_head = (w->water_head + 1) % WORLD_WATER_QUEUE_CAP;
            w->water_count--;
            int cx = floor_div_16(update.x);
            int cz = floor_div_16(update.z);
            Chunk *c = world_get_chunk(w, cx, cz);
            if (c == NULL || update.y < 0 || update.y >= CHUNK_Y) {
                continue;
            }
            size_t idx = chunk_index(mod_16(update.x), update.y, mod_16(update.z));
            c->water_queued[idx >> 3] &= (uint8_t)~(1u << (idx & 7u));
            water_step(w, update.x, update.y, update.z, replicate_changes);
        }
        if (w->water_rescan_needed) {
            water_rescan_loaded(w, WATER_RESCAN_BUDGET);
        }
    }
}

void world_water_tick(World *w, float dt)
{
    world_water_tick_mode(w, dt, true);
}

size_t world_water_pending(const World *w)
{
    return w != NULL ? w->water_count : 0;
}
