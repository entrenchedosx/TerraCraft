#include "test_main.h"

#include "game/player.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/gravity.h"
#include "world/world.h"
#include "world/world_save.h"

#include <string.h>

typedef struct GravityEventCounts {
    int starts;
    int landings;
    int block_changes;
    int sequence;
    int start_sequence;
    int source_air_sequence;
} GravityEventCounts;

typedef struct GravityEventGate {
    bool allow;
    int starts;
    int landings;
} GravityEventGate;

static bool gravity_test_event(void *context, bool landed, int x, int y, int z, uint16_t block_id)
{
    GravityEventCounts *counts = (GravityEventCounts *)context;
    (void)x;
    (void)y;
    (void)z;
    (void)block_id;
    if (counts != NULL) {
        counts->sequence++;
        if (landed) {
            counts->landings++;
        } else {
            counts->starts++;
            counts->start_sequence = counts->sequence;
        }
    }
    return true;
}

static bool gravity_test_event_gate(void *context, bool landed, int x, int y, int z, uint16_t block_id)
{
    GravityEventGate *gate = (GravityEventGate *)context;
    (void)x;
    (void)y;
    (void)z;
    (void)block_id;
    if (gate != NULL) {
        if (landed) {
            gate->landings++;
        } else {
            gate->starts++;
        }
        return gate->allow;
    }
    return false;
}

static void gravity_test_block_change(void *context, int x, int y, int z, uint16_t block_id)
{
    GravityEventCounts *counts = (GravityEventCounts *)context;
    (void)x;
    (void)z;
    if (counts != NULL) {
        counts->sequence++;
        counts->block_changes++;
        if (y == 8 && block_id == BLOCK_AIR) {
            counts->source_air_sequence = counts->sequence;
        }
    }
}

static World *gravity_test_world(void)
{
    World *w = world_create();
    Chunk *c = chunk_create(0, 0);
    if (w == NULL || c == NULL) {
        world_destroy(w);
        chunk_destroy(c);
        return NULL;
    }
    for (int z = 0; z < CHUNK_Z; ++z) {
        for (int x = 0; x < CHUNK_X; ++x) {
            chunk_set_block(c, x, 0, z, BLOCK_BEDROCK);
        }
    }
    if (world_add_chunk(w, c) != 0) {
        world_destroy(w);
        chunk_destroy(c);
        return NULL;
    }
    return w;
}

static void gravity_advance(World *w, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        world_gravity_tick(w, 1.0f / 60.0f, true);
    }
}

int test_gravity_blocks_fall_land_and_save_settle(void)
{
    int failures = 0;
    World *w = gravity_test_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    TEST_ASSERT(block_has_gravity(BLOCK_SAND));
    TEST_ASSERT(!block_has_gravity(BLOCK_STONE));
    TEST_ASSERT(world_set_block(w, 5, 7, 5, BLOCK_SAND));
    gravity_advance(w, 1);
    TEST_ASSERT(world_get_block(w, 5, 7, 5) == BLOCK_AIR);
    TEST_ASSERT(world_gravity_active_count(w) == 1);
    gravity_advance(w, 120);
    TEST_ASSERT(world_gravity_active_count(w) == 0);
    TEST_ASSERT(world_get_block(w, 5, 1, 5) == BLOCK_SAND);

    /* A moving block is reduced to canonical chunk state before persistence. */
    TEST_ASSERT(world_set_block(w, 8, 8, 8, BLOCK_SAND));
    gravity_advance(w, 1);
    TEST_ASSERT(world_gravity_active_count(w) == 1);
    TEST_ASSERT(world_gravity_settle_all(w, true));
    TEST_ASSERT(world_gravity_active_count(w) == 0);
    TEST_ASSERT(world_get_block(w, 8, 1, 8) == BLOCK_SAND);
    world_destroy(w);
    return failures;
}

