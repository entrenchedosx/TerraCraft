#pragma once

/* Java Edition 1.21.11 gradient-noise primitives (ImprovedNoise,
 * PerlinNoise, NormalNoise), the building blocks of the vanilla density
 * functions. Evaluation order follows the reference exactly; the translation
 * unit disables floating-point contraction so results are not fused.
 * Differentially tested against the 1.21.11 server jar.
 */

#include "world/jrandom.h"

#include <stdbool.h>
#include <stdint.h>

#define JN_MAX_OCTAVES 32

/* net.minecraft.world.level.levelgen.synth.ImprovedNoise */
typedef struct JImprovedNoise {
    double xo, yo, zo;
    uint8_t p[256];
} JImprovedNoise;

/* Consumes nextDouble x3 then 256 nextInt(bound) calls from r. */
void jn_improved_init(JImprovedNoise *n, JrRandom *r);
double jn_improved_noise(const JImprovedNoise *n, double x, double y, double z);
double jn_improved_noise_fudge(const JImprovedNoise *n, double x, double y, double z, double y_scale,
                               double y_fudge);

/* PerlinNoise created with the 1.18+ ("new") initialisation. */
typedef struct JPerlinNoise {
    JImprovedNoise levels[JN_MAX_OCTAVES];
    bool present[JN_MAX_OCTAVES];
    double amplitudes[JN_MAX_OCTAVES];
    int count;
    int first_octave;
    double lowest_freq_value_factor;
    double lowest_freq_input_factor;
    double max_value;
} JPerlinNoise;

/* PerlinNoise.create(random, firstOctave, amplitudes). Returns false on bad
 * arguments (count outside 1..JN_MAX_OCTAVES). */
bool jn_perlin_init(JPerlinNoise *n, JrRandom *r, int first_octave, const double *amplitudes, int count);
/* PerlinNoise.createLegacyForBlendedNoise (pre-1.18 octave initialisation:
 * zero octave first, then octaves downwards; used only by BlendedNoise).
 * Amplitudes must be all non-zero in this port (vanilla would skip octaves
 * with random.consumeCount(262), implemented for completeness). */
bool jn_perlin_init_legacy(JPerlinNoise *n, JrRandom *r, int first_octave, const double *amplitudes, int count);
/* PerlinNoise.getOctaveNoise(i): octaves are indexed from the highest frequency. */
const JImprovedNoise *jn_perlin_octave(const JPerlinNoise *n, int i);
/* PerlinNoise.wrap. */
double jn_perlin_wrap(double v);
double jn_perlin_value(const JPerlinNoise *n, double x, double y, double z);
double jn_perlin_max_value(const JPerlinNoise *n);

/* NormalNoise: two Perlin samples at slightly different frequencies. */
typedef struct JNormalNoise {
    JPerlinNoise first;
    JPerlinNoise second;
    double value_factor;
    double max_value;
} JNormalNoise;

bool jn_normal_init(JNormalNoise *n, JrRandom *r, int first_octave, const double *amplitudes, int count);
double jn_normal_value(const JNormalNoise *n, double x, double y, double z);
double jn_normal_max_value(const JNormalNoise *n);
