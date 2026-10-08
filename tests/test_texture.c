#include "test_main.h"

#include "core/path.h"
#include "render/texture_atlas.h"
#include "world/block.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test: atlas constants and UV rectangles.
 *
 * Returns: failure count.
 */
int test_atlas_layout(void)
{
    int failures = 0;
    /* NOTE: compared via variables to avoid MSVC C4127 under /W4. */
    int atlas_size = ATLAS_SIZE;
    int atlas_tiles = ATLAS_TILES;
    int tile_px = ATLAS_TILE_PX;
    TEST_ASSERT(atlas_size == 256);
    TEST_ASSERT(atlas_tiles == 16);
    TEST_ASSERT(tile_px == 16);

    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    texture_atlas_tile_uv(TILE_GRASS_TOP, &u0, &v0, &u1, &v1);
    TEST_ASSERT(u0 >= 0.0f && u0 < u1 && u1 <= 1.0f);
    TEST_ASSERT(v0 >= 0.0f && v0 < v1 && v1 <= 1.0f);
    /* Tile 0 occupies the first column of the bottom row. */
    TEST_ASSERT_FLOAT_EQ(u1 - u0, 15.0f / 256.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(v1 - v0, 15.0f / 256.0f, 1e-6f);

    /* Tile 5 (second row... actually first row, col 5) sits right of tile 0. */
    float a0 = 0.0f, b0 = 0.0f, a1 = 0.0f, b1 = 0.0f;
    texture_atlas_tile_uv(5, &a0, &b0, &a1, &b1);
    TEST_ASSERT(a0 > u0);
    TEST_ASSERT_FLOAT_EQ(b0, v0, 1e-6f);

    /* Out-of-range tiles clamp instead of producing garbage. */
    texture_atlas_tile_uv(-7, &a0, &b0, &a1, &b1);
    TEST_ASSERT(a0 >= 0.0f && a1 <= 1.0f);
    texture_atlas_tile_uv(9999, &a0, &b0, &a1, &b1);
    TEST_ASSERT(a0 >= 0.0f && a1 <= 1.0f);

    /* NULL outputs are safe. */
    texture_atlas_tile_uv(3, NULL, NULL, NULL, NULL);
    return failures;
}

/* Test: block -> tile mapping covers every block with a valid tile.
 *
 * Returns: failure count.
 */
int test_atlas_tile_mapping(void)
{
    int failures = 0;
    for (int b = 0; b < BLOCK_COUNT; ++b) {
        for (int f = 0; f < 6; ++f) {
            int t = block_tile_for_face((uint16_t)b, f);
            TEST_ASSERT(t >= 0 && t < 256);
        }
    }
    /* Grass uses three distinct tiles by face direction. */
    int top = block_tile_for_face(BLOCK_GRASS, ATLAS_FACE_POS_Y);
    int side = block_tile_for_face(BLOCK_GRASS, ATLAS_FACE_POS_X);
    int bottom = block_tile_for_face(BLOCK_GRASS, ATLAS_FACE_NEG_Y);
    TEST_ASSERT(top == TILE_GRASS_TOP);
    TEST_ASSERT(side == TILE_GRASS_SIDE);
    TEST_ASSERT(bottom == TILE_GRASS_BOTTOM);
    TEST_ASSERT(top != side && side != bottom && top != bottom);
    /* Ores map to distinct tiles. */
    TEST_ASSERT(block_tile_for_face(BLOCK_COAL_ORE, 0) == TILE_COAL_ORE);
    TEST_ASSERT(block_tile_for_face(BLOCK_DIAMOND_ORE, 0) == TILE_DIAMOND_ORE);
    TEST_ASSERT(block_tile_for_face(BLOCK_WATER, 0) == TILE_WATER);
    return failures;
}

/* Test: generated pixels are non-trivial (dimensions + content).
 *
 * Returns: failure count.
 */
int test_atlas_pixels(void)
{
    int failures = 0;
    const size_t px_size = (size_t)ATLAS_SIZE * (size_t)ATLAS_SIZE * 4;
    unsigned char *px = (unsigned char *)malloc(px_size);
    TEST_ASSERT(px != NULL);
    if (px == NULL) {
        return failures + 1;
    }
    texture_atlas_fill_rgba(px);

    /* Buffer is not blank: some pixels differ from the first. */
    int varied = 0;
    for (size_t i = 4; i < px_size; i += 401) {
        if (px[i] != px[0]) {
            varied = 1;
            break;
        }
    }
    TEST_ASSERT(varied == 1);

    /* Grass top is greenish and opaque. */
    size_t go = ((size_t)0 * ATLAS_SIZE + 0) * 4; /* tile 0, pixel (0,0). */
    TEST_ASSERT(px[go + 1] > px[go + 0]);
    TEST_ASSERT(px[go + 1] > px[go + 2]);
    TEST_ASSERT(px[go + 3] == 255);

    /* Leaves tile has transparent holes (cutout). */
    {
        int col = TILE_LEAVES % ATLAS_TILES;
        int row = TILE_LEAVES / ATLAS_TILES;
        int transparent = 0;
        int opaque = 0;
        for (int y = 0; y < ATLAS_TILE_PX; ++y) {
            for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                size_t o = ((size_t)(row * ATLAS_TILE_PX + y) * ATLAS_SIZE + (size_t)(col * ATLAS_TILE_PX + x)) * 4;
                if (px[o + 3] == 0) {
                    transparent++;
                } else {
                    opaque++;
                }
            }
        }
        TEST_ASSERT(transparent > 0);
        TEST_ASSERT(opaque > 0);
    }

    /* Water tile is semi-transparent (blended, not cut out). */
    {
        int col = TILE_WATER % ATLAS_TILES;
        int row = TILE_WATER / ATLAS_TILES;
        size_t o = ((size_t)(row * ATLAS_TILE_PX) * ATLAS_SIZE + (size_t)(col * ATLAS_TILE_PX)) * 4;
        TEST_ASSERT(px[o + 3] > 25 && px[o + 3] < 255);
    }

    /* The original player sleeve and skin tiles are fully opaque, so the
     * first-person arm never inherits uninitialized atlas pixels. */
    {
        int tiles[2] = {TILE_PLAYER_SKIN, TILE_PLAYER_SLEEVE};
        for (int t = 0; t < 2; ++t) {
            int col = tiles[t] % ATLAS_TILES;
            int row = tiles[t] / ATLAS_TILES;
            int opaque = 0;
            for (int y = 0; y < ATLAS_TILE_PX; ++y) {
                for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                    size_t o = ((size_t)(row * ATLAS_TILE_PX + y) * ATLAS_SIZE +
                                (size_t)(col * ATLAS_TILE_PX + x)) * 4;
                    opaque += px[o + 3] == 255;
                }
            }
            TEST_ASSERT(opaque == ATLAS_TILE_PX * ATLAS_TILE_PX);
        }
    }

    /* NULL fill is safe. */
    texture_atlas_fill_rgba(NULL);
    free(px);
    return failures;
}

