#include "world/gravity.h"

#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <math.h>
#include <limits.h>
#include <stddef.h>

#define GRAVITY_STEP_SECONDS (1.0f / 60.0f)
#define GRAVITY_BLOCK_BUDGET 256u
#define GRAVITY_RESCAN_BUDGET 4096u
#define GRAVITY_ACCELERATION 32.0f
#define GRAVITY_TERMINAL_SPEED 80.0f
#define GRAVITY_COLLISION_EPSILON 0.0001f

static int floor_div_16(int value)
{
    int q = value / 16;
    if (value % 16 < 0) {
        --q;
    }
    return q;
}

static int mod_16(int value)
{
    int r = value % 16;
    return r < 0 ? r + 16 : r;
}

static void gravity_request_rescan(World *w)
{
    if (w->gravity_rescan_needed) {
        w->gravity_rescan_repeat = true;
    } else {
        w->gravity_scan_chunk = 0;
        w->gravity_scan_cell = 0;
    }
    w->gravity_rescan_needed = true;
}

static void gravity_defer_cell(Chunk *c, size_t idx)
{
    if (c == NULL || idx >= CHUNK_VOLUME) {
        return;
    }
    uint8_t bit = (uint8_t)(1u << (idx & 7u));
    uint8_t *slot = &c->gravity_deferred[idx >> 3];
    if ((*slot & bit) == 0) {
        *slot |= bit;
        c->gravity_deferred_count++;
    }
}

/* true means queued or already represented; false means invalid/unloaded or
 * capacity prevented a new entry. Per-chunk bits make the queue idempotent. */
static bool gravity_enqueue(World *w, int x, int y, int z)
{
    if (w == NULL || w->gravity_updates == NULL || y < 0 || y >= CHUNK_Y) {
        return false;
    }
    int cx = floor_div_16(x);
    int cz = floor_div_16(z);
    Chunk *c = world_get_chunk(w, cx, cz);
    if (c == NULL) {
        return false; /* Never hold work for an unloaded chunk. */
    }
    size_t idx = chunk_index(mod_16(x), y, mod_16(z));
    uint8_t bit = (uint8_t)(1u << (idx & 7u));
    uint8_t *slot = &c->gravity_queued[idx >> 3];
    if ((*slot & bit) != 0) {
        return true;
    }
    if (w->gravity_count >= WORLD_GRAVITY_QUEUE_CAP) {
        gravity_defer_cell(c, idx);
        gravity_request_rescan(w);
        return false;
    }
    size_t tail = (w->gravity_head + w->gravity_count) % WORLD_GRAVITY_QUEUE_CAP;
    w->gravity_updates[tail] = (WorldGravityUpdate){x, y, z};
    w->gravity_count++;
    *slot |= bit;
    return true;
}

void world_gravity_notify_block_changed(World *w, int x, int y, int z)
{
    if (w == NULL || y < 0 || y >= CHUNK_Y) {
        return;
    }
    if (block_has_gravity(world_get_block(w, x, y, z))) {
        (void)gravity_enqueue(w, x, y, z);
    }
    if (y + 1 < CHUNK_Y && block_has_gravity(world_get_block(w, x, y + 1, z))) {
        (void)gravity_enqueue(w, x, y + 1, z);
    }
}

void world_gravity_seed_chunk(World *w, int cx, int cz)
{
    Chunk *c = world_get_chunk(w, cx, cz);
    if (w == NULL || c == NULL) {
        return;
    }
    int ox = cx * CHUNK_X;
    int oz = cz * CHUNK_Z;
    for (size_t idx = 0; idx < CHUNK_VOLUME; ++idx) {
        if (!block_has_gravity(c->blocks[idx])) {
            continue;
        }
        int x = (int)(idx % CHUNK_X);
        int z = (int)((idx / CHUNK_X) % CHUNK_Z);
        int y = (int)(idx / CHUNK_XZ_AREA);
        (void)gravity_enqueue(w, ox + x, y, oz + z);
    }
}

