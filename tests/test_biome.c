#include "test_main.h"

#include "world/biome.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"
#include "world/world_gen.h"

/* Test: biome selection is deterministic and well-formed.
 *
 * Returns: failure count.
 */
int test_biome_determinism(void)
{
    int failures = 0;
    int b1 = biome_at(1234L, 10, -20, 70);
    int b2 = biome_at(1234L, 10, -20, 70);
    TEST_ASSERT(b1 == b2);
    TEST_ASSERT(b1 >= 0 && b1 < (int)BIOME_COUNT);
    TEST_ASSERT(biome_at(1234L, 10, -20, 70) == biome_at(1234L, 10, -20, 70));
    /* Climate fields are deterministic too. */
    TEST_ASSERT_FLOAT_EQ(biome_temperature(99L, 3, 4), biome_temperature(99L, 3, 4), 1e-6f);
    TEST_ASSERT_FLOAT_EQ(biome_humidity(99L, 3, 4), biome_humidity(99L, 3, 4), 1e-6f);
    TEST_ASSERT_FLOAT_EQ(biome_mountains(99L, 3, 4), biome_mountains(99L, 3, 4), 1e-6f);
    /* Ranges. */
    float t = biome_temperature(99L, 3, 4);
    float hu = biome_humidity(99L, 3, 4);
    float mo = biome_mountains(99L, 3, 4);
    TEST_ASSERT(t >= -1.0f && t <= 1.0f);
    TEST_ASSERT(hu >= -1.0f && hu <= 1.0f);
    TEST_ASSERT(mo >= 0.0f && mo <= 1.0f);
    /* Names never NULL. */
    for (int b = 0; b < (int)BIOME_COUNT; ++b) {
        TEST_ASSERT(biome_name(b) != NULL);
    }
    TEST_ASSERT(biome_name(-1) != NULL);
    TEST_ASSERT(biome_name(99) != NULL);
    /* Densities sane. */
    TEST_ASSERT(biome_tree_density(BIOME_FOREST) > biome_tree_density(BIOME_PLAINS));
    TEST_ASSERT_FLOAT_EQ(biome_tree_density(BIOME_OCEAN), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(biome_tree_density(BIOME_DESERT), 0.0f, 1e-6f);
    return failures;
}

/* Test: all seven biomes occur in a large scan (variety, no dead biome).
 *
 * Returns: failure count.
 */
int test_biome_coverage(void)
{
    int failures = 0;
    int seen[BIOME_COUNT] = {0, 0, 0, 0, 0, 0, 0};
    for (int wx = -512; wx < 512; wx += 8) {
        for (int wz = -512; wz < 512; wz += 8) {
            int h = world_gen_height(20240L, wx, wz);
            int b = biome_at(20240L, wx, wz, h);
            if (b >= 0 && b < (int)BIOME_COUNT) {
                seen[b] = 1;
            }
        }
    }
    for (int b = 0; b < (int)BIOME_COUNT; ++b) {
        TEST_ASSERT(seen[b] == 1);
    }
    return failures;
}

/* Test: versioned terrain is deterministic and has both sea basins and
 * elevated ridges without changing the frozen v1 field. */
int test_world_gen_terrain_v2(void)
{
    int failures = 0;
    int min_h = CHUNK_Y;
    int max_h = 0;
    int low = 0;
    int high = 0;
    int changed = 0;
    for (int x = -768; x <= 768; x += 16) {
        for (int z = -768; z <= 768; z += 16) {
            int old_h = world_gen_height(481516L, x, z);
            int h = world_gen_height_version(481516L, x, z, 2);
            TEST_ASSERT(h == world_gen_height_version(481516L, x, z, 2));
            TEST_ASSERT(old_h == world_gen_height_version(481516L, x, z, 1));
            TEST_ASSERT(h >= 4 && h <= 200);
            if (h < min_h) {
                min_h = h;
            }
            if (h > max_h) {
                max_h = h;
            }
            low += h < WORLD_SEA_LEVEL;
            high += h > 100;
            changed += h != old_h;
        }
    }
    TEST_ASSERT(min_h < WORLD_SEA_LEVEL);
    TEST_ASSERT(max_h > 100);
    TEST_ASSERT(low > 0 && high > 0 && changed > 100);
    return failures;
}

/* Test: trees are deterministic, bounded, and order-independent.
 *
 * Returns: failure count.
 */
int test_tree_determinism(void)
{
    int failures = 0;
    int th1 = 0;
    int th2 = 0;
    bool t1 = world_gen_tree(555L, 17, -42, &th1);
    bool t2 = world_gen_tree(555L, 17, -42, &th2);
    TEST_ASSERT(t1 == t2);
    if (t1) {
        TEST_ASSERT(th1 == th2);
        TEST_ASSERT(th1 >= 4 && th1 <= 6);
    }
    TEST_ASSERT(world_gen_tree(555L, 17, -42, NULL) == t1);

    /* Some tree exists in a 256x256 area (density makes absence absurd). */
    int found = 0;
    for (int x = 0; x < 256 && !found; x += 2) {
        for (int z = 0; z < 256 && !found; z += 2) {
            if (world_gen_tree(555L, x, z, NULL)) {
                found = 1;
            }
        }
    }
    TEST_ASSERT(found == 1);

    /* Cross-chunk order independence: A-then-B vs B-then-A identical. */
    World *w1 = world_create();
    World *w2 = world_create();
    TEST_ASSERT(w1 != NULL && w2 != NULL);
    if (w1 == NULL || w2 == NULL) {
        world_destroy(w1);
        world_destroy(w2);
        return failures + 1;
    }
    w1->seed = 555L;
    w2->seed = 555L;
    TEST_ASSERT(world_generate_chunk(w1, 0, 0) == 0);
    TEST_ASSERT(world_generate_chunk(w1, 1, 0) == 0);
    TEST_ASSERT(world_generate_chunk(w2, 1, 0) == 0);
    TEST_ASSERT(world_generate_chunk(w2, 0, 0) == 0);
    int same = 1;
    for (int x = 0; x < 32 && same; ++x) {
        for (int z = 0; z < 16 && same; ++z) {
            for (int y = 0; y < 256 && same; ++y) {
                if (world_get_block(w1, x, y, z) != world_get_block(w2, x, y, z)) {
                    same = 0;
                }
            }
        }
    }
    TEST_ASSERT(same == 1);
    world_destroy(w1);
    world_destroy(w2);
    return failures;
}

/* Test: vegetation lottery is deterministic and plausible.
 *
 * Returns: failure count.
 */
int test_vegetation(void)
{
    int failures = 0;
    TEST_ASSERT(world_gen_vegetation(77L, 10, 10, 70) == world_gen_vegetation(77L, 10, 10, 70));
    /* Below sea level: never vegetation. */
    TEST_ASSERT(world_gen_vegetation(77L, 10, 10, 60) == BLOCK_AIR);
    /* Some vegetation exists across a wide area. */
    int plants = 0;
    int flowers = 0;
    for (int x = 0; x < 256; x += 2) {
        for (int z = 0; z < 256; z += 2) {
            int h = world_gen_height(77L, x, z);
            uint16_t v = world_gen_vegetation(77L, x, z, h);
            if (v == BLOCK_GRASS_PLANT) {
                ++plants;
            } else if (v == BLOCK_FLOWER) {
                ++flowers;
            } else {
                TEST_ASSERT(v == BLOCK_AIR);
            }
        }
    }
    TEST_ASSERT(plants > 0);
    TEST_ASSERT(flowers > 0);
    return failures;
}

/* Test: caves carve some (not all) underground rock, deterministically.
 *
 * Returns: failure count.
 */
int test_caves(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    w->seed = 31337L;
    TEST_ASSERT(world_generate_chunk(w, 0, 0) == 0);
    TEST_ASSERT(world_generate_chunk(w, 1, 0) == 0);
    TEST_ASSERT(world_generate_chunk(w, 2, 0) == 0);
    TEST_ASSERT(world_generate_chunk(w, 0, 1) == 0);
    TEST_ASSERT(world_generate_chunk(w, 1, 1) == 0);
    TEST_ASSERT(world_generate_chunk(w, 2, 1) == 0);
    int air = 0;
    int solid = 0;
    for (int x = 0; x < 48; ++x) {
        for (int z = 0; z < 32; ++z) {
            int h = world_gen_height(31337L, x, z);
            for (int y = 5; y < h - 4 && y < 60; ++y) {
                uint16_t b = world_get_block(w, x, y, z);
                if (b == BLOCK_AIR) {
                    ++air;
                } else {
                    ++solid;
                }
            }
        }
    }
    TEST_ASSERT(air > 0);   /* Caves exist. */
    TEST_ASSERT(solid > 0); /* Terrain not hollowed out. */
    {
        float frac = (float)air / (float)(air + solid);
        TEST_ASSERT(frac > 0.005f && frac < 0.30f);
    }
    /* Bedrock floor survives carving. */
    TEST_ASSERT(world_get_block(w, 8, 0, 8) == BLOCK_BEDROCK);
    world_destroy(w);
    return failures;
}

/* Test: every ore type occurs in its depth band across a few chunks.
 *
 * Returns: failure count.
 */
int test_ores(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    w->seed = 8080L;
    for (int cx = 0; cx < 3; ++cx) {
        for (int cz = 0; cz < 3; ++cz) {
            TEST_ASSERT(world_generate_chunk(w, cx, cz) == 0);
        }
    }
    int coal = 0, iron = 0, gold = 0, diamond = 0;
    for (int x = 0; x < 48; ++x) {
        for (int z = 0; z < 48; ++z) {
            for (int y = 1; y <= 32; ++y) {
                uint16_t b = world_get_block(w, x, y, z);
                if (b == BLOCK_COAL_ORE) {
                    ++coal;
                } else if (b == BLOCK_IRON_ORE) {
                    ++iron;
                } else if (b == BLOCK_GOLD_ORE) {
                    ++gold;
                } else if (b == BLOCK_DIAMOND_ORE) {
                    ++diamond;
                }
            }
        }
    }
    TEST_ASSERT(coal > 0 && iron > 0 && gold > 0 && diamond > 0);
    world_destroy(w);
    return failures;
}
