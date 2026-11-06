#include "test_main.h"
#include "core/path.h"
#include "game/player.h"
#include "world/biome.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"
#include "world/world_gen.h"
#include "world/world_meta.h"
#include "world/world_save.h"
#include "world/worldgen_v4.h"
#include "world/worldgen_v5.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* The player simulates at the vanilla 20 TPS: one test step is one game tick. */
#undef PLAYER_STEP_DT
#define PLAYER_STEP_DT 0.05f

/* Hash bytes explicitly so the compatibility fixture is independent of
 * host endianness and struct padding. These hashes predate profile 5. */
static uint64_t block_hash(const Chunk *chunk)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int i = 0; i < 16 * 256 * 16; ++i) { /* y 0..255: the frozen profiles never reach above it */
        hash = (hash ^ (chunk->blocks[i] & 255u)) * UINT64_C(1099511628211);
        hash = (hash ^ (chunk->blocks[i] >> 8)) * UINT64_C(1099511628211);
    }
    return hash;
}

int test_worldgen_v5_determinism(void)
{
    int failures = 0;
    World *forward = world_create(), *reverse = world_create();
    TEST_ASSERT(forward && reverse);
    if (!forward || !reverse) {
        world_destroy(forward);
        world_destroy(reverse);
        return failures;
    }
    forward->seed = reverse->seed = -7654321;
    forward->terrain_version = reverse->terrain_version = 5;
    const int coords[][2] = {{-1, -1}, {0, -1}, {-1, 0}, {0, 0}, {31, -17}};
    const int count = (int)(sizeof(coords) / sizeof(coords[0]));
    for (int i = 0; i < count; ++i)
        TEST_ASSERT(world_generate_chunk(forward, coords[i][0], coords[i][1]) == 0);
    for (int i = count - 1; i >= 0; --i)
        TEST_ASSERT(world_generate_chunk(reverse, coords[i][0], coords[i][1]) == 0);
    for (int i = 0; i < count; ++i) {
        Chunk *a = world_get_chunk(forward, coords[i][0], coords[i][1]);
        Chunk *b = world_get_chunk(reverse, coords[i][0], coords[i][1]);
        TEST_ASSERT(a && b);
        if (!a || !b)
            continue;
        TEST_ASSERT(memcmp(a->blocks, b->blocks, sizeof(a->blocks)) == 0);
        TEST_ASSERT(!a->save_dirty && !b->save_dirty);
        bool heights_match = true;
        for (int z = 0; z < CHUNK_Z; ++z)
            for (int x = 0; x < CHUNK_X; ++x) {
                int top = CHUNK_Y - 1;
                while (top > 0) {
                    uint16_t id = chunk_get_block(a, x, top, z);
                    if (block_is_solid(id) && id != BLOCK_WOOD && id != BLOCK_LEAVES)
                        break;
                    --top;
                }
                if (top != worldgen5_height(forward->seed, a->cx * CHUNK_X + x, a->cz * CHUNK_Z + z))
                    heights_match = false;
            }
        TEST_ASSERT(heights_match);
    }

    /* Compare the cached fill against scalar sampling throughout an entire
     * negative-coordinate chunk. Rock may become surface material or ore;
     * air/fluid and solid occupancy must agree exactly. No trees here. */
    Chunk *raw = chunk_create(-2, -1);
    TEST_ASSERT(raw);
    if (raw) {
        worldgen5_fill(raw, forward->seed);
        bool volume_matches = true;
        for (int z = 0; z < CHUNK_Z; ++z)
            for (int x = 0; x < CHUNK_X; ++x)
                for (int y = 0; y < CHUNK_Y; ++y) {
                    uint16_t expected =
                        worldgen5_base_block(forward->seed, raw->cx * CHUNK_X + x, y, raw->cz * CHUNK_Z + z);
                    uint16_t actual = chunk_get_block(raw, x, y, z);
                    if (block_is_solid(expected) ? !block_is_solid(actual) : expected != actual)
                        volume_matches = false;
                }
        TEST_ASSERT(volume_matches);
        TEST_ASSERT(worldgen5_base_block(forward->seed, -1, -1, -1) == BLOCK_AIR);
        TEST_ASSERT(worldgen5_base_block(forward->seed, -1, CHUNK_Y, -1) == BLOCK_AIR);
        chunk_destroy(raw);
    }
    world_destroy(forward);
    world_destroy(reverse);
    return failures;
}

