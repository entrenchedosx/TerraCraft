#include "test_main.h"

#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"
#include "world/water.h"

/* Test: chunk_index formula (y*256 + z*16 + x).
 *
 * Returns: failure count.
 */
int test_chunk_index(void)
{
    int failures = 0;
    TEST_ASSERT(chunk_index(0, 0, 0) == 0);
    TEST_ASSERT(chunk_index(1, 0, 0) == 1);
    TEST_ASSERT(chunk_index(0, 0, 1) == 16);
    TEST_ASSERT(chunk_index(0, 1, 0) == 256);
    TEST_ASSERT(chunk_index(15, 0, 0) == 15);
    TEST_ASSERT(chunk_index(15, 255, 15) == (size_t)(CHUNK_VOLUME - 1));
    { /* Avoid MSVC C4127 (constant conditional) under /W4. */
        int vol = CHUNK_VOLUME;
        TEST_ASSERT(vol == 16 * 256 * 16);
    }
    /* Distinct neighbors map to distinct indices. */
    TEST_ASSERT(chunk_index(1, 64, 1) != chunk_index(2, 64, 1));
    TEST_ASSERT(chunk_index(1, 64, 1) != chunk_index(1, 65, 1));
    return failures;
}

/* Test: M1 canonical block IDs.
 *
 * Returns: failure count.
 */
