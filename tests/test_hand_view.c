#include "test_main.h"

#include "game/hand_view.h"
#include "fixtures/hand_golden.h"

#include <math.h>
#include <string.h>

/* Golden matrices come from tools/reference/HandGolden.java, which replays the
 * vanilla call sequence through the installed game's own PoseStack/Axis/Mth.
 * Tolerance: 1e-4 absolute (float accumulation across ~10 matrix products;
 * rotations differ only in sin/cos evaluation precision). */
#define HAND_TOL 1e-4f

static float max_diff(const Mat4 *a, const float *b)
{
    float d = 0.0f;
    for (int i = 0; i < 16; ++i) {
        float e = fabsf(a->m[i] - b[i]);
        if (e > d) {
            d = e;
        }
    }
    return d;
}

/* A hand state that makes the frame's sway zero and pins swing/equip. */
static void pin_state(HandView *hv, float swing, float equipped)
{
    hand_view_init(hv);
    hv->have_rot = true;
    hv->yaw_unwrapped = 0.0f;
    hv->o_attack_anim = hv->attack_anim = swing;
    hv->o_main_hand_height = hv->main_hand_height = 1.0f - equipped;
}

int test_hand_golden_arm_matrices(void)
{
    int failures = 0;
    size_t n = sizeof(HAND_GOLDEN_ARM) / sizeof(HAND_GOLDEN_ARM[0]);
    for (size_t i = 0; i < n; ++i) {
        HandView hv;
        HandViewFrame fr;
        pin_state(&hv, HAND_GOLDEN_ARM[i].swing, HAND_GOLDEN_ARM[i].equipped);
        hand_view_frame(&hv, 0.0f, 0.0f, 0.0f, HAND_ITEM_NONE, NULL, &fr);
        float d = max_diff(&fr.arm, HAND_GOLDEN_ARM[i].m);
        if (d > HAND_TOL) {
            printf("  arm case %zu (swing %.4f equip %.2f) max diff %g\n", i,
                   (double)HAND_GOLDEN_ARM[i].swing, (double)HAND_GOLDEN_ARM[i].equipped, (double)d);
        }
        TEST_ASSERT(d <= HAND_TOL);
    }
    Mat4 part = hand_arm_part_matrix();
    TEST_ASSERT(max_diff(&part, HAND_GOLDEN_ARM_PART) <= HAND_TOL);
    return failures;
}

int test_hand_golden_item_matrices(void)
{
    int failures = 0;
    size_t n = sizeof(HAND_GOLDEN_ITEM) / sizeof(HAND_GOLDEN_ITEM[0]);
    Mat4 disp_sprite = hand_item_display_matrix(HAND_ITEM_SPRITE);
    Mat4 disp_block = hand_item_display_matrix(HAND_ITEM_BLOCK);
    TEST_ASSERT(max_diff(&disp_sprite, HAND_GOLDEN_DISPLAY_SPRITE) <= HAND_TOL);
    TEST_ASSERT(max_diff(&disp_block, HAND_GOLDEN_DISPLAY_BLOCK) <= HAND_TOL);
    for (size_t i = 0; i < n; ++i) {
        HandView hv;
        HandViewFrame fr;
        pin_state(&hv, HAND_GOLDEN_ITEM[i].swing, HAND_GOLDEN_ITEM[i].equipped);
        hand_view_frame(&hv, 0.0f, 0.0f, 0.0f, HAND_ITEM_SPRITE, NULL, &fr);
        /* frame.item = swing chain * display matrix; strip the display part. */
        Mat4 expect = mmath_mat4_mul(*(const Mat4 *)HAND_GOLDEN_ITEM[i].m, disp_sprite);
        float d = max_diff(&fr.item, expect.m);
        if (d > HAND_TOL) {
            printf("  item case %zu max diff %g\n", i, (double)d);
        }
        TEST_ASSERT(d <= HAND_TOL);
    }
    return failures;
}