static void gravity_rescan_loaded(World *w, size_t budget)
{
    while (budget > 0 && w->gravity_rescan_needed) {
        if (w->gravity_scan_chunk >= w->count) {
            w->gravity_scan_chunk = 0;
            w->gravity_scan_cell = 0;
            if (w->gravity_rescan_repeat) {
                w->gravity_rescan_repeat = false;
            } else {
                bool any_deferred = false;
                for (size_t i = 0; i < w->count; ++i) {
                    if (w->chunks[i] != NULL && w->chunks[i]->gravity_deferred_count > 0) {
                        any_deferred = true;
                        break;
                    }
                }
                w->gravity_rescan_needed = any_deferred;
            }
            return;
        }
        Chunk *c = w->chunks[w->gravity_scan_chunk];
        if (c == NULL || c->gravity_deferred_count == 0) {
            w->gravity_scan_chunk++;
            w->gravity_scan_cell = 0;
            continue;
        }
        if (w->gravity_scan_cell >= (CHUNK_VOLUME + 7u) / 8u) {
            w->gravity_scan_chunk++;
            w->gravity_scan_cell = 0;
            continue;
        }
        size_t byte_index = w->gravity_scan_cell;
        uint8_t bits = c->gravity_deferred[byte_index];
        if (bits == 0) {
            w->gravity_scan_cell++;
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
            if (idx >= CHUNK_VOLUME || !block_has_gravity(c->blocks[idx])) {
                c->gravity_deferred[byte_index] &= (uint8_t)~bit;
                c->gravity_deferred_count--;
                continue;
            }
            if ((c->gravity_queued[byte_index] & bit) != 0) {
                c->gravity_deferred[byte_index] &= (uint8_t)~bit;
                c->gravity_deferred_count--;
                continue;
            }
            if (w->gravity_count >= WORLD_GRAVITY_QUEUE_CAP) {
                return;
            }
            int x = (int)(idx % CHUNK_X);
            int z = (int)((idx / CHUNK_X) % CHUNK_Z);
            int y = (int)(idx / CHUNK_XZ_AREA);
            int wx = c->cx * CHUNK_X + x;
            int wz = c->cz * CHUNK_Z + z;
            if (gravity_enqueue(w, wx, y, wz)) {
                c->gravity_deferred[byte_index] &= (uint8_t)~bit;
                c->gravity_deferred_count--;
            } else if (w->gravity_count >= WORLD_GRAVITY_QUEUE_CAP) {
                return;
            }
        }
        if (c->gravity_deferred[byte_index] == 0) {
            w->gravity_scan_cell++;
        }
    }
}

static bool gravity_supported(const World *w, int x, int y, int z)
{
    return y <= 0 || block_is_solid(world_get_block(w, x, y - 1, z));
}

static bool gravity_write(World *w, int x, int y, int z, uint16_t id, bool replicate)
{
    return replicate ? world_set_block(w, x, y, z, id) : world_set_block_unreplicated(w, x, y, z, id);
}

static bool gravity_start_fall(World *w, int x, int y, int z, bool replicate)
{
    uint16_t id = world_get_block(w, x, y, z);
    if (!block_has_gravity(id) || gravity_supported(w, x, y, z)) {
        return false;
    }
    WorldFallingBlock *record = NULL;
    for (size_t i = 0; i < WORLD_FALLING_BLOCK_CAP; ++i) {
        if (!w->falling_blocks[i].active) {
            record = &w->falling_blocks[i];
            break;
        }
    }
    if (record == NULL) {
        gravity_request_rescan(w);
        return false;
    }
    /* Do not clear the canonical block unless a physics record is ready. */
    if (replicate && w->gravity_event_callback != NULL &&
        !w->gravity_event_callback(w->gravity_event_context, false, x, y, z, id)) {
        return false; /* The authoritative source stays put until the event queues. */
    }
    if (!gravity_write(w, x, y, z, BLOCK_AIR, replicate)) {
        return false;
    }
    *record = (WorldFallingBlock){.active = true,
                                  .awaiting_authority = false,
                                  .locally_settled = false,
                                  .block_id = id,
                                  .x = x,
                                  .z = z,
                                  .source_y = y,
                                  .predicted_landing_y = -1,
                                  .y = (float)y,
                                  .velocity_y = 0.0f};
    w->falling_active++;
    return true;
}

