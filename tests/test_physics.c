#include "test_main.h"

#include "game/interaction.h"
#include "game/player.h"
#include "game/raycast.h"
#include "game/survival.h"
#include "math/mmath.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <math.h>

/* The player simulates at the vanilla 20 TPS: one test step is one game tick. */
#undef PLAYER_STEP_DT
#define PLAYER_STEP_DT 0.05f

/* Test-local flat floor: chunk (0,0), stone y in [60..64], air above.
 * Top surface at y=65 (feet level). */

/* Build the floor world. Returns NULL on OOM (callers assert non-NULL). */
static World *make_floor_world(void)
{
    World *w = world_create();
    if (w == NULL) {
        return NULL;
    }
    Chunk *c = chunk_create(0, 0);
    if (c == NULL) {
        world_destroy(w);
        return NULL;
    }
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 60; y <= 64; ++y) {
                chunk_set_block(c, x, y, z, BLOCK_STONE);
            }
        }
    }
    if (world_add_chunk(w, c) != 0) {
        chunk_destroy(c);
        world_destroy(w);
        return NULL;
    }
    return w;
}

/* Pool world builder (defined below; forward declaration for earlier tests). */
static World *make_pool_world(void);

/* Empty input snapshot. */
static PlayerInput no_input(void)
{
    PlayerInput in;
    in.fwd = 0.0f;
    in.strafe = 0.0f;
    in.jump = false;
    in.sneak = false;
    in.sprint = false;
    in.auto_jump = false;
    return in;
}

/* Test: free fall ends grounded exactly on the floor (no tunneling).
 *
 * Returns: failure count.
 */
int test_physics_fall_land(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(8.5f, 70.0f, 8.5f);
    PlayerInput in = no_input();
    for (int i = 0; i < 240; ++i) {
        player_update(&p, &in, w, 0.05f);
    }
    TEST_ASSERT(p.grounded == true);
    TEST_ASSERT_FLOAT_EQ(p.pos.y, 65.0f, 0.02f);
    /* Resting contact keeps the vanilla -0.0784 blocks/tick gravity velocity. */
    TEST_ASSERT_FLOAT_EQ(p.vel.y, -1.568f, 1e-3f);
    /* Still grounded after more steps (resting contact is stable). */
    for (int i = 0; i < 60; ++i) {
        player_update(&p, &in, w, 0.05f);
    }
    TEST_ASSERT(p.grounded == true);
    TEST_ASSERT_FLOAT_EQ(p.pos.y, 65.0f, 0.02f);
    world_destroy(w);
    return failures;
}

/* Test: high drop hits terminal velocity but never tunnels the floor.
 *
 * Returns: failure count.
 */
int test_physics_high_drop(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(8.5f, 150.0f, 8.5f);
    PlayerInput in = no_input();
    for (int i = 0; i < 600; ++i) {
        player_update(&p, &in, w, 0.05f);
        if (p.grounded) {
            break;
        }
    }
    TEST_ASSERT(p.grounded == true);
    TEST_ASSERT_FLOAT_EQ(p.pos.y, 65.0f, 0.02f);
    world_destroy(w);
    return failures;
}

/* Test: walking into a wall stops (no pass-through), keeps height/ground.
 *
 * Returns: failure count.
 */
