#include "test_main.h"

#include "core/hashmap.h"
#include "math/mmath.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/streamer.h"
#include "world/world.h"
#include "world/world_gen.h"

#include <stdint.h>

/* Test: hashmap put/get/remove/count, including key 0 and negatives.
 *
 * Returns: failure count.
 */
int test_hashmap_basic(void)
{
    int failures = 0;
    HashMap m;
    TEST_ASSERT(hashmap_init(&m, 0) == 0);
    TEST_ASSERT(hashmap_count(&m) == 0);
    TEST_ASSERT(hashmap_get(&m, 0) == NULL);

    int a = 1, b = 2;
    TEST_ASSERT(hashmap_put(&m, 0, &a) == 0); /* Key 0 must work. */
    TEST_ASSERT(hashmap_put(&m, hashmap_chunk_key(-3, -7), &b) == 0);
    TEST_ASSERT(hashmap_count(&m) == 2);
    TEST_ASSERT(hashmap_get(&m, 0) == &a);
    TEST_ASSERT(hashmap_get(&m, hashmap_chunk_key(-3, -7)) == &b);
    TEST_ASSERT(hashmap_get(&m, hashmap_chunk_key(1, 2)) == NULL);

    /* Replace keeps count. */
    TEST_ASSERT(hashmap_put(&m, 0, &b) == 0);
    TEST_ASSERT(hashmap_get(&m, 0) == &b);
    TEST_ASSERT(hashmap_count(&m) == 2);

    /* Remove + tombstone reuse. */
    TEST_ASSERT(hashmap_remove(&m, 0) == true);
    TEST_ASSERT(hashmap_get(&m, 0) == NULL);
    TEST_ASSERT(hashmap_count(&m) == 1);
    TEST_ASSERT(hashmap_remove(&m, 0) == false);
    TEST_ASSERT(hashmap_put(&m, 0, &a) == 0);
    TEST_ASSERT(hashmap_count(&m) == 2);

    /* Distinct chunk keys for distinct coords (incl. negatives). */
    TEST_ASSERT(hashmap_chunk_key(0, 0) != hashmap_chunk_key(1, 0));
    TEST_ASSERT(hashmap_chunk_key(-1, 0) != hashmap_chunk_key(0, -1));
    TEST_ASSERT(hashmap_chunk_key(-1, -1) != hashmap_chunk_key(1, 1));

    /* Stress: 500 entries stay retrievable (forces resizes). */
    {
        static int vals[500];
        for (int i = 0; i < 500; ++i) {
            vals[i] = i;
            TEST_ASSERT(hashmap_put(&m, (int64_t)i * 7919 - 13, &vals[i]) == 0);
        }
        int ok = 1;
        for (int i = 0; i < 500; ++i) {
            if (hashmap_get(&m, (int64_t)i * 7919 - 13) != &vals[i]) {
                ok = 0;
                break;
            }
        }
        TEST_ASSERT(ok == 1);
    }
    hashmap_free(&m);
    TEST_ASSERT(hashmap_count(NULL) == 0);
    return failures;
}

/* Test: single-chunk noise generation is sane (bedrock/column/water rules).
 *
 * Returns: failure count.
 */