int test_hand_golden_eat_and_bob(void)
{
    int failures = 0;
    size_t n = sizeof(HAND_GOLDEN_EAT) / sizeof(HAND_GOLDEN_EAT[0]);
    Mat4 disp = hand_item_display_matrix(HAND_ITEM_SPRITE);
    for (size_t i = 0; i < n; ++i) {
        const HandGoldenEat *g = &HAND_GOLDEN_EAT[i];
        HandView hv;
        HandViewFrame fr;
        pin_state(&hv, 0.0f, g->equipped);
        HandViewUse use = {HAND_USE_EAT, g->remaining, g->duration};
        hand_view_frame(&hv, g->partial, 0.0f, 0.0f, HAND_ITEM_SPRITE, &use, &fr);
        Mat4 expect = mmath_mat4_mul(*(const Mat4 *)g->m, disp);
        float d = max_diff(&fr.item, expect.m);
        if (d > HAND_TOL) {
            printf("  eat case %zu max diff %g\n", i, (double)d);
        }
        TEST_ASSERT(d <= HAND_TOL);
    }
    size_t nb = sizeof(HAND_GOLDEN_BOB) / sizeof(HAND_GOLDEN_BOB[0]);
    for (size_t i = 0; i < nb; ++i) {
        const HandGoldenBob *g = &HAND_GOLDEN_BOB[i];
        HandView hv;
        hand_view_init(&hv);
        hv.walk_dist = g->walk;
        hv.walk_dist_o = g->walk_o;
        hv.o_bob = g->o_bob;
        hv.bob = g->bob;
        Mat4 m = hand_view_bob_matrix(&hv, g->partial, true);
        float d = max_diff(&m, g->m);
        if (d > HAND_TOL) {
            printf("  bob case %zu max diff %g\n", i, (double)d);
        }
        TEST_ASSERT(d <= HAND_TOL);
        Mat4 off = hand_view_bob_matrix(&hv, g->partial, false);
        Mat4 id = mmath_mat4_identity();
        TEST_ASSERT(max_diff(&off, id.m) == 0.0f);
    }
    return failures;
}

/* Feed `seconds` of simulation in steps of `dt`. */
static void run(HandView *hv, const HandViewSample *s, float seconds, float dt)
{
    int steps = (int)lroundf(seconds / dt);
    for (int i = 0; i < steps; ++i) {
        hand_view_advance(hv, dt, s);
    }
}

static HandViewSample base_sample(void)
{
    HandViewSample s;
    memset(&s, 0, sizeof(s));
    s.held_key = 7;
    s.on_ground = true;
    s.attack_delay_ticks = 5.0f;
    return s;
}