int test_gravity_support_chain_and_water(void)
{
    int failures = 0;
    World *w = gravity_test_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }

    /* Supported sand stays canonical; removing its support wakes the whole
     * stack and creates independent continuous physics records. */
    TEST_ASSERT(world_set_block(w, 4, 4, 4, BLOCK_STONE));
    TEST_ASSERT(world_set_block(w, 4, 5, 4, BLOCK_SAND));
    TEST_ASSERT(world_set_block(w, 4, 6, 4, BLOCK_SAND));
    TEST_ASSERT(world_set_block(w, 4, 7, 4, BLOCK_SAND));
    gravity_advance(w, 1);
    TEST_ASSERT(world_gravity_active_count(w) == 0);
    TEST_ASSERT(world_set_block(w, 4, 4, 4, BLOCK_AIR));
    gravity_advance(w, 1);
    TEST_ASSERT(world_gravity_active_count(w) == 3);
    gravity_advance(w, 120);
    TEST_ASSERT(world_gravity_active_count(w) == 0);
    TEST_ASSERT(world_get_block(w, 4, 1, 4) == BLOCK_SAND);
    TEST_ASSERT(world_get_block(w, 4, 2, 4) == BLOCK_SAND);
    TEST_ASSERT(world_get_block(w, 4, 3, 4) == BLOCK_SAND);

    /* Water is non-solid: sand falls through it, replaces the bottom fluid
     * cell on landing, and wakes the fluid simulation through world_set_block. */
    TEST_ASSERT(world_set_block(w, 10, 1, 10, BLOCK_WATER));
    TEST_ASSERT(world_set_block(w, 10, 2, 10, BLOCK_WATER));
    TEST_ASSERT(world_set_block(w, 10, 3, 10, BLOCK_WATER));
    TEST_ASSERT(world_set_block(w, 10, 5, 10, BLOCK_SAND));
    gravity_advance(w, 120);
    TEST_ASSERT(world_gravity_active_count(w) == 0);
    TEST_ASSERT(world_get_block(w, 10, 1, 10) == BLOCK_SAND);
    world_destroy(w);
    return failures;
}

int test_gravity_queue_bound_recovery(void)
{
    int failures = 0;
    World *w = world_create();
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(w != NULL && c != NULL);
    if (w == NULL || c == NULL) {
        world_destroy(w);
        chunk_destroy(c);
        return failures + 1;
    }
    for (int y = 0; y < CHUNK_Y; ++y) {
        for (int z = 0; z < CHUNK_Z; ++z) {
            for (int x = 0; x < CHUNK_X; ++x) {
                chunk_set_block(c, x, y, z, BLOCK_SAND);
            }
        }
    }
    /* One missing support makes this late, overflow-deferred block observably
     * fall into the gap after the bounded scheduler drains and rescans. */
    chunk_set_block(c, 15, 199, 15, BLOCK_AIR);
    TEST_ASSERT(world_add_chunk(w, c) == 0);
    size_t sentinel = chunk_index(15, 200, 15);
    TEST_ASSERT(w->gravity_count == WORLD_GRAVITY_QUEUE_CAP);
    TEST_ASSERT(c->gravity_deferred_count > 0);
    TEST_ASSERT((c->gravity_deferred[sentinel >> 3] & (uint8_t)(1u << (sentinel & 7u))) != 0);
    gravity_advance(w, 600);
    TEST_ASSERT(w->gravity_count == 0);
    TEST_ASSERT(c->gravity_deferred_count == 0);
    TEST_ASSERT(!w->gravity_rescan_needed);
    TEST_ASSERT(world_gravity_active_count(w) == 0);
    TEST_ASSERT(world_get_block(w, 15, 199, 15) == BLOCK_SAND);
    world_destroy(w);
    return failures;
}

int test_gravity_retries_when_lan_queue_is_full(void)
{
    int failures = 0;
    World *w = gravity_test_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    GravityEventGate gate = {0};
    world_set_gravity_event_callback(w, gravity_test_event_gate, &gate);
    TEST_ASSERT(world_set_block(w, 5, 8, 5, BLOCK_SAND));

    /* A full host LAN queue refuses a start event. The source remains intact
     * and is retried after the reliable queue drains. */
    gravity_advance(w, 1);
    TEST_ASSERT(gate.starts == 1);
    TEST_ASSERT(world_get_block(w, 5, 8, 5) == BLOCK_SAND);
    TEST_ASSERT(world_gravity_active_count(w) == 0);
    gate.allow = true;
    gravity_advance(w, 1);
    TEST_ASSERT(gate.starts == 2);
    TEST_ASSERT(world_get_block(w, 5, 8, 5) == BLOCK_AIR);
    TEST_ASSERT(world_gravity_active_count(w) == 1);

    /* A full queue at landing holds the falling record in place. Once the
     * event can queue, the block commits exactly once to canonical state. */
    gate.allow = false;
    gravity_advance(w, 120);
    TEST_ASSERT(gate.landings > 0);
    TEST_ASSERT(world_gravity_active_count(w) == 1);
    TEST_ASSERT(world_get_block(w, 5, 1, 5) == BLOCK_AIR);
    gate.allow = true;
    gravity_advance(w, 1);
    TEST_ASSERT(world_gravity_active_count(w) == 0);
    TEST_ASSERT(world_get_block(w, 5, 1, 5) == BLOCK_SAND);
    world_destroy(w);
    return failures;
}