static bool gravity_find_landing(const World *w, const WorldFallingBlock *falling, int *out_y)
{
    if (w == NULL || falling == NULL || out_y == NULL) {
        return false;
    }
    float bounded_y = falling->y;
    if (!isfinite(bounded_y)) {
        return false;
    }
    int start = (int)floorf(bounded_y + GRAVITY_COLLISION_EPSILON);
    if (start >= CHUNK_Y) {
        start = CHUNK_Y - 1;
    }
    if (start < 0) {
        start = 0;
    }
    for (int cell = start; cell >= 0; --cell) {
        uint16_t occupying = world_get_block(w, falling->x, cell, falling->z);
        if (block_is_solid(occupying)) {
            *out_y = cell + 1;
            return *out_y < CHUNK_Y;
        }
        if (cell == 0 || block_is_solid(world_get_block(w, falling->x, cell - 1, falling->z))) {
            *out_y = cell;
            return true;
        }
    }
    *out_y = 0;
    return true;
}

static bool gravity_land(World *w, WorldFallingBlock *falling, int y, bool replicate, bool settling)
{
    if (y < 0 || y >= CHUNK_Y || world_get_chunk(w, floor_div_16(falling->x), floor_div_16(falling->z)) == NULL) {
        return false;
    }
    uint16_t target = world_get_block(w, falling->x, y, falling->z);
    while (block_is_solid(target)) {
        if (++y >= CHUNK_Y) {
            return false;
        }
        target = world_get_block(w, falling->x, y, falling->z);
    }
    if (!replicate && !settling) {
        /* LAN clients predict the fall visually but wait for the host before
         * writing a canonical landing block. */
        falling->y = (float)y;
        falling->velocity_y = 0.0f;
        falling->awaiting_authority = true;
        return true;
    }
    if (!replicate && settling) {
        /* Persist local state while keeping the prediction reconcilable until
         * the host's LAND event reaches this client. */
        if (target != falling->block_id &&
            !gravity_write(w, falling->x, y, falling->z, falling->block_id, false)) {
            return false;
        }
        falling->y = (float)y;
        falling->velocity_y = 0.0f;
        falling->awaiting_authority = true;
        falling->locally_settled = true;
        falling->predicted_landing_y = y;
        return true;
    }
    if (replicate && w->gravity_event_callback != NULL &&
        !w->gravity_event_callback(w->gravity_event_context, true, falling->x, y, falling->z,
                                   falling->block_id)) {
        falling->y = (float)y;
        falling->velocity_y = 0.0f;
        return false; /* Hold at the landing until its reliable event can queue. */
    }
    if (target != falling->block_id && !gravity_write(w, falling->x, y, falling->z, falling->block_id, replicate)) {
        return false;
    }
    falling->active = false;
    if (w->falling_active > 0) {
        w->falling_active--;
    }
    return true;
}

