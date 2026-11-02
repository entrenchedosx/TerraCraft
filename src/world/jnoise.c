#include "world/jnoise.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Java never fuses a*b+c; keep the C compilers from doing so. */
#if defined(_MSC_VER)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

/* SimplexNoise.GRADIENT (the table ImprovedNoise.gradDot indexes). */
static const int GRAD[16][3] = {
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
    {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0},  {0, -1, 1}, {-1, 1, 0}, {0, -1, -1},
};

static int32_t mth_floor(double v)
{
    int32_t i = (int32_t)v;
    return v < (double)i ? i - 1 : i;
}

static int64_t mth_lfloor(double v)
{
    int64_t i = (int64_t)v;
    return v < (double)i ? i - 1 : i;
}

static double mth_lerp(double d, double s, double e)
{
    return s + d * (e - s);
}

static double mth_lerp2(double a, double b, double c, double d, double e, double f)
{
    return mth_lerp(b, mth_lerp(a, c, d), mth_lerp(a, e, f));
}

static double mth_lerp3(double a, double b, double c, double d000, double d100, double d010, double d110,
                        double d001, double d101, double d011, double d111)
{
    return mth_lerp(c, mth_lerp2(a, b, d000, d100, d010, d110), mth_lerp2(a, b, d001, d101, d011, d111));
}

