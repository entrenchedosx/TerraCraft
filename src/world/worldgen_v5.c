#include "world/worldgen_v5.h"
#include "world/world_gen.h"
#include "world/world.h"
#include "world/biome.h"
#include "world/block.h"
#include "core/noise.h"
#include <math.h>

static float clamp(float v, float lo, float hi)
{
    return fminf(hi, fmaxf(lo, v));
}
static float mix(float a, float b, float t)
{
    return a + (b - a) * t;
}
static float smooth(float lo, float hi, float v)
{
    float t = clamp((v - lo) / (hi - lo), 0, 1);
    return t * t * (3 - 2 * t);
}
static float field(long seed, float x, float z, float scale, uint32_t salt, int octaves)
{
    return noise_fbm2(x * scale, z * scale, octaves, 2, .5f, (uint32_t)seed ^ salt);
}
static float continental_height(float c)
{
    /* Monotone shelf, shallow coast and inland baseline, with a continuous
     * zero-slope join at every knot. Detail cannot erase the ocean shelf. */
    static const float knots[][2] = {{-1, 20}, {-.48f, 34}, {-.24f, 52}, {-.13f, 64}, {.05f, 73}, {.38f, 84}, {1, 96}};
    for (int i = 0; i < 6; ++i)
        if (c <= knots[i + 1][0])
            return mix(knots[i][1], knots[i + 1][1], smooth(knots[i][0], knots[i + 1][0], c));
    return 96;
}
GenClimate worldgen5_climate(long seed, int x, int z)
{
    GenClimate c;
    float fx = (float)x, fz = (float)z;
    /* Continuous float warps preserve derivatives across block/chunk edges. */
    float wx = fx + field(seed, fx, fz, .0007f, 0x51A1u, 2) * 100;
    float wz = fz + field(seed, fx, fz, .0007f, 0x51A2u, 2) * 100;
    c.continentalness = clamp(field(seed, wx, wz, .00052f, 0xC001u, 3) * 1.12f, -1, 1);
    c.erosion = field(seed, wx, wz, .0013f, 0xE001u, 3);
    c.temperature = field(seed, wx, wz, .00085f, 0x7101u, 3);
    c.humidity = field(seed, wx, wz, .0010f, 0x8101u, 3);
    c.weirdness = clamp(field(seed, wx, wz, .00165f, 0x9101u, 3) * 1.65f, -1, 1);
    float land = smooth(-.20f, -.04f, c.continentalness);
    float inland = smooth(-.10f, .25f, c.continentalness);
    float resistant = 1 - smooth(-.48f, .22f, c.erosion);
    c.mountain = inland * resistant;
    float a = fabsf(c.weirdness);
    /* Valley zero contours and flanking ridges belong to the same field.
     * Different erosion bands expose rolling land, foothills or high peaks. */
    float ridge = powf(smooth(.10f, .62f, a), 1.35f) * (1 - .65f * smooth(.62f, 1, a));
    float rolling = 1 - smooth(-.08f, .62f, c.erosion);
    float regional = field(seed, wx, wz, .0045f, 0xB001u, 3);
    float local = field(seed, fx, fz, .028f, 0xB002u, 2);
    float base = continental_height(c.continentalness);
    float relief = land * (regional * (2 + rolling * 7 + c.mountain * 8) + local * (1 + resistant));
    c.height = base + relief + c.mountain * (7 * rolling + 116 * ridge);
    float valley = (1 - smooth(.025f, .21f, a)) * land;
    float valley_floor = mix(60.5f, base - 5, smooth(.18f, .65f, c.mountain));
    c.height = mix(c.height, fminf(c.height, valley_floor), valley);
    /* Wet channels stay in lowlands. Mountain zero contours remain dry
     * graded valleys, rather than sea-level trenches through every range. */
    c.river = (1 - smooth(.015f, .055f, a)) * land * (1 - smooth(.18f, .5f, c.mountain));
    c.height = mix(c.height, 60.5f + regional * .75f, c.river);
    c.height = clamp(c.height, 8, 225);
    if (c.continentalness < -.13f && c.height < 65)
        c.biome = BIOME_OCEAN;
    else if (c.height > 59 && c.height < 68 && (c.continentalness < .04f || c.river > .2f))
        c.biome = BIOME_BEACH;
    else if (c.mountain > .34f && c.height > 110)
        c.biome = BIOME_MOUNTAINS;
    else if (c.temperature < -.25f)
        c.biome = BIOME_SNOW;
    else if (c.temperature > .20f && c.humidity < -.12f)
        c.biome = BIOME_DESERT;
    else if (c.humidity > .08f)
        c.biome = BIOME_FOREST;
    else
        c.biome = BIOME_PLAINS;
    return c;
}
static float slope(long seed, int x, int z, GenClimate c)
{
    float dx = worldgen5_climate(seed, x + 2, z).height - worldgen5_climate(seed, x - 2, z).height;
    float dz = worldgen5_climate(seed, x, z + 2).height - worldgen5_climate(seed, x, z - 2).height;
    (void)c;
    return sqrtf(dx * dx + dz * dz) * .25f;
}
uint16_t worldgen5_surface(long seed, int x, int z, int h)
{
    GenClimate c = worldgen5_climate(seed, x, z);
    float steep = slope(seed, x, z, c);
    float cold = c.temperature - fmaxf(0, (float)h - 80) * .0045f;
    if (h < 60)
        return c.humidity > 0 || steep > 1 ? BLOCK_GRAVEL : BLOCK_SAND;
    if (c.biome == BIOME_DESERT || c.biome == BIOME_BEACH || (h <= 65 && steep < .9f))
        return BLOCK_SAND;
    /* Exposed rock interrupts snowy slopes instead of coating every cliff. */
    if (steep > 1.25f || (c.mountain > .4f && h > 155 && cold > -.5f))
        return BLOCK_STONE;
    if (cold < -.30f)
        return BLOCK_SNOW;
    return BLOCK_GRASS;
}
bool worldgen5_tree(long seed, int x, int z, int *trunk)
{
    float lottery = noise_hash_to_unit(noise_hash2(x, z, (uint32_t)seed ^ 0x7EE50001u));
    if (lottery >= .04f)
        return false;
    GenClimate c = worldgen5_climate(seed, x, z);
    if (c.biome == BIOME_OCEAN || c.biome == BIOME_BEACH || c.biome == BIOME_DESERT)
        return false;
    float patch = smooth(-.45f, .45f, field(seed, (float)x, (float)z, .009f, 0x5EE1u, 2));
    float density = c.biome == BIOME_FOREST      ? mix(.012f, .04f, patch)
                    : c.biome == BIOME_SNOW      ? .006f * patch
                    : c.biome == BIOME_MOUNTAINS ? .003f * patch
                                                 : .006f * patch;
    density *= 1 - smooth(.35f, 1.15f, slope(seed, x, z, c));
    density *= 1 - smooth(105, 175, c.height);
    if (lottery >= density)
        return false;
    int h = worldgen5_height(seed, x, z);
    if (h <= WORLD_SEA_LEVEL + 1 || h >= 190)
        return false;
    uint16_t surface = worldgen5_surface(seed, x, z, h);
    if (surface != BLOCK_GRASS && surface != BLOCK_SNOW)
        return false;
    if (trunk)
        *trunk = 4 + (int)(noise_hash_to_unit(noise_hash2(x, z, (uint32_t)seed ^ 0x7EE50002u)) * 3);
    return true;
}
uint16_t worldgen5_vegetation(long seed, int x, int z, int h)
{
    if (h <= WORLD_SEA_LEVEL + 1)
        return BLOCK_AIR;
    GenClimate c = worldgen5_climate(seed, x, z);
    if (!biome_has_vegetation(c.biome))
        return BLOCK_AIR;
    float moisture = smooth(-.5f, .45f, c.humidity);
    float cover = mix(.45f, 1.4f, moisture) * (1 - smooth(115, 175, (float)h));
    float r = noise_hash_to_unit(noise_hash2(x, z, (uint32_t)seed ^ 0x9E6E7A01u));
    if (c.biome == BIOME_SNOW)
        return r < .015f * cover ? BLOCK_GRASS_PLANT : BLOCK_AIR;
    if (r < .05f * cover)
        return BLOCK_GRASS_PLANT;
    return r < .062f * cover ? BLOCK_FLOWER : BLOCK_AIR;
}
GenDensity worldgen5_density(long s, int x, int y, int z)
{
    return worldgen_density_profile(s, x, y, z, 5);
}
GenAquifer worldgen5_aquifer(long s, int x, int y, int z)
{
    return worldgen_aquifer_profile(s, x, y, z, 5);
}
int worldgen5_height(long s, int x, int z)
{
    return worldgen_height_profile(s, x, z, 5);
}
uint16_t worldgen5_base_block(long s, int x, int y, int z)
{
    return worldgen_base_block_profile(s, x, y, z, 5);
}
void worldgen5_fill(Chunk *c, long s)
{
    worldgen_fill_profile(c, s, 5);
}
