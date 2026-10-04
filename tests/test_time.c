#include "test_main.h"

#include "game/time_system.h"
#include "math/mmath.h"

/* Test: keyframe sky colors and intensity ranges.
 *
 * Returns: failure count.
 */
int test_time_phases(void)
{
    int failures = 0;
    TimeSystem t;

    time_system_init(&t, 0.5f, TIME_DEFAULT_SPEED);
    Vec3 noon = time_get_sky_color(&t);
    TEST_ASSERT_FLOAT_EQ(noon.x, 0.529f, 0.02f);
    TEST_ASSERT_FLOAT_EQ(noon.y, 0.808f, 0.02f);
    TEST_ASSERT_FLOAT_EQ(noon.z, 0.922f, 0.02f);
    TEST_ASSERT_FLOAT_EQ(time_get_light_intensity(&t), 1.0f, 1e-5f);

    time_system_init(&t, 0.0f, 0.0f);
    Vec3 night = time_get_sky_color(&t);
    TEST_ASSERT(night.x < 0.1f && night.y < 0.1f && night.z < 0.2f);
    TEST_ASSERT_FLOAT_EQ(time_get_light_intensity(&t), TIME_MIN_INTENSITY, 1e-5f);

    /* Dawn is warm (red dominates), dusk too. */
    time_system_init(&t, 0.27f, 0.0f);
    Vec3 dawn = time_get_sky_color(&t);
    TEST_ASSERT(dawn.x > dawn.y && dawn.x > dawn.z);
    time_system_init(&t, 0.73f, 0.0f);
    Vec3 dusk = time_get_sky_color(&t);
    TEST_ASSERT(dusk.x > dusk.z);

    /* NULL-safe fallbacks. */
    Vec3 fallback = time_get_sky_color(NULL);
    TEST_ASSERT_FLOAT_EQ(fallback.x, 0.529f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(time_get_light_intensity(NULL), 1.0f, 1e-5f);
    return failures;
}

/* Test: sun direction is unit, rises/sets correctly, continuous over a day.
 *
 * Returns: failure count.
 */
int test_time_sun(void)
{
    int failures = 0;
    TimeSystem t;

    /* Zenith at noon (straight up, z-tilt aside). */
    time_system_init(&t, 0.5f, 0.0f);
    Vec3 noon = time_get_sun_dir(&t);
    TEST_ASSERT_FLOAT_EQ(mmath_vec3_length(noon), 1.0f, 1e-5f);
    TEST_ASSERT(noon.y > 0.9f);

    /* East horizon at sunrise, west at sunset. */
    time_system_init(&t, 0.25f, 0.0f);
    Vec3 rise = time_get_sun_dir(&t);
    TEST_ASSERT(rise.x > 0.9f);
    TEST_ASSERT_FLOAT_EQ(rise.y, 0.0f, 0.05f);
    time_system_init(&t, 0.75f, 0.0f);
    Vec3 set = time_get_sun_dir(&t);
    TEST_ASSERT(set.x < -0.9f);

    /* Below horizon at midnight (diffuse goes to 0, ambient remains). */
    time_system_init(&t, 0.0f, 0.0f);
    Vec3 mid = time_get_sun_dir(&t);
    TEST_ASSERT(mid.y < 0.0f);

    /* Continuity: max per-step direction delta over a full day is small. */
    time_system_init(&t, 0.0f, 1.0f / 360.0f);
    Vec3 prev = time_get_sun_dir(&t);
    float max_jump = 0.0f;
    for (int i = 0; i < 360; ++i) {
        time_system_update(&t, 1.0f);
        Vec3 cur = time_get_sun_dir(&t);
        float d = mmath_vec3_length(mmath_vec3_sub(cur, prev));
        if (d > max_jump) {
            max_jump = d;
        }
        prev = cur;
        Vec3 sky = time_get_sky_color(&t);
        TEST_ASSERT(sky.x >= 0.0f && sky.x <= 1.0f);
        float inten = time_get_light_intensity(&t);
        TEST_ASSERT(inten >= TIME_MIN_INTENSITY && inten <= 1.0f);
    }
    TEST_ASSERT(max_jump < 0.05f);
    return failures;
}

/* Test: update wraps and clamps; init wraps start progress.
 *
 * Returns: failure count.
 */
int test_time_wrap(void)
{
    int failures = 0;
    TimeSystem t;
    time_system_init(&t, 1.2f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(t.day_progress, 0.2f, 1e-5f);
    time_system_init(&t, 0.9f, 0.5f);
    time_system_update(&t, 0.4f); /* 0.9 + 0.2 = 1.1 -> 0.1. */
    TEST_ASSERT_FLOAT_EQ(t.day_progress, 0.1f, 1e-4f);

    /* Paused clock (speed 0) never advances; negative speed clamps. */
    time_system_init(&t, 0.3f, 0.0f);
    time_system_update(&t, 100.0f);
    TEST_ASSERT_FLOAT_EQ(t.day_progress, 0.3f, 1e-6f);
    time_system_init(&t, 0.3f, -5.0f);
    TEST_ASSERT_FLOAT_EQ(t.speed, 0.0f, 1e-6f);

    /* NULL- and negative-dt-safe. */
    time_system_update(NULL, 1.0f);
    time_system_init(&t, 0.4f, 1.0f);
    time_system_update(&t, -1.0f);
    TEST_ASSERT_FLOAT_EQ(t.day_progress, 0.4f, 1e-6f);
    time_system_init(NULL, 0.0f, 0.0f);
    return failures;
}