static double mth_smoothstep(double x)
{
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

void jn_improved_init(JImprovedNoise *n, JrRandom *r)
{
    n->xo = jr_next_double(r) * 256.0;
    n->yo = jr_next_double(r) * 256.0;
    n->zo = jr_next_double(r) * 256.0;
    for (int i = 0; i < 256; ++i) {
        n->p[i] = (uint8_t)i;
    }
    for (int i = 0; i < 256; ++i) {
        int j = jr_next_int_bound(r, 256 - i);
        uint8_t b = n->p[i];
        n->p[i] = n->p[i + j];
        n->p[i + j] = b;
    }
}

static int perm(const JImprovedNoise *n, int i)
{
    return n->p[i & 0xFF];
}

static double grad_dot(int hash, double x, double y, double z)
{
    const int *g = GRAD[hash & 15];
    return (double)g[0] * x + (double)g[1] * y + (double)g[2] * z;
}

static double sample_and_lerp(const JImprovedNoise *n, int gx, int gy, int gz, double dx, double wdy, double dz,
                              double dy)
{
    int i = perm(n, gx);
    int j = perm(n, gx + 1);
    int k = perm(n, i + gy);
    int l = perm(n, i + gy + 1);
    int m = perm(n, j + gy);
    int nn = perm(n, j + gy + 1);
    double d = grad_dot(perm(n, k + gz), dx, wdy, dz);
    double e = grad_dot(perm(n, m + gz), dx - 1.0, wdy, dz);
    double f = grad_dot(perm(n, l + gz), dx, wdy - 1.0, dz);
    double g = grad_dot(perm(n, nn + gz), dx - 1.0, wdy - 1.0, dz);
    double h = grad_dot(perm(n, k + gz + 1), dx, wdy, dz - 1.0);
    double o = grad_dot(perm(n, m + gz + 1), dx - 1.0, wdy, dz - 1.0);
    double q = grad_dot(perm(n, l + gz + 1), dx, wdy - 1.0, dz - 1.0);
    double r = grad_dot(perm(n, nn + gz + 1), dx - 1.0, wdy - 1.0, dz - 1.0);
    double s = mth_smoothstep(dx);
    double t = mth_smoothstep(dy);
    double u = mth_smoothstep(dz);
    return mth_lerp3(s, t, u, d, e, f, g, h, o, q, r);
}

double jn_improved_noise_fudge(const JImprovedNoise *n, double x, double y, double z, double y_scale,
                               double y_fudge)
{
    double d = x + n->xo;
    double e = y + n->yo;
    double f = z + n->zo;
    int i = mth_floor(d);
    int j = mth_floor(e);
    int k = mth_floor(f);
    double g = d - (double)i;
    double h = e - (double)j;
    double l = f - (double)k;
    double o;
    if (y_scale != 0.0) {
        double m = (y_fudge >= 0.0 && y_fudge < h) ? y_fudge : h;
        o = (double)mth_floor(m / y_scale + (double)1.0E-7f) * y_scale;
    } else {
        o = 0.0;
    }
    return sample_and_lerp(n, i, j, k, g, h - o, l, h);
}

double jn_improved_noise(const JImprovedNoise *n, double x, double y, double z)
{
    return jn_improved_noise_fudge(n, x, y, z, 0.0, 0.0);
}

/* ---- PerlinNoise --------------------------------------------------------- */

static double perlin_wrap(double v)
{
    return v - (double)mth_lfloor(v / 3.3554432E7 + 0.5) * 3.3554432E7;
}

static double perlin_edge_value(const JPerlinNoise *n, double noise_value)
{
    double d = 0.0;
    double e = n->lowest_freq_value_factor;
    for (int i = 0; i < n->count; ++i) {
        if (n->present[i]) {
            d += n->amplitudes[i] * noise_value * e;
        }
        e /= 2.0;
    }
    return d;
}

bool jn_perlin_init(JPerlinNoise *n, JrRandom *r, int first_octave, const double *amplitudes, int count)
{
    if (n == NULL || r == NULL || amplitudes == NULL || count < 1 || count > JN_MAX_OCTAVES) {
        return false;
    }
    memset(n, 0, sizeof(*n));
    n->count = count;
    n->first_octave = first_octave;
    JrPositional prf = jr_fork_positional(r);
    for (int k = 0; k < count; ++k) {
        n->amplitudes[k] = amplitudes[k];
        if (amplitudes[k] != 0.0) {
            char name[48];
            snprintf(name, sizeof(name), "octave_%d", first_octave + k);
            JrRandom octave = jr_pos_from_hash_of(&prf, name);
            jn_improved_init(&n->levels[k], &octave);
            n->present[k] = true;
        }
    }
    n->lowest_freq_input_factor = pow(2.0, (double)first_octave);
    n->lowest_freq_value_factor = pow(2.0, (double)(count - 1)) / (pow(2.0, (double)count) - 1.0);
    n->max_value = perlin_edge_value(n, 2.0);
    return true;
}

double jn_perlin_wrap(double v)
{
    return perlin_wrap(v);
}

const JImprovedNoise *jn_perlin_octave(const JPerlinNoise *n, int i)
{
    int idx = n->count - 1 - i;
    if (idx < 0 || idx >= n->count || !n->present[idx]) {
        return NULL;
    }
    return &n->levels[idx];
}

/* RandomSource.consumeCount(262) as used by PerlinNoise.skipOctave. */
static void skip_octave(JrRandom *r)
{
    for (int i = 0; i < 262; ++i) {
        (void)jr_next_long(r);
    }
}

bool jn_perlin_init_legacy(JPerlinNoise *n, JrRandom *r, int first_octave, const double *amplitudes, int count)
{
    if (n == NULL || r == NULL || amplitudes == NULL || count < 1 || count > JN_MAX_OCTAVES) {
        return false;
    }
    memset(n, 0, sizeof(*n));
    n->count = count;
    n->first_octave = first_octave;
    for (int k = 0; k < count; ++k) {
        n->amplitudes[k] = amplitudes[k];
    }
    int j = -first_octave;
    JImprovedNoise zero;
    jn_improved_init(&zero, r); /* always consumed first */
    if (j >= 0 && j < count && amplitudes[j] != 0.0) {
        n->levels[j] = zero;
        n->present[j] = true;
    }
    for (int k = j - 1; k >= 0; --k) {
        if (k < count && amplitudes[k] != 0.0) {
            jn_improved_init(&n->levels[k], r);
            n->present[k] = true;
        } else {
            skip_octave(r);
        }
    }
    if (j < count - 1) {
        return false; /* "Positive octaves are temporarily disabled" */
    }
    n->lowest_freq_input_factor = pow(2.0, (double)first_octave);
    n->lowest_freq_value_factor = pow(2.0, (double)(count - 1)) / (pow(2.0, (double)count) - 1.0);
    n->max_value = perlin_edge_value(n, 2.0);
    return true;
}

double jn_perlin_value(const JPerlinNoise *n, double x, double y, double z)
{
    double d = 0.0;
    double e = n->lowest_freq_input_factor;
    double f = n->lowest_freq_value_factor;
    for (int i = 0; i < n->count; ++i) {
        if (n->present[i]) {
            double g = jn_improved_noise_fudge(&n->levels[i], perlin_wrap(x * e), perlin_wrap(y * e),
                                               perlin_wrap(z * e), 0.0 * e, 0.0 * e);
            d += n->amplitudes[i] * g * f;
        }
        e *= 2.0;
        f /= 2.0;
    }
    return d;
}

double jn_perlin_max_value(const JPerlinNoise *n)
{
    return n->max_value;
}

/* ---- NormalNoise --------------------------------------------------------- */

#define NORMAL_INPUT_FACTOR 1.0181268882175227
#define NORMAL_TARGET_DEVIATION 0.3333333333333333

static double normal_expected_deviation(int octaves)
{
    return 0.1 * (1.0 + 1.0 / (double)(octaves + 1));
}

bool jn_normal_init(JNormalNoise *n, JrRandom *r, int first_octave, const double *amplitudes, int count)
{
    if (n == NULL) {
        return false;
    }
    if (!jn_perlin_init(&n->first, r, first_octave, amplitudes, count) ||
        !jn_perlin_init(&n->second, r, first_octave, amplitudes, count)) {
        return false;
    }
    int lo = 2147483647;
    int hi = -2147483647 - 1;
    for (int i = 0; i < count; ++i) {
        if (amplitudes[i] != 0.0) {
            if (i < lo) {
                lo = i;
            }
            if (i > hi) {
                hi = i;
            }
        }
    }
    n->value_factor = 0.16666666666666666 / normal_expected_deviation(hi - lo);
    n->max_value = (n->first.max_value + n->second.max_value) * n->value_factor;
    (void)NORMAL_TARGET_DEVIATION;
    return true;
}

double jn_normal_value(const JNormalNoise *n, double x, double y, double z)
{
    double d = x * NORMAL_INPUT_FACTOR;
    double e = y * NORMAL_INPUT_FACTOR;
    double f = z * NORMAL_INPUT_FACTOR;
    return (jn_perlin_value(&n->first, x, y, z) + jn_perlin_value(&n->second, d, e, f)) * n->value_factor;
}

double jn_normal_max_value(const JNormalNoise *n)
{
    return n->max_value;
}
