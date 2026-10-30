#include "test_main.h"

#include "game/game_rules.h"
#include "game/pvp.h"
#include "math/mmath.h"
#include "world/world_meta.h"

#include <math.h>
#include <string.h>

/* PvP melee hit detection: struck within reach. */
int test_pvp_hit_in_range(void)
{
    int failures = 0;
    Vec3 eye = mmath_vec3(0.0f, 1.62f, 0.0f);
    Vec3 dir = mmath_vec3(0.0f, -0.1f, -1.0f);
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    dir = mmath_vec3(dir.x / len, dir.y / len, dir.z / len);
    /* Victim 2.5 blocks ahead, feet on the ground. */
    Vec3 victim = mmath_vec3(0.0f, 0.0f, -2.5f);
    float dist = 0.0f;
    TEST_ASSERT(pvp_ray_hits_player(eye, dir, PVP_REACH, victim, PVP_TARGET_WIDTH,
                                    PVP_TARGET_HEIGHT_STAND, &dist));
    TEST_ASSERT(dist > 0.0f && dist <= PVP_REACH);
    return failures;
}

/* Beyond reach: no hit (prevents cross-map strikes). */
int test_pvp_miss_beyond_range(void)
{
    int failures = 0;
    Vec3 eye = mmath_vec3(0.0f, 1.62f, 0.0f);
    Vec3 dir = mmath_vec3(0.0f, 0.0f, -1.0f);
    Vec3 victim = mmath_vec3(0.0f, 0.0f, -10.0f);
    TEST_ASSERT(!pvp_ray_hits_player(eye, dir, PVP_REACH, victim, PVP_TARGET_WIDTH,
                                     PVP_TARGET_HEIGHT_STAND, NULL));
    return failures;
}

/* Aiming away from the victim misses (orientation matters). */
int test_pvp_miss_wrong_direction(void)
{
    int failures = 0;
    Vec3 eye = mmath_vec3(0.0f, 1.62f, 0.0f);
    Vec3 dir = mmath_vec3(0.0f, 0.0f, 1.0f);
    Vec3 victim = mmath_vec3(0.0f, 0.0f, -2.0f);
    TEST_ASSERT(!pvp_ray_hits_player(eye, dir, PVP_REACH, victim, PVP_TARGET_WIDTH,
                                     PVP_TARGET_HEIGHT_STAND, NULL));
    return failures;
}

/* Sneaking victims use the shorter hitbox (stand reaches, sneak ducks). */
int test_pvp_sneak_hitbox(void)
{
    int failures = 0;
    /* Eye at 1.62 looking slightly down at a victim 2 blocks away. */
    Vec3 eye = mmath_vec3(0.0f, 1.62f, 0.0f);
    Vec3 dir = mmath_vec3(0.0f, -0.35f, -1.0f);
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    dir = mmath_vec3(dir.x / len, dir.y / len, dir.z / len);
    Vec3 victim = mmath_vec3(0.0f, 0.0f, -2.0f);
    /* A high ray over a sneaking victim's head must miss. */
    Vec3 high_eye = mmath_vec3(0.0f, 3.2f, 0.0f);
    Vec3 flat = mmath_vec3(0.0f, 0.0f, -1.0f);
    TEST_ASSERT(!pvp_ray_hits_player(high_eye, flat, PVP_REACH, victim, PVP_TARGET_WIDTH,
                                     PVP_TARGET_HEIGHT_SNEAK, NULL));
    /* Same geometry against the standing height hits. */
    TEST_ASSERT(pvp_ray_hits_player(eye, dir, PVP_REACH, victim, PVP_TARGET_WIDTH,
                                    PVP_TARGET_HEIGHT_STAND, NULL));
    return failures;
}

