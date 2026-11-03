#include "test_main.h"
#include "render/visual_style.h"
#include "render/texture_atlas.h"
#include "world/biome.h"
#include "world/block.h"
#include "world/chunk.h"
#include "render/mesher.h"
#include "game/time_system.h"

#include <string.h>

int test_visual_lightmap_and_fog(void)
{
    int failures = 0;
    unsigned char day[16 * 16 * 4], night[16 * 16 * 4];
    visual_lightmap(day, 1.0f, 0.5f);
    visual_lightmap(night, 0.2f, 0.5f);
    /* Fully exposed daytime surfaces are neutral, never overbright.
     * Darkness has a small gamma-dependent floor, not a sun-direction bias. */
    const unsigned char *outdoor = day + 15 * 16 * 4;
    TEST_ASSERT(outdoor[0] == outdoor[1] && outdoor[1] == outdoor[2]);
    TEST_ASSERT(outdoor[0] >= 250 && outdoor[0] <= 253);
    TEST_ASSERT(day[0] >= 20 && day[0] <= 30);
    TEST_ASSERT(memcmp(day, night, 16 * 4) == 0); /* No sky -> independent of day. */
    TEST_ASSERT(night[15 * 16 * 4 + 2] > night[15 * 16 * 4]);
    for (int sky = 0; sky < 16; ++sky)
        for (int block = 1; block < 16; ++block)
            TEST_ASSERT(day[(sky * 16 + block) * 4] >= day[(sky * 16 + block - 1) * 4]);
    float start, end;
    visual_fog_range(8, false, &start, &end);
    TEST_ASSERT_FLOAT_EQ(start, 112.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(end, 128.0f, 0.0f);
    TEST_ASSERT(64 < start); /* A nearby 64-block vista must stay clear. */
    visual_fog_range(4, false, &start, &end);
    TEST_ASSERT_FLOAT_EQ(start, 48.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(end, 64.0f, 0.0f);
    visual_fog_range(8, true, &start, &end);
    TEST_ASSERT(start < 0 && end == 96);
    return failures;
}

int test_visual_biome_tints(void)
{
    int failures = 0;
    Vec3 forest = visual_biome_color(BIOME_FOREST, false);
    Vec3 snow = visual_biome_color(BIOME_SNOW, true);
    TEST_ASSERT_FLOAT_EQ(forest.x, 121.0f / 255, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(forest.y, 192.0f / 255, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(snow.z, 123.0f / 255, 1e-6f);
    VisualTintGrid grid;
    visual_build_tints(NULL, -1, 0, &grid);
    Vec3 grass = visual_tile_tint(&grid, 15, 0, TILE_GRASS_TOP);
    Vec3 water = visual_tile_tint(&grid, 15, 0, TILE_WATER);
    TEST_ASSERT_FLOAT_EQ(grass.x, 1.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(grass.z, 1.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(water.x, 63.0f / 255, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(water.z, 228.0f / 255, 1e-6f);
    return failures;
}

int test_texture_mipmap_edges(void)
{
    int failures = 0;
    /* Transparent blue pixels must not put blue fringes around red art. */
    const unsigned char source[16] = {255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0};
    unsigned char pixel[4];
    texture_atlas_downsample(source, 2, 2, pixel);
    TEST_ASSERT(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0);
    TEST_ASSERT(pixel[3] == 64);
    /* Two touching 16px atlas tiles keep distinct colors at all four
     * mip levels; generating a fifth would mix unrelated block textures. */
    unsigned char tiles[32 * 16 * 4], reduced[16 * 8 * 4];
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x) {
            unsigned char *p = tiles + (y * 32 + x) * 4;
            p[0] = x < 16 ? 255 : 0;
            p[1] = 0;
            p[2] = x < 16 ? 0 : 255;
            p[3] = 255;
        }
    int width = 32, height = 16;
    for (int level = 0; level < 4; ++level) {
        texture_atlas_downsample(tiles, width, height, reduced);
        width /= 2;
        height /= 2;
        memcpy(tiles, reduced, (size_t)width * height * 4);
        for (int x = 0; x < width; ++x) {
            TEST_ASSERT(tiles[x * 4] == (x < width / 2 ? 255 : 0));
            TEST_ASSERT(tiles[x * 4 + 2] == (x < width / 2 ? 0 : 255));
        }
    }
    return failures;
}

int test_visual_leaves_and_timeline(void)
{
    int failures = 0;
    Chunk *chunk = chunk_create(0, 0);
    TEST_ASSERT(chunk != NULL);
    if (!chunk)
        return failures + 1;
    chunk_set_block(chunk, 8, 64, 8, BLOCK_LEAVES);
    chunk_set_block(chunk, 9, 64, 8, BLOCK_LEAVES);
    MeshData *mesh = mesher_build_chunk_mesh(chunk, NULL);
    TEST_ASSERT(mesh != NULL);
    if (mesh) {
        /* Fancy leaves need both interior faces to fill cutout holes. */
        TEST_ASSERT(mesh->vertex_count == 48);
        for (size_t i = 0; i < mesh->vertex_count; ++i)
            TEST_ASSERT_FLOAT_EQ(mesh->vertices[i * MESHER_FLOATS_PER_VERTEX + 8], 1, 1e-6f);
        mesher_free(mesh);
    }
    chunk_destroy(chunk);
    TimeSystem time;
    time_system_init(&time, 0, 0);
    Vec3 cloud = time_get_cloud_color(&time), fog = time_get_fog_multiplier(&time);
    TEST_ASSERT_FLOAT_EQ(cloud.z, 38.0f / 255, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(fog.x, 15.0f / 255, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(time_get_star_brightness(&time), 0.5f, 1e-6f);
    /* The sky-color and skylight tracks have different dusk endpoints in
     * the 1.21.11 data. Darkness must not prematurely blacken the whole sky. */
    time_system_init(&time, 13140.0f / 24000 + 0.25f, 0);
    TEST_ASSERT(time_get_daylight(&time) > 0.25f);
    TEST_ASSERT_FLOAT_EQ(time_get_light_intensity(&time), TIME_MIN_INTENSITY, 1e-5f);
    return failures;
}