int test_hand_swing_timeline(void)
{
    int failures = 0;
    HandView hv;
    HandViewSample s = base_sample();
    hand_view_init(&hv);
    hand_view_advance(&hv, 0.0f, &s);
    TEST_ASSERT(hand_view_swing(&hv));
    /* Vanilla: swingTime = -1, then 0,1,2,...,5, then reset. attackAnim =
     * swingTime / 6 after each of the following 20 Hz ticks. */
    const float expect[7] = {0.0f, 1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f, 4.0f / 6.0f, 5.0f / 6.0f, 0.0f};
    for (int t = 0; t < 7; ++t) {
        hand_view_advance(&hv, HAND_TICK_SECONDS, &s);
        TEST_ASSERT_FLOAT_EQ(hv.attack_anim, expect[t], 1e-6f);
    }
    TEST_ASSERT(!hv.swinging);
    /* getAttackAnim wraps (0 - 5/6 + 1): the last interpolated frame of the
     * swing reaches exactly 1.0 as the state resets to 0. */
    TEST_ASSERT_FLOAT_EQ(hand_view_swing_progress(&hv, 1.0f), 1.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(hand_view_swing_progress(&hv, 0.0f), 5.0f / 6.0f, 1e-6f);
    return failures;
}

int test_hand_swing_retrigger_rule(void)
{
    int failures = 0;
    HandView hv;
    HandViewSample s = base_sample();
    hand_view_init(&hv);
    hand_view_advance(&hv, 0.0f, &s);
    TEST_ASSERT(hand_view_swing(&hv));
    /* swingTime == -1 still satisfies the restart rule (< 0): same state. */
    TEST_ASSERT(hand_view_swing(&hv));
    TEST_ASSERT(hv.swing_time == -1);
    hand_view_advance(&hv, HAND_TICK_SECONDS, &s); /* swingTime 0 */
    TEST_ASSERT(!hand_view_swing(&hv));
    hand_view_advance(&hv, HAND_TICK_SECONDS, &s); /* 1 */
    hand_view_advance(&hv, HAND_TICK_SECONDS, &s); /* 2 */
    TEST_ASSERT(!hand_view_swing(&hv));
    hand_view_advance(&hv, HAND_TICK_SECONDS, &s); /* 3 == duration / 2 */
    TEST_ASSERT(hand_view_swing(&hv));
    TEST_ASSERT(hv.swing_time == -1);
    return failures;
}

int test_hand_equip_transition(void)
{
    int failures = 0;
    HandView hv;
    HandViewSample s = base_sample();
    hand_view_init(&hv);
    hand_view_advance(&hv, 0.0f, &s);
    run(&hv, &s, 1.0f, HAND_TICK_SECONDS);
    TEST_ASSERT_FLOAT_EQ(hv.main_hand_height, 1.0f, 1e-6f);
    TEST_ASSERT(hv.shown_key == 7);
    /* Switch item (1.21.11 bytecode: Player.tick resets itemSwapTicker when
     * the item type changes; the hand rises along getItemSwapScale^3). The old
     * item lowers by at most 0.4 per tick and swaps below 0.1; the new one
     * then rises with scale (ticker + 1) / 5 cubed:
     * 1.0 -> 0.6 -> 0.2 -> 0.0 (swap) -> 0.216 -> 0.512 -> 0.912 -> 1.0. */
    s.held_key = 9;
    const float expect[7] = {0.6f, 0.2f, 0.0f, 0.216f, 0.512f, 0.912f, 1.0f};
    for (int t = 0; t < 7; ++t) {
        hand_view_advance(&hv, HAND_TICK_SECONDS, &s);
        TEST_ASSERT_FLOAT_EQ(hv.main_hand_height, expect[t], 1e-5f);
        if (t == 2) {
            TEST_ASSERT(hv.shown_key == 9);
        } else if (t < 2) {
            TEST_ASSERT(hv.shown_key == 7);
        }
    }
    return failures;
}

int test_hand_attack_cooldown_dip(void)
{
    int failures = 0;
    HandView hv;
    HandViewSample s = base_sample(); /* delay 5 ticks */
    hand_view_init(&hv);
    hand_view_advance(&hv, 0.0f, &s);
    run(&hv, &s, 1.0f, HAND_TICK_SECONDS);

    /* A melee hit (Player.onAttack -> resetOnlyAttackStrengthTicker) restarts
     * the damage cooldown but must NOT dip the hand in 1.21.11. */
    hand_view_reset_attack_strength(&hv);
    for (int t = 0; t < 5; ++t) {
        hand_view_advance(&hv, HAND_TICK_SECONDS, &s);
        TEST_ASSERT_FLOAT_EQ(hv.main_hand_height, 1.0f, 1e-6f);
    }
    TEST_ASSERT_FLOAT_EQ(hand_view_attack_strength(&hv, 0.0f), 1.0f, 1e-6f);

    /* An air swing (Minecraft.startAttack MISS -> resetAttackStrengthTicker)
     * restarts the item-swap ticker too. swapScale = (ticker + 1) / 5 read
     * before the tick's increment: 0.2^3 = 0.008 -> height 0.6; 0.4^3 = 0.064
     * -> 0.2; 0.6^3 = 0.216 -> 0.216; 0.8^3 = 0.512; target 1.0 -> 0.912. */
    hand_view_reset_swap(&hv);
    const float expect[5] = {0.6f, 0.2f, 0.216f, 0.512f, 0.912f};
    for (int t = 0; t < 5; ++t) {
        hand_view_advance(&hv, HAND_TICK_SECONDS, &s);
        TEST_ASSERT_FLOAT_EQ(hv.main_hand_height, expect[t], 1e-5f);
    }
    return failures;
}

int test_hand_tick_rate_independence(void)
{
    int failures = 0;
    const float rates[4] = {1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 144.0f, 1.0f / 240.0f};
    HandView ref;
    memset(&ref, 0, sizeof(ref));
    for (int r = 0; r < 4; ++r) {
        HandView hv;
        HandViewSample s = base_sample();
        hand_view_init(&hv);
        hand_view_advance(&hv, 0.0f, &s);
        hand_view_swing(&hv);
        float dt = rates[r];
        int steps = (int)lroundf(0.4f / dt);
        for (int i = 0; i < steps; ++i) {
            s.step_dist = 0.2158f * dt; /* ~4.317 blocks/s */
            hand_view_advance(&hv, dt, &s);
        }
        /* 0.4 s = 8 ticks regardless of render/sim rate. */
        if (r == 0) {
            ref = hv;
        } else {
            TEST_ASSERT(hv.swing_time == ref.swing_time);
            TEST_ASSERT_FLOAT_EQ(hv.attack_anim, ref.attack_anim, 1e-6f);
            TEST_ASSERT_FLOAT_EQ(hv.main_hand_height, ref.main_hand_height, 1e-6f);
            TEST_ASSERT_FLOAT_EQ(hv.walk_dist, ref.walk_dist, 2e-3f);
        }
    }
    /* Partial tick is monotone within a tick and bounded. */
    HandView hv;
    HandViewSample s = base_sample();
    hand_view_init(&hv);
    hand_view_advance(&hv, 0.0f, &s);
    hand_view_advance(&hv, 0.02f, &s);
    float p0 = hand_view_partial(&hv);
    float p1 = hand_view_partial_ahead(&hv, 0.01f);
    float p2 = hand_view_partial_ahead(&hv, 10.0f);
    TEST_ASSERT_FLOAT_EQ(p0, 0.4f, 1e-5f);
    TEST_ASSERT(p1 > p0);
    TEST_ASSERT_FLOAT_EQ(p2, 1.0f, 0.0f);
    return failures;
}

int test_hand_walk_bob_state(void)
{
    int failures = 0;
    HandView hv;
    HandViewSample s = base_sample();
    hand_view_init(&hv);
    hand_view_advance(&hv, 0.0f, &s);
    /* 0.2158 blocks per tick -> walkDist += 0.2158 * 0.6, bob targets 0.1. */
    for (int t = 0; t < 10; ++t) {
        s.step_dist = 0.2158f;
        hand_view_advance(&hv, HAND_TICK_SECONDS, &s);
    }
    TEST_ASSERT_FLOAT_EQ(hv.walk_dist, 10.0f * 0.2158f * 0.6f, 1e-4f);
    /* bob converges to min(0.1, speed) with factor 0.4 per tick. */
    float expect = 0.1f * (1.0f - powf(0.6f, 10.0f));
    TEST_ASSERT_FLOAT_EQ(hv.bob, expect, 1e-5f);
    /* Airborne/swimming/dead: target 0, bob decays. */
    s.on_ground = false;
    s.step_dist = 0.0f;
    hand_view_advance(&hv, HAND_TICK_SECONDS, &s);
    TEST_ASSERT_FLOAT_EQ(hv.bob, expect * 0.6f, 1e-5f);
    return failures;
}

int test_hand_yaw_wrap_does_not_spike_sway(void)
{
    int failures = 0;
    HandView hv;
    HandViewSample s = base_sample();
    s.yaw_deg = 179.0f;
    hand_view_init(&hv);
    hand_view_advance(&hv, 0.0f, &s);
    s.yaw_deg = -179.0f; /* crossed the +-180 seam: a 2 degree turn */
    hand_view_advance(&hv, HAND_TICK_SECONDS, &s);
    TEST_ASSERT_FLOAT_EQ(hv.y_bob - hv.y_bob_o, 1.0f, 1e-4f); /* half of 2 */
    HandViewFrame fr;
    hand_view_frame(&hv, 0.0f, -179.0f, 0.0f, HAND_ITEM_NONE, NULL, &fr);
    TEST_ASSERT(fabsf(fr.arm.m[12]) < 20.0f);
    return failures;
}

int test_hand_cube_matches_modelpart(void)
{
    int failures = 0;
    HandCube c;
    hand_arm_cube(&c);
    /* ModelPart.Cube(texOffs 40,16; box -3,-2,-2 / 4,12,4): u = 40,44,48,52,
     * 56; v = 16,20,32 on a 64x64 sheet. Polygon order DOWN,UP,WEST,NORTH,
     * EAST,SOUTH. */
    /* NORTH (front, z = -2/16): uv rect (44,20)-(48,32); remap order is
     * (u1,v0) (u0,v0) (u0,v1) (u1,v1). */
    TEST_ASSERT_FLOAT_EQ(c.v[3][0].u, 48.0f / 64.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[3][0].v, 20.0f / 64.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[3][1].u, 44.0f / 64.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[3][2].v, 32.0f / 64.0f, 1e-7f);
    /* NORTH vertices are v1,v0,v3,v2: (x1,y0,z0) (x0,y0,z0) (x0,y1,z0) (x1,y1,z0). */
    TEST_ASSERT_FLOAT_EQ(c.v[3][0].x, 1.0f / 16.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[3][0].y, -2.0f / 16.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[3][0].z, -2.0f / 16.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[3][2].x, -3.0f / 16.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[3][2].y, 10.0f / 16.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[3][0].nz, -1.0f, 0.0f);
    /* UP polygon has v0/v1 swapped relative to DOWN (the flipped top). */
    TEST_ASSERT_FLOAT_EQ(c.v[1][0].v, 20.0f / 64.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[1][2].v, 16.0f / 64.0f, 1e-7f);
    /* EAST uses u 48..52 (+Z side after front), SOUTH 52..56. */
    TEST_ASSERT_FLOAT_EQ(c.v[4][1].u, 48.0f / 64.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[4][0].u, 52.0f / 64.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(c.v[5][0].u, 56.0f / 64.0f, 1e-7f);
    /* Sleeve: same box inflated by 0.25 texels, UV origin (40,32). */
    HandCube sl;
    hand_sleeve_cube(&sl);
    TEST_ASSERT_FLOAT_EQ(sl.v[3][0].x, 1.25f / 16.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(sl.v[3][0].v, 36.0f / 64.0f, 1e-7f);
    return failures;
}

int test_hand_rest_pose_screen_placement(void)
{
    int failures = 0;
    /* At rest (swing 0, fully equipped) the vanilla right arm sits in the
     * lower right of the view: positive X, negative Y, in front (-Z). Its
     * wrist end (model y = +10/16) must be nearer the screen centre than the
     * shoulder end (y = -2/16), and neither may be behind the camera. */
    HandView hv;
    HandViewFrame fr;
    pin_state(&hv, 0.0f, 0.0f);
    hand_view_frame(&hv, 0.0f, 0.0f, 0.0f, HAND_ITEM_NONE, NULL, &fr);
    Mat4 m = mmath_mat4_mul(fr.arm, hand_arm_part_matrix());
    Vec3 wrist = hand_mat_point(&m, mmath_vec3(-1.0f / 16.0f, 10.0f / 16.0f, 0.0f));
    Vec3 shoulder = hand_mat_point(&m, mmath_vec3(-1.0f / 16.0f, -2.0f / 16.0f, 0.0f));
    TEST_ASSERT(wrist.z < 0.0f);
    TEST_ASSERT(shoulder.z < 0.0f);
    TEST_ASSERT(wrist.x > 0.0f);
    TEST_ASSERT(wrist.y < 0.0f);
    float wr = hypotf(wrist.x / -wrist.z, wrist.y / -wrist.z);
    float sr = hypotf(shoulder.x / -shoulder.z, shoulder.y / -shoulder.z);
    TEST_ASSERT(wr < sr);
    return failures;
}
