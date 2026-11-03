#include "render/visual_style.h"
#include "render/texture_atlas.h"
#include "world/biome.h"
#include "world/world.h"
#include "world/world_gen.h"
#include "world/worldgen_v4.h"
#include "world/worldgen_v5.h"

#include <math.h>

static float unit(float x)
{
    return fminf(1.0f, fmaxf(0.0f, x));
}

Vec3 visual_biome_color(int biome, bool foliage)
{
    static const unsigned char grass[BIOME_COUNT][3] = {{142, 185, 113}, {145, 189, 89}, {145, 189, 89},
                                                        {121, 192, 90},  {191, 183, 85}, {128, 180, 151},
                                                        {138, 182, 137}};
    static const unsigned char leaves[BIOME_COUNT][3] = {{113, 167, 77}, {119, 171, 47}, {119, 171, 47}, {89, 174, 48},
                                                         {174, 164, 42}, {96, 161, 123}, {109, 163, 107}};
    if (biome < 0 || biome >= BIOME_COUNT)
        biome = BIOME_PLAINS;
    const unsigned char *c = foliage ? leaves[biome] : grass[biome];
    return mmath_vec3(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f);
}

static int visual_biome(const World *w, int x, int z)
{
    if (!w)
        return BIOME_PLAINS;
    if (w->terrain_version >= 5)
        return worldgen5_climate(w->seed, x, z).biome;
    if (w->terrain_version >= 4)
        return worldgen_climate(w->seed, x, z).biome;
    return biome_at(w->seed, x, z, world_gen_height_version(w->seed, x, z, w->terrain_version));
}

void visual_build_tints(const World *w, int cx, int cz, VisualTintGrid *out)
{
    if (!out)
        return;
    /* Vanilla's default blend radius is two blocks (a 5 x 5 box). Cache
     * climate samples once per column instead of per vertex or face. */
    Vec3 grass[20 * 20], foliage[20 * 20];
    for (int z = 0; z < 20; ++z)
        for (int x = 0; x < 20; ++x) {
            int b = visual_biome(w, cx * 16 + x - 2, cz * 16 + z - 2);
            grass[z * 20 + x] = visual_biome_color(b, false);
            foliage[z * 20 + x] = visual_biome_color(b, true);
        }
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            Vec3 g = {0}, f = {0};
            for (int dz = 0; dz < 5; ++dz)
                for (int dx = 0; dx < 5; ++dx) {
                    int i = (z + dz) * 20 + x + dx;
                    g = mmath_vec3_add(g, grass[i]);
                    f = mmath_vec3_add(f, foliage[i]);
                }
            out->grass[z * 16 + x] = mmath_vec3_scale(g, 1.0f / 25.0f);
            out->foliage[z * 16 + x] = mmath_vec3_scale(f, 1.0f / 25.0f);
        }
}

static Vec3 tint_for_colors(Vec3 grass, Vec3 foliage, int tile)
{
    /* Existing converted grass/leaf tiles bake the plains multiplier.
     * Apply a relative tint so legacy owner assets and packs keep working. */
    if (tile == TILE_GRASS_TOP || tile == TILE_PLANT)
        return mmath_vec3(grass.x / (145.0f / 255), grass.y / (189.0f / 255), grass.z / (89.0f / 255));
    if (tile == TILE_LEAVES)
        return mmath_vec3(foliage.x / (119.0f / 255), foliage.y / (171.0f / 255), foliage.z / (47.0f / 255));
    if (tile == TILE_GRASS_SIDE_OVERLAY)
        return grass;
    if (tile == TILE_WATER)
        return mmath_vec3(63.0f / 255, 118.0f / 255, 228.0f / 255);
    return mmath_vec3(1, 1, 1);
}

Vec3 visual_tile_tint(const VisualTintGrid *g, int x, int z, int tile)
{
    if (!g || x < 0 || x >= 16 || z < 0 || z >= 16)
        return tint_for_colors(visual_biome_color(BIOME_PLAINS, false), visual_biome_color(BIOME_PLAINS, true), tile);
    return tint_for_colors(g->grass[z * 16 + x], g->foliage[z * 16 + x], tile);
}

Vec3 visual_world_tint(const World *w, int x, int z, int tile)
{
    if (tile != TILE_GRASS_TOP && tile != TILE_PLANT && tile != TILE_LEAVES && tile != TILE_GRASS_SIDE_OVERLAY)
        return tint_for_colors(mmath_vec3(1, 1, 1), mmath_vec3(1, 1, 1), tile);
    Vec3 g = {0}, f = {0};
    for (int dz = -2; dz <= 2; ++dz)
        for (int dx = -2; dx <= 2; ++dx) {
            int b = visual_biome(w, x + dx, z + dz);
            g = mmath_vec3_add(g, visual_biome_color(b, false));
            f = mmath_vec3_add(f, visual_biome_color(b, true));
        }
    return tint_for_colors(mmath_vec3_scale(g, 1.0f / 25), mmath_vec3_scale(f, 1.0f / 25), tile);
}

Vec3 visual_sky_color(const World *w, int x, int z, float daylight)
{
    static const unsigned char colors[BIOME_COUNT][3] = {{123, 164, 255}, {120, 167, 255}, {120, 167, 255},
                                                         {121, 166, 255}, {110, 177, 255}, {127, 161, 255},
                                                         {125, 162, 255}};
    int b = visual_biome(w, x, z);
    if (b < 0 || b >= BIOME_COUNT)
        b = BIOME_PLAINS;
    return mmath_vec3(colors[b][0] * unit(daylight) / 255, colors[b][1] * unit(daylight) / 255,
                      colors[b][2] * unit(daylight) / 255);
}

void visual_lightmap(unsigned char out[16 * 16 * 4], float sky_brightness, float gamma)
{
    if (!out)
        return;
    float s = unit(sky_brightness), factor = s * 0.95f + 0.05f;
    for (int sky = 0; sky < 16; ++sky)
        for (int block = 0; block < 16; ++block) {
            float b = (float)block / 15, k = (float)sky / 15;
            b = b / (4 - 3 * b) * 1.5f;
            k = k / (4 - 3 * k) * factor;
            float c[3] = {b, b * ((b * 0.6f + 0.4f) * 0.6f + 0.4f), b * (b * b * 0.6f + 0.4f)};
            float day = unit((s - 0.2f) / 0.8f);
            float sky_color = (122.0f + 133.0f * day) / 255.0f;
            c[0] += sky_color * k;
            c[1] += sky_color * k;
            c[2] += k;
            for (int i = 0; i < 3; ++i)
                c[i] = unit(c[i] * 0.96f + 0.03f);
            float maxc = fmaxf(c[0], fmaxf(c[1], c[2]));
            float inv = 1 - maxc;
            float corrected = 1 - inv * inv * inv * inv;
            for (int i = 0; i < 3; ++i) {
                float v = c[i] * (1 + unit(gamma) * (corrected / maxc - 1));
                out[(sky * 16 + block) * 4 + i] = (unsigned char)floorf(unit(v * 0.96f + 0.03f) * 255 + 0.5f);
            }
            out[(sky * 16 + block) * 4 + 3] = 255;
        }
}

void visual_fog_range(int radius, bool underwater, float *start, float *end)
{
    float distance = (float)(radius < 2 ? 2 : radius) * 16;
    if (start)
        *start = underwater ? -8.0f : fmaxf(0, distance - 16);
    if (end)
        *end = underwater ? fminf(96, distance) : distance;
}
