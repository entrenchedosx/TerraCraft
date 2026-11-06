#include "test_main.h"

#include "core/app.h"
#include "core/path.h"
#include "core/time.h"
#include "game/session.h"
#include "world/biome.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/streamer.h"
#include "world/entity_save.h"
#include "world/vgen.h"
#include "world/water.h"
#include "world/world.h"
#include "world/world_gen.h"
#include "world/world_meta.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The generator reads the vanilla data pack from a local directory. Machines without it (CI) skip the
 * generation tests, exactly like the worldgen parity stages. */
static bool pack_root(char *root, int cap)
{
    if (!vgen_find_datapack(root, cap)) {
        printf("  [skip] vanilla data pack not installed\n");
        return false;
    }
    return true;
}

int test_vgen_block_map(void)
{
    int failures = 0;
    TEST_ASSERT(vgen_map_block_name("minecraft:stone") == BLOCK_STONE);
    TEST_ASSERT(vgen_map_block_name("minecraft:deepslate") == BLOCK_DEEPSLATE);
    TEST_ASSERT(vgen_map_block_name("minecraft:grass_block") == BLOCK_GRASS);
    TEST_ASSERT(vgen_map_block_name("minecraft:oak_log") == BLOCK_WOOD);
    TEST_ASSERT(vgen_map_block_name("minecraft:spruce_log") == BLOCK_SPRUCE_LOG);
    TEST_ASSERT(vgen_map_block_name("minecraft:mangrove_log") == BLOCK_WOOD);
    TEST_ASSERT(vgen_map_block_name("minecraft:podzol") == BLOCK_PODZOL);
    TEST_ASSERT(vgen_map_block_name("minecraft:blue_ice") == BLOCK_PACKED_ICE);
    TEST_ASSERT(vgen_map_block_name("minecraft:orange_terracotta") == BLOCK_ORANGE_TERRACOTTA);
    TEST_ASSERT(vgen_map_block_name("minecraft:magenta_terracotta") == BLOCK_TERRACOTTA);
    TEST_ASSERT(vgen_map_block_name("minecraft:birch_leaves") == BLOCK_BIRCH_LEAVES);
    TEST_ASSERT(vgen_map_block_name("minecraft:cherry_leaves") == BLOCK_LEAVES);
    TEST_ASSERT(vgen_map_block_name("minecraft:deepslate_diamond_ore") == BLOCK_DIAMOND_ORE);
    TEST_ASSERT(vgen_map_block_name("minecraft:short_grass") == BLOCK_GRASS_PLANT);
    TEST_ASSERT(vgen_map_block_name("minecraft:poppy") == BLOCK_FLOWER);
    TEST_ASSERT(vgen_map_block_name("minecraft:water") == BLOCK_WATER);
    TEST_ASSERT(vgen_map_block_name("minecraft:lava") == BLOCK_LAVA);
    TEST_ASSERT(vgen_map_block_name("minecraft:bedrock") == BLOCK_BEDROCK);
    TEST_ASSERT(vgen_map_block_name("minecraft:not_a_real_block") == BLOCK_AIR);
    TEST_ASSERT(vgen_biome_class("minecraft:ocean") == BIOME_OCEAN);
    TEST_ASSERT(vgen_biome_class("minecraft:deep_cold_ocean") == BIOME_OCEAN);
    TEST_ASSERT(vgen_biome_class("minecraft:snowy_plains") == BIOME_SNOW);
    TEST_ASSERT(vgen_biome_class("minecraft:desert") == BIOME_DESERT);
    TEST_ASSERT(vgen_biome_class("minecraft:jagged_peaks") == BIOME_MOUNTAINS);
    TEST_ASSERT(vgen_biome_class("minecraft:dark_forest") == BIOME_FOREST);
    TEST_ASSERT(vgen_biome_class("minecraft:plains") == BIOME_PLAINS);
    return failures;
}