/* Authoritative validation: exactly-once, gated, range-checked. */
int test_pvp_validate_strike(void)
{
    int failures = 0;
    uint32_t last = 0;
    /* Valid first strike. */
    TEST_ASSERT(pvp_validate_strike(true, true, true, 2, 3, 5.0f, 2.0f, 1, &last));
    TEST_ASSERT(last == 1);
    /* Duplicate/replayed sequence rejected (retransmission safe). */
    TEST_ASSERT(!pvp_validate_strike(true, true, true, 2, 3, 5.0f, 2.0f, 1, &last));
    TEST_ASSERT(last == 1);
    /* Fresh sequence accepted. */
    TEST_ASSERT(pvp_validate_strike(true, true, true, 2, 3, 5.0f, 2.0f, 2, &last));
    /* PvP-disabled worlds reject. */
    TEST_ASSERT(!pvp_validate_strike(false, true, true, 2, 3, 5.0f, 2.0f, 3, &last));
    /* Dead attacker or victim rejected. */
    TEST_ASSERT(!pvp_validate_strike(true, false, true, 2, 3, 5.0f, 2.0f, 4, &last));
    TEST_ASSERT(!pvp_validate_strike(true, true, false, 2, 3, 5.0f, 2.0f, 5, &last));
    /* Self-hit rejected. */
    TEST_ASSERT(!pvp_validate_strike(true, true, true, 2, 2, 5.0f, 2.0f, 6, &last));
    /* Out-of-range rejected. */
    TEST_ASSERT(!pvp_validate_strike(true, true, true, 2, 3, 5.0f, 9.0f, 7, &last));
    /* Non-positive damage rejected. */
    TEST_ASSERT(!pvp_validate_strike(true, true, true, 2, 3, 0.0f, 2.0f, 8, &last));
    /* Invalid target IDs are rejected by callers: NULL seq tracking still
     * validates the rest (no crash, correct verdict). */
    TEST_ASSERT(pvp_validate_strike(true, true, true, 2, 99, 5.0f, 2.0f, 9, NULL));
    TEST_ASSERT(!pvp_validate_strike(false, true, true, 2, 99, 5.0f, 2.0f, 10, NULL));
    return failures;
}

/* Sequence dedup core: strictly increasing, 0 never valid, epoch
 * rollover owned by the caller. */
int test_pvp_seq_accept(void)
{
    int failures = 0;
    uint32_t last = 0;
    TEST_ASSERT(pvp_seq_accept(1, &last) == true && last == 1);
    TEST_ASSERT(pvp_seq_accept(1, &last) == false && last == 1);
    TEST_ASSERT(pvp_seq_accept(0, &last) == false && last == 1);
    TEST_ASSERT(pvp_seq_accept(2, &last) == true && last == 2);
    TEST_ASSERT(pvp_seq_accept(100, NULL) == true);
    TEST_ASSERT(pvp_seq_accept(0, NULL) == false);
    /* Epoch rollover: caller resets after UINT32_MAX, stream resumes. */
    last = UINT32_MAX;
    TEST_ASSERT(pvp_seq_accept(1, &last) == false);
    last = 0;
    TEST_ASSERT(pvp_seq_accept(1, &last) == true && last == 1);
    return failures;
}

/* Client-supplied arrow damage is capped at the strongest bow shot. */
int test_pvp_clamp_arrow(void)
{
    int failures = 0;
    TEST_ASSERT_FLOAT_EQ(pvp_clamp_arrow_damage(4.0f), 4.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(pvp_clamp_arrow_damage(6.0f), 6.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(pvp_clamp_arrow_damage(20.0f), 6.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(pvp_clamp_arrow_damage(0.0f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(pvp_clamp_arrow_damage(-2.0f), 0.0f, 1e-6f);
    return failures;
}

/* Wrap-aware angle interpolation takes the short arc. */
int test_pvp_lerp_angle(void)
{
    int failures = 0;
    TEST_ASSERT_FLOAT_EQ(pvp_lerp_angle(0.0f, 1.0f, 0.5f), 0.5f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(pvp_lerp_angle(0.0f, 1.0f, 0.0f), 0.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(pvp_lerp_angle(0.0f, 1.0f, 1.0f), 1.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(pvp_lerp_angle(0.0f, 1.0f, -1.0f), 0.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(pvp_lerp_angle(0.0f, 1.0f, 2.0f), 1.0f, 1e-5f);
    /* Across the seam the short way: 3.0 -> -3.0 passes through PI. */
    {
        float mid = pvp_lerp_angle(3.0f, -3.0f, 0.5f);
        TEST_ASSERT(mid > 3.1f && mid <= 3.14159265f + 1e-5f);
    }
    {
        float mid = pvp_lerp_angle(-3.0f, 3.0f, 0.5f);
        TEST_ASSERT(mid < -3.1f && mid >= -3.14159265f - 1e-5f);
    }
    return failures;
}

/* Client-supplied damage is clamped (never trust the wire). */
int test_pvp_clamp_damage(void)
{
    int failures = 0;
    TEST_ASSERT_FLOAT_EQ(pvp_clamp_damage(5.0f), 5.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(pvp_clamp_damage(0.0f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(pvp_clamp_damage(-3.0f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(pvp_clamp_damage(1000.0f), 20.0f, 1e-6f);
    return failures;
}
