#include "game/time_system.h"
#include <math.h>
#include <stddef.h>

static float wrap01(float p)
{
    p = fmodf(p, 1.0f);
    return p < 0 ? p + 1 : p;
}

void time_system_init(TimeSystem *t, float start, float speed)
{
    if (!t)
        return;
    t->day_progress = wrap01(start);
    t->speed = speed < 0 ? 0 : speed;
}

void time_system_update(TimeSystem *t, float dt)
{
    if (t && dt > 0 && t->speed > 0)
        t->day_progress = wrap01(t->day_progress + dt * t->speed);
}

/* Release 1.21.11 data/minecraft/timeline/day.json. Cyclic tracks are
 * linearly interpolated; celestial angles use the supplied cubic Bezier. */
static float day_ticks(const TimeSystem *t)
{
    return wrap01((t ? t->day_progress : 0.5f) - 0.25f) * 24000;
}

static float day_track(const TimeSystem *t, float dawn_end, float dusk_start, float dusk_end, float dawn_start)
{
    float tick = day_ticks(t);
    if (tick >= dawn_end && tick <= dusk_start)
        return 1;
    if (tick > dusk_start && tick < dusk_end)
        return 1 - (tick - dusk_start) / (dusk_end - dusk_start);
    if (tick >= dusk_end && tick <= dawn_start)
        return 0;
    if (tick < dawn_end)
        tick += 24000;
    return (tick - dawn_start) / (24000 + dawn_end - dawn_start);
}

static float bezier(float t, float a, float b)
{
    float u = 1 - t;
    return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
}

Vec3 time_get_sun_dir(const TimeSystem *t)
{
    float fraction = wrap01((t ? t->day_progress : 0.5f) - 0.5f);
    float low = 0, high = 1;
    for (int i = 0; i < 20; ++i) {
        float middle = (low + high) * 0.5f;
        if (bezier(middle, 0.362f, 0.638f) < fraction)
            low = middle;
        else
            high = middle;
    }
    float angle = bezier((low + high) * 0.5f, 0.241f, 0.759f) * 2 * MMATH_PI;
    return mmath_vec3(-sinf(angle), cosf(angle), 0);
}

float time_get_daylight(const TimeSystem *t)
{
    return day_track(t, 133, 11867, 13670, 22330);
}

Vec3 time_get_sky_color(const TimeSystem *t)
{
    float f = time_get_daylight(t);
    return mmath_vec3(120.0f / 255 * f, 167.0f / 255 * f, f);
}

float time_get_light_intensity(const TimeSystem *t)
{
    return TIME_MIN_INTENSITY + (1 - TIME_MIN_INTENSITY) * day_track(t, 730, 11270, 13140, 22860);
}

Vec3 time_get_fog_multiplier(const TimeSystem *t)
{
    float f = time_get_daylight(t);
    return mmath_vec3((15 + 240 * f) / 255, (15 + 240 * f) / 255, (22 + 233 * f) / 255);
}

Vec3 time_get_cloud_color(const TimeSystem *t)
{
    float f = time_get_daylight(t);
    return mmath_vec3((25 + 230 * f) / 255, (25 + 230 * f) / 255, (38 + 217 * f) / 255);
}

float time_get_star_brightness(const TimeSystem *t)
{
    static const float keys[][2] = {{92, 0.037f},    {627, 0},        {11373, 0},      {11732, 0.016f},
                                    {11959, 0.044f}, {12399, 0.143f}, {12729, 0.258f}, {13228, 0.5f},
                                    {22772, 0.5f},   {23032, 0.364f}, {23356, 0.225f}, {23758, 0.101f}};
    float tick = day_ticks(t);
    for (int i = 0; i < 12; ++i) {
        int j = (i + 1) % 12;
        float a = keys[i][0], b = keys[j][0] + (j == 0 ? 24000 : 0);
        float x = tick;
        if (i == 11 && x < a)
            x += 24000;
        if (x >= a && x <= b)
            return keys[i][1] + (keys[j][1] - keys[i][1]) * (x - a) / (b - a);
    }
    return 0;
}
