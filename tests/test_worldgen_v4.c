#include "test_main.h"
#include "world/worldgen_v4.h"
#include "world/world_gen.h"
#include "world/world.h"
#include "world/chunk.h"
#include "world/block.h"
#include "world/biome.h"
#include "world/world_meta.h"
#include "game/player.h"
#include "game/item.h"
#include "render/texture_atlas.h"
#include "world/world_save.h"
#include "core/path.h"
#include "core/noise.h"
#include <string.h>
#include <math.h>
_Static_assert(TILE_LAVA == 72, "Lava shader expects atlas column 8, row 4");

int test_worldgen_v4_determinism(void)
{
    int failures = 0;
    World *a = world_create(), *b = world_create();
    TEST_ASSERT(a && b);
    if (!a || !b) {
        world_destroy(a);
        world_destroy(b);
        return failures;
    }
    a->seed = b->seed = -123456;
    a->terrain_version = b->terrain_version = 4;
    const int coords[4][2] = {{-1, -1}, {0, -1}, {-1, 0}, {0, 0}};
    for (int i = 0; i < 4; ++i)
        TEST_ASSERT(world_generate_chunk(a, coords[i][0], coords[i][1]) == 0);
    for (int i = 3; i >= 0; --i)
        TEST_ASSERT(world_generate_chunk(b, coords[i][0], coords[i][1]) == 0);
    for (int i = 0; i < 4; ++i) {
        Chunk *ca = world_get_chunk(a, coords[i][0], coords[i][1]),
              *cb = world_get_chunk(b, coords[i][0], coords[i][1]);
        TEST_ASSERT(ca && cb);
        if (!ca || !cb)
            continue;
        TEST_ASSERT(memcmp(ca->blocks, cb->blocks, sizeof(ca->blocks)) == 0);
        TEST_ASSERT(!ca->save_dirty);
        for (int z = 0; z < 16; z += 3)
            for (int x = 0; x < 16; x += 3) {
                int wx = ca->cx * 16 + x, wz = ca->cz * 16 + z, h = worldgen_height(a->seed, wx, wz);
                TEST_ASSERT(block_is_solid(chunk_get_block(ca, x, h, z)));
                for (int y = 4; y < h - 5; y += 3) {
                    uint16_t expected = worldgen_base_block(a->seed, wx, y, wz), got = chunk_get_block(ca, x, y, z);
                    /* Materials may replace rock, but air and fluids are exact. */
                    if (expected == BLOCK_AIR || expected == BLOCK_WATER || expected == BLOCK_LAVA)
                        TEST_ASSERT(got == expected);
                    else
                        TEST_ASSERT(block_is_solid(got));
                }
            }
    }
    for (int i = 0; i < 4; ++i) {
        Chunk *ca = world_get_chunk(a, coords[i][0], coords[i][1]);
        if (!ca)
            continue;
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                int actual = 255;
                while (actual > 0) {
                    uint16_t id = chunk_get_block(ca, x, actual, z);
                    if (block_is_solid(id) && id != BLOCK_WOOD && id != BLOCK_LEAVES)
                        break;
                    --actual;
                }
                TEST_ASSERT(actual == worldgen_height(a->seed, ca->cx * 16 + x, ca->cz * 16 + z));
            }
    }
    world_destroy(a);
    world_destroy(b);
    return failures;
}
int test_worldgen_v4_fields(void)
{
    int failures = 0;
    int biomes[BIOME_COUNT] = {0}, cheese = 0, spaghetti = 0, noodle = 0, wet = 0, dry = 0, barrier = 0;
    float low = 256, high = 0, max_delta = 0;
    for (int z = -2048; z <= 2048; z += 32)
        for (int x = -2048; x <= 2048; x += 32) {
            GenClimate c = worldgen_climate(123456, x, z), next = worldgen_climate(123456, x + 1, z);
            TEST_ASSERT(isfinite(c.height) && c.height >= 8 && c.height <= 225);
            ++biomes[c.biome];
            if (c.height < low)
                low = c.height;
            if (c.height > high)
                high = c.height;
            float delta = fabsf(c.height - next.height);
            if (delta > max_delta)
                max_delta = delta;
            for (int y = 8; y < 64; y += 8) {
                GenDensity d = worldgen_density(123456, x, y, z);
                if (d.cheese < 0)
                    ++cheese;
                if (d.spaghetti < 0)
                    ++spaghetti;
                if (d.noodle < 0)
                    ++noodle;
                GenAquifer a = worldgen_aquifer(123456, x, y, z);
                if (a.wet)
                    ++wet;
                else
                    ++dry;
                if (a.barrier)
                    ++barrier;
            }
        }
    TEST_ASSERT(low < 40 && high > 150 && max_delta < 3.5f);
    for (int i = 0; i < BIOME_COUNT; ++i)
        TEST_ASSERT(biomes[i] > 0);
    TEST_ASSERT(cheese > 0 && spaghetti > 0 && noodle > 0 && wet > 0 && dry > 0 && barrier > 0);
    return failures;
}
int test_worldgen_v4_rescue_save(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w);
    if (!w)
        return failures;
    w->seed = 123456;
    w->terrain_version = 4;
    strcpy(w->name, "Rescue Test");
    uint32_t hash = noise_hash2(0, 0, (uint32_t)w->seed ^ 0x5FA1u);
    int cx = (int)(hash % 17) - 8, cz = (int)((hash >> 8) % 17) - 8;
    for (int z = cz - 1; z <= cz + 1; ++z)
        for (int x = cx - 1; x <= cx + 1; ++x) {
            Chunk *c = chunk_create(x, z);
            TEST_ASSERT(c);
            if (!c)
                continue;
            for (int i = 0; i < CHUNK_VOLUME; ++i)
                c->blocks[i] = BLOCK_STONE;
            TEST_ASSERT(world_add_chunk(w, c) == 0);
        }
    Vec3 spawn;
    TEST_ASSERT(worldgen_find_spawn_with_budget(w, &spawn, 0) == 1);
    TEST_ASSERT(worldgen_spawn_valid(w, (int)spawn.x, (int)spawn.z, NULL, NULL));
    Chunk *c = world_get_chunk(w, cx, cz);
    TEST_ASSERT(c && c->save_dirty);
    const char *dir = "test_tmp_worldgen_v4";
    (void)world_save_delete(dir);
    TEST_ASSERT(path_mkdir_p(dir) == 0);
    Player player;
    player_init(&player);
    player.pos = spawn;
    TEST_ASSERT(world_save_all(dir, w, &player, spawn, true, 0) == 0);
    WorldMeta meta;
    TEST_ASSERT(world_meta_read(dir, &meta) == 0);
    TEST_ASSERT(meta.terrain_version == 4 && meta.has_spawn);
    World *back = world_create();
    TEST_ASSERT(back);
    if (back) {
        back->seed = w->seed;
        back->terrain_version = 4;
        world_set_save_dir(back, dir);
        TEST_ASSERT(world_generate_chunk(back, cx, cz) == 0);
        Chunk *loaded = world_get_chunk(back, cx, cz);
        TEST_ASSERT(loaded && memcmp(loaded->blocks, c->blocks, sizeof(c->blocks)) == 0);
        world_destroy(back);
    }
    world_destroy(w);
    TEST_ASSERT(world_save_delete(dir) == 0);
    return failures;
}
int test_worldgen_v4_spawn(void)
{
    int failures = 0;
    for (int i = 0; i < 100; ++i) {
        World *w = world_create();
        TEST_ASSERT(w);
        if (!w)
            return failures;
        w->seed = (long)((unsigned)i * 2654435761u);
        w->terrain_version = 4;
        Vec3 spawn = mmath_vec3(0, 0, 0);
        TEST_ASSERT(worldgen_find_spawn(w, &spawn) == 0);
        TEST_ASSERT(worldgen_spawn_valid(w, (int)floorf(spawn.x), (int)floorf(spawn.z), NULL, NULL));
        Vec3 repeat;
        TEST_ASSERT(worldgen_find_spawn(w, &repeat) == 0);
        TEST_ASSERT_FLOAT_EQ(spawn.x, repeat.x, 0);
        TEST_ASSERT_FLOAT_EQ(spawn.y, repeat.y, 0);
        TEST_ASSERT_FLOAT_EQ(spawn.z, repeat.z, 0);
        Player p;
        player_init(&p);
        p.pos = spawn;
        p.render_pos = spawn;
        PlayerInput input = {0};
        for (int tick = 0; tick < 60; ++tick)
            player_update(&p, &input, w, 1.0f / 60);
        TEST_ASSERT(p.grounded && fabsf(p.pos.y - spawn.y) < .02f);
        input.fwd = 1;
        for (int tick = 0; tick < 15; ++tick)
            player_update(&p, &input, w, 1.0f / 60);
        TEST_ASSERT(fabsf(p.pos.x - spawn.x) + fabsf(p.pos.z - spawn.z) > .1f);
        TEST_ASSERT(w->count <= 9);
        world_destroy(w);
    }
    return failures;
}
int test_worldgen_v4_materials(void)
{
    int failures = 0;
    for (int id = BLOCK_GRAVEL; id < BLOCK_LAVA; ++id) {
        TEST_ASSERT(block_is_solid((uint16_t)id));
        TEST_ASSERT(item_get_info((uint16_t)id)->id == id);
        TEST_ASSERT(block_tile_for_face((uint16_t)id, 3) >= 61);
    }
    TEST_ASSERT(!block_is_solid(BLOCK_LAVA));
    WorldMeta meta;
    TEST_ASSERT(world_meta_parse("format_version=1\nworld_name=Density\nseed=8\nterrain_version=4\n", &meta) == 0);
    TEST_ASSERT(meta.terrain_version == 4);
    return failures;
}
int test_worldgen_v4_ore_depth(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w);
    if (!w)
        return failures;
    w->seed = 123456;
    w->terrain_version = 4;
    size_t low[BLOCK_COUNT] = {0}, high[BLOCK_COUNT] = {0}, total[BLOCK_COUNT] = {0};
    int mountain_x = 0, mountain_z = 0;
    bool found = false;
    for (int z = -2048; z < 2048 && !found; z += 64)
        for (int x = -2048; x < 2048; x += 64) {
            GenClimate c = worldgen_climate(w->seed, x, z);
            if (c.biome == BIOME_MOUNTAINS && c.height > 140) {
                mountain_x = x / 16;
                mountain_z = z / 16;
                found = true;
                break;
            }
        }
    TEST_ASSERT(found);
    for (int i = 0; i < 32; ++i) {
        int cx = i < 16 ? i - 8 : mountain_x + (i - 16) % 4;
        int cz = i < 16 ? 0 : mountain_z + (i - 16) / 4;
        TEST_ASSERT(world_generate_chunk(w, cx, cz) == 0);
        Chunk *c = world_get_chunk(w, cx, cz);
        if (!c)
            continue;
        for (int y = 0; y < 256; ++y)
            for (int p = 0; p < 256; ++p) {
                uint16_t id = c->blocks[y * 256 + p];
                ++total[id];
                if (y < 32)
                    ++low[id];
                else
                    ++high[id];
            }
        world_remove_chunk(w, cx, cz);
    }
    const uint16_t ids[] = {BLOCK_COAL_ORE,   BLOCK_IRON_ORE,  BLOCK_GOLD_ORE,     BLOCK_DIAMOND_ORE,
                            BLOCK_COPPER_ORE, BLOCK_LAPIS_ORE, BLOCK_REDSTONE_ORE, BLOCK_EMERALD_ORE};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        TEST_ASSERT(total[ids[i]] > 0);
    TEST_ASSERT(low[BLOCK_DIAMOND_ORE] > high[BLOCK_DIAMOND_ORE] * 2);
    TEST_ASSERT(high[BLOCK_COAL_ORE] > low[BLOCK_COAL_ORE] * 10);
    TEST_ASSERT(total[BLOCK_DIAMOND_ORE] < total[BLOCK_IRON_ORE]);
    TEST_ASSERT(total[BLOCK_EMERALD_ORE] < total[BLOCK_IRON_ORE]);
    world_destroy(w);
    return failures;
}