int test_block_ids(void)
{
    int failures = 0;
    /* NOTE: compared via variables (not `BLOCK_X == N` directly) to avoid
     * MSVC C4127 "conditional expression is constant" under /W4. */
    int bid_air = BLOCK_AIR;
    int bid_stone = BLOCK_STONE;
    int bid_dirt = BLOCK_DIRT;
    int bid_grass = BLOCK_GRASS;
    int bid_water = BLOCK_WATER;
    int bid_bedrock = BLOCK_BEDROCK;
    TEST_ASSERT(bid_air == 0);
    TEST_ASSERT(bid_stone == 1);
    TEST_ASSERT(bid_dirt == 2);
    TEST_ASSERT(bid_grass == 3);
    TEST_ASSERT(bid_water == 4);
    TEST_ASSERT(bid_bedrock == 5);
    /* M1 visualization colors. */
    const BlockInfo *g = block_get_info(BLOCK_GRASS);
    TEST_ASSERT_FLOAT_EQ(g->color_r, 0.2f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(g->color_g, 0.8f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(g->color_b, 0.2f, 1e-6f);
    const BlockInfo *s = block_get_info(BLOCK_STONE);
    TEST_ASSERT_FLOAT_EQ(s->color_r, 0.5f, 1e-6f);
    /* Face visibility rules. */
    TEST_ASSERT(block_is_face_visible(BLOCK_STONE, BLOCK_AIR) == true);
    TEST_ASSERT(block_is_face_visible(BLOCK_AIR, BLOCK_STONE) == false);
    TEST_ASSERT(block_is_face_visible(BLOCK_STONE, BLOCK_STONE) == false);
    TEST_ASSERT(block_is_face_visible(BLOCK_STONE, BLOCK_WATER) == true);
    return failures;
}

/* Test: world generation produces a 3x3 grid with sane columns.
 *
 * Returns: failure count.
 */
int test_world_gen(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    world_generate_stub(w, 1337);
    TEST_ASSERT(world_chunk_count(w) == 9);
    /* All 3x3 coords present. */
    for (int cx = -1; cx <= 1; ++cx) {
        for (int cz = -1; cz <= 1; ++cz) {
            TEST_ASSERT(world_get_chunk(w, cx, cz) != NULL);
        }
    }
    /* Spot-check a column: bedrock at 0, grass at H, air above. */
    {
        int wx = 0;
        int wz = 0;
        int h = world_height_at(1337, wx, wz);
        TEST_ASSERT(h >= 56 && h <= 72);
        TEST_ASSERT(world_get_block(w, wx, 0, wz) == BLOCK_BEDROCK);
        TEST_ASSERT(world_get_block(w, wx, h, wz) == BLOCK_GRASS);
        TEST_ASSERT(world_get_block(w, wx, h + 1, wz) == BLOCK_AIR);
        if (h - 1 >= 1) {
            TEST_ASSERT(world_get_block(w, wx, h - 1, wz) == BLOCK_DIRT);
        }
        if (h - 5 >= 1) {
            TEST_ASSERT(world_get_block(w, wx, h - 5, wz) == BLOCK_STONE);
        }
        /* Cross-chunk lookup: world x=16 is chunk 1 local 0. */
        int h2 = world_height_at(1337, 16, 0);
        TEST_ASSERT(world_get_block(w, 16, h2, 0) == BLOCK_GRASS);
        /* Missing chunk reads as air. */
        TEST_ASSERT(world_get_block(w, 1000, 64, 1000) == BLOCK_AIR);
        /* y out of range reads as air. */
        TEST_ASSERT(world_get_block(w, wx, -1, wz) == BLOCK_AIR);
        TEST_ASSERT(world_get_block(w, wx, 256, wz) == BLOCK_AIR);
    }
    world_destroy(w);
    return failures;
}

/* Test: generation is deterministic for a fixed seed.
 *
 * Returns: failure count.
 */
int test_world_gen_determinism(void)
{
    int failures = 0;
    World *a = world_create();
    World *b = world_create();
    TEST_ASSERT(a != NULL && b != NULL);
    if (a == NULL || b == NULL) {
        world_destroy(a);
        world_destroy(b);
        return failures + 1;
    }
    world_generate_stub(a, 42);
    world_generate_stub(b, 42);
    TEST_ASSERT(world_chunk_count(a) == world_chunk_count(b));
    for (int wx = -16; wx < 32; wx += 7) {
        for (int wz = -16; wz < 32; wz += 5) {
            for (int wy = 60; wy < 72; ++wy) {
                TEST_ASSERT(world_get_block(a, wx, wy, wz) == world_get_block(b, wx, wy, wz));
            }
        }
    }
    /* Different seeds usually differ somewhere (phase shift). Not strictly
     * guaranteed for every column, so scan a region for any difference. */
    World *c = world_create();
    TEST_ASSERT(c != NULL);
    if (c != NULL) {
        world_generate_stub(c, 43);
        int diff_found = 0;
        for (int wx = -16; wx < 32 && !diff_found; wx += 2) {
            for (int wz = -16; wz < 32 && !diff_found; wz += 2) {
                if (world_height_at(42, wx, wz) != world_height_at(43, wx, wz)) {
                    diff_found = 1;
                }
            }
        }
        TEST_ASSERT(diff_found == 1);
        world_destroy(c);
    }
    world_destroy(a);
    world_destroy(b);
    return failures;
}

/* Test: water flows with finite levels, crosses chunk borders, retracts
 * after source removal, and dirties both affected chunk meshes. */
int test_water_simulation(void)
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
    for (int x = 0; x < 32; ++x) {
        Chunk *c = x < 16 ? a : b;
        for (int z = 0; z < 16; ++z) {
            chunk_set_block(c, x % 16, 11, z, BLOCK_STONE);
        }
    }
    chunk_set_block(a, 14, 12, 8, BLOCK_WATER);
    TEST_ASSERT(world_add_chunk(w, a) == 0);
    TEST_ASSERT(world_add_chunk(w, b) == 0);
    a->dirty = false;
    b->dirty = false;
    TEST_ASSERT(world_water_pending(w) > 0);
    for (int i = 0; i < 12; ++i) {
        world_water_tick(w, 0.25f);
    }
    TEST_ASSERT(world_get_block(w, 14, 12, 8) == BLOCK_WATER);
    TEST_ASSERT(world_get_block(w, 15, 12, 8) == BLOCK_WATER_FLOW_1);
    TEST_ASSERT(world_get_block(w, 16, 12, 8) == BLOCK_WATER_FLOW_2);
    TEST_ASSERT(world_get_block(w, 21, 12, 8) == BLOCK_WATER_FLOW_7);
    TEST_ASSERT(world_get_block(w, 22, 12, 8) == BLOCK_AIR);
    TEST_ASSERT(a->dirty && b->dirty);
    TEST_ASSERT(block_water_height(BLOCK_WATER_FLOW_4) < 1.0f);
    TEST_ASSERT(block_water_height(BLOCK_WATER_FALLING) == 1.0f);

    TEST_ASSERT(world_set_block(w, 14, 12, 8, BLOCK_AIR));
    for (int i = 0; i < 12; ++i) {
        world_water_tick(w, 0.25f);
    }
    TEST_ASSERT(world_get_block(w, 15, 12, 8) == BLOCK_AIR);
    TEST_ASSERT(world_get_block(w, 16, 12, 8) == BLOCK_AIR);
    TEST_ASSERT(world_get_block(w, 21, 12, 8) == BLOCK_AIR);
    world_destroy(w);

    /* Water at a loaded edge waits while the adjacent chunk is absent, then
     * resumes spreading as soon as that chunk streams in. */
    w = world_create();
    a = chunk_create(0, 0);
    b = chunk_create(1, 0);
    TEST_ASSERT(w != NULL && a != NULL && b != NULL);
    if (w == NULL || a == NULL || b == NULL) {
        world_destroy(w);
        chunk_destroy(a);
        chunk_destroy(b);
        return failures + 1;
    }
    for (int z = 0; z < CHUNK_Z; ++z) {
        chunk_set_block(a, CHUNK_X - 1, 11, z, BLOCK_STONE);
        chunk_set_block(b, 0, 11, z, BLOCK_STONE);
    }
    chunk_set_block(a, CHUNK_X - 1, 12, 8, BLOCK_WATER);
    TEST_ASSERT(world_add_chunk(w, a) == 0);
    world_water_tick(w, 0.25f);
    TEST_ASSERT(world_get_block(w, CHUNK_X, 12, 8) == BLOCK_AIR);
    TEST_ASSERT(world_add_chunk(w, b) == 0);
    for (int i = 0; i < 4; ++i) {
        world_water_tick(w, 0.25f);
    }
    TEST_ASSERT(block_is_water(world_get_block(w, CHUNK_X, 12, 8)));
    world_destroy(w);

    /* Water falls through open cells, and two sources over a solid floor
     * restore an empty cell between them. */
    w = world_create();
    a = chunk_create(0, 0);
    TEST_ASSERT(w != NULL && a != NULL);
    if (w == NULL || a == NULL) {
        world_destroy(w);
        chunk_destroy(a);
        return failures + 1;
    }
    for (int z = 0; z < CHUNK_Z; ++z) {
        for (int x = 0; x < CHUNK_X; ++x) {
            chunk_set_block(a, x, 8, z, BLOCK_STONE);
            chunk_set_block(a, x, 11, z, BLOCK_STONE);
        }
    }
    chunk_set_block(a, 8, 11, 8, BLOCK_AIR); /* A shaft beneath the first source. */
    chunk_set_block(a, 8, 12, 8, BLOCK_WATER);
    chunk_set_block(a, 8, 12, 10, BLOCK_WATER);
    TEST_ASSERT(world_add_chunk(w, a) == 0);
    for (int i = 0; i < 4; ++i) {
        world_water_tick(w, 0.25f);
    }
    TEST_ASSERT(block_water_is_falling(world_get_block(w, 8, 10, 8)));
    TEST_ASSERT(block_water_is_falling(world_get_block(w, 8, 9, 8)));
    TEST_ASSERT(world_get_block(w, 8, 12, 9) == BLOCK_WATER);
    world_destroy(w);
    return failures;
}

/* Saturating the fluid work queue never exceeds its fixed capacity; a loaded
 * world scan advances to discover fluid that could not be enqueued. */
int test_water_queue_bound(void)
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
    TEST_ASSERT(world_add_chunk(w, c) == 0);
    for (int y = 0; y < CHUNK_Y; ++y) {
        for (int z = 0; z < CHUNK_Z; ++z) {
            for (int x = 0; x < CHUNK_X; ++x) {
                if (x == 15 && y == 255 && z == 15) {
                    continue; /* Observable target for a source that is deferred below. */
                }
                (void)world_set_block(w, x, y, z, BLOCK_WATER);
            }
        }
    }
    /* Isolate the target so only this late, deferred source can fill it. */
    TEST_ASSERT(world_set_block(w, 15, 255, 14, BLOCK_STONE));
    TEST_ASSERT(world_water_pending(w) == WORLD_WATER_QUEUE_CAP);
    TEST_ASSERT(w->water_rescan_needed);
    size_t sentinel = chunk_index(14, 255, 15);
    TEST_ASSERT((c->water_deferred[sentinel >> 3] & (uint8_t)(1u << (sentinel & 7u))) != 0);
    world_water_tick(w, 0.25f);
    TEST_ASSERT(world_water_pending(w) <= WORLD_WATER_QUEUE_CAP);
    TEST_ASSERT(w->water_scan_cell > 0 || w->water_scan_chunk > 0);
    /* Saturated work must eventually be replayed exactly once, then stop
     * requesting recovery scans instead of leaving stale deferred bits. */
    for (int tick = 0; tick < 320 && (w->water_rescan_needed || world_water_pending(w) > 0); ++tick) {
        world_water_tick(w, 0.25f);
    }
    TEST_ASSERT(world_water_pending(w) == 0);
    TEST_ASSERT(c->water_deferred_count == 0);
    TEST_ASSERT(w->water_rescan_needed == false);
    TEST_ASSERT(block_is_water(world_get_block(w, 15, 255, 15)));
    world_destroy(w);
    return failures;
}