static void gravity_step_falling(World *w, WorldFallingBlock *falling, float dt, bool replicate)
{
    if (!falling->active || falling->awaiting_authority || dt <= 0.0f) {
        return;
    }
    /* Coordinates are fixed to the source column, so no stale chunk pointer
     * or horizontal tunneling is possible. Missing chunks pause safely. */
    if (world_get_chunk(w, floor_div_16(falling->x), floor_div_16(falling->z)) == NULL) {
        return;
    }
    falling->velocity_y -= GRAVITY_ACCELERATION * dt;
    if (!isfinite(falling->velocity_y) || falling->velocity_y < -GRAVITY_TERMINAL_SPEED) {
        falling->velocity_y = -GRAVITY_TERMINAL_SPEED;
    }
    float next_y = falling->y + falling->velocity_y * dt;
    if (!isfinite(next_y)) {
        return;
    }
    if (next_y < 0.0f) {
        next_y = 0.0f;
    }
    int from = (int)floorf(falling->y + GRAVITY_COLLISION_EPSILON);
    int to = (int)floorf(next_y + GRAVITY_COLLISION_EPSILON);
    if (from >= CHUNK_Y) {
        from = CHUNK_Y - 1;
    }
    if (to < 0) {
        to = 0;
    }
    /* Check every crossed voxel so even a long catch-up step cannot tunnel. */
    for (int cell = from; cell >= to; --cell) {
        uint16_t occupying = world_get_block(w, falling->x, cell, falling->z);
        if (block_is_solid(occupying)) {
            (void)gravity_land(w, falling, cell + 1, replicate, false);
            return;
        }
        if (cell == 0) {
            (void)gravity_land(w, falling, 0, replicate, false);
            return;
        }
        if (block_is_solid(world_get_block(w, falling->x, cell - 1, falling->z))) {
            (void)gravity_land(w, falling, cell, replicate, false);
            return;
        }
        if (cell == to) {
            break;
        }
    }
    falling->y = next_y;
}

static void gravity_process_queue(World *w, unsigned budget, bool replicate)
{
    while (budget-- > 0 && w->gravity_count > 0) {
        if (w->falling_active >= WORLD_FALLING_BLOCK_CAP) {
            return; /* Leave queued blocks intact until a fall lands. */
        }
        WorldGravityUpdate update = w->gravity_updates[w->gravity_head];
        w->gravity_head = (w->gravity_head + 1u) % WORLD_GRAVITY_QUEUE_CAP;
        w->gravity_count--;
        Chunk *c = world_get_chunk(w, floor_div_16(update.x), floor_div_16(update.z));
        if (c == NULL || update.y < 0 || update.y >= CHUNK_Y) {
            continue;
        }
        size_t idx = chunk_index(mod_16(update.x), update.y, mod_16(update.z));
        c->gravity_queued[idx >> 3] &= (uint8_t)~(1u << (idx & 7u));
        if (!gravity_start_fall(w, update.x, update.y, update.z, replicate) &&
            block_has_gravity(world_get_block(w, update.x, update.y, update.z)) &&
            !gravity_supported(w, update.x, update.y, update.z)) {
            /* Keep failed authoritative starts queued for a later tick. This
             * includes a full LAN event queue and transient setter failures. */
            (void)gravity_enqueue(w, update.x, update.y, update.z);
            return;
        }
    }
}

void world_gravity_tick(World *w, float dt, bool replicate_changes)
{
    if (w == NULL || w->gravity_updates == NULL || w->falling_blocks == NULL || !(dt > 0.0f) ||
        !isfinite(dt)) {
        return;
    }
    w->gravity_replicate_changes = replicate_changes;
    w->gravity_accumulator += dt;
    if (w->gravity_accumulator > 1.0f) {
        w->gravity_accumulator = 1.0f;
    }
    while (w->gravity_accumulator >= GRAVITY_STEP_SECONDS) {
        w->gravity_accumulator -= GRAVITY_STEP_SECONDS;
        gravity_process_queue(w, GRAVITY_BLOCK_BUDGET, replicate_changes);
        for (size_t i = 0; i < WORLD_FALLING_BLOCK_CAP; ++i) {
            gravity_step_falling(w, &w->falling_blocks[i], GRAVITY_STEP_SECONDS, replicate_changes);
        }
        if (w->gravity_rescan_needed) {
            gravity_rescan_loaded(w, GRAVITY_RESCAN_BUDGET);
        }
    }
}

static bool gravity_settle_record(World *w, WorldFallingBlock *falling, bool replicate)
{
    if (!falling->active) {
        return true;
    }
    if (!replicate && falling->locally_settled) {
        return true;
    }
    int y = 0;
    if (!gravity_find_landing(w, falling, &y)) {
        return false;
    }
    return gravity_land(w, falling, y, replicate, true);
}