int test_worldgen_v5_fields(void)
{
    int failures = 0;
    int biomes[BIOME_COUNT] = {0};
    int cheese = 0, spaghetti = 0, noodle = 0, wet = 0, dry = 0, barrier = 0;
    float low = 256, high = 0, max_step = 0;
    bool valid_fields = true;
    const long seeds[] = {123456, -7654321, 42};
    for (size_t s = 0; s < sizeof(seeds) / sizeof(seeds[0]); ++s)
        for (int z = -2048; z <= 2048; z += 96)
            for (int x = -2048; x <= 2048; x += 96) {
                GenClimate c = worldgen5_climate(seeds[s], x, z);
                GenClimate east = worldgen5_climate(seeds[s], x + 1, z);
                GenClimate south = worldgen5_climate(seeds[s], x, z + 1);
                if (!isfinite(c.height) || c.height < 8 || c.height > 225 || !isfinite(c.continentalness) ||
                    !isfinite(c.erosion) || !isfinite(c.temperature) || !isfinite(c.humidity) ||
                    !isfinite(c.weirdness) || !isfinite(c.mountain) || !isfinite(c.river) || c.mountain < 0 ||
                    c.mountain > 1 || c.river < 0 || c.river > 1 || c.biome < 0 || c.biome >= BIOME_COUNT)
                    valid_fields = false;
                else
                    ++biomes[c.biome];
                low = fminf(low, c.height);
                high = fmaxf(high, c.height);
                max_step = fmaxf(max_step, fmaxf(fabsf(c.height - east.height), fabsf(c.height - south.height)));
                for (int y = 8; y < 64; y += 8) {
                    GenDensity d = worldgen5_density(seeds[s], x, y, z);
                    GenAquifer a = worldgen5_aquifer(seeds[s], x, y, z);
                    if (!isfinite(d.terrain) || !isfinite(d.cheese) || !isfinite(d.spaghetti) || !isfinite(d.noodle) ||
                        !isfinite(d.final_density))
                        valid_fields = false;
                    if (d.terrain > 0 && y < c.height - 12) {
                        cheese += d.cheese < 0;
                        spaghetti += d.spaghetti < 0;
                        noodle += d.noodle < 0;
                        wet += a.wet;
                        dry += !a.wet;
                        barrier += a.barrier;
                    }
                }
            }
    TEST_ASSERT(valid_fields);
    TEST_ASSERT(low < WORLD_SEA_LEVEL - 4 && high > 130);
    TEST_ASSERT(max_step < 8);
    for (int i = 0; i < BIOME_COUNT; ++i)
        TEST_ASSERT(biomes[i] > 0);
    TEST_ASSERT(cheese > 0 && spaghetti > 0 && noodle > 0);
    TEST_ASSERT(wet > 0 && dry > 0 && barrier > 0);

    /* A chunk boundary must not introduce a special jump, including at
     * negative multiples of 16 where C's integer division differs from floor. */
    for (int x = -64; x <= 64; x += CHUNK_X)
        for (int z = -64; z <= 64; z += CHUNK_Z) {
            GenClimate a = worldgen5_climate(123456, x - 1, z);
            GenClimate b = worldgen5_climate(123456, x, z);
            TEST_ASSERT(fabsf(a.height - b.height) < 8);
            TEST_ASSERT(fabsf(a.continentalness - b.continentalness) < .05f);
        }
    return failures;
}

int test_worldgen_v5_spawn(void)
{
    int failures = 0;
    for (int i = 0; i < 30; ++i) {
        World *first = world_create(), *repeat = world_create();
        TEST_ASSERT(first && repeat);
        if (!first || !repeat) {
            world_destroy(first);
            world_destroy(repeat);
            return failures;
        }
        first->seed = repeat->seed = (long)((uint32_t)i * UINT32_C(2654435761));
        first->terrain_version = repeat->terrain_version = 5;
        Vec3 a = mmath_vec3(0, 0, 0), b = a;
        TEST_ASSERT(worldgen_find_spawn(first, &a) == 0);
        TEST_ASSERT(worldgen_find_spawn(repeat, &b) == 0);
        TEST_ASSERT_FLOAT_EQ(a.x, b.x, 0);
        TEST_ASSERT_FLOAT_EQ(a.y, b.y, 0);
        TEST_ASSERT_FLOAT_EQ(a.z, b.z, 0);
        TEST_ASSERT(worldgen_spawn_valid(first, (int)floorf(a.x), (int)floorf(a.z), NULL, NULL));
        TEST_ASSERT(a.y > WORLD_SEA_LEVEL);
        Player player;
        player_init(&player);
        player.pos = player.render_pos = a;
        PlayerInput input = {0};
        for (int tick = 0; tick < 60; ++tick)
            player_update(&player, &input, first, 1.0f / 60);
        TEST_ASSERT(player.grounded && fabsf(player.pos.y - a.y) < .02f);
        input.fwd = 1;
        for (int tick = 0; tick < 15; ++tick)
            player_update(&player, &input, first, 1.0f / 60);
        TEST_ASSERT(fabsf(player.pos.x - a.x) + fabsf(player.pos.z - a.z) > .1f);
        TEST_ASSERT(first->count <= 9 && repeat->count <= 9);
        world_destroy(first);
        world_destroy(repeat);
    }
    return failures;
}