/* A chunk must come out the same whatever was generated before it. */
int test_vgen_order_independent(void)
{
    int failures = 0;
    char root[1024];
    if (!pack_root(root, (int)sizeof(root))) {
        return 0;
    }
    VGen *a = vgen_create(root, 987654321);
    VGen *b = vgen_create(root, 987654321);
    TEST_ASSERT(a != NULL && b != NULL);
    if (a == NULL || b == NULL) {
        vgen_destroy(a);
        vgen_destroy(b);
        return failures;
    }
    Chunk *a0 = chunk_create(0, 0), *a1 = chunk_create(1, 0);
    Chunk *b0 = chunk_create(0, 0), *b1 = chunk_create(1, 0), *bx = chunk_create(3, -2);
    TEST_ASSERT(a0 && a1 && b0 && b1 && bx);
    if (a0 && a1 && b0 && b1 && bx) {
        TEST_ASSERT(vgen_build_chunk(a, a0, 0, 0));
        TEST_ASSERT(vgen_build_chunk(a, a1, 1, 0));
        TEST_ASSERT(vgen_build_chunk(b, bx, 3, -2));
        TEST_ASSERT(vgen_build_chunk(b, b1, 1, 0));
        TEST_ASSERT(vgen_build_chunk(b, b0, 0, 0));
        TEST_ASSERT(memcmp(a0->blocks, b0->blocks, sizeof(a0->blocks)) == 0);
        TEST_ASSERT(memcmp(a1->blocks, b1->blocks, sizeof(a1->blocks)) == 0);
        TEST_ASSERT(memcmp(a0->biome, b0->biome, sizeof(a0->biome)) == 0);

        /* Sanity of the shape: a bedrock floor at chunk y 0, air at the top, solid ground in between. */
        int solid = 0;
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                TEST_ASSERT(chunk_get_block(a0, x, 0, z) == BLOCK_BEDROCK);
                TEST_ASSERT(chunk_get_block(a0, x, CHUNK_Y - 1, z) == BLOCK_AIR);
                for (int y = 0; y < CHUNK_Y; ++y) {
                    solid += block_is_solid(chunk_get_block(a0, x, y, z)) ? 1 : 0;
                }
            }
        }
        TEST_ASSERT(solid > 20000);
        TEST_ASSERT(!a0->save_dirty && a0->dirty);
    }
    chunk_destroy(a0);
    chunk_destroy(a1);
    chunk_destroy(b0);
    chunk_destroy(b1);
    chunk_destroy(bx);
    vgen_destroy(a);
    vgen_destroy(b);
    return failures;
}

/* The worker thread must hand back exactly what a direct build produces. */
int test_vgen_async_matches_sync(void)
{
    int failures = 0;
    char root[1024];
    if (!pack_root(root, (int)sizeof(root))) {
        return 0;
    }
    VGen *g = vgen_create(root, 24680);
    TEST_ASSERT(g != NULL);
    if (g == NULL) {
        return failures;
    }
    TEST_ASSERT(vgen_async_start(g));
    vgen_async_set_center(g, 5, 5, 4);
    static const int want[3][2] = {{5, 5}, {6, 5}, {5, 6}};
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT(vgen_async_request(g, want[i][0], want[i][1]));
        TEST_ASSERT(!vgen_async_request(g, want[i][0], want[i][1])); /* already pending */
    }
    int got = 0;
    double deadline = time_now_seconds() + 60.0;
    while (got < 3 && time_now_seconds() < deadline) {
        Chunk *c = vgen_async_poll(g);
        if (c == NULL) {
            continue;
        }
        Chunk *ref = chunk_create(c->cx, c->cz);
        TEST_ASSERT(ref != NULL && vgen_build_chunk(g, ref, c->cx, c->cz));
        if (ref != NULL) {
            TEST_ASSERT(memcmp(c->blocks, ref->blocks, sizeof(c->blocks)) == 0);
        }
        chunk_destroy(ref);
        chunk_destroy(c);
        ++got;
    }
    TEST_ASSERT(got == 3);
    TEST_ASSERT(vgen_async_pending(g) == 0);
    vgen_destroy(g);
    return failures;
}