int test_physics_wall_slide(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    /* Wall column at world x=10 (local 10), z=8, y 60..70. */
    Chunk *c = world_get_chunk(w, 0, 0);
    TEST_ASSERT(c != NULL);
    for (int y = 60; y <= 70; ++y) {
        chunk_set_block(c, 10, y, 8, BLOCK_STONE);
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(7.5f, 65.0f, 8.5f);
    p.grounded = true;
    p.yaw = -MMATH_PI * 0.5f; /* Face +X. */
    PlayerInput in = no_input();
    in.fwd = 1.0f;
    for (int i = 0; i < 180; ++i) {
        player_update(&p, &in, w, 0.05f);
    }
    TEST_ASSERT(p.pos.x > 9.0f); /* Walked up to the wall. */
    TEST_ASSERT(p.pos.x < 9.8f); /* Blocked: face stops at 10-0.3-eps. */
    TEST_ASSERT_FLOAT_EQ(p.vel.x, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(p.pos.y, 65.0f, 0.05f);
    TEST_ASSERT(p.grounded == true);
    world_destroy(w);
    return failures;
}

/* Test: jump clears a full block and lands back on the floor.
 *
 * Returns: failure count.
 */
int test_physics_jump(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    PlayerInput in = no_input();
    /* Settle two ticks so grounded latches (gravity applies after the move). */
    player_update(&p, &in, w, 0.05f);
    player_update(&p, &in, w, 0.05f);
    TEST_ASSERT(p.grounded == true);

    in.jump = true;
    player_update(&p, &in, w, 0.05f);
    in.jump = false;
    TEST_ASSERT(p.grounded == false);

    float max_y = p.pos.y;
    for (int i = 0; i < 180; ++i) {
        player_update(&p, &in, w, 0.05f);
        if (p.pos.y > max_y) {
            max_y = p.pos.y;
        }
    }
    TEST_ASSERT(max_y > 66.0f); /* Cleared a full block. */
    TEST_ASSERT(p.grounded == true);
    TEST_ASSERT_FLOAT_EQ(p.pos.y, 65.0f, 0.02f);
    world_destroy(w);
    return failures;
}

/* Test: horizontal control eases toward the wish velocity instead of
 * snapping; ground accel/stop are quick, air control is weaker, and
 * reversing direction carries momentum for a few steps.
 *
 * Returns: failure count.
 */
int test_physics_acceleration(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    /* Player.vel is the post-friction tick velocity in m/s; the distance
     * moved per tick (x20) is the vanilla "blocks per second" figure. */
    PlayerInput idle = no_input();
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    PlayerInput in = no_input();
    in.fwd = 1.0f;
    for (int i = 0; i < 3; ++i) { /* settle on the floor */
        player_update(&p, &idle, w, PLAYER_STEP_DT);
    }
    float z0 = p.pos.z;
    player_update(&p, &in, w, PLAYER_STEP_DT);
    float first = fabsf(p.pos.z - z0) * 20.0f;
    TEST_ASSERT(first > 0.0f);
    TEST_ASSERT(first < p.walk_speed * 0.5f); /* accelerates, does not snap */

    Player g;
    player_init(&g);
    g.pos = mmath_vec3(8.5f, 65.0f, 12.5f);
    for (int i = 0; i < 3; ++i) {
        player_update(&g, &idle, w, PLAYER_STEP_DT);
    }
    for (int i = 0; i < 25; ++i) {
        player_update(&g, &in, w, PLAYER_STEP_DT);
    }
    float zb = g.pos.z;
    player_update(&g, &in, w, PLAYER_STEP_DT);
    TEST_ASSERT_FLOAT_EQ(fabsf(g.pos.z - zb) * 20.0f, 4.317f, 0.02f); /* walking pace */

    /* Release: one tick keeps friction*0.91 = 0.546 of the velocity. */
    PlayerInput stop = no_input();
    float before = fabsf(g.vel.z);
    player_update(&g, &stop, w, PLAYER_STEP_DT);
    TEST_ASSERT_FLOAT_EQ(fabsf(g.vel.z) / before, 0.546f, 0.01f);
    for (int i = 0; i < 30; ++i) {
        player_update(&g, &stop, w, PLAYER_STEP_DT);
    }
    TEST_ASSERT(fabsf(g.vel.z) < 0.01f);

    /* Air control is weak (0.02 per tick) and air friction is only 0.91. */
    Player ground, air;
    player_init(&ground);
    player_init(&air);
    ground.pos = air.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    for (int i = 0; i < 3; ++i) {
        player_update(&ground, &idle, w, PLAYER_STEP_DT);
    }
    air.pos = mmath_vec3(8.5f, 90.0f, 8.5f);
    for (int i = 0; i < 6; ++i) {
        player_update(&ground, &in, w, PLAYER_STEP_DT);
        player_update(&air, &in, NULL, PLAYER_STEP_DT);
    }
    TEST_ASSERT(fabsf(air.vel.z) < fabsf(ground.vel.z));

    /* Sprint needs forward input >= 0.8: strafing never sprints, and sneaking
     * cancels it; sprint pace is 5.612 blocks/s, sneak pace 1.295. */
    Player sp, st, sn;
    player_init(&sp);
    player_init(&st);
    player_init(&sn);
    sp.pos = mmath_vec3(8.5f, 65.0f, 14.5f);
    st.pos = mmath_vec3(1.5f, 65.0f, 8.5f);
    sn.pos = mmath_vec3(8.5f, 65.0f, 12.5f);
    PlayerInput sprint_fwd = no_input();
    sprint_fwd.fwd = 1.0f;
    sprint_fwd.sprint = true;
    PlayerInput sprint_strafe = no_input();
    sprint_strafe.strafe = 1.0f;
    sprint_strafe.sprint = true;
    PlayerInput sprint_sneak = no_input();
    sprint_sneak.fwd = 1.0f;
    sprint_sneak.sprint = true;
    sprint_sneak.sneak = true;
    for (int i = 0; i < 3; ++i) {
        player_update(&sp, &idle, w, PLAYER_STEP_DT);
        player_update(&st, &idle, w, PLAYER_STEP_DT);
        player_update(&sn, &idle, w, PLAYER_STEP_DT);
    }
    for (int i = 0; i < 40; ++i) {
        player_update(&sp, &sprint_fwd, w, PLAYER_STEP_DT);
        player_update(&st, &sprint_strafe, w, PLAYER_STEP_DT);
        player_update(&sn, &sprint_sneak, w, PLAYER_STEP_DT);
    }
    Vec3 sp0 = sp.pos, st0 = st.pos, sn0 = sn.pos;
    player_update(&sp, &sprint_fwd, w, PLAYER_STEP_DT);
    player_update(&st, &sprint_strafe, w, PLAYER_STEP_DT);
    player_update(&sn, &sprint_sneak, w, PLAYER_STEP_DT);
    TEST_ASSERT(sp.sprinting == true);
    TEST_ASSERT_FLOAT_EQ(fabsf(sp.pos.z - sp0.z) * 20.0f, 5.612f, 0.03f);
    TEST_ASSERT(st.sprinting == false);
    TEST_ASSERT_FLOAT_EQ(fabsf(st.pos.x - st0.x) * 20.0f, 4.317f, 0.03f);
    TEST_ASSERT(sn.sprinting == false);
    TEST_ASSERT_FLOAT_EQ(fabsf(sn.pos.z - sn0.z) * 20.0f, 1.295f, 0.03f);
    world_destroy(w);
    return failures;
}

/* Test: water reduces horizontal movement and jump adds buoyancy. */
int test_physics_water_motion(void)
{
    int failures = 0;
    World *dry = make_floor_world();
    World *wet = make_floor_world();
    World *partial = make_floor_world();
    World *shallow = make_floor_world();
    TEST_ASSERT(dry != NULL && wet != NULL && partial != NULL && shallow != NULL);
    if (dry == NULL || wet == NULL || partial == NULL || shallow == NULL) {
        world_destroy(dry);
        world_destroy(wet);
        world_destroy(partial);
        world_destroy(shallow);
        return failures + 1;
    }
    TEST_ASSERT(world_set_block(wet, 8, 65, 8, BLOCK_WATER));
    TEST_ASSERT(world_set_block(wet, 8, 66, 8, BLOCK_WATER));
    TEST_ASSERT(world_set_block(partial, 8, 65, 8, BLOCK_WATER));
    TEST_ASSERT(world_set_block(shallow, 8, 65, 8, BLOCK_WATER_FLOW_7));
    Player dry_player, wet_player, partial_player, shallow_player;
    player_init(&dry_player);
    player_init(&wet_player);
    player_init(&partial_player);
    player_init(&shallow_player);
    dry_player.pos = wet_player.pos = partial_player.pos = shallow_player.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    dry_player.grounded = wet_player.grounded = partial_player.grounded = shallow_player.grounded = true;
    PlayerInput in = no_input();
    in.fwd = 1.0f;
    /* Horizontal: run to the accelerated steady state (no jump, so the
     * buoyancy impulse does not disturb the comparison). */
    for (int i = 0; i < 300; ++i) {
        /* Keep each player inside its one-cell water fixture while the
         * horizontal velocity converges. */
        dry_player.pos = wet_player.pos = partial_player.pos = shallow_player.pos =
            mmath_vec3(8.5f, 65.0f, 8.5f);
        player_update(&dry_player, &in, dry, PLAYER_STEP_DT);
        player_update(&wet_player, &in, wet, PLAYER_STEP_DT);
        player_update(&partial_player, &in, partial, PLAYER_STEP_DT);
        player_update(&shallow_player, &in, shallow, PLAYER_STEP_DT);
    }
    /* Vanilla water is binary (touching or not): in water the player gets
     * 0.02 acceleration and 0.8 drag, steady 4 * 0.0196 = 0.0784 blocks/tick
     * (1.568 m/s); walking on land settles at 2.357 m/s. Any water cell the
     * body touches (even a 1/9 flow) counts the same. */
    TEST_ASSERT(fabsf(wet_player.vel.z) < fabsf(dry_player.vel.z));
    TEST_ASSERT_FLOAT_EQ(wet_player.vel.z, -1.568f, 2e-3f);
    TEST_ASSERT_FLOAT_EQ(partial_player.vel.z, wet_player.vel.z, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(shallow_player.vel.z, wet_player.vel.z, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(dry_player.vel.z, -2.357f, 5e-3f);

    /* Vertical: holding jump in water adds 0.04 blocks/tick (jumpInLiquid);
     * on dry land it launches at 0.42. */
    PlayerInput idle_input = no_input();
    Player dry_jump, wet_jump;
    player_init(&dry_jump);
    player_init(&wet_jump);
    dry_jump.pos = wet_jump.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    for (int i = 0; i < 3; ++i) {
        player_update(&dry_jump, &idle_input, dry, PLAYER_STEP_DT);
        player_update(&wet_jump, &idle_input, wet, PLAYER_STEP_DT);
    }
    in.jump = true;
    player_update(&dry_jump, &in, dry, PLAYER_STEP_DT);
    player_update(&wet_jump, &in, wet, PLAYER_STEP_DT);
    TEST_ASSERT_FLOAT_EQ(dry_jump.vel.y, (0.42f - 0.08f) * 0.98f * 20.0f, 0.05f);
    TEST_ASSERT(wet_jump.vel.y > 0.0f);
    TEST_ASSERT(dry_jump.vel.y > wet_jump.vel.y);

    /* A shallow fluid surface below the player's feet is not contact. */
    Player dry_above, shallow_above;
    player_init(&dry_above);
    player_init(&shallow_above);
    dry_above.pos = shallow_above.pos = mmath_vec3(8.5f, 65.2f, 8.5f);
    in.jump = false;
    player_update(&dry_above, &in, dry, PLAYER_STEP_DT);
    player_update(&shallow_above, &in, shallow, PLAYER_STEP_DT);
    TEST_ASSERT_FLOAT_EQ(shallow_above.vel.z, dry_above.vel.z, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(shallow_above.vel.y, dry_above.vel.y, 1e-6f);
    world_destroy(dry);
    world_destroy(wet);
    world_destroy(partial);
    world_destroy(shallow);
    return failures;
}

/* Test: spawn finder locates the floor top; misses empty columns.
 *
 * Returns: failure count.
 */
int test_physics_spawn(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Vec3 out = mmath_vec3(0.0f, 0.0f, 0.0f);
    TEST_ASSERT(player_find_spawn(w, 8, 8, &out) == true);
    TEST_ASSERT_FLOAT_EQ(out.y, 65.0f, 1e-6f);
    /* Column in an unloaded chunk has no ground. */
    TEST_ASSERT(player_find_spawn(w, 100, 100, &out) == false);
    TEST_ASSERT(player_find_spawn(NULL, 0, 0, &out) == false);
    world_destroy(w);
    return failures;
}

/* Test: dry step-up climbs a 1-block ledge, embed ejects, knockback caps,
 * and spawn rejects water-filled headroom.
 *
 * Returns: failure count.
 */
int test_physics_step_up_and_eject(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    /* A 4-block ledge at x=10..13 (feet 65, headroom 66..67 clear). */
    TEST_ASSERT(world_set_block(w, 10, 65, 8, BLOCK_STONE));
    TEST_ASSERT(world_get_block(w, 10, 65, 8) == BLOCK_STONE);
    TEST_ASSERT(world_set_block(w, 11, 65, 8, BLOCK_STONE));
    TEST_ASSERT(world_set_block(w, 12, 65, 8, BLOCK_STONE));
    TEST_ASSERT(world_set_block(w, 13, 65, 8, BLOCK_STONE));
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    p.yaw = -1.5707963f; /* Face +X (wish dir convention: yaw -pi/2). */
    PlayerInput in = no_input();
    in.fwd = 1.0f;
    in.jump = true; /* vanilla has no auto-step: hop onto the ledge */
    float peak_y = p.pos.y;
    bool on_ledge = false;
    for (int i = 0; i < 40; ++i) {
        player_update(&p, &in, w, PLAYER_STEP_DT);
        peak_y = fmaxf(peak_y, p.pos.y);
        if (p.grounded && p.pos.y > 65.9f && p.pos.x > 10.0f) {
            on_ledge = true;
        }
    }
    /* Hopped onto the ledge (y=66) and stood/walked on it. */
    TEST_ASSERT(peak_y > 65.5f);
    TEST_ASSERT(on_ledge);

    /* (Vanilla's LocalPlayer.moveTowardsClosestSpace eject is not ported.) */
    /* Knockback obeys the mob caps (hs <= 12, up <= 6). */
    Player hit;
    player_init(&hit);
    player_apply_knockback(&hit, 100.0f, 100.0f, 0.0f);
    float hs = sqrtf(hit.vel.x * hit.vel.x + hit.vel.z * hit.vel.z);
    TEST_ASSERT(hs <= 12.0f + 1e-4f);
    TEST_ASSERT(hit.vel.y <= 6.0f + 1e-4f);
    player_apply_knockback(NULL, 1.0f, 1.0f, 1.0f);

    /* Flood the headroom: the floor column is no longer a valid spawn. */
    TEST_ASSERT(world_set_block(w, 8, 65, 8, BLOCK_WATER));
    TEST_ASSERT(world_set_block(w, 8, 66, 8, BLOCK_WATER));
    Vec3 out = mmath_vec3(0.0f, 0.0f, 0.0f);
    TEST_ASSERT(player_find_spawn(w, 8, 8, &out) == false);
    world_destroy(w);
    return failures;
}

/* Test: autoJump OFF (MC default) disables the dry 1-block auto-step but
 * never water escape: a dry ledge blocks walking, while swimming into a
 * shore still climbs out.
 *
 * Returns: failure count.
 */
int test_physics_auto_jump_off(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    TEST_ASSERT(world_set_block(w, 10, 65, 8, BLOCK_STONE));
    TEST_ASSERT(world_set_block(w, 11, 65, 8, BLOCK_STONE));
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    p.yaw = -1.5707963f; /* Face +X. */
    PlayerInput in = no_input();
    in.fwd = 1.0f;
    in.auto_jump = false;
    for (int i = 0; i < 120; ++i) {
        player_update(&p, &in, w, PLAYER_STEP_DT);
    }
    /* Still on the floor level, held at the ledge (never climbed). */
    TEST_ASSERT(p.pos.y < 65.5f);
    TEST_ASSERT(p.pos.x < 10.0f);
    world_destroy(w);

    /* Water escape is independent of autoJump: swimming into the bank
     * still climbs out onto the shore. */
    World *pool = make_pool_world();
    TEST_ASSERT(pool != NULL);
    if (pool == NULL) {
        return failures + 1;
    }
    Player s;
    player_init(&s);
    s.pos = mmath_vec3(6.5f, 63.0f, 8.5f);
    s.yaw = -1.5707963f;
    PlayerInput swim = no_input();
    swim.fwd = 1.0f;
    swim.auto_jump = false;
    swim.jump = true; /* swim up while pushing into the bank */
    bool escaped = false;
    for (int i = 0; i < 120; ++i) {
        player_update(&s, &swim, pool, 0.05f);
        if (s.grounded && s.pos.y >= 64.0f) {
            escaped = true;
            break;
        }
    }
    TEST_ASSERT(escaped == true);
    TEST_ASSERT(player_water_contact(pool, &s) < 0.05f);
    world_destroy(pool);
    return failures;
}

/* Test: sneaking stops at a cliff edge (MC edge protection) while a
 * non-sneaking control walks off and falls in. A 1-block drop also holds:
 * this matches the traced 1.5-era reference (Entity.moveEntity clips X/Z
 * where the 1-below slab has no support; descent needs unsneak or a jump).
 *
 * Returns: failure count.
 */
int test_physics_sneak_edge(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    /* Pit x8..15 (floor top 63, walls 2 high): carve y63..64 to air. */
    for (int x = 8; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            TEST_ASSERT(world_set_block(w, x, 63, z, BLOCK_AIR));
            TEST_ASSERT(world_set_block(w, x, 64, z, BLOCK_AIR));
        }
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(6.5f, 65.0f, 8.5f);
    p.yaw = -1.5707963f; /* Face +X (toward the pit). */
    PlayerInput in = no_input();
    in.fwd = 1.0f;
    in.sneak = true;
    for (int i = 0; i < 240; ++i) {
        player_update(&p, &in, w, PLAYER_STEP_DT);
    }
    /* Held on the top level at the edge, standing, never fell in. */
    TEST_ASSERT(p.pos.y > 64.5f);
    TEST_ASSERT_FLOAT_EQ(p.pos.y, 65.0f, 0.05f);
    TEST_ASSERT(p.pos.x < 8.5f);
    TEST_ASSERT(p.grounded == true);

    /* Control: same walk without sneak leaves the top level. */
    Player c;
    player_init(&c);
    c.pos = mmath_vec3(6.5f, 65.0f, 8.5f);
    c.yaw = -1.5707963f;
    PlayerInput walk = no_input();
    walk.fwd = 1.0f;
    for (int i = 0; i < 240; ++i) {
        player_update(&c, &walk, w, PLAYER_STEP_DT);
    }
    TEST_ASSERT(c.pos.x > 8.0f);
    TEST_ASSERT(c.pos.y < 64.5f);
    world_destroy(w);

    /* Single 1-block drop while sneaking: holds the top edge (documented
     * 1.5-era semantics; release sneak or jump to descend). */
    World *step = make_floor_world();
    TEST_ASSERT(step != NULL);
    if (step == NULL) {
        return failures + 1;
    }
    for (int x = 8; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            TEST_ASSERT(world_set_block(step, x, 64, z, BLOCK_AIR));
        }
    }
    Player d;
    player_init(&d);
    d.pos = mmath_vec3(6.5f, 65.0f, 8.5f);
    d.yaw = -1.5707963f;
    PlayerInput creep = no_input();
    creep.fwd = 1.0f;
    creep.sneak = true;
    for (int i = 0; i < 240; ++i) {
        player_update(&d, &creep, step, PLAYER_STEP_DT);
    }
    TEST_ASSERT_FLOAT_EQ(d.pos.y, 65.0f, 0.05f);
    TEST_ASSERT(d.pos.x < 8.5f);
    TEST_ASSERT(d.grounded == true);
    world_destroy(step);
    return failures;
}

/* Test: break/place rules incl. bedrock, occupied cells, self-overlap.
 *
 * Returns: failure count.
 */
int test_interaction_break_place(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(8.5f, 65.0f, 8.5f);

    /* Break the stone under the crosshair. */
    HitResult hit;
    hit.hit = true;
    hit.block[0] = 8;
    hit.block[1] = 64;
    hit.block[2] = 8;
    hit.normal[0] = 0;
    hit.normal[1] = 1;
    hit.normal[2] = 0;
    int broken = interaction_break(w, &hit);
    TEST_ASSERT(broken == (int)BLOCK_STONE);
    TEST_ASSERT(world_get_block(w, 8, 64, 8) == BLOCK_AIR);
    TEST_ASSERT(world_get_chunk(w, 0, 0)->dirty == true);

    /* Bedrock is unbreakable: plant one and try. */
    {
        Chunk *c = world_get_chunk(w, 0, 0);
        chunk_set_block(c, 5, 64, 5, BLOCK_BEDROCK);
        c->dirty = false;
        HitResult bh = hit;
        bh.block[0] = 5;
        bh.block[2] = 5;
        TEST_ASSERT(interaction_break(w, &bh) == -1);
        TEST_ASSERT(world_get_block(w, 5, 64, 5) == BLOCK_BEDROCK);
    }

    /* Place into the hole we made: target (8,64,8) is free and the player
     * stands clear (feet at 65 -> AABB y [65,66.8] above the cell). */
    p.pos = mmath_vec3(2.5f, 65.0f, 2.5f);
    HitResult ph;
    ph.hit = true;
    ph.block[0] = 8;
    ph.block[1] = 63;
    ph.block[2] = 8;
    ph.normal[0] = 0;
    ph.normal[1] = 1;
    ph.normal[2] = 0;
    TEST_ASSERT(interaction_place(w, &p, &ph, BLOCK_WOOD, NULL) == true);
    TEST_ASSERT(world_get_block(w, 8, 64, 8) == BLOCK_WOOD);

    /* Ordinary solid block target is rejected. */
    TEST_ASSERT(interaction_place(w, &p, &ph, BLOCK_WOOD, NULL) == false);

    /* Small vegetation gives way to a placed block. */
    {
        Chunk *c = world_get_chunk(w, 0, 0);
        uint16_t replaced = BLOCK_AIR;
        ItemStack drop;
        chunk_set_block(c, 8, 64, 8, BLOCK_GRASS_PLANT);
        TEST_ASSERT(interaction_place(w, &p, &ph, BLOCK_STONE, &replaced) == true);
        TEST_ASSERT(replaced == BLOCK_GRASS_PLANT);
        drop = survival_block_drop(replaced, ITEM_NONE);
        TEST_ASSERT(drop.item == BLOCK_GRASS_PLANT && drop.count == 1);
        TEST_ASSERT(world_get_block(w, 8, 64, 8) == BLOCK_STONE);
        chunk_set_block(c, 8, 64, 8, BLOCK_FLOWER);
        TEST_ASSERT(interaction_place(w, &p, &ph, BLOCK_DIRT, &replaced) == true);
        TEST_ASSERT(replaced == BLOCK_FLOWER);
        drop = survival_block_drop(replaced, ITEM_NONE);
        TEST_ASSERT(drop.item == BLOCK_FLOWER && drop.count == 1);
        TEST_ASSERT(world_get_block(w, 8, 64, 8) == BLOCK_DIRT);
        chunk_set_block(c, 8, 64, 8, BLOCK_TORCH);
        TEST_ASSERT(interaction_place(w, &p, &ph, BLOCK_PLANKS, &replaced) == true);
        TEST_ASSERT(replaced == BLOCK_TORCH);
        drop = survival_block_drop(replaced, ITEM_NONE);
        TEST_ASSERT(drop.item == BLOCK_TORCH && drop.count == 1);
        TEST_ASSERT(world_get_block(w, 8, 64, 8) == BLOCK_PLANKS);

        /* A plant can only be planted on soil, and clicking an existing
         * plant replaces that cell instead of stacking another plant above. */
        chunk_set_block(c, 8, 63, 8, BLOCK_GRASS);
        chunk_set_block(c, 8, 64, 8, BLOCK_GRASS_PLANT);
        HitResult plant_hit = ph;
        plant_hit.block[1] = 64;
        plant_hit.normal[1] = 1;
        TEST_ASSERT(interaction_place(w, &p, &plant_hit, BLOCK_FLOWER, &replaced) == true);
        TEST_ASSERT(replaced == BLOCK_GRASS_PLANT);
        TEST_ASSERT(world_get_block(w, 8, 64, 8) == BLOCK_FLOWER);
        TEST_ASSERT(world_get_block(w, 8, 65, 8) == BLOCK_AIR);

        /* New vegetation above stone is rejected; dirt is valid support. */
        chunk_set_block(c, 7, 63, 8, BLOCK_STONE);
        chunk_set_block(c, 7, 64, 8, BLOCK_AIR);
        HitResult unsupported = ph;
        unsupported.block[0] = 7;
        unsupported.block[1] = 63;
        TEST_ASSERT(interaction_place(w, &p, &unsupported, BLOCK_GRASS_PLANT, NULL) == false);
        TEST_ASSERT(world_get_block(w, 7, 64, 8) == BLOCK_AIR);
        chunk_set_block(c, 7, 63, 8, BLOCK_DIRT);
        TEST_ASSERT(interaction_place(w, &p, &unsupported, BLOCK_GRASS_PLANT, NULL) == true);
        TEST_ASSERT(world_get_block(w, 7, 64, 8) == BLOCK_GRASS_PLANT);

        /* Snow biome terrain also supports its naturally generated grass. */
        chunk_set_block(c, 6, 63, 8, BLOCK_SNOW);
        chunk_set_block(c, 6, 64, 8, BLOCK_AIR);
        HitResult snow_surface = unsupported;
        snow_surface.block[0] = 6;
        TEST_ASSERT(interaction_place(w, &p, &snow_surface, BLOCK_GRASS_PLANT, NULL) == true);
        TEST_ASSERT(world_get_block(w, 6, 64, 8) == BLOCK_GRASS_PLANT);

        /* A second plant aimed at the first plant replaces it at the same
         * coordinate; it never creates a vertical flower/grass stack. */
        HitResult grass_hit = unsupported;
        grass_hit.block[1] = 64;
        TEST_ASSERT(interaction_place(w, &p, &grass_hit, BLOCK_FLOWER, &replaced) == true);
        TEST_ASSERT(replaced == BLOCK_GRASS_PLANT);
        TEST_ASSERT(world_get_block(w, 7, 64, 8) == BLOCK_FLOWER);
        TEST_ASSERT(world_get_block(w, 7, 65, 8) == BLOCK_AIR);
    }

    /* Self-overlap rejected: aim at the floor under our feet. */
    chunk_set_block(world_get_chunk(w, 0, 0), 8, 64, 8, BLOCK_STONE);
    chunk_set_block(world_get_chunk(w, 0, 0), 8, 65, 8, BLOCK_AIR);
    p.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    HitResult sh;
    sh.hit = true;
    sh.block[0] = 8;
    sh.block[1] = 64;
    sh.block[2] = 8;
    sh.normal[0] = 0;
    sh.normal[1] = 1;
    sh.normal[2] = 0;
    /* (8,65,8) overlaps the player AABB -> must refuse solid blocks. */
    TEST_ASSERT(interaction_place(w, &p, &sh, BLOCK_STONE, NULL) == false);
    TEST_ASSERT(world_get_block(w, 8, 65, 8) == BLOCK_AIR);

    /* Misses do nothing. */
    HitResult miss;
    miss.hit = false;
    TEST_ASSERT(interaction_break(w, &miss) == -1);
    TEST_ASSERT(interaction_place(w, &p, &miss, BLOCK_STONE, NULL) == false);

    world_destroy(w);
    return failures;
}

/* Pool world: stone lakebed y 60..62; deep end x 2..4 water y 63..65;
 * shallow x 5..7 water y 63; stone shore x 8..15 up to y 64.
 * Shallow waders stand feet-at-63; the deep end fully submerges. */
static World *make_pool_world(void)
{
    World *w = world_create();
    if (w == NULL) {
        return NULL;
    }
    Chunk *c = chunk_create(0, 0);
    if (c == NULL) {
        world_destroy(w);
        return NULL;
    }
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 60; y <= 62; ++y) {
                chunk_set_block(c, x, y, z, BLOCK_STONE);
            }
        }
    }
    for (int x = 2; x <= 4; ++x) {
        for (int z = 2; z <= 13; ++z) {
            for (int y = 63; y <= 65; ++y) {
                chunk_set_block(c, x, y, z, BLOCK_WATER);
            }
        }
    }
    for (int x = 5; x <= 7; ++x) {
        for (int z = 2; z <= 13; ++z) {
            chunk_set_block(c, x, 63, z, BLOCK_WATER);
        }
    }
    for (int x = 8; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            chunk_set_block(c, x, 63, z, BLOCK_STONE);
            chunk_set_block(c, x, 64, z, BLOCK_AIR);
        }
    }
    if (world_add_chunk(w, c) != 0) {
        chunk_destroy(c);
        world_destroy(w);
        return NULL;
    }
    return w;
}