int test_worldgen_v5_save_compatibility(void)
{
    int failures = 0;
    const char *dir = "test_tmp_worldgen_v5_compatibility";
    for (int profile = 4; profile <= 5; ++profile) {
        (void)world_save_delete(dir);
        TEST_ASSERT(path_mkdir_p(dir) == 0);
        World *original = world_create(), *loaded = world_create();
        TEST_ASSERT(original && loaded);
        if (!original || !loaded) {
            world_destroy(original);
            world_destroy(loaded);
            (void)world_save_delete(dir);
            return failures;
        }
        original->seed = 123456;
        original->terrain_version = profile;
        snprintf(original->name, sizeof(original->name), "Versioned terrain test");
        TEST_ASSERT(world_generate_chunk(original, -1, -1) == 0);
        world_set_block(original, -8, 220, -8, BLOCK_GLASS);
        Player player;
        player_init(&player);
        player.pos = mmath_vec3(-7.5f, 221, -7.5f);
        TEST_ASSERT(world_save_all(dir, original, &player, player.pos, true, .83f) == 0);
        WorldMeta metadata;
        TEST_ASSERT(world_meta_read(dir, &metadata) == 0);
        TEST_ASSERT(metadata.terrain_version == profile && metadata.has_spawn && metadata.has_player);
        loaded->seed = (long)metadata.seed;
        loaded->terrain_version = metadata.terrain_version;
        world_set_save_dir(loaded, dir);
        TEST_ASSERT(world_generate_chunk(loaded, -1, -1) == 0);
        Chunk *a = world_get_chunk(original, -1, -1), *b = world_get_chunk(loaded, -1, -1);
        TEST_ASSERT(a && b);
        if (a && b)
            TEST_ASSERT(memcmp(a->blocks, b->blocks, sizeof(a->blocks)) == 0);
        TEST_ASSERT(world_get_block(loaded, -8, 220, -8) == BLOCK_GLASS);
        /* Untouched terrain after reload must use the saved profile. */
        TEST_ASSERT(world_generate_chunk(original, 1, -2) == 0);
        TEST_ASSERT(world_generate_chunk(loaded, 1, -2) == 0);
        a = world_get_chunk(original, 1, -2);
        b = world_get_chunk(loaded, 1, -2);
        TEST_ASSERT(a && b);
        if (a && b)
            TEST_ASSERT(memcmp(a->blocks, b->blocks, sizeof(a->blocks)) == 0);
        world_destroy(original);
        world_destroy(loaded);
        TEST_ASSERT(world_save_delete(dir) == 0);
    }
    return failures;
}

int test_worldgen_v4_frozen_hashes(void)
{
    int failures = 0;
    const long seeds[] = {123456, -7654321};
    const int coords[][2] = {{-1, -1}, {0, 0}, {31, -17}, {-63, 42}};
    const uint64_t golden[2][4] = {{UINT64_C(16920655413020762143), UINT64_C(12810449121684251749),
                                    UINT64_C(13562305742983305363), UINT64_C(12892963968350158328)},
                                   {UINT64_C(6952644569921309907), UINT64_C(6817188050835607282),
                                    UINT64_C(5322904237629876834), UINT64_C(9382868513966923658)}};
    for (size_t s = 0; s < sizeof(seeds) / sizeof(seeds[0]); ++s) {
        World *world = world_create();
        TEST_ASSERT(world);
        if (!world)
            return failures;
        world->seed = seeds[s];
        world->terrain_version = 4;
        for (int i = 0; i < 4; ++i) {
            TEST_ASSERT(world_generate_chunk(world, coords[i][0], coords[i][1]) == 0);
            Chunk *chunk = world_get_chunk(world, coords[i][0], coords[i][1]);
            TEST_ASSERT(chunk);
            if (chunk)
                TEST_ASSERT(block_hash(chunk) == golden[s][i]);
        }
        world_destroy(world);
    }
    return failures;
}