int test_world_gen_chunk(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    w->seed = 1337;
    TEST_ASSERT(world_generate_chunk(w, 0, 0) == 0);
    TEST_ASSERT(world_chunk_count(w) == 1);
    /* Duplicate generates fail without leaking a second chunk. */
    TEST_ASSERT(world_generate_chunk(w, 0, 0) != 0);
    TEST_ASSERT(world_chunk_count(w) == 1);

    /* Bedrock floor + surface agreement with height/surface functions. */
    {
        int wx = 5, wz = 7;
        int h = world_gen_height(1337, wx, wz);
        TEST_ASSERT(h >= 4 && h <= 200);
        TEST_ASSERT(world_get_block(w, wx, 0, wz) == BLOCK_BEDROCK);
        uint16_t surf = world_gen_surface(1337, wx, wz, h);
        TEST_ASSERT(world_get_block(w, wx, h, wz) == surf);
        if (h < WORLD_SEA_LEVEL) {
            TEST_ASSERT(world_get_block(w, wx, WORLD_SEA_LEVEL, wz) == BLOCK_WATER);
        } else {
            /* M5: trees/vegetation may overgrow the surface (documented). */
            uint16_t above = world_get_block(w, wx, h + 1, wz);
            int natural = above == BLOCK_AIR || above == BLOCK_WOOD || above == BLOCK_LEAVES ||
                          above == BLOCK_GRASS_PLANT || above == BLOCK_FLOWER;
            TEST_ASSERT(natural == 1);
        }
    }
    world_destroy(w);
    return failures;
}

/* Test: streamer loads a full square and unloads on teleport.
 *
 * Returns: failure count.
 */
int test_streamer_load_unload(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Streamer s;
    TEST_ASSERT(streamer_init(&s, w, 777, 1) == 0);
    TEST_ASSERT(streamer_init(NULL, w, 777, 1) != 0);

    streamer_update(&s, mmath_vec3(0.0f, 70.0f, 0.0f), 0);
    TEST_ASSERT(world_chunk_count(w) == 9); /* (2*1+1)^2. */
    TEST_ASSERT(streamer_loaded_count(&s) == 9);
    TEST_ASSERT(world_get_chunk(w, -1, -1) != NULL);
    TEST_ASSERT(world_get_chunk(w, 1, 1) != NULL);

    /* Teleport far away: old square unloads, new square loads. */
    streamer_update(&s, mmath_vec3(1000.0f, 70.0f, 1000.0f), 0);
    TEST_ASSERT(world_chunk_count(w) == 9);
    TEST_ASSERT(world_get_chunk(w, 0, 0) == NULL);
    TEST_ASSERT(world_get_chunk(w, 62, 62) != NULL); /* 1000/16 = 62. */

    /* Radius 0 keeps exactly the player chunk. */
    streamer_init(&s, w, 777, 0);
    streamer_update(&s, mmath_vec3(0.0f, 70.0f, 0.0f), 0);
    TEST_ASSERT(world_chunk_count(w) == 1);
    TEST_ASSERT(world_get_chunk(w, 0, 0) != NULL);

    /* Budget limits work done per call: R=2 needs 25, budget 5 leaves gaps
     * that later calls fill. */
    streamer_init(&s, w, 777, 2);
    streamer_update(&s, mmath_vec3(0.0f, 70.0f, 0.0f), 5);
    size_t partial = world_chunk_count(w);
    TEST_ASSERT(partial >= 5 && partial < 25);
    streamer_update(&s, mmath_vec3(0.0f, 70.0f, 0.0f), 0);
    TEST_ASSERT(world_chunk_count(w) == 25);

    world_destroy(w);
    return failures;
}

/* Test: streaming is deterministic for a fixed seed.
 *
 * Returns: failure count.
 */
int test_streamer_determinism(void)
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
    Streamer sa, sb;
    streamer_init(&sa, a, 4242, 1);
    streamer_init(&sb, b, 4242, 1);
    Vec3 p = mmath_vec3(32.0f, 70.0f, -48.0f);
    streamer_update(&sa, p, 0);
    streamer_update(&sb, p, 0);
    TEST_ASSERT(world_chunk_count(a) == world_chunk_count(b));
    int same = 1;
    for (int wx = 32; wx < 64 && same; wx += 3) {
        for (int wz = -48; wz < -16 && same; wz += 3) {
            for (int wy = 60; wy < 72 && same; ++wy) {
                if (world_get_block(a, wx, wy, wz) != world_get_block(b, wx, wy, wz)) {
                    same = 0;
                }
            }
        }
    }
    TEST_ASSERT(same == 1);
    world_destroy(a);
    world_destroy(b);
    return failures;
}
