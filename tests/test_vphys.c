#include "test_main.h"

#include "game/vphys.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <math.h>
#include <string.h>

/* Expected values below are derived from the 1.21.11 movement rules read in
 * the client bytecode (docs/PHYSICS_PARITY.md): friction 0.6*0.91, input scale
 * 0.98, base speed 0.1 (+30% sprinting), jump power 0.42, gravity 0.08 with
 * 0.98 vertical drag, water drag 0.8 / gravity/16. They are the well-known
 * vanilla figures (4.317 / 5.612 / 1.295 blocks/s, 1.2522 block jump). */

/* Flat floor: stone y 40..62 (top surface 63) over chunk (0,0). */
static World *floor_world(void)
{
    World *w = world_create();
    Chunk *c = chunk_create(0, 0);
    if (w == NULL || c == NULL) {
        return w;
    }
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 40; y <= 62; ++y) {
                chunk_set_block(c, x, y, z, BLOCK_STONE);
            }
        }
    }
    world_add_chunk(w, c);
    return w;
}

/* Pool: water y 63..(62+depth) for x 0..7, stone bank (top 64) for x >= 8. */
static World *pool_world(int depth)
{
    World *w = floor_world();
    Chunk *c = world_get_chunk(w, 0, 0);
    for (int x = 0; x < 8; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 63; y < 63 + depth; ++y) {
                chunk_set_block(c, x, y, z, BLOCK_WATER);
            }
        }
    }
    for (int x = 8; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            chunk_set_block(c, x, 63, z, BLOCK_STONE);
        }
    }
    return w;
}

static VInput input(float fwd, float left, bool jump, bool shift, bool sprint)
{
    VInput in;
    in.forward = fwd;
    in.left = left;
    in.jump = jump;
    in.shift = shift;
    in.sprint_key = sprint;
    return in;
}

#define YAW_PLUS_X (-90.0f) /* vanilla yRot -90 faces +X */

static double run_ticks(VPhys *s, const VInput *in, const World *w, int n)
{
    double x0 = s->x;
    for (int i = 0; i < n; ++i) {
        vphys_tick(s, in, w, YAW_PLUS_X, 0.0f);
    }
    return s->x - x0;
}

int test_vphys_walk_sprint_sneak_speeds(void)
{
    int failures = 0;
    World *w = floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return 1;
    }
    VPhys s;
    /* Walk: 4.317 blocks/s = 0.21585 per tick. */
    vphys_init(&s, 0.5, 63.0, 8.5);
    VInput walk = input(1.0f, 0.0f, false, false, false);
    run_ticks(&s, &walk, w, 30);
    double d = run_ticks(&s, &walk, w, 10) / 10.0;
    TEST_ASSERT(s.on_ground);
    TEST_ASSERT_FLOAT_EQ((float)(d * 20.0), 4.317f, 0.01f);

    /* Sprint: 5.612 blocks/s. */
    vphys_init(&s, 0.5, 63.0, 8.5);
    VInput sprint = input(1.0f, 0.0f, false, false, true);
    run_ticks(&s, &sprint, w, 20);
    TEST_ASSERT(s.sprinting);
    d = run_ticks(&s, &sprint, w, 8) / 8.0;
    TEST_ASSERT_FLOAT_EQ((float)(d * 20.0), 5.612f, 0.01f);

    /* Sneak: 1.295 blocks/s (the 0.3 factor applies from the tick after the
     * crouch pose is set). */
    vphys_init(&s, 0.5, 63.0, 8.5);
    VInput sneak = input(1.0f, 0.0f, false, true, false);
    run_ticks(&s, &sneak, w, 40);
    TEST_ASSERT(s.pose == VPOSE_CROUCHING);
    d = run_ticks(&s, &sneak, w, 10) / 10.0;
    TEST_ASSERT_FLOAT_EQ((float)(d * 20.0), 1.295f, 0.01f);
    world_destroy(w);
    return failures;
}

int test_vphys_jump_apex_and_air(void)
{
    int failures = 0;
    World *w = floor_world();
    VPhys s;
    vphys_init(&s, 8.5, 63.0, 8.5);
    run_ticks(&s, &(VInput){0}, w, 5); /* settle */
    TEST_ASSERT(s.on_ground);
    double base = s.y;
    double apex = base;
    VInput jump = input(0.0f, 0.0f, true, false, false);
    for (int i = 0; i < 40; ++i) {
        vphys_tick(&s, &jump, w, 0.0f, 0.0f);
        if (s.y > apex) {
            apex = s.y;
        }
    }
    /* The famous 1.2522-block vanilla jump. */
    TEST_ASSERT_FLOAT_EQ((float)(apex - base), 1.2522f, 0.0005f);

    /* Free fall tends to -3.92 blocks/tick. */
    vphys_init(&s, 8.5, 400.0, 8.5);
    for (int i = 0; i < 900; ++i) {
        vphys_tick(&s, NULL, NULL, 0.0f, 0.0f);
    }
    TEST_ASSERT_FLOAT_EQ((float)s.vy, -3.92f, 0.005f);
    world_destroy(w);
    return failures;
}