/* A vanilla world streams in through both the budgeted (async) and the unlimited (sync) paths. */
int test_vgen_streamer(void)
{
    int failures = 0;
    char root[1024];
    if (!pack_root(root, (int)sizeof(root))) {
        return 0;
    }
    for (int mode = 0; mode < 2; ++mode) {
        World *w = world_create();
        TEST_ASSERT(w != NULL);
        if (w == NULL) {
            return failures;
        }
        w->seed = 4242;
        w->terrain_version = 6;
        Streamer s;
        TEST_ASSERT(streamer_init(&s, w, 4242, 1) == 0);
        Vec3 pos = mmath_vec3(8.0f, 140.0f, 8.0f);
        if (mode == 0) {
            streamer_update(&s, pos, 0); /* unlimited: everything is built before it returns */
            TEST_ASSERT(world_chunk_count(w) == 9);
        } else {
            double deadline = time_now_seconds() + 60.0;
            while (world_chunk_count(w) < 9 && time_now_seconds() < deadline) {
                streamer_update(&s, pos, 4);
            }
            TEST_ASSERT(world_chunk_count(w) == 9);
        }
        const Chunk *c = world_get_chunk(w, 0, 0);
        TEST_ASSERT(c != NULL && chunk_get_block(c, 3, 0, 3) == BLOCK_BEDROCK);
        world_destroy(w);
    }
    return failures;
}

/* Generated oceans and rivers must settle: the bounded water simulation may not flood or choke on them. */
int test_vgen_water_settles(void)
{
    int failures = 0;
    char root[1024];
    if (!pack_root(root, (int)sizeof(root))) {
        return 0;
    }
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures;
    }
    w->seed = 777;
    w->terrain_version = 6;
    for (int cz = -3; cz <= 3; ++cz) {
        for (int cx = -3; cx <= 3; ++cx) {
            TEST_ASSERT(world_generate_chunk(w, cx, cz) == 0);
        }
    }
    long before = 0;
    for (size_t i = 0; i < w->count; ++i) {
        for (int k = 0; k < CHUNK_VOLUME; ++k) {
            before += block_is_water(w->chunks[i]->blocks[k]) ? 1 : 0;
        }
    }
    size_t peak = world_water_pending(w);
    double t0 = time_now_seconds();
    for (int tick = 0; tick < 1200; ++tick) {
        world_water_tick(w, 0.05f);
        size_t pending = world_water_pending(w);
        if (pending > peak) {
            peak = pending;
        }
    }
    double ms = (time_now_seconds() - t0) * 1000.0;
    long after = 0;
    for (size_t i = 0; i < w->count; ++i) {
        for (int k = 0; k < CHUNK_VOLUME; ++k) {
            after += block_is_water(w->chunks[i]->blocks[k]) ? 1 : 0;
        }
    }
    printf("  water: %ld -> %ld cells, peak queue %zu, %zu left, %.0f ms for 60 simulated seconds\n", before, after,
           peak, world_water_pending(w), ms);
    TEST_ASSERT(peak < WORLD_WATER_QUEUE_CAP);
    TEST_ASSERT(after > before / 2 && after < before + before / 2 + 1000);
    world_destroy(w);
    return failures;
}

/* Seed 12345 is open ocean around the origin: the spawn search must skip it cheaply and land on dry ground. */
int test_vgen_spawn(void)
{
    int failures = 0;
    char root[1024];
    if (!pack_root(root, (int)sizeof(root))) {
        return 0;
    }
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures;
    }
    w->seed = 12345;
    w->terrain_version = 6;
    Vec3 spawn;
    double t0 = time_now_seconds();
    TEST_ASSERT(world_find_spawn_vanilla(w, &spawn) == 0);
    printf("  spawn (%.1f, %.1f, %.1f) found in %.0f ms with %zu chunks generated\n", spawn.x, spawn.y, spawn.z,
           (time_now_seconds() - t0) * 1000.0, world_chunk_count(w));
    TEST_ASSERT(spawn.y >= 128.0f); /* above the sea surface (Minecraft y 63 -> 127) */
    int bx = (int)floorf(spawn.x), by = (int)floorf(spawn.y), bz = (int)floorf(spawn.z);
    TEST_ASSERT(block_is_solid(world_get_block(w, bx, by - 1, bz)));
    TEST_ASSERT(world_get_block(w, bx, by, bz) == BLOCK_AIR && world_get_block(w, bx, by + 1, bz) == BLOCK_AIR);
    world_destroy(w);
    return failures;
}

