#include "game/time_system.h"

#include <math.h>
#include <stddef.h>

/* Keyframed sky track: {progress, r, g, b}. Must start at 0 and end at 1
 * with matching endpoints for seamless wrap. */
static const float SKY_KEYS[][4] = {
    {0.00f, 0.030f, 0.040f, 0.100f}, /* Midnight. */
    {0.20f, 0.030f, 0.040f, 0.100f}, /* Late night. */
    {0.27f, 0.960f, 0.600f, 0.400f}, /* Dawn. */
    {0.35f, 0.529f, 0.808f, 0.922f}, /* Morning (M1 sky). */
    {0.65f, 0.529f, 0.808f, 0.922f}, /* Afternoon. */
    {0.73f, 0.950f, 0.450f, 0.300f}, /* Dusk. */
    {0.80f, 0.030f, 0.040f, 0.100f}, /* Nightfall. */
    {1.00f, 0.030f, 0.040f, 0.100f}, /* Midnight (wrap). */
};
#define SKY_KEY_COUNT 8

/* Keyframed intensity track: {progress, value}. */
static const float INT_KEYS[][2] = {
    {0.00f, TIME_MIN_INTENSITY},
    {0.22f, TIME_MIN_INTENSITY},
    {0.30f, 0.900f},
    {0.40f, 1.000f},
    {0.60f, 1.000f},
    {0.72f, 0.700f},
    {0.80f, TIME_MIN_INTENSITY},
    {1.00f, TIME_MIN_INTENSITY},
};
#define INT_KEY_COUNT 8

/* Wrap progress into [0,1). */
static float wrap01(float p)
{
    p = fmodf(p, 1.0f);
    if (p < 0.0f) {
        p += 1.0f;
    }
    return p;
}

/* Locate the segment [keys[i], keys[i+1]] containing p and return the
 * fractional position. Keys must span [0,1]. */
static float segment(const float *progs, int n, float p, int *out_i)
{
    for (int i = 0; i < n - 1; ++i) {
        if (p <= progs[i + 1] || i == n - 2) {
            if (out_i != NULL) {
                *out_i = i;
            }
            float span = progs[i + 1] - progs[i];
            if (span <= 0.0f) {
                return 0.0f;
            }
            float f = (p - progs[i]) / span;
            if (f < 0.0f) {
                f = 0.0f;
            }
            if (f > 1.0f) {
                f = 1.0f;
            }
            return f;
        }
    }
    if (out_i != NULL) {
        *out_i = 0;
    }
    return 0.0f;
}

/* Initialise the clock.
 *
 * Args:
 *   t: system.
 *   start_progress: initial phase.
 *   speed: cycles per second.
 */
void time_system_init(TimeSystem *t, float start_progress, float speed)
{
    if (t == NULL) {
        return;
    }
    t->day_progress = wrap01(start_progress);
    t->speed = speed < 0.0f ? 0.0f : speed;
}

/* Advance the clock.
 *
 * Args:
 *   t: system.
 *   dt: seconds.
 */
void time_system_update(TimeSystem *t, float dt)
{
    if (t == NULL || dt <= 0.0f || t->speed <= 0.0f) {
        return;
    }
    t->day_progress = wrap01(t->day_progress + dt * t->speed);
}

/* Interpolated sky color.
 *
 * Args:
 *   t: system.
 *
 * Returns: RGB sky.
 */
Vec3 time_get_sky_color(const TimeSystem *t)
{
    if (t == NULL) {
        return mmath_vec3(0.529f, 0.808f, 0.922f);
    }
    float progs[SKY_KEY_COUNT];
    for (int i = 0; i < SKY_KEY_COUNT; ++i) {
        progs[i] = SKY_KEYS[i][0];
    }
    int idx = 0;
    float f = segment(progs, SKY_KEY_COUNT, t->day_progress, &idx);
    float r = SKY_KEYS[idx][1] + (SKY_KEYS[idx + 1][1] - SKY_KEYS[idx][1]) * f;
    float g = SKY_KEYS[idx][2] + (SKY_KEYS[idx + 1][2] - SKY_KEYS[idx][2]) * f;
    float b = SKY_KEYS[idx][3] + (SKY_KEYS[idx + 1][3] - SKY_KEYS[idx][3]) * f;
    return mmath_vec3(r, g, b);
}

/* Sun direction (unit, east rise -> zenith -> west set).
 *
 * Args:
 *   t: system.
 *
 * Returns: normalized sun direction.
 */
Vec3 time_get_sun_dir(const TimeSystem *t)
{
    if (t == NULL) {
        return mmath_vec3(-0.45f, 0.85f, 0.30f);
    }
    float theta = (t->day_progress - 0.25f) * 2.0f * MMATH_PI;
    Vec3 d = mmath_vec3(cosf(theta), sinf(theta), 0.30f);
    return mmath_vec3_normalize(d);
}

/* Global light multiplier.
 *
 * Args:
 *   t: system.
 *
 * Returns: intensity in [MIN, 1].
 */
float time_get_light_intensity(const TimeSystem *t)
{
    if (t == NULL) {
        return 1.0f;
    }
    float progs[INT_KEY_COUNT];
    for (int i = 0; i < INT_KEY_COUNT; ++i) {
        progs[i] = INT_KEYS[i][0];
    }
    int idx = 0;
    float f = segment(progs, INT_KEY_COUNT, t->day_progress, &idx);
    float v = INT_KEYS[idx][1] + (INT_KEYS[idx + 1][1] - INT_KEYS[idx][1]) * f;
    if (v < TIME_MIN_INTENSITY) {
        v = TIME_MIN_INTENSITY;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return v;
}