/* Test: per-face tiles for the M7 blocks + extended file table.
 *
 * Returns: failure count.
 */
int test_atlas_mcfaces(void)
{
    int failures = 0;
    /* Log ends show rings; sides show bark. */
    TEST_ASSERT(block_tile_for_face(BLOCK_WOOD, ATLAS_FACE_POS_Y) == TILE_WOOD_TOP);
    TEST_ASSERT(block_tile_for_face(BLOCK_WOOD, ATLAS_FACE_NEG_Y) == TILE_WOOD_TOP);
    TEST_ASSERT(block_tile_for_face(BLOCK_WOOD, ATLAS_FACE_POS_X) == TILE_WOOD);
    /* Workbench: crafted top, plank bottom, tool-grid sides. */
    TEST_ASSERT(block_tile_for_face(BLOCK_WORKBENCH, ATLAS_FACE_POS_Y) == TILE_WORKBENCH_TOP);
    TEST_ASSERT(block_tile_for_face(BLOCK_WORKBENCH, ATLAS_FACE_NEG_Y) == TILE_PLANKS);
    TEST_ASSERT(block_tile_for_face(BLOCK_WORKBENCH, ATLAS_FACE_POS_X) == TILE_WORKBENCH_SIDE);
/* Per-material tool tiles exist in the pack file table.
     * Lower bound, not an exact pin: new tiles must not break this. */
    TEST_ASSERT(texture_atlas_tile_file_count() >= 61);
    TEST_ASSERT(texture_atlas_tile_file(TILE_WOOD_PICKAXE) != NULL);
    TEST_ASSERT(texture_atlas_tile_file(TILE_STONE_SHOVEL) != NULL);
    TEST_ASSERT(texture_atlas_tile_file(TILE_DIAMOND_SWORD) != NULL);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_WOOD_TOP), "wood_top") == 0);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_BOW), "bow") == 0);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_ARROW), "arrow") == 0);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_BONE), "bone") == 0);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_BEEF), "beef") == 0);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_LEATHER), "leather") == 0);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_FLESH), "flesh") == 0);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_PLAYER_SKIN), "player_skin") == 0);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_PLAYER_SLEEVE), "player_sleeve") == 0);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_WOOD_SWORD), "wood_sword") == 0);
    TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_STONE_SWORD), "stone_sword") == 0);