bool world_gravity_apply_remote_start(World *w, int x, int y, int z, uint16_t block_id)
{
    if (w == NULL || y < 0 || y >= CHUNK_Y || !block_has_gravity(block_id) ||
        world_get_chunk(w, floor_div_16(x), floor_div_16(z)) == NULL || w->falling_blocks == NULL) {
        return false;
    }
    for (size_t i = 0; i < WORLD_FALLING_BLOCK_CAP; ++i) {
        const WorldFallingBlock *falling = &w->falling_blocks[i];
        if (falling->active && falling->x == x && falling->z == z && falling->source_y == y &&
            falling->block_id == block_id) {
            return true; /* A prior local prediction already represents it. */
        }
    }
    uint16_t source = world_get_block(w, x, y, z);
    if (source != BLOCK_AIR && !world_set_block_unreplicated(w, x, y, z, BLOCK_AIR)) {
        return false;
    }
    WorldFallingBlock *record = NULL;
    for (size_t i = 0; i < WORLD_FALLING_BLOCK_CAP; ++i) {
        if (!w->falling_blocks[i].active) {
            record = &w->falling_blocks[i];
            break;
        }
    }
    if (record == NULL) {
        return true; /* Apply the authoritative source edit without a visual record. */
    }
    *record = (WorldFallingBlock){.active = true,
                                  .awaiting_authority = false,
                                  .locally_settled = false,
                                  .block_id = block_id,
                                  .x = x,
                                  .z = z,
                                  .source_y = y,
                                  .predicted_landing_y = -1,
                                  .y = (float)y,
                                  .velocity_y = 0.0f};
    w->falling_active++;
    return true;
}

bool world_gravity_apply_remote_landing(World *w, int x, int y, int z, uint16_t block_id)
{
    if (w == NULL || y < 0 || y >= CHUNK_Y || !block_has_gravity(block_id) || w->falling_blocks == NULL) {
        return false;
    }
    size_t best = WORLD_FALLING_BLOCK_CAP;
    float best_distance = INFINITY;
    for (size_t i = 0; i < WORLD_FALLING_BLOCK_CAP; ++i) {
        WorldFallingBlock *falling = &w->falling_blocks[i];
        if (!falling->active || falling->x != x || falling->z != z || falling->block_id != block_id) {
            continue;
        }
        float distance = fabsf(falling->y - (float)y);
        if (distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    if (best == WORLD_FALLING_BLOCK_CAP) {
        return false;
    }
    WorldFallingBlock *falling = &w->falling_blocks[best];
    if (falling->locally_settled && falling->predicted_landing_y != y &&
        falling->predicted_landing_y >= 0 && falling->predicted_landing_y < CHUNK_Y &&
        world_get_block(w, x, falling->predicted_landing_y, z) == block_id) {
        (void)world_set_block_unreplicated(w, x, falling->predicted_landing_y, z, BLOCK_AIR);
    }
    w->falling_blocks[best].active = false;
    if (w->falling_active > 0) {
        w->falling_active--;
    }
    return true;
}

bool world_gravity_settle_chunk(World *w, int cx, int cz, bool replicate_changes)
{
    if (w == NULL || w->falling_blocks == NULL) {
        return false;
    }
    for (size_t i = 0; i < WORLD_FALLING_BLOCK_CAP; ++i) {
        WorldFallingBlock *falling = &w->falling_blocks[i];
        if (falling->active && floor_div_16(falling->x) == cx && floor_div_16(falling->z) == cz &&
            !gravity_settle_record(w, falling, replicate_changes)) {
            return false;
        }
    }
    return true;
}

bool world_gravity_settle_all(World *w, bool replicate_changes)
{
    if (w == NULL || w->falling_blocks == NULL) {
        return false;
    }
    for (size_t i = 0; i < WORLD_FALLING_BLOCK_CAP; ++i) {
        if (!gravity_settle_record(w, &w->falling_blocks[i], replicate_changes)) {
            return false;
        }
    }
    return true;
}

unsigned world_gravity_active_count(const World *w)
{
    return w != NULL && w->falling_active <= UINT_MAX ? (unsigned)w->falling_active : 0u;
}
