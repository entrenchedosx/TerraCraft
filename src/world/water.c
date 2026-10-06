#include "world/water.h"

#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <math.h>
#include <stdint.h>

#define WATER_TICK_SECONDS 0.25f
#define WATER_TICK_BUDGET 384u
#define WATER_RESCAN_BUDGET 4096u

static const int WATER_SIDE_X[4] = {1, -1, 0, 0};
static const int WATER_SIDE_Z[4] = {0, 0, 1, -1};

static int floor_div_16(int v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

static int mod_16(int v)
{
    int r = v % 16;
    return r < 0 ? r + 16 : r;
}

static bool water_replaceable(uint16_t id)
{
    return id == BLOCK_AIR || block_is_cross(id);
}

static bool water_enqueue(World *w, int x, int y, int z)
{
    if (w == NULL || w->water_updates == NULL || y < 0 || y >= CHUNK_Y ||
        w->water_count >= WORLD_WATER_QUEUE_CAP) {
        if (w != NULL && w->water_updates != NULL && w->water_count >= WORLD_WATER_QUEUE_CAP) {
            if (w->water_rescan_needed) {
                w->water_rescan_repeat = true;
            } else {
                w->water_scan_chunk = 0;
                w->water_scan_cell = 0;
            }
            w->water_rescan_needed = true;
        }
        return false;
    }
    int cx = floor_div_16(x);
    int cz = floor_div_16(z);
    Chunk *c = world_get_chunk(w, cx, cz);
    if (c == NULL) {
        return false; /* Never simulate into an unloaded chunk. */
    }
    size_t idx = chunk_index(mod_16(x), y, mod_16(z));
    uint8_t bit = (uint8_t)(1u << (idx & 7u));
    uint8_t *slot = &c->water_queued[idx >> 3];
    if ((*slot & bit) != 0) {
        return false;
    }
    size_t tail = (w->water_head + w->water_count) % WORLD_WATER_QUEUE_CAP;
    w->water_updates[tail] = (WorldWaterUpdate){x, y, z};
    w->water_count++;
    *slot |= bit;
    return true;
}

void world_water_notify_block_changed(World *w, int x, int y, int z)
{
    if (w == NULL) {
        return;
    }
    if (block_is_water(world_get_block(w, x, y, z))) {
        (void)water_enqueue(w, x, y, z);
    }
    for (int i = 0; i < 4; ++i) {
        int nx = x + WATER_SIDE_X[i];
        int nz = z + WATER_SIDE_Z[i];
        if (block_is_water(world_get_block(w, nx, y, nz))) {
            (void)water_enqueue(w, nx, y, nz);
        }
    }
    if (y > 0 && block_is_water(world_get_block(w, x, y - 1, z))) {
        (void)water_enqueue(w, x, y - 1, z);
    }
    if (y + 1 < CHUNK_Y && block_is_water(world_get_block(w, x, y + 1, z))) {
        (void)water_enqueue(w, x, y + 1, z);
    }
}

void world_water_seed_chunk(World *w, int cx, int cz)
{
    Chunk *c = world_get_chunk(w, cx, cz);
    if (w == NULL || c == NULL) {
        return;
    }
    int ox = cx * CHUNK_X;
    int oz = cz * CHUNK_Z;
    for (int y = 0; y < CHUNK_Y; ++y) {
        for (int z = 0; z < CHUNK_Z; ++z) {
            for (int x = 0; x < CHUNK_X; ++x) {
                if (!block_is_water(chunk_get_block(c, x, y, z))) {
                    continue;
                }
                int wx = ox + x;
                int wz = oz + z;
                bool exposed = y > 0 && water_replaceable(world_get_block(w, wx, y - 1, wz));
                for (int i = 0; !exposed && i < 4; ++i) {
                    exposed = water_replaceable(world_get_block(w, wx + WATER_SIDE_X[i], y,
                                                                wz + WATER_SIDE_Z[i]));
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
        Chunk *neighbor = world_get_chunk(w, cx + dcx[side], cz + dcz[side]);
        if (neighbor == NULL) {
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
                int wx = (cx + dcx[side]) * CHUNK_X + lx;
                int wz = (cz + dcz[side]) * CHUNK_Z + lz;
                (void)water_enqueue(w, wx, y, wz);
            }
        }
    }
}

static int water_min_horizontal_parent(const World *w, int x, int y, int z)
{
    int best = 8;
    for (int i = 0; i < 4; ++i) {
        uint16_t n = world_get_block(w, x + WATER_SIDE_X[i], y, z + WATER_SIDE_Z[i]);
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

static void water_spread_side(World *w, int x, int y, int z, int level)
{
    if (level < 1 || level > 7) {
        return;
    }
    uint16_t candidate = block_water_flowing(level, false);
    for (int i = 0; i < 4; ++i) {
        int nx = x + WATER_SIDE_X[i];
        int nz = z + WATER_SIDE_Z[i];
        uint16_t target = world_get_block(w, nx, y, nz);
        if (water_replaceable(target)) {
            (void)world_set_block(w, nx, y, nz, candidate);
        } else if (block_is_water(target) && !block_water_is_falling(target) &&
                   block_water_level(target) > level) {
            (void)world_set_block(w, nx, y, nz, candidate);
        }
    }
}

static void water_step(World *w, int x, int y, int z)
{
    uint16_t id = world_get_block(w, x, y, z);
    if (!block_is_water(id)) {
        return;
    }
    bool source = id == BLOCK_WATER;
    bool falling = block_water_is_falling(id);
    int level = block_water_level(id);

    if (falling && !block_is_water(world_get_block(w, x, y + 1, z))) {
        int parent = water_min_horizontal_parent(w, x, y, z);
        if (parent < 7) {
            (void)world_set_block(w, x, y, z, block_water_flowing(parent + 1, false));
        } else {
            (void)world_set_block(w, x, y, z, BLOCK_AIR);
        }
        return;
    }

    /* Horizontal flow loses one level per block. Removing orphaned levels
     * here lets a stream retract after its source is blocked or removed. */
    if (!source && !falling) {
        int parent = water_min_horizontal_parent(w, x, y, z);
        if (parent >= 7 || parent + 1 > level) {
            (void)world_set_block(w, x, y, z, BLOCK_AIR);
            return;
        }
        if (parent + 1 < level) {
            level = parent + 1;
            (void)world_set_block(w, x, y, z, block_water_flowing(level, false));
        }
    }

    uint16_t below = y > 0 ? world_get_block(w, x, y - 1, z) : BLOCK_BEDROCK;
    if (water_replaceable(below)) {
        if (world_set_block(w, x, y - 1, z, BLOCK_WATER_FALLING)) {
            return; /* Fall first; the lower cell continues the stream. */
        }
    }

    /* Minecraft's infinite-water rule: two source neighbors over a solid
     * floor can refill the middle cell. */
    if (!source && !falling && level <= 1 && block_is_solid(below)) {
        int sources = 0;
        for (int i = 0; i < 4; ++i) {
            if (world_get_block(w, x + WATER_SIDE_X[i], y, z + WATER_SIDE_Z[i]) == BLOCK_WATER) {
                ++sources;
            }
        }
        if (sources >= 2) {
            (void)world_set_block(w, x, y, z, BLOCK_WATER);
            return;
        }
    }

    int flow_level = source || falling ? 1 : level + 1;
    water_spread_side(w, x, y, z, flow_level);
}

/* Queue saturation must delay fluid work, not permanently lose it. Walk the
 * loaded block arrays incrementally while the queue catches up. */
static void water_rescan_loaded(World *w, size_t budget)
{
    while (budget > 0 && w->water_rescan_needed) {
        budget--;
        if (w->water_scan_chunk >= WORLD_MAX_CHUNKS) {
            w->water_scan_chunk = 0;
            w->water_scan_cell = 0;
            if (w->water_rescan_repeat) {
                w->water_rescan_repeat = false;
            } else {
                w->water_rescan_needed = false;
            }
            return;
        }
        Chunk *c = w->chunks[w->water_scan_chunk];
        if (c == NULL) {
            w->water_scan_chunk++;
            w->water_scan_cell = 0;
            continue;
        }
        if (w->water_scan_cell >= CHUNK_VOLUME) {
            w->water_scan_chunk++;
            w->water_scan_cell = 0;
            continue;
        }
        size_t idx = w->water_scan_cell++;
        if (!block_is_water(c->blocks[idx])) {
            continue;
        }
        int x = (int)(idx % CHUNK_X);
        int z = (int)((idx / CHUNK_X) % CHUNK_Z);
        int y = (int)(idx / CHUNK_XZ_AREA);
        int wx = c->cx * CHUNK_X + x;
        int wz = c->cz * CHUNK_Z + z;
        (void)water_enqueue(w, wx, y, wz);
        if (w->water_count >= WORLD_WATER_QUEUE_CAP) {
            /* Retry this same cell after queued work opens a slot. */
            w->water_scan_cell--;
            return;
        }
    }
}

void world_water_tick(World *w, float dt)
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
            water_step(w, update.x, update.y, update.z);
        }
        if (w->water_rescan_needed) {
            water_rescan_loaded(w, WATER_RESCAN_BUDGET);
        }
    }
}

size_t world_water_pending(const World *w)
{
    return w != NULL ? w->water_count : 0;
}