TEST_ASSERT(strcmp(texture_atlas_tile_file(TILE_DIAMOND_SWORD), "diamond_sword") == 0);
    TEST_ASSERT(texture_atlas_tile_file(61) == NULL);
    TEST_ASSERT(texture_atlas_tile_file(-1) == NULL);
    return failures;
}

/* Write a solid 16x16 32-bit BMP (bottom-up) for overlay tests. */
static int test_write_solid_bmp(const char *path, unsigned char b, unsigned char g, unsigned char r)
{
    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    uint32_t size = 54u + 1024u;
    hdr[2] = (unsigned char)(size & 0xFFu);
    hdr[3] = (unsigned char)((size >> 8) & 0xFFu);
    hdr[4] = (unsigned char)((size >> 16) & 0xFFu);
    hdr[5] = (unsigned char)((size >> 24) & 0xFFu);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = 16;
    hdr[22] = 16; /* Positive height: bottom-up rows. */
    hdr[26] = 1;
    hdr[28] = 32;
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f);
        return -1;
    }
    unsigned char px[4] = {b, g, r, 255};
    for (int i = 0; i < 256; ++i) {
        if (fwrite(px, 1, 4, f) != 4) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

/* Tile-3 (stone) top-left pixel offset in the bottom-up atlas buffer. */
static size_t test_tile_px(int tile)
{
    int col = tile % ATLAS_TILES;
    int row = tile / ATLAS_TILES;
    return ((size_t)(row * ATLAS_TILE_PX) * ATLAS_SIZE + (size_t)(col * ATLAS_TILE_PX)) * 4;
}

/* Test: directory overlay replaces listed tiles, keeps the rest, and
 * tolerates missing directories (procedural fallback).
 *
 * Returns: failure count.
 */
int test_atlas_apply_dir(void)
{
    int failures = 0;
    const size_t px_size = (size_t)ATLAS_SIZE * (size_t)ATLAS_SIZE * 4;
    unsigned char *px = (unsigned char *)malloc(px_size);
    TEST_ASSERT(px != NULL);
    if (px == NULL) {
        return failures + 1;
    }
    texture_atlas_fill_rgba(px);
    size_t stone = test_tile_px(TILE_STONE);
    size_t grass = test_tile_px(TILE_GRASS_TOP);
    unsigned char stone_before[4];
    unsigned char grass_before[4];
    memcpy(stone_before, px + stone, 4);
    memcpy(grass_before, px + grass, 4);

    /* Missing dir: buffer untouched, no crash. */
    texture_atlas_apply_dir(px, "test_tmp_nope_xyz");
    TEST_ASSERT(memcmp(px + stone, stone_before, 4) == 0);
    texture_atlas_apply_dir(NULL, "test_tmp_nope_xyz");
    texture_atlas_apply_dir(px, NULL);

    /* One solid-gray stone.bmp: only the stone tile changes. */
    const char *dir = "test_tmp_mctiles";
    TEST_ASSERT(path_mkdir_p(dir) == 0);
    char full[PATH_MAX_LEN];
    TEST_ASSERT(path_join(full, sizeof(full), dir, "stone.bmp") == 0);
    TEST_ASSERT(test_write_solid_bmp(full, 128, 128, 128) == 0);
    texture_atlas_apply_dir(px, dir);
    TEST_ASSERT(px[stone + 0] == 128 && px[stone + 1] == 128 && px[stone + 2] == 128 &&
                px[stone + 3] == 255);
    /* Whole stone tile is exactly the blit (all 256 pixels). */
    {
        int exact = 1;
        for (int y = 0; y < 16 && exact; ++y) {
            for (int x = 0; x < 16; ++x) {
                size_t o = ((size_t)y * ATLAS_SIZE + (size_t)(3 * 16 + x)) * 4;
                if (px[o] != 128 || px[o + 1] != 128 || px[o + 2] != 128 || px[o + 3] != 255) {
                    exact = 0;
                    break;
                }
            }
        }
        TEST_ASSERT(exact == 1);
    }
    TEST_ASSERT(memcmp(px + grass, grass_before, 4) == 0);
    free(px);
    path_remove_file(full);
    path_remove_dir(dir);
    return failures;
}

/* Test: resource-pack paths preserve their root and pack names, loaded tile
 * overrides are applied, and only folders with tiles/ are discoverable.
 */
int test_atlas_resource_pack_paths(void)
{
    int failures = 0;
    const char *root = "test_tmp_packroot";
    const char *pack = "Example Pack";
    char pack_dir[PATH_MAX_LEN] = {0};
    char tiles_dir[PATH_MAX_LEN] = {0};
    char expected_tiles[PATH_MAX_LEN] = {0};
    char tile_file[PATH_MAX_LEN] = {0};
    TEST_ASSERT(path_join(pack_dir, sizeof(pack_dir), root, pack) == 0);
    TEST_ASSERT(path_join(expected_tiles, sizeof(expected_tiles), pack_dir, "tiles") == 0);
    TEST_ASSERT(texture_atlas_pack_tiles_dir(tiles_dir, sizeof(tiles_dir), root, pack) == 0);
    TEST_ASSERT(strcmp(tiles_dir, expected_tiles) == 0);
    TEST_ASSERT(texture_atlas_pack_tiles_dir(tiles_dir, sizeof(tiles_dir), root, "../escape") != 0);

    TEST_ASSERT(path_mkdir_p(tiles_dir) == 0);
    TEST_ASSERT(path_join(tile_file, sizeof(tile_file), tiles_dir, "wood_pickaxe.bmp") == 0);
    TEST_ASSERT(test_write_solid_bmp(tile_file, 3, 5, 7) == 0);

    const size_t px_size = (size_t)ATLAS_SIZE * (size_t)ATLAS_SIZE * 4;
    unsigned char *px = (unsigned char *)malloc(px_size);
    TEST_ASSERT(px != NULL);
    if (px != NULL) {
        texture_atlas_fill_rgba(px);
        texture_atlas_apply_dir(px, tiles_dir);
        size_t icon = test_tile_px(TILE_WOOD_PICKAXE);
        TEST_ASSERT(px[icon] == 7 && px[icon + 1] == 5 && px[icon + 2] == 3 && px[icon + 3] == 255);
        free(px);
    }

    char packs[4][64] = {{0}};
    TEST_ASSERT(texture_atlas_list_packs_in("", packs, 4) == 1);
    TEST_ASSERT(strcmp(packs[0], "Default") == 0);
    size_t count = texture_atlas_list_packs_in(root, packs, 4);
    TEST_ASSERT(count == 2);
    TEST_ASSERT(strcmp(packs[0], "Default") == 0);
    TEST_ASSERT(strcmp(packs[1], pack) == 0);

    path_remove_file(tile_file);
    path_remove_dir(tiles_dir);
    path_remove_dir(pack_dir);
    path_remove_dir(root);
    return failures;
}
