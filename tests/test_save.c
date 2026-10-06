#include "test_main.h"

#include "core/path.h"
#include "game/player.h"
#include "game/session.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"
#include "world/world_gen.h"
#include "world/world_meta.h"
#include "world/world_save.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Scratch root for save tests (created + removed by the tests). */
#define SAVE_TMP "test_tmp_m5"

/* Remove a test world tree created by these tests (best effort). */
static void save_tmp_cleanup(const char *dir)
{
    char chunks[1024];
    char names[256][64];
    size_t count = 0;
    if (path_join(chunks, sizeof(chunks), dir, "chunks") == 0) {
        if (path_list_files(chunks, names, 256, &count) == 0) {
            for (size_t i = 0; i < count; ++i) {
                char full[1024];
                if (path_join(full, sizeof(full), chunks, names[i]) == 0) {
                    path_remove_file(full);
                }
            }
        }
        path_remove_dir(chunks);
    }
    {
        char meta[1024];
        if (path_join(meta, sizeof(meta), dir, WORLD_META_FILE) == 0) {
            path_remove_file(meta);
        }
    }
    path_remove_dir(dir);
}

/* Test: metadata write/read round trip preserves every field.
 *
 * Returns: failure count.
 */
int test_meta_roundtrip(void)
{
    int failures = 0;
    char dir[256];
    path_join(dir, sizeof(dir), SAVE_TMP, "meta_rt");
    save_tmp_cleanup(dir);

    WorldMeta m;
    world_meta_defaults(&m);
    memcpy(m.name, "Test World", 11);
    m.seed = 123456LL;
    m.mode = WORLD_MODE_CREATIVE;
    m.terrain_version = 2;
    m.px = 1.5f;
    m.py = 70.25f;
    m.pz = -3.75f;
    m.yaw = 1.1f;
    m.pitch = -0.2f;
    m.day = 0.42f;
    m.has_player = true;
    TEST_ASSERT(world_meta_write(dir, &m) == 0);

    WorldMeta back;
    memset(&back, 0, sizeof(back));
    TEST_ASSERT(world_meta_read(dir, &back) == 0);
    TEST_ASSERT(back.version == WORLD_META_VERSION);
    TEST_ASSERT(strcmp(back.name, "Test World") == 0);
    TEST_ASSERT(back.seed == 123456LL);
    TEST_ASSERT(back.mode == WORLD_MODE_CREATIVE);
    TEST_ASSERT(back.terrain_version == 2);
    TEST_ASSERT_FLOAT_EQ(back.px, 1.5f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(back.py, 70.25f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(back.pz, -3.75f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(back.yaw, 1.1f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(back.pitch, -0.2f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(back.day, 0.42f, 1e-4f);
    TEST_ASSERT(back.has_player == true);
    TEST_ASSERT(back.last_played > 0);

    save_tmp_cleanup(dir);
    path_remove_dir(SAVE_TMP);
    return failures;
}

/* Test: corrupt/future/missing metadata fails safely (never crashes).
 *
 * Returns: failure count.
 */
int test_meta_corrupt(void)
{
    int failures = 0;
    WorldMeta m;
    /* Missing required fields. */
    TEST_ASSERT(world_meta_parse("world_name=Nope\n", &m) != 0);
    TEST_ASSERT(world_meta_parse("", &m) != 0);
    TEST_ASSERT(world_meta_parse(NULL, &m) != 0);
    TEST_ASSERT(world_meta_parse("format_version=1\nworld_name=X\nseed=5\n", NULL) != 0);
    /* Future version refused. */
    TEST_ASSERT(world_meta_parse("format_version=99\nworld_name=X\nseed=5\n", &m) != 0);
    TEST_ASSERT(world_meta_parse("format_version=0\nworld_name=X\nseed=5\n", &m) != 0);
    /* Bad seed value. */
    TEST_ASSERT(world_meta_parse("format_version=1\nworld_name=X\nseed=abc\n", &m) != 0);
    /* Minimal valid record still parses (tolerant reader). */
    TEST_ASSERT(world_meta_parse("# comment\nformat_version=1\nworld_name=Y\nseed=-7\njunkline\n", &m) == 0);
    TEST_ASSERT(strcmp(m.name, "Y") == 0);
    TEST_ASSERT(m.seed == -7LL);
    TEST_ASSERT(m.terrain_version == 1); /* Missing key pins old terrain. */
    TEST_ASSERT(world_meta_parse("format_version=1\nworld_name=Future Terrain\nseed=4\nterrain_version=3\n",
                                 &m) != 0);
    /* Missing directory fails. */
    TEST_ASSERT(world_meta_read("test_tmp_m5_no_such_dir_xyz", &m) != 0);
    TEST_ASSERT(world_meta_read(NULL, &m) != 0);
    TEST_ASSERT(world_meta_write(NULL, &m) != 0);
    return failures;
}

/* Fill a chunk with a position-dependent pattern (covers all IDs). */
static void save_pattern_fill(Chunk *c)
{
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < 256; ++y) {
                uint16_t id = (uint16_t)((x * 131 + y * 17 + z * 7) % (int)BLOCK_COUNT);
                chunk_set_block(c, x, y, z, id);
            }
        }
    }
    c->save_dirty = true;
}

/* Test: chunk save/load round trip, including negative coordinates.
 *
 * Returns: failure count.
 */
int test_chunk_roundtrip(void)
{
    int failures = 0;
    char dir[256];
    path_join(dir, sizeof(dir), SAVE_TMP, "chunk_rt");
    save_tmp_cleanup(dir);

    const int coords[3][2] = {{0, 0}, {-3, -7}, {123, -456}};
    for (int t = 0; t < 3; ++t) {
        Chunk *c = chunk_create(coords[t][0], coords[t][1]);
        TEST_ASSERT(c != NULL);
        if (c == NULL) {
            continue;
        }
        save_pattern_fill(c);
        TEST_ASSERT(world_save_write_chunk(dir, c) == 0);
        chunk_destroy(c);

        Chunk *back = chunk_create(coords[t][0], coords[t][1]);
        TEST_ASSERT(back != NULL);
        if (back == NULL) {
            continue;
        }
        TEST_ASSERT(world_save_read_chunk(dir, back) == 0);
        int same = 1;
        for (int x = 0; x < 16 && same; ++x) {
            for (int z = 0; z < 16 && same; ++z) {
                for (int y = 0; y < 256 && same; ++y) {
                    uint16_t want = (uint16_t)((x * 131 + y * 17 + z * 7) % (int)BLOCK_COUNT);
                    if (chunk_get_block(back, x, y, z) != want) {
                        same = 0;
                    }
                }
            }
        }
        TEST_ASSERT(same == 1);
        TEST_ASSERT(back->dirty == true);      /* Needs a remesh after load. */
        TEST_ASSERT(back->save_dirty == false); /* On-disk state is current. */
        chunk_destroy(back);
    }
    save_tmp_cleanup(dir);
    path_remove_dir(SAVE_TMP);
    return failures;
}

/* Version 2 preserves flowing-water IDs, while a version 1 payload still
 * migrates unsupported post-v1 IDs to the established safe fallback. */
int test_chunk_water_version_compat(void)
{
    int failures = 0;
    char dir[256], chunks[512], file[512];
    path_join(dir, sizeof(dir), SAVE_TMP, "water_compat");
    save_tmp_cleanup(dir);
    Chunk *src = chunk_create(0, 0);
    Chunk *dst = chunk_create(0, 0);
    TEST_ASSERT(src != NULL && dst != NULL);
    if (src == NULL || dst == NULL) {
        chunk_destroy(src);
        chunk_destroy(dst);
        return failures + 1;
    }
    chunk_set_block(src, 1, 10, 1, BLOCK_WATER);
    chunk_set_block(src, 2, 10, 1, BLOCK_WATER_FLOW_1);
    TEST_ASSERT(world_save_write_chunk(dir, src) == 0);
    TEST_ASSERT(path_join(chunks, sizeof(chunks), dir, "chunks") == 0);
    TEST_ASSERT(path_join(file, sizeof(file), chunks, "c_0_0.bin") == 0);
    TEST_ASSERT(world_save_read_chunk(dir, dst) == 0);
    TEST_ASSERT(chunk_get_block(dst, 1, 10, 1) == BLOCK_WATER);
    TEST_ASSERT(chunk_get_block(dst, 2, 10, 1) == BLOCK_WATER_FLOW_1);

    FILE *f = fopen(file, "r+b");
    TEST_ASSERT(f != NULL);
    if (f != NULL) {
        TEST_ASSERT(fseek(f, 4, SEEK_SET) == 0);
        TEST_ASSERT(fputc(1, f) != EOF && fputc(0, f) != EOF);
        long flow_offset = 16L + (long)chunk_index(2, 10, 1) * 2L;
        TEST_ASSERT(fseek(f, flow_offset, SEEK_SET) == 0);
        TEST_ASSERT(fputc((int)(BLOCK_WATER_FLOW_1 & 0xffu), f) != EOF);
        TEST_ASSERT(fputc((int)(BLOCK_WATER_FLOW_1 >> 8), f) != EOF);
        TEST_ASSERT(fclose(f) == 0);
    }
    chunk_fill(dst, BLOCK_AIR);
    TEST_ASSERT(world_save_read_chunk(dir, dst) == 0);
    TEST_ASSERT(chunk_get_block(dst, 1, 10, 1) == BLOCK_WATER);
    TEST_ASSERT(chunk_get_block(dst, 2, 10, 1) == BLOCK_STONE);
    chunk_destroy(dst);
    chunk_destroy(src);
    save_tmp_cleanup(dir);
    path_remove_dir(SAVE_TMP);
    return failures;
}

/* Corrupt a chunk file in place (flip one byte at offset). */
static void save_corrupt_byte(const char *dir, int cx, int cz, size_t off, unsigned char val)
{
    char path[1024];
    char chunks[1024];
    char leaf[64];
    path_join(chunks, sizeof(chunks), dir, "chunks");
    snprintf(leaf, sizeof(leaf), "c_%d_%d.bin", cx, cz);
    path_join(path, sizeof(path), chunks, leaf);
    FILE *f = fopen(path, "r+b");
    if (f == NULL) {
        return;
    }
    fseek(f, (long)off, SEEK_SET);
    fwrite(&val, 1, 1, f);
    fclose(f);
}

/* Test: corrupt/truncated/mismatched chunk files are rejected safely.
 *
 * Returns: failure count.
 */
int test_chunk_corrupt(void)
{
    int failures = 0;
    char dir[256];
    path_join(dir, sizeof(dir), SAVE_TMP, "chunk_bad");
    save_tmp_cleanup(dir);

    Chunk *c = chunk_create(2, -5);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    save_pattern_fill(c);
    TEST_ASSERT(world_save_write_chunk(dir, c) == 0);

    /* Bad magic. */
    save_corrupt_byte(dir, 2, -5, 0, 'X');
    Chunk *back = chunk_create(2, -5);
    TEST_ASSERT(back != NULL);
    TEST_ASSERT(world_save_read_chunk(dir, back) != 0);
    /* Blocks untouched on failure (still AIR from init). */
    TEST_ASSERT(chunk_get_block(back, 0, 0, 0) == BLOCK_AIR);

    /* Restore, then bad version. */
    TEST_ASSERT(world_save_write_chunk(dir, c) == 0);
    save_corrupt_byte(dir, 2, -5, 4, 0xFF);
    save_corrupt_byte(dir, 2, -5, 5, 0xFF);
    TEST_ASSERT(world_save_read_chunk(dir, back) != 0);

    /* Restore, then coordinate mismatch (read (2,-5) file into (9,9)). */
    TEST_ASSERT(world_save_write_chunk(dir, c) == 0);
    Chunk *other = chunk_create(9, 9);
    TEST_ASSERT(other != NULL);
    /* NOTE: read targets other->coords, whose file does not exist. */
    TEST_ASSERT(world_save_read_chunk(dir, other) != 0);
    chunk_destroy(other);

    /* Truncated file. */
    {
        char path[1024];
        char chunks[1024];
        char leaf[64];
        path_join(chunks, sizeof(chunks), dir, "chunks");
        snprintf(leaf, sizeof(leaf), "c_%d_%d.bin", 2, -5);
        path_join(path, sizeof(path), chunks, leaf);
        FILE *f = fopen(path, "wb");
        TEST_ASSERT(f != NULL);
        if (f != NULL) {
            fwrite("MNC1", 1, 4, f);
            fclose(f);
        }
    }
    TEST_ASSERT(world_save_read_chunk(dir, back) != 0);

    /* Missing file reads as failure (caller generates fresh). */
    Chunk *missing = chunk_create(77, 78);
    TEST_ASSERT(missing != NULL);
    TEST_ASSERT(world_save_read_chunk(dir, missing) != 0);
    chunk_destroy(missing);

    /* NULL safety. */
    TEST_ASSERT(world_save_write_chunk(NULL, c) != 0);
    TEST_ASSERT(world_save_write_chunk(dir, NULL) != 0);
    TEST_ASSERT(world_save_read_chunk(NULL, back) != 0);
    TEST_ASSERT(world_save_read_chunk(dir, NULL) != 0);

    chunk_destroy(c);
    chunk_destroy(back);
    save_tmp_cleanup(dir);
    path_remove_dir(SAVE_TMP);
    return failures;
}

/* Test: name sanitization blocks traversal and junk, stays usable.
 *
 * Returns: failure count.
 */
int test_sanitize(void)
{
    int failures = 0;
    char out[64];
    TEST_ASSERT(path_sanitize_name(out, sizeof(out), "Test World") == 0);
    TEST_ASSERT(strcmp(out, "Test_World") == 0);
    TEST_ASSERT(path_sanitize_name(out, sizeof(out), "../evil") == 0);
    TEST_ASSERT(strcmp(out, "evil") == 0);
    TEST_ASSERT(path_sanitize_name(out, sizeof(out), "..") == 0);
    TEST_ASSERT(strcmp(out, "World") == 0);
    TEST_ASSERT(path_sanitize_name(out, sizeof(out), "") == 0);
    TEST_ASSERT(strcmp(out, "World") == 0);
    TEST_ASSERT(path_sanitize_name(out, sizeof(out), NULL) == 0);
    TEST_ASSERT(strcmp(out, "World") == 0);
    TEST_ASSERT(path_sanitize_name(out, sizeof(out), "a/b\\c:d*e?f\"g<h>i|j") == 0);
    TEST_ASSERT(strchr(out, '/') == NULL && strchr(out, '\\') == NULL);
    TEST_ASSERT(strchr(out, ':') == NULL && strchr(out, '*') == NULL);
    /* No output may ever contain a separator or be dotty. */
    TEST_ASSERT(path_sanitize_name(out, sizeof(out), " ... ") == 0);
    TEST_ASSERT(strcmp(out, "World") == 0);
    TEST_ASSERT(path_sanitize_name(NULL, sizeof(out), "x") != 0);
    TEST_ASSERT(path_sanitize_name(out, 0, "x") != 0);
    return failures;
}

/* Test: world delete removes the tree and refuses foreign layouts.
 *
 * Returns: failure count.
 */
int test_world_delete(void)
{
    int failures = 0;
    char dir[256];
    path_join(dir, sizeof(dir), SAVE_TMP, "del_me");
    save_tmp_cleanup(dir);

    WorldMeta m;
    world_meta_defaults(&m);
    memcpy(m.name, "Delete Me", 10);
    TEST_ASSERT(world_meta_write(dir, &m) == 0);
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c != NULL) {
        c->save_dirty = true;
        TEST_ASSERT(world_save_write_chunk(dir, c) == 0);
        chunk_destroy(c);
    }
    TEST_ASSERT(path_is_dir(dir));
    TEST_ASSERT(world_save_delete(dir) == 0);
    TEST_ASSERT(!path_is_dir(dir));

    /* Deleting nothing fails (no silent success lie). */
    TEST_ASSERT(world_save_delete(dir) != 0);
    TEST_ASSERT(world_save_delete(NULL) != 0);
    TEST_ASSERT(world_save_delete("") != 0);
    path_remove_dir(SAVE_TMP);
    return failures;
}

/* Test: full session persistence round trip — create, generate, edit,
 * save-all, destroy, reload: edits persist, untouched terrain matches a
 * fresh reference, meta (seed/pos/day/mode) survives.
 *
 * Returns: failure count.
 */
int test_session_persist(void)
{
    int failures = 0;
    char dir[512];
    TEST_ASSERT(session_create_world(SAVE_TMP, "Persist Me", "123456", 0, dir) == 0);
    TEST_ASSERT(path_is_dir(dir));

    /* Session A: generate two chunks, edit a few blocks, save-all. */
    World *a = world_create();
    TEST_ASSERT(a != NULL);
    if (a == NULL) {
        return failures + 1;
    }
    a->seed = 123456L;
    memcpy(a->name, "Persist_Me", 11);
    a->mode = 0; /* WORLD_MODE_SURVIVAL. */
    world_set_save_dir(a, dir);
    TEST_ASSERT(world_generate_chunk(a, 0, 0) == 0);
    TEST_ASSERT(world_generate_chunk(a, 1, 0) == 0);
    /* Break two blocks (edits get save_dirty via the same path as gameplay
     * would use through interaction.c; set the flag directly here). */
    Chunk *ca = world_get_chunk(a, 0, 0);
    TEST_ASSERT(ca != NULL);
    int ex[2] = {5, 20};
    int ez[2] = {5, 9};
    int ey[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        int h = world_gen_height(123456L, ex[i], ez[i]);
        ey[i] = h; /* Surface block. */
        int ccx = ex[i] >= 0 ? ex[i] / 16 : -((-ex[i] + 15) / 16);
        Chunk *ce = world_get_chunk(a, ccx, 0);
        TEST_ASSERT(ce != NULL);
        if (ce != NULL) {
            chunk_set_block(ce, ex[i] - ccx * 16, h, ez[i], BLOCK_AIR);
            ce->save_dirty = true;
        }
    }
    Player pl;
    player_init(&pl);
    pl.pos = mmath_vec3(8.5f, 70.0f, 8.5f);
    pl.yaw = 0.5f;
    pl.pitch = -0.1f;
    Vec3 spawn = mmath_vec3(8.5f, 70.0f, 8.5f);
    TEST_ASSERT(world_save_all(dir, a, &pl, spawn, true, 0.42f) == 0);
    TEST_ASSERT(ca->save_dirty == false);
    world_destroy(a);

    /* Session B: reload from disk. Edits present, meta intact. */
    World *b = world_create();
    TEST_ASSERT(b != NULL);
    if (b == NULL) {
        save_tmp_cleanup(dir);
        path_remove_dir(SAVE_TMP);
        return failures + 1;
    }
    b->seed = 123456L;
    world_set_save_dir(b, dir);
    TEST_ASSERT(world_generate_chunk(b, 0, 0) == 0);
    TEST_ASSERT(world_generate_chunk(b, 1, 0) == 0);
    for (int i = 0; i < 2; ++i) {
        TEST_ASSERT(world_get_block(b, ex[i], ey[i], ez[i]) == BLOCK_AIR);
    }
    WorldMeta m;
    TEST_ASSERT(world_meta_read(dir, &m) == 0);
    TEST_ASSERT(m.seed == 123456LL);
    TEST_ASSERT(strcmp(m.name, "Persist_Me") == 0);
    TEST_ASSERT_FLOAT_EQ(m.px, 8.5f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(m.day, 0.42f, 1e-4f);
    world_destroy(b);

    /* Reference: fresh generation without a save dir matches B everywhere
     * except the two edited cells. */
    World *ref = world_create();
    TEST_ASSERT(ref != NULL);
    if (ref != NULL) {
        ref->seed = 123456L;
        TEST_ASSERT(world_generate_chunk(ref, 0, 0) == 0);
        TEST_ASSERT(world_generate_chunk(ref, 1, 0) == 0);
        World *b2 = world_create();
        TEST_ASSERT(b2 != NULL);
        if (b2 != NULL) {
            b2->seed = 123456L;
            world_set_save_dir(b2, dir);
            TEST_ASSERT(world_generate_chunk(b2, 0, 0) == 0);
            TEST_ASSERT(world_generate_chunk(b2, 1, 0) == 0);
            int same = 1;
            for (int x = 0; x < 32 && same; ++x) {
                for (int z = 0; z < 16 && same; ++z) {
                    for (int y = 0; y < 100 && same; ++y) {
                        bool edited = (x == ex[0] && z == ez[0] && y == ey[0]) ||
                                      (x == ex[1] && z == ez[1] && y == ey[1]);
                        uint16_t want = world_get_block(ref, x, y, z);
                        uint16_t got = world_get_block(b2, x, y, z);
                        if (edited) {
                            if (got != BLOCK_AIR) {
                                same = 0;
                            }
                        } else if (got != want) {
                            same = 0;
                        }
                    }
                }
            }
            TEST_ASSERT(same == 1);
            world_destroy(b2);
        }
        world_destroy(ref);
    }

    /* session_create_world rejects bad args safely. */
    char junk[512];
    TEST_ASSERT(session_create_world(NULL, "x", "1", 0, junk) != 0);
    TEST_ASSERT(session_create_world(SAVE_TMP, "x", "1", 0, NULL) != 0);

    save_tmp_cleanup(dir);
    path_remove_dir(SAVE_TMP);
    return failures;
}
