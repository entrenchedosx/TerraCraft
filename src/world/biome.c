#include "world/biome.h"
#include "core/noise.h"

/* Biome names (index matches BiomeType). */
static const char *BIOME_NAMES[BIOME_COUNT] = {
    "ocean", "beach", "plains", "forest", "desert", "snow", "mountains",
};

/* Human-readable biome name. */
const char *biome_name(int b)
{
    if (b < 0 || b >= (int)BIOME_COUNT) {
        return "unknown";
    }
    return BIOME_NAMES[b];
}

/* Low-frequency climate temperature. */
float biome_temperature(long seed, int wx, int wz)
{
    uint32_t s = (uint32_t)seed ^ 0x1a2b3c4du;
    return noise_fbm2((float)wx * 0.0021f, (float)wz * 0.0021f, 3, 2.0f, 0.5f, s);
}

/* Independent low-frequency humidity stream. */
float biome_humidity(long seed, int wx, int wz)
{
    uint32_t s = (uint32_t)seed ^ 0x5d6e7f80u;
    return noise_fbm2((float)wx * 0.0023f + 500.0f, (float)wz * 0.0023f - 500.0f, 3, 2.0f, 0.5f, s);
}

/* Ridged highland mask: sharp peaks where ridged noise folds high. */
float biome_mountains(long seed, int wx, int wz)
{
    uint32_t s = (uint32_t)seed ^ 0x9fa0b1c2u;
    float n = noise_fbm2((float)wx * 0.0016f + 900.0f, (float)wz * 0.0016f + 900.0f, 3, 2.0f, 0.5f, s);
    float ridge = 1.0f - (n < 0.0f ? -n : n); /* 0 at extremes, 1 at center. */
    float m = (ridge - 0.82f) / 0.18f;        /* Gate: only strong ridges. */
    if (m < 0.0f) {
        m = 0.0f;
    }
    if (m > 1.0f) {
        m = 1.0f;
    }
    return m;
}

/* Select the biome for a column. */
int biome_at(long seed, int wx, int wz, int h)
{
    if (h < 64 - 3) {
        return BIOME_OCEAN;
    }
    if (h <= 64 + 1) {
        return BIOME_BEACH;
    }
    float m = biome_mountains(seed, wx, wz);
    if (m > 0.45f || h >= 100) {
        return BIOME_MOUNTAINS;
    }
    float t = biome_temperature(seed, wx, wz);
    float hu = biome_humidity(seed, wx, wz);
    if (t < -0.22f) {
        return BIOME_SNOW;
    }
    if (t > 0.28f && hu < -0.08f) {
        return BIOME_DESERT;
    }
    if (hu > 0.12f) {
        return BIOME_FOREST;
    }
    return BIOME_PLAINS;
}

/* Trees per column by biome. */
float biome_tree_density(int b)
{
    switch (b) {
    case BIOME_FOREST:
        return 0.022f;
    case BIOME_PLAINS:
        return 0.006f;
    case BIOME_SNOW:
        return 0.004f;
    default:
        return 0.0f;
    }
}

/* Vegetation permission by biome. */
bool biome_has_vegetation(int b)
{
    return b == BIOME_PLAINS || b == BIOME_FOREST || b == BIOME_SNOW;
}
