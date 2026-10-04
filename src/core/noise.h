#pragma once

/* Deterministic seeded value noise (M2): integer-lattice hashing, no rand().
 * Used for terrain heightmaps and ore scatter. Pure CPU, headless-testable.
 *
 * All functions are deterministic for a given seed. Output ranges are
 * documented per function. No global state.
 */

#include <stdint.h>

/* Hash two integer lattice coords with a seed into a 32-bit value.
 * Well-distributed (splitmix-style avalanche); safe for negative inputs.
 *
 * Args:
 *   x, y: lattice coordinates.
 *   seed: world seed (any 32-bit value).
 *
 * Returns: 32-bit hash.
 */
uint32_t noise_hash2(int x, int y, uint32_t seed);

/* Hash three integer coords with a seed (for 3D scatter such as ores).
 *
 * Args:
 *   x, y, z: lattice coordinates.
 *   seed: world seed.
 *
 * Returns: 32-bit hash.
 */
uint32_t noise_hash3(int x, int y, int z, uint32_t seed);

/* Hash to a float in [0,1).
 *
 * Args:
 *   h: hash value from noise_hash2/3.
 *
 * Returns: float in [0,1).
 */
float noise_hash_to_unit(uint32_t h);

/* Smooth 2D value noise in [-1,1].
 * Bilinear interpolation of hashed lattice corners with smootherstep fade.
 *
 * Args:
 *   x, y: sample position (continuous).
 *   seed: noise seed.
 *
 * Returns: smooth noise in [-1,1].
 */
float noise_value2(float x, float y, uint32_t seed);

/* Fractal Brownian motion: layered value noise, output roughly in [-1,1].
 * Normalised by total amplitude so the range is stable across octaves.
 *
 * Args:
 *   x, y: sample position.
 *   octaves: layer count (>= 1; clamped to [1,8]).
 *   lacunarity: frequency multiplier per octave (typically 2.0).
 *   gain: amplitude multiplier per octave (typically 0.5).
 *   seed: noise seed (each octave rehashes with seed+octave*101).
 *
 * Returns: fBm noise, approximately in [-1,1].
 */
float noise_fbm2(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed);

/* Smooth 3D value noise in [-1,1] (trilinear, smootherstep fade).
 * Used for caves and other volumetric features. Deterministic per seed.
 *
 * Args:
 *   x, y, z: sample position (continuous).
 *   seed: noise seed.
 *
 * Returns: smooth noise in [-1,1].
 */
float noise_value3(float x, float y, float z, uint32_t seed);

/* 3D fractal Brownian motion, output roughly in [-1,1].
 *
 * Args:
 *   x, y, z: sample position.
 *   octaves: layer count (>= 1; clamped to [1,8]).
 *   lacunarity: frequency multiplier per octave (typically 2.0).
 *   gain: amplitude multiplier per octave (typically 0.5).
 *   seed: noise seed (each octave rehashes with seed+octave*101).
 *
 * Returns: fBm noise, approximately in [-1,1].
 */
float noise_fbm3(float x, float y, float z, int octaves, float lacunarity, float gain, uint32_t seed);