#define VGEN_TMP "test_tmp_vgen"

static void vgen_tmp_cleanup(const char *dir)
{
    char chunks[1024], names[256][64], full[1024], file[1024];
    size_t count = 0;
    if (path_join(chunks, sizeof(chunks), dir, "chunks") == 0) {
        if (path_list_files(chunks, names, 256, &count) == 0) {
            for (size_t i = 0; i < count; ++i) {
                if (path_join(full, sizeof(full), chunks, names[i]) == 0) {
                    path_remove_file(full);
                }
            }
        }
        path_remove_dir(chunks);
    }
    if (path_join(file, sizeof(file), dir, WORLD_META_FILE) == 0) {
        path_remove_file(file);
    }
    if (path_join(file, sizeof(file), dir, ENTITY_SAVE_FILE) == 0) {
        path_remove_file(file);
    }
    path_remove_dir(dir);
}

static bool vgen_load_around_player(AppContext *app, int want)
{
    double deadline = time_now_seconds() + 120.0;
    while ((int)world_chunk_count(app->world) < want && time_now_seconds() < deadline) {
        streamer_update(&app->streamer, app->player.pos, MINEC_LOAD_BUDGET);
    }
    return (int)world_chunk_count(app->world) >= want;
}

/* A new world defaults to the vanilla generator, loads in through the session layer, and an edit survives a
 * save and reopen (the 384-high chunk file) without the surrounding terrain changing. */
int test_vgen_session_roundtrip(void)
{
    int failures = 0;
    char root[1024];
    if (!pack_root(root, (int)sizeof(root))) {
        return 0;
    }
    vgen_tmp_cleanup(VGEN_TMP "/Vanilla");
    char dir[512];
    TEST_ASSERT(session_create_world(VGEN_TMP, "Vanilla", "hello", 0, dir) == 0);
    WorldMeta meta;
    TEST_ASSERT(world_meta_read(dir, &meta) == 0);
    TEST_ASSERT(meta.terrain_version == 6);
    TEST_ASSERT(meta.seed == 99162322); /* Java "hello".hashCode() */

    AppContext *app = (AppContext *)calloc(1, sizeof(AppContext));
    TEST_ASSERT(app != NULL);
    if (app == NULL) {
        return failures;
    }
    TEST_ASSERT(session_open_world(app, dir) == 0);
    TEST_ASSERT(app->world != NULL && app->world->terrain_version == 6);
    TEST_ASSERT(app->load_total > 0 && app->load_total <= 49);
    TEST_ASSERT(vgen_load_around_player(app, app->load_total));
    Vec3 feet = app->player.pos;
    int bx = (int)floorf(feet.x), by = (int)floorf(feet.y), bz = (int)floorf(feet.z);
    uint16_t ground = world_get_block(app->world, bx, by - 1, bz);
    TEST_ASSERT(block_is_solid(ground));
    TEST_ASSERT(world_set_block(app->world, bx, by - 1, bz, BLOCK_GLASS));
    uint16_t beside = world_get_block(app->world, bx + 3, by - 1, bz + 2);
    session_close_world(app, true);

    TEST_ASSERT(session_open_world(app, dir) == 0);
    TEST_ASSERT(vgen_load_around_player(app, app->load_total));
    TEST_ASSERT(world_get_block(app->world, bx, by - 1, bz) == BLOCK_GLASS);
    TEST_ASSERT(world_get_block(app->world, bx + 3, by - 1, bz + 2) == beside);
    session_close_world(app, false);
    free(app);
    vgen_tmp_cleanup(dir);
    path_remove_dir(VGEN_TMP);
    return failures;
}