int test_world_chunk_compaction_and_seam_dirtying(void)
{
    int failures = 0;
    World *w = world_create();
    Chunk *a = chunk_create(0, 0);
    Chunk *b = chunk_create(1, 0);
    TEST_ASSERT(w != NULL && a != NULL && b != NULL);
    if (w == NULL || a == NULL || b == NULL) {
        world_destroy(w);
        chunk_destroy(a);
        chunk_destroy(b);
        return failures + 1;
    }
    TEST_ASSERT(world_add_chunk(w, a) == 0);
    TEST_ASSERT(world_add_chunk(w, b) == 0);
    a->dirty = false;
    b->dirty = false;
    TEST_ASSERT(world_remove_chunk(w, 0, 0));
    TEST_ASSERT(world_chunk_count(w) == 1);
    TEST_ASSERT(w->chunks[0] == b);
    TEST_ASSERT(b->dirty);
    world_destroy(w);
    return failures;
}

int test_gravity_lan_prediction_and_authority(void)
{
    int failures = 0;
    World *host = gravity_test_world();
    World *client = gravity_test_world();
    TEST_ASSERT(host != NULL && client != NULL);
    if (host == NULL || client == NULL) {
        world_destroy(host);
        world_destroy(client);
        return failures + 1;
    }
    GravityEventCounts host_events = {0};
    GravityEventCounts client_events = {0};
    TEST_ASSERT(world_set_block(host, 6, 8, 6, BLOCK_SAND));
    world_set_block_change_callback(host, gravity_test_block_change, &host_events);
    world_set_gravity_event_callback(host, gravity_test_event, &host_events);
    world_gravity_tick(host, 1.0f / 60.0f, true);
    TEST_ASSERT(host_events.starts == 1);
    TEST_ASSERT(host_events.block_changes == 1);
    TEST_ASSERT(host_events.start_sequence < host_events.source_air_sequence);

    /* The client receives the explicit start before the ordinary source AIR
     * delta. Applying it twice must not duplicate its predicted record. */
    TEST_ASSERT(world_set_block(client, 6, 8, 6, BLOCK_SAND));
    world_set_block_change_callback(client, gravity_test_block_change, &client_events);
    world_set_gravity_event_callback(client, gravity_test_event, &client_events);
    world_set_gravity_replication(client, false);
    TEST_ASSERT(world_gravity_apply_remote_start(client, 6, 8, 6, BLOCK_SAND));
    TEST_ASSERT(world_gravity_active_count(client) == 1);
    TEST_ASSERT(world_gravity_apply_remote_start(client, 6, 8, 6, BLOCK_SAND));
    TEST_ASSERT(world_gravity_active_count(client) == 1);
    TEST_ASSERT(world_get_block(client, 6, 8, 6) == BLOCK_AIR);
    TEST_ASSERT(!world_set_block_unreplicated(client, 6, 8, 6, BLOCK_AIR));

    /* A predicting client stops at its landing cell, leaves canonical chunk
     * data untouched, then removes the record when the host landing arrives. */
    for (int i = 0; i < 120; ++i) {
        world_gravity_tick(client, 1.0f / 60.0f, false);
    }
    TEST_ASSERT(world_gravity_active_count(client) == 1);
    TEST_ASSERT(client->falling_blocks[0].awaiting_authority || client->falling_blocks[1].awaiting_authority);
    TEST_ASSERT(world_get_block(client, 6, 1, 6) == BLOCK_AIR);
    TEST_ASSERT(world_gravity_apply_remote_landing(client, 6, 1, 6, BLOCK_SAND));
    TEST_ASSERT(world_set_block_unreplicated(client, 6, 1, 6, BLOCK_SAND));
    TEST_ASSERT(world_gravity_active_count(client) == 0);
    TEST_ASSERT(world_get_block(client, 6, 1, 6) == BLOCK_SAND);
    TEST_ASSERT(client_events.starts == 0 && client_events.landings == 0 && client_events.block_changes == 0);

    /* Client saves settle local predictions without broadcasting them. */
    TEST_ASSERT(world_set_block_unreplicated(client, 7, 8, 7, BLOCK_SAND));
    world_gravity_tick(client, 1.0f / 60.0f, false);
    TEST_ASSERT(world_gravity_active_count(client) == 1);
    memcpy(client->name, "gravity_lan", sizeof("gravity_lan"));
    Player saved_player;
    player_init(&saved_player);
    const char *save_dir = "test_tmp_gravity_lan";
    TEST_ASSERT(world_save_all(save_dir, client, &saved_player, saved_player.pos, true, 0.5f) == 0);
    TEST_ASSERT(world_gravity_active_count(client) == 1);
    bool saved_prediction = false;
    for (size_t i = 0; i < WORLD_FALLING_BLOCK_CAP; ++i) {
        saved_prediction = saved_prediction || (client->falling_blocks[i].active &&
                                                client->falling_blocks[i].locally_settled &&
                                                client->falling_blocks[i].predicted_landing_y == 1);
    }
    TEST_ASSERT(saved_prediction);
    TEST_ASSERT(world_get_block(client, 7, 1, 7) == BLOCK_SAND);
    /* The host has an obstacle the client did not predict. Reconciliation
     * removes the saved prediction and keeps only the authoritative landing. */
    TEST_ASSERT(world_set_block_unreplicated(client, 7, 3, 7, BLOCK_STONE));
    TEST_ASSERT(world_gravity_apply_remote_landing(client, 7, 4, 7, BLOCK_SAND));
    TEST_ASSERT(world_set_block_unreplicated(client, 7, 4, 7, BLOCK_SAND));
    TEST_ASSERT(world_gravity_active_count(client) == 0);
    TEST_ASSERT(world_get_block(client, 7, 1, 7) == BLOCK_AIR);
    TEST_ASSERT(world_get_block(client, 7, 4, 7) == BLOCK_SAND);
    TEST_ASSERT(client_events.starts == 0 && client_events.landings == 0 && client_events.block_changes == 0);
    TEST_ASSERT(world_save_delete(save_dir) == 0);

    /* Chunk unload uses the same local-only policy. */
    TEST_ASSERT(world_set_block_unreplicated(client, 7, 8, 7, BLOCK_SAND));
    world_gravity_tick(client, 1.0f / 60.0f, false);
    TEST_ASSERT(world_gravity_active_count(client) == 1);
    TEST_ASSERT(world_remove_chunk(client, 0, 0));
    TEST_ASSERT(world_gravity_active_count(client) == 1);
    TEST_ASSERT(client_events.starts == 0 && client_events.landings == 0 && client_events.block_changes == 0);

    world_destroy(host);
    world_destroy(client);
    return failures;
}

