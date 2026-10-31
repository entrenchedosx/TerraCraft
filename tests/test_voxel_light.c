#include "test_main.h"
#include "render/voxel_light.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

static World *light_test_world(void)
{
    World *w = world_create();
    if (!w) return NULL;
    for (int z = -1; z <= 1; ++z) {
        for (int x = -1; x <= 1; ++x) {
            Chunk *c = chunk_create(x, z);
            if (!c || world_add_chunk(w, c) != 0) {
                chunk_destroy(c); world_destroy(w); return NULL;
            }
            chunk_fill(c, BLOCK_STONE);
            for (int y = 65; y < CHUNK_Y; ++y)
                for (int sz = 0; sz < CHUNK_Z; ++sz)
                    for (int sx = 0; sx < CHUNK_X; ++sx)
                        chunk_set_block(c, sx, y, sz, BLOCK_AIR);
            c->save_dirty = false;
        }
    }
    return w;
}

int test_voxel_light_sky_and_cave(void)
{
    int failures = 0;
    World *w = light_test_world();
    TEST_ASSERT(w != NULL);
    if (!w) return failures + 1;
    /* A sealed room with a roof forty blocks above it must not receive
     * daylight from the old twelve-cell shortcut. */
    for (int y = 20; y <= 60; ++y)
        for (int z = 5; z <= 10; ++z)
            for (int x = 5; x <= 10; ++x)
                world_set_block(w, x, y, z, BLOCK_AIR);
    VoxelLightBake *b = voxel_light_bake(world_get_chunk(w, 0, 0), w);
    TEST_ASSERT(b != NULL);
    if (b) {
        float sky = -1.0f, block = -1.0f;
        voxel_light_sample(b, 8, 65, 8, &sky, &block);
        TEST_ASSERT_FLOAT_EQ(sky, 1.0f, 1e-6f);
        voxel_light_sample(b, 8, 22, 8, &sky, &block);
        TEST_ASSERT_FLOAT_EQ(sky, 0.0f, 1e-6f);
        TEST_ASSERT_FLOAT_EQ(block, 0.0f, 1e-6f);
        voxel_light_free(b);
    }
    /* A shaft carries daylight downward, then it attenuates around the
     * corner into the room instead of leaking through its stone roof. */
    for (int y = 20; y <= 64; ++y) world_set_block(w, 5, y, 8, BLOCK_AIR);
    world_set_block(w, 5, 66, 8, BLOCK_GLASS);
    b = voxel_light_bake(world_get_chunk(w, 0, 0), w);
    TEST_ASSERT(b != NULL);
    if (b) {
        float shaft, room;
        voxel_light_sample(b, 5, 22, 8, &shaft, NULL);
        voxel_light_sample(b, 9, 22, 8, &room, NULL);
        TEST_ASSERT_FLOAT_EQ(shaft, 1.0f, 1e-6f);
        TEST_ASSERT(room > 0.0f && room < shaft);
        voxel_light_free(b);
    }
    world_set_block(w, 5, 64, 8, BLOCK_WATER);
    b = voxel_light_bake(world_get_chunk(w, 0, 0), w);
    TEST_ASSERT(b != NULL);
    if (b) {
        float wet;
        voxel_light_sample(b, 5, 22, 8, &wet, NULL);
        TEST_ASSERT(wet > 0.0f && wet < 1.0f);
        voxel_light_free(b);
    }
    world_destroy(w);
    return failures;
}

int test_voxel_light_torch_and_removal(void)
{
    int failures = 0;
    World *w = light_test_world();
    TEST_ASSERT(w != NULL);
    if (!w) return failures + 1;
    for (int y = 20; y <= 22; ++y)
        for (int x = 1; x <= 29; ++x)
            for (int z = 7; z <= 9; ++z)
                world_set_block(w, x, y, z, x == 16 ? BLOCK_STONE : BLOCK_AIR);
    world_set_block(w, 14, 21, 8, BLOCK_TORCH);
    VoxelLightBake *b = voxel_light_bake(world_get_chunk(w, 0, 0), w);
    TEST_ASSERT(b != NULL);
    if (b) {
        float near_light, far_light, blocked, sky;
        voxel_light_sample(b, 14, 21, 8, &sky, &near_light);
        voxel_light_sample(b, 8, 21, 8, NULL, &far_light);
        voxel_light_sample(b, 17, 21, 8, NULL, &blocked);
        TEST_ASSERT_FLOAT_EQ(sky, 0.0f, 1e-6f);
        TEST_ASSERT_FLOAT_EQ(near_light, 14.0f / 15.0f, 1e-6f);
        TEST_ASSERT(far_light > 0.0f && far_light < near_light);
        TEST_ASSERT_FLOAT_EQ(blocked, 0.0f, 1e-6f);
        voxel_light_free(b);
    }
    world_set_block(w, 14, 21, 8, BLOCK_AIR);
    b = voxel_light_bake(world_get_chunk(w, 0, 0), w);
    TEST_ASSERT(b != NULL);
    if (b) {
        float emitted;
        voxel_light_sample(b, 14, 21, 8, NULL, &emitted);
        TEST_ASSERT_FLOAT_EQ(emitted, 0.0f, 1e-6f);
        voxel_light_free(b);
    }
    world_destroy(w);
    return failures;
}

int test_voxel_light_seams_and_invalidation(void)
{
    int failures = 0;
    World *w = light_test_world();
    TEST_ASSERT(w != NULL);
    if (!w) return failures + 1;
    for (int x = -12; x <= 28; ++x) world_set_block(w, x, 21, 8, BLOCK_AIR);
    for (int y = 21; y <= 64; ++y) world_set_block(w, -5, y, 8, BLOCK_AIR);
    Chunk *a = world_get_chunk(w, -1, 0), *c = world_get_chunk(w, 0, 0);
    for (size_t i = 0; i < w->count; ++i) w->chunks[i]->dirty = false;
    world_set_block(w, 5, 21, 8, BLOCK_TORCH);
    TEST_ASSERT(a->dirty); /* An interior edit still reaches adjacent chunks. */
    VoxelLightBake *left = voxel_light_bake(a, w), *right = voxel_light_bake(c, w);
    TEST_ASSERT(left != NULL && right != NULL);
    if (left && right) {
        float ls, lb, rs, rb;
        voxel_light_sample(left, 16, 21, 8, &ls, &lb);
        voxel_light_sample(right, 0, 21, 8, &rs, &rb);
        TEST_ASSERT_FLOAT_EQ(ls, rs, 1e-6f);
        TEST_ASSERT(rs > 0.0f);
        TEST_ASSERT_FLOAT_EQ(lb, rb, 1e-6f);
        TEST_ASSERT(rb > 0.0f);
    }
    voxel_light_free(left); voxel_light_free(right);
    world_destroy(w);
    return failures;
}
