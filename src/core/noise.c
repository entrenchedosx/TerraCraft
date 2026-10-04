#include "core/noise.h"

#include <math.h>

/* Integer mix: avalanche all input bits (splitmix64 finalizer style, 32-bit).
 * Multiplication by odd constants + xorshift gives good diffusion for
 * sequential lattice coords, avoiding visible grid artifacts.
 */
static uint32_t noise_mix(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

/* Hash two integer coords with a seed.
 *
 * Args:
 *   x, y: lattice coords.
 *   seed: seed.
 *
 * Returns: 32-bit hash.
 */
uint32_t noise_hash2(int x, int y, uint32_t seed)
{
    uint32_t h = seed + 0x9e3779b9u;
    h = noise_mix(h ^ (uint32_t)x);
    h = noise_mix(h ^ ((uint32_t)y * 0x85ebca6bu));
    return noise_mix(h);
}

/* Hash three integer coords with a seed.
 *
 * Args:
 *   x, y, z: lattice coords.
 *   seed: seed.
 *
 * Returns: 32-bit hash.
 */
uint32_t noise_hash3(int x, int y, int z, uint32_t seed)
{
    uint32_t h = seed + 0x9e3779b9u;
    h = noise_mix(h ^ (uint32_t)x);
    h = noise_mix(h ^ ((uint32_t)y * 0x85ebca6bu));
    h = noise_mix(h ^ ((uint32_t)z * 0xc2b2ae35u));
    return noise_mix(h);
}

/* Map a hash to [0,1).
 *
 * Args:
 *   h: hash value.
 *
 * Returns: float in [0,1).
 */
float noise_hash_to_unit(uint32_t h)
{
    /* Use the high 24 bits for exact float representation. */
    return (float)(h >> 8) / 16777216.0f;
}

/* Smootherstep fade (6t^5 - 15t^4 + 10t^3), zero 1st/2nd derivatives at ends. */
static float fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/* Smooth 2D value noise in [-1,1].
 *
 * Args:
 *   x, y: sample position.
 *   seed: seed.
 *
 * Returns: noise in [-1,1].
 */
float noise_value2(float x, float y, uint32_t seed)
{
    int xi = (int)floorf(x);
    int yi = (int)floorf(y);
    float xf = x - (float)xi; /* floorf keeps xf in [0,1) for negatives too. */
    float yf = y - (float)yi;
    float u = fade(xf);
    float v = fade(yf);
    float a = noise_hash_to_unit(noise_hash2(xi, yi, seed));
    float b = noise_hash_to_unit(noise_hash2(xi + 1, yi, seed));
    float c = noise_hash_to_unit(noise_hash2(xi, yi + 1, seed));
    float d = noise_hash_to_unit(noise_hash2(xi + 1, yi + 1, seed));
    float ab = a + u * (b - a);
    float cd = c + u * (d - c);
    float n = ab + v * (cd - ab);
    return n * 2.0f - 1.0f;
}

/* fBm: layered value noise normalised by total amplitude.
 *
 * Args:
 *   x, y: sample position.
 *   octaves, lacunarity, gain, seed: layering params.
 *
 * Returns: approximately [-1,1].
 */
float noise_fbm2(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed)
{
    if (octaves < 1) {
        octaves = 1;
    }
    if (octaves > 8) {
        octaves = 8;
    }
    if (lacunarity <= 0.0f) {
        lacunarity = 2.0f;
    }
    if (gain <= 0.0f) {
        gain = 0.5f;
    }
    float sum = 0.0f;
    float amp = 1.0f;
    float amp_total = 0.0f;
    float fx = x;
    float fy = y;
    for (int i = 0; i < octaves; ++i) {
        sum += noise_value2(fx, fy, seed + (uint32_t)i * 101u) * amp;
        amp_total += amp;
        amp *= gain;
        fx *= lacunarity;
        fy *= lacunarity;
    }
    if (amp_total <= 0.0f) {
        return 0.0f;
    }
    return sum / amp_total;
}

/* Smooth 3D value noise in [-1,1].
 *
 * Args:
 *   x, y, z: sample position.
 *   seed: noise seed.
 *
 * Returns: noise in [-1,1].
 */
float noise_value3(float x, float y, float z, uint32_t seed)
{
    int xi = (int)floorf(x);
    int yi = (int)floorf(y);
    int zi = (int)floorf(z);
    float xf = x - (float)xi; /* floorf keeps fractions in [0,1) for negatives too. */
    float yf = y - (float)yi;
    float zf = z - (float)zi;
    float u = fade(xf);
    float v = fade(yf);
    float w = fade(zf);
    float c000 = noise_hash_to_unit(noise_hash3(xi, yi, zi, seed));
    float c100 = noise_hash_to_unit(noise_hash3(xi + 1, yi, zi, seed));
    float c010 = noise_hash_to_unit(noise_hash3(xi, yi + 1, zi, seed));
    float c110 = noise_hash_to_unit(noise_hash3(xi + 1, yi + 1, zi, seed));
    float c001 = noise_hash_to_unit(noise_hash3(xi, yi, zi + 1, seed));
    float c101 = noise_hash_to_unit(noise_hash3(xi + 1, yi, zi + 1, seed));
    float c011 = noise_hash_to_unit(noise_hash3(xi, yi + 1, zi + 1, seed));
    float c111 = noise_hash_to_unit(noise_hash3(xi + 1, yi + 1, zi + 1, seed));
    float x00 = c000 + u * (c100 - c000);
    float x10 = c010 + u * (c110 - c010);
    float x01 = c001 + u * (c101 - c001);
    float x11 = c011 + u * (c111 - c011);
    float y0 = x00 + v * (x10 - x00);
    float y1 = x01 + v * (x11 - x01);
    return (y0 + w * (y1 - y0)) * 2.0f - 1.0f;
}

/* 3D fBm normalised by total amplitude.
 *
 * Args:
 *   x, y, z: sample position.
 *   octaves, lacunarity, gain, seed: layering params.
 *
 * Returns: approximately [-1,1].
 */
float noise_fbm3(float x, float y, float z, int octaves, float lacunarity, float gain, uint32_t seed)
{
    if (octaves < 1) {
        octaves = 1;
    }
    if (octaves > 8) {
        octaves = 8;
    }
    if (lacunarity <= 0.0f) {
        lacunarity = 2.0f;
    }
    if (gain <= 0.0f) {
        gain = 0.5f;
    }
    float sum = 0.0f;
    float amp = 1.0f;
    float amp_total = 0.0f;
    float fx = x;
    float fy = y;
    float fz = z;
    for (int i = 0; i < octaves; ++i) {
        sum += noise_value3(fx, fy, fz, seed + (uint32_t)i * 101u) * amp;
        amp_total += amp;
        amp *= gain;
        fx *= lacunarity;
        fy *= lacunarity;
        fz *= lacunarity;
    }
    if (amp_total <= 0.0f) {
        return 0.0f;
    }
    return sum / amp_total;
}