int test_gravity_lan_pool_full_start_clears_source(void)
{
    int failures = 0;
    World *client = gravity_test_world();
    TEST_ASSERT(client != NULL);
    if (client == NULL) {
        return failures + 1;
    }
    for (size_t i = 0; i < WORLD_FALLING_BLOCK_CAP; ++i) {
        client->falling_blocks[i] = (WorldFallingBlock){.active = true,
                                                        .block_id = BLOCK_SAND,
                                                        .x = 100 + (int)i,
                                                        .z = 100,
                                                        .source_y = 8,
                                                        .predicted_landing_y = -1,
                                                        .y = 8.0f};
    }
    client->falling_active = WORLD_FALLING_BLOCK_CAP;
    TEST_ASSERT(world_set_block(client, 4, 8, 4, BLOCK_SAND));
    TEST_ASSERT(world_gravity_apply_remote_start(client, 4, 8, 4, BLOCK_SAND));
    TEST_ASSERT(world_get_block(client, 4, 8, 4) == BLOCK_AIR);
    TEST_ASSERT(world_gravity_active_count(client) == WORLD_FALLING_BLOCK_CAP);
    /* The authoritative LAND still installs exactly one canonical block even
     * though this client had no spare render/physics record for the fall. */
    TEST_ASSERT(!world_gravity_apply_remote_landing(client, 4, 1, 4, BLOCK_SAND));
    TEST_ASSERT(world_set_block_unreplicated(client, 4, 1, 4, BLOCK_SAND));
    TEST_ASSERT(world_get_block(client, 4, 8, 4) == BLOCK_AIR);
    TEST_ASSERT(world_get_block(client, 4, 1, 4) == BLOCK_SAND);
    world_destroy(client);
    return failures;
}