int test_vphys_fall_distance_and_landing(void)
{
    int failures = 0;
    World *w = floor_world();
    VPhys s;
    vphys_init(&s, 8.5, 73.0, 8.5); /* 10 blocks above the floor */
    bool landed = false;
    for (int i = 0; i < 60 && !landed; ++i) {
        vphys_tick(&s, NULL, w, 0.0f, 0.0f);
        if (s.landed_fall >= 0.0) {
            landed = true;
        }
    }
    TEST_ASSERT(landed);
    TEST_ASSERT_FLOAT_EQ((float)s.y, 63.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ((float)s.landed_fall, 10.0f, 0.05f);
    TEST_ASSERT(s.fall_distance == 0.0);
    world_destroy(w);
    return failures;
}

int test_vphys_water_sinking_and_exit(void)
{
    int failures = 0;
    World *w = pool_world(3);
    VPhys s;
    /* Free sinking in water: v' = 0.8 v - 0.005 -> -0.025 blocks/tick. */
    vphys_init(&s, 4.5, 65.0, 8.5);
    for (int i = 0; i < 6; ++i) {
        vphys_tick(&s, NULL, w, 0.0f, 0.0f);
    }
    TEST_ASSERT(s.in_water);
    TEST_ASSERT(s.vy < 0.0);
    /* Fall distance is reset in water: no landing damage. */
    for (int i = 0; i < 40; ++i) {
        vphys_tick(&s, NULL, w, 0.0f, 0.0f);
    }
    TEST_ASSERT_FLOAT_EQ((float)s.vy, -0.025f, 0.003f);
    TEST_ASSERT(s.fall_distance == 0.0);

    world_destroy(w);

    /* One-deep pool next to a 1-block bank. Vanilla only pops out when the
     * body shifted up 0.6 is free of water (jumpOutOfFluid), so the player
     * swims up (jump) while pushing forward; the exit is the 0.3 blocks/tick
     * impulse, never a position jump. */
    w = pool_world(1);
    vphys_init(&s, 6.5, 63.0, 8.5);
    VInput fwd = input(1.0f, 0.0f, true, false, false);
    double max_dy = 0.0;
    bool out = false;
    for (int i = 0; i < 120; ++i) {
        double y0 = s.y;
        vphys_tick(&s, &fwd, w, YAW_PLUS_X, 0.0f);
        double dy = fabs(s.y - y0);
        if (dy > max_dy) {
            max_dy = dy;
        }
        if (s.on_ground && s.y >= 64.0 - 1e-6) {
            out = true;
            break;
        }
    }
    TEST_ASSERT(out);
    TEST_ASSERT(max_dy < 0.45); /* 0.3 impulse plus a tick of fluid motion */
    TEST_ASSERT_FLOAT_EQ((float)s.y, 64.0f, 1e-3f);

    /* Without jumping, a bank wall in water blocks the swimmer (vanilla). */
    vphys_init(&s, 6.5, 63.0, 8.5);
    VInput push = input(1.0f, 0.0f, false, false, false);
    run_ticks(&s, &push, w, 60);
    TEST_ASSERT(s.y < 63.5);
    world_destroy(w);
    return failures;
}

int test_vphys_sneak_edge_and_collision(void)
{
    int failures = 0;
    World *w = floor_world();
    VPhys s;
    /* Sneak toward the +X edge of the 16-block floor: stays on it. */
    vphys_init(&s, 14.5, 63.0, 8.5);
    VInput sneak = input(1.0f, 0.0f, false, true, false);
    run_ticks(&s, &sneak, w, 200);
    TEST_ASSERT(s.on_ground);
    TEST_ASSERT(s.y >= 63.0 - 1e-6);
    TEST_ASSERT(s.x <= 16.0 + 0.3 + 1e-6);

    /* Sprinting off the edge falls (no edge protection). */
    vphys_init(&s, 14.5, 63.0, 8.5);
    VInput run = input(1.0f, 0.0f, false, false, true);
    run_ticks(&s, &run, w, 40);
    TEST_ASSERT(s.y < 63.0);

    /* Walking into a wall stops and clears sprint. */
    Chunk *c = world_get_chunk(w, 0, 0);
    for (int y = 63; y <= 66; ++y) {
        for (int z = 0; z < 16; ++z) {
            chunk_set_block(c, 10, y, z, BLOCK_STONE);
        }
    }
    vphys_init(&s, 2.5, 63.0, 8.5);
    run_ticks(&s, &run, w, 60);
    TEST_ASSERT_FLOAT_EQ((float)s.x, 10.0f - 0.3f, 1e-4f);
    TEST_ASSERT(s.horizontal_collision);
    TEST_ASSERT(!s.sprinting);
    world_destroy(w);
    return failures;
}

int test_vphys_diagonal_input_matches_vanilla_scaling(void)
{
    int failures = 0;
    World *w = floor_world();
    VPhys a, b;
    vphys_init(&a, 3.5, 63.0, 8.5);
    vphys_init(&b, 3.5, 63.0, 8.5);
    VInput straight = input(1.0f, 0.0f, false, false, false);
    VInput diag = input(1.0f, 1.0f, false, false, false);
    run_ticks(&a, &straight, w, 30);
    for (int i = 0; i < 30; ++i) {
        vphys_tick(&b, &diag, w, YAW_PLUS_X, 0.0f);
    }
    double sa = sqrt(a.vx * a.vx + a.vz * a.vz);
    double sb = sqrt(b.vx * b.vx + b.vz * b.vz);
    /* modifyInputSpeedForSquareMovement: diagonal (1,1) -> unit vector,
     * straight stays 0.98: ratio 1/0.98. */
    TEST_ASSERT_FLOAT_EQ((float)(sb / sa), 1.0f / 0.98f, 0.002f);
    world_destroy(w);
    return failures;
}

int test_vphys_mth_trig_table(void)
{
    int failures = 0;
    /* Mth.sin(float) uses the 65536-entry table; exact at table nodes. */
    TEST_ASSERT_FLOAT_EQ(vphys_mth_sin(0.0f), 0.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(vphys_mth_sin(1.5707964f), 1.0f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(vphys_mth_cos(0.0f), 1.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(vphys_mth_cos(3.1415927f), -1.0f, 1e-3f);
    return failures;
}