/* Test: water contact scales with immersion; eye test tracks submersion.
 *
 * Returns: failure count.
 */
int test_physics_water_contact(void)
{
    int failures = 0;
    World *w = make_pool_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Player p;
    player_init(&p);
    /* Dry on the shore. */
    p.pos = mmath_vec3(10.5f, 64.0f, 8.5f);
    TEST_ASSERT_FLOAT_EQ(player_water_contact(w, &p), 0.0f, 1e-6f);
    TEST_ASSERT(player_eye_in_water(w, &p) == false);
    /* Wading the shallows: partial contact, head out. */
    p.pos = mmath_vec3(6.5f, 63.0f, 8.5f);
    float shallow = player_water_contact(w, &p);
    TEST_ASSERT(shallow > 0.3f && shallow < 0.8f);
    TEST_ASSERT(player_eye_in_water(w, &p) == false);
    /* Deep end afloat: full contact, eyes under. */
    p.pos = mmath_vec3(3.5f, 64.0f, 8.5f);
    TEST_ASSERT_FLOAT_EQ(player_water_contact(w, &p), 1.0f, 1e-4f);
    TEST_ASSERT(player_eye_in_water(w, &p) == true);
    /* Bad args read dry. */
    TEST_ASSERT_FLOAT_EQ(player_water_contact(NULL, &p), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(player_water_contact(w, NULL), 0.0f, 1e-6f);
    TEST_ASSERT(player_eye_in_water(NULL, &p) == false);
    TEST_ASSERT(player_eye_in_water(w, NULL) == false);
    world_destroy(w);
    return failures;
}

/* Test: swimming into a 1-high shore climbs out instead of grinding.
 * Water escape is basic movement, so this must work with the autoJump
 * setting OFF (MC default).
 *
 * Returns: failure count.
 */
int test_physics_water_step_up(void)
{
    int failures = 0;
    World *w = make_pool_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(6.5f, 63.0f, 8.5f);
    p.yaw = -1.5707963f; /* Face +X (toward the x=8 shore). */
    PlayerInput in = no_input();
    in.fwd = 1.0f;
    in.auto_jump = false;
    in.jump = true; /* vanilla: swim up, then the jump-out impulse fires */
    bool climbed = false;
    float prev_y = p.pos.y;
    float max_step_dy = 0.0f;
    for (int i = 0; i < 120; ++i) {
        player_update(&p, &in, w, 0.05f);
        float step_dy = fabsf(p.pos.y - prev_y);
        if (step_dy > max_step_dy) {
            max_step_dy = step_dy;
        }
        prev_y = p.pos.y;
        if (p.grounded && p.pos.y > 63.5f) {
            climbed = true;
            break;
        }
    }
    /* Up on the shore (feet 64), still at the waterline, standing. */
    TEST_ASSERT(climbed == true);
    /* Never a teleport: the exit is the 0.3 blocks/tick fluid jump-out
     * impulse (0.3 blocks/tick), so no single tick may move the feet by
     * more than that plus a tick of fluid motion. */
    TEST_ASSERT(max_step_dy <= 0.45f);
    TEST_ASSERT_FLOAT_EQ(p.pos.y, 64.0f, 0.05f);
    TEST_ASSERT(p.pos.x > 7.0f && p.pos.x < 10.0f);
    TEST_ASSERT(p.grounded == true);
    /* Essentially dry (a toe may still touch the waterline). */
    TEST_ASSERT(player_water_contact(w, &p) < 0.05f);
    world_destroy(w);
    return failures;
}

/* Test: escaping deep water from a fully submerged start with
 * autoJump OFF: swim up with jump and hold forward into the shore.
 *
 * Returns: failure count.
 */
int test_physics_water_escape_deep(void)
{
    int failures = 0;
    World *w = make_pool_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(3.5f, 64.0f, 8.5f); /* Fully submerged deep end. */
    p.yaw = -1.5707963f;                   /* Face +X (toward the shore). */
    TEST_ASSERT(player_water_contact(w, &p) > 0.9f);
    PlayerInput in = no_input();
    in.fwd = 1.0f;
    in.jump = true;
    in.auto_jump = false;
    bool escaped = false;
    for (int i = 0; i < 600; ++i) {
        player_update(&p, &in, w, 0.05f);
        if (p.grounded && p.pos.y >= 64.0f && player_water_contact(w, &p) < 0.05f) {
            escaped = true;
            break;
        }
    }
    TEST_ASSERT(escaped == true);
    TEST_ASSERT(p.pos.x > 7.0f);
    world_destroy(w);
    return failures;
}

/* Test: landing in the shallows reports a fall but touches water, which
 * is exactly the cushion condition (no damage where MC forgives).
 *
 * Returns: failure count.
 */
int test_physics_water_fall_cushion(void)
{
    int failures = 0;
    World *w = make_pool_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(6.5f, 75.0f, 8.5f);
    PlayerInput in = no_input();
    for (int i = 0; i < 400 && !p.grounded; ++i) {
        player_update(&p, &in, w, 0.05f);
    }
    TEST_ASSERT(p.grounded == true);
    /* Water resets the fall distance: no landing report at all. */
    TEST_ASSERT(p.last_fall < 0.0f);
    TEST_ASSERT(player_water_contact(w, &p) > 0.0f);
    /* Dry control: the same drop onto stone reports no water. */
    p.pos = mmath_vec3(10.5f, 75.0f, 8.5f);
    p.vel = mmath_vec3(0.0f, 0.0f, 0.0f);
    p.grounded = false;
    p.fall_peak = -1.0f;
    p.last_fall = -1.0f;
    for (int i = 0; i < 400 && !p.grounded; ++i) {
        player_update(&p, &in, w, 0.05f);
    }
    TEST_ASSERT(p.grounded == true);
    TEST_ASSERT(p.last_fall > 9.0f);
    TEST_ASSERT_FLOAT_EQ(player_water_contact(w, &p), 0.0f, 1e-6f);
    world_destroy(w);
    return failures;
}

/* Test: sneak posture uses crouched dimensions (1.5 / eye 1.27), restores
 * standing (1.8 / 1.62) on release, and stays crouched without headroom
 * (forced). Fractional headroom proves the 0.3 difference with full cubes.
 *
 * Returns: failure count.
 */
int test_physics_sneak_posture(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    p.render_pos = p.pos;
    PlayerInput in = no_input();
    player_update(&p, &in, w, PLAYER_STEP_DT);
    TEST_ASSERT(p.sneaking == false);
    TEST_ASSERT_FLOAT_EQ(p.height, PLAYER_STAND_HEIGHT, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(p.eye_height, PLAYER_STAND_EYE, 1e-6f);
    TEST_ASSERT(player_can_stand(&p, w) == true);
    TEST_ASSERT_FLOAT_EQ(p.pos.y + p.eye_height, 65.0f + PLAYER_STAND_EYE, 0.05f);

    /* Crouch: dimensions shrink, eye follows. */
    PlayerInput creep = no_input();
    creep.sneak = true;
    /* The pose flips at the end of the first tick; the camera eye then eases
     * toward it at 0.5 per tick (vanilla Camera.tick). */
    for (int i = 0; i < 12; ++i) {
        player_update(&p, &creep, w, PLAYER_STEP_DT);
    }
    TEST_ASSERT(p.sneaking == true);
    TEST_ASSERT_FLOAT_EQ(p.height, PLAYER_SNEAK_HEIGHT, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(p.eye_height, PLAYER_SNEAK_EYE, 0.01f);

    /* Release: standing restores. */
    for (int i = 0; i < 12; ++i) {
        player_update(&p, &in, w, PLAYER_STEP_DT);
    }
    TEST_ASSERT(p.sneaking == false);
    TEST_ASSERT_FLOAT_EQ(p.height, PLAYER_STAND_HEIGHT, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(p.eye_height, PLAYER_STAND_EYE, 0.01f);

    /* Forced crouch uses fractional headroom (full cubes need it): feet
     * at 65.4 with a ceiling block at 67 leaves crouched top 66.9 free
     * but standing top 67.2 colliding. Crouch first, add the ceiling,
     * release sneak: the pose persists until headroom returns. */
    p.pos = mmath_vec3(8.5f, 65.4f, 8.5f);
    p.render_pos = p.pos;
    p.vel = mmath_vec3(0.0f, 0.0f, 0.0f);
    p.grounded = false;
    player_update(&p, &creep, w, PLAYER_STEP_DT);
    TEST_ASSERT(p.sneaking == true);
    TEST_ASSERT(world_set_block(w, 8, 67, 8, BLOCK_STONE));
    p.pos.y = 65.4f;
    p.render_pos = p.pos;
    player_update(&p, &in, w, PLAYER_STEP_DT);
    TEST_ASSERT(p.sneaking == true);
    TEST_ASSERT_FLOAT_EQ(p.height, PLAYER_SNEAK_HEIGHT, 1e-6f);
    p.pos.y = 65.4f;
    TEST_ASSERT(player_can_stand(&p, w) == false);
    TEST_ASSERT(world_set_block(w, 8, 67, 8, BLOCK_AIR));
    player_update(&p, &in, w, PLAYER_STEP_DT);
    TEST_ASSERT(p.sneaking == false);
    TEST_ASSERT(player_can_stand(&p, w) == true);

    /* Fractional headroom direct: same 65.4/67 pair blocks standing. */
    TEST_ASSERT(world_set_block(w, 8, 67, 8, BLOCK_STONE));
    p.pos = mmath_vec3(8.5f, 65.4f, 8.5f);
    TEST_ASSERT(player_can_stand(&p, w) == false);
    p.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    TEST_ASSERT(player_can_stand(&p, w) == true);
    TEST_ASSERT(world_set_block(w, 8, 67, 8, BLOCK_AIR));

    /* Bad args: NULL player never stands, NULL world reads roomy. */
    TEST_ASSERT(player_can_stand(NULL, w) == false);
    TEST_ASSERT(player_can_stand(&p, NULL) == true);
    world_destroy(w);
    return failures;
}

/* Test: sneak edge corner-cut guard and narrow-ledge behavior.
 * Outer L-corner (void quadrant x>=8,z>=8): diagonal sneak holds instead
 * of cutting the corner (MC third clip loop). Narrow 1-wide bridge along
 * X at z=8: walking its length advances, sideways sneak holds, sideways
 * walk falls.
 *
 * Returns: failure count.
 */
int test_physics_sneak_edge_corner(void)
{
    int failures = 0;
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    /* Void quadrant x 8..15, z 8..15, y 63..64 (2 deep). */
    for (int x = 8; x < 16; ++x) {
        for (int z = 8; z < 16; ++z) {
            TEST_ASSERT(world_set_block(w, x, 63, z, BLOCK_AIR));
            TEST_ASSERT(world_set_block(w, x, 64, z, BLOCK_AIR));
        }
    }
    Player p;
    player_init(&p);
    p.pos = mmath_vec3(7.5f, 65.0f, 7.5f);
    p.yaw = 0.0f; /* fwd -Z, right +X: fwd -1 + strafe +1 = +X+Z diagonal. */
    PlayerInput diag = no_input();
    diag.fwd = -1.0f;
    diag.strafe = 1.0f;
    diag.sneak = true;
    for (int i = 0; i < 240; ++i) {
        player_update(&p, &diag, w, PLAYER_STEP_DT);
    }
    /* Held before the void, never fell, corner not cut. */
    TEST_ASSERT(p.grounded == true);
    TEST_ASSERT(p.pos.y > 64.5f);
    TEST_ASSERT(p.pos.x < 8.5f);
    TEST_ASSERT(p.pos.z < 8.5f);
    world_destroy(w);

    /* Narrow bridge: keep only the z=8 row solid, void both sides. */
    World *narrow = make_floor_world();
    TEST_ASSERT(narrow != NULL);
    if (narrow == NULL) {
        return failures + 1;
    }
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            if (z == 8) {
                continue;
            }
            TEST_ASSERT(world_set_block(narrow, x, 63, z, BLOCK_AIR));
            TEST_ASSERT(world_set_block(narrow, x, 64, z, BLOCK_AIR));
        }
    }
    /* Along the bridge while sneaking: advances, stays centered. */
    Player q;
    player_init(&q);
    q.pos = mmath_vec3(4.5f, 65.0f, 8.5f);
    q.yaw = -1.5707963f; /* Face +X. */
    PlayerInput along = no_input();
    along.fwd = 1.0f;
    along.sneak = true;
    for (int i = 0; i < 120; ++i) {
        player_update(&q, &along, narrow, PLAYER_STEP_DT);
    }
    TEST_ASSERT(q.pos.x > 5.0f);
    TEST_ASSERT_FLOAT_EQ(q.pos.y, 65.0f, 0.05f);
    TEST_ASSERT(q.grounded == true);
    TEST_ASSERT(q.pos.z > 8.0f && q.pos.z < 9.0f);

    /* Sideways off the bridge while sneaking: holds the edge. */
    Player s;
    player_init(&s);
    s.pos = mmath_vec3(4.5f, 65.0f, 8.5f);
    s.yaw = 0.0f; /* Face -Z (toward z decreasing, off the bridge). */
    PlayerInput side = no_input();
    side.fwd = 1.0f;
    side.sneak = true;
    for (int i = 0; i < 240; ++i) {
        player_update(&s, &side, narrow, PLAYER_STEP_DT);
    }
    TEST_ASSERT(s.pos.y > 64.5f);
    TEST_ASSERT_FLOAT_EQ(s.pos.y, 65.0f, 0.05f);
    TEST_ASSERT(s.grounded == true);
    TEST_ASSERT(s.pos.z > 7.5f);

    /* Control: same sideways walk without sneak falls off. */
    Player c;
    player_init(&c);
    c.pos = mmath_vec3(4.5f, 65.0f, 8.5f);
    c.yaw = 0.0f;
    PlayerInput walk = no_input();
    walk.fwd = 1.0f;
    for (int i = 0; i < 240; ++i) {
        player_update(&c, &walk, narrow, PLAYER_STEP_DT);
    }
    TEST_ASSERT(c.pos.z < 8.0f);
    TEST_ASSERT(c.pos.y < 64.5f);
    world_destroy(narrow);
    return failures;
}

/* Test: distance-driven view bob (MC viewBobbing, default ON).
 * Offset derives from walk_phase/bob_amp only (no wall clock): zero amp
 * reads zero, known phases give exact lateral/vertical, walking builds
 * amplitude (sneak calmer), stopping/flying decays to still.
 *
 * Returns: failure count.
 */
int test_player_view_bob(void)
{
    int failures = 0;
    Player p;
    player_init(&p);
    TEST_ASSERT_FLOAT_EQ(p.bob_amp, 0.0f, 1e-6f);
    float lat = 99.0f;
    float vert = 99.0f;
    player_view_bob_offset(&p, &lat, &vert);
    TEST_ASSERT_FLOAT_EQ(lat, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(vert, 0.0f, 1e-6f);

    /* Exact phases at full walk amplitude. */
    p.walk_phase = 0.0f;
    p.bob_amp = PLAYER_BOB_AMP_WALK;
    player_view_bob_offset(&p, &lat, &vert);
    TEST_ASSERT_FLOAT_EQ(lat, 0.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(vert, -PLAYER_BOB_AMP_WALK, 1e-5f);
    p.walk_phase = 1.5707963f; /* pi/2: full lateral, zero dip. */
    player_view_bob_offset(&p, &lat, &vert);
    TEST_ASSERT_FLOAT_EQ(lat, PLAYER_BOB_AMP_WALK * 0.5f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(vert, 0.0f, 1e-4f);

    /* Zero amplitude always reads zero, any phase. */
    p.walk_phase = 2.3f;
    p.bob_amp = 0.0f;
    player_view_bob_offset(&p, &lat, &vert);
    TEST_ASSERT_FLOAT_EQ(lat, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(vert, 0.0f, 1e-6f);
    player_view_bob_offset(NULL, &lat, &vert);
    TEST_ASSERT_FLOAT_EQ(lat, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(vert, 0.0f, 1e-6f);

    /* Walking builds the stride clock and amplitude; stopping settles.
     * Walk 30 ticks (1.5 s, ~6.5 blocks) to stay inside the 16-wide floor. */
    World *w = make_floor_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Player m;
    player_init(&m);
    m.pos = mmath_vec3(4.5f, 65.0f, 8.5f);
    m.render_pos = m.pos;
    m.yaw = -1.5707963f;
    PlayerInput walk = no_input();
    walk.fwd = 1.0f;
    for (int i = 0; i < 30; ++i) {
        player_update(&m, &walk, w, PLAYER_STEP_DT);
    }
    TEST_ASSERT(m.grounded == true);
    TEST_ASSERT(m.walk_phase > 1.0f);
    TEST_ASSERT(m.bob_amp > 0.05f && m.bob_amp <= PLAYER_BOB_AMP_WALK + 1e-5f);
    player_view_bob_offset(&m, &lat, &vert);
    TEST_ASSERT(fabsf(lat) <= PLAYER_BOB_AMP_WALK * 0.5f + 1e-5f);
    TEST_ASSERT(vert <= 1e-6f && vert >= -PLAYER_BOB_AMP_WALK - 1e-5f);

    /* Sneak strides calmer (0.05 target, below the walk level). */
    Player sne;
    player_init(&sne);
    sne.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    sne.yaw = -1.5707963f;
    PlayerInput creep = no_input();
    creep.fwd = 1.0f;
    creep.sneak = true;
    for (int i = 0; i < 120; ++i) {
        player_update(&sne, &creep, w, PLAYER_STEP_DT);
    }
    TEST_ASSERT(sne.bob_amp > 0.01f && sne.bob_amp <= PLAYER_BOB_AMP_SNEAK + 1e-4f);
    TEST_ASSERT(sne.bob_amp < m.bob_amp);

    /* Stillness decays to zero (no frozen offset). */
    PlayerInput still = no_input();
    for (int i = 0; i < 120; ++i) {
        player_update(&m, &still, w, PLAYER_STEP_DT);
    }
    TEST_ASSERT(m.bob_amp < 0.01f);
    player_view_bob_offset(&m, &lat, &vert);
    TEST_ASSERT_FLOAT_EQ(lat, 0.0f, 0.01f);
    TEST_ASSERT_FLOAT_EQ(vert, 0.0f, 0.01f);

    /* Flight never bobs. */
    Player f;
    player_init(&f);
    f.flying = true;
    f.pos = mmath_vec3(8.5f, 70.0f, 8.5f);
    PlayerInput fly = no_input();
    fly.fwd = 1.0f;
    for (int i = 0; i < 60; ++i) {
        player_update(&f, &fly, w, PLAYER_STEP_DT);
    }
    TEST_ASSERT_FLOAT_EQ(f.bob_amp, 0.0f, 1e-5f);
    world_destroy(w);
    return failures;
}
