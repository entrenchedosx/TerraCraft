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

/* Empty input snapshot. */
static PlayerInput no_input(void)
{
    PlayerInput in;
    in.fwd = 0.0f;
    in.strafe = 0.0f;
    in.jump = false;
    in.sneak = false;
    in.sprint = false;
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
        player_update(&p, &in, w, 1.0f / 60.0f);
    }
    TEST_ASSERT(p.grounded == true);
    TEST_ASSERT_FLOAT_EQ(p.pos.y, 65.0f, 0.02f);
    TEST_ASSERT_FLOAT_EQ(p.vel.y, 0.0f, 1e-6f);
    /* Still grounded after more steps (resting contact is stable). */
    for (int i = 0; i < 60; ++i) {
        player_update(&p, &in, w, 1.0f / 60.0f);
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
        player_update(&p, &in, w, 1.0f / 60.0f);
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
        player_update(&p, &in, w, 1.0f / 60.0f);
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
    /* Settle one step so grounded latches. */
    player_update(&p, &in, w, 1.0f / 60.0f);
    TEST_ASSERT(p.grounded == true);

    in.jump = true;
    player_update(&p, &in, w, 1.0f / 60.0f);
    in.jump = false;
    TEST_ASSERT(p.grounded == false);

    float max_y = p.pos.y;
    for (int i = 0; i < 180; ++i) {
        player_update(&p, &in, w, 1.0f / 60.0f);
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
    in.jump = true;
    player_update(&dry_player, &in, dry, PLAYER_STEP_DT);
    player_update(&wet_player, &in, wet, PLAYER_STEP_DT);
    player_update(&partial_player, &in, partial, PLAYER_STEP_DT);
    player_update(&shallow_player, &in, shallow, PLAYER_STEP_DT);
    TEST_ASSERT(fabsf(wet_player.vel.z) < fabsf(dry_player.vel.z));
    TEST_ASSERT_FLOAT_EQ(wet_player.vel.z, -wet_player.walk_speed * 0.55f * 0.92f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(wet_player.vel.y, 2.4f, 1e-5f);
    /* A single source at the feet wets only part of the 1.8-block player;
     * a shallow flow wets less still. */
    TEST_ASSERT(fabsf(partial_player.vel.z) > fabsf(wet_player.vel.z));
    TEST_ASSERT(fabsf(partial_player.vel.z) < fabsf(dry_player.vel.z));
    TEST_ASSERT(partial_player.vel.y > wet_player.vel.y);
    TEST_ASSERT(fabsf(shallow_player.vel.z) > fabsf(partial_player.vel.z));
    TEST_ASSERT(fabsf(shallow_player.vel.z) < fabsf(dry_player.vel.z));
    TEST_ASSERT(shallow_player.vel.y > partial_player.vel.y);

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
    for (int i = 0; i < 60; ++i) {
        player_update(&p, &in, w, PLAYER_STEP_DT);
    }
    /* Climbed onto the ledge (y=66) and kept walking across it. */
    TEST_ASSERT(p.pos.y > 65.5f);
    TEST_ASSERT(p.pos.x > 10.0f);

    /* Embedded with zero velocity: one update ejects to free space. */
    Player stuck;
    player_init(&stuck);
    stuck.pos = mmath_vec3(10.5f, 65.0f, 8.5f);
    stuck.vel = mmath_vec3(0.0f, 0.0f, 0.0f);
    PlayerInput still = no_input();
    player_update(&stuck, &still, w, PLAYER_STEP_DT);
    float half = stuck.width * 0.5f;
    Vec3 mn = mmath_vec3(stuck.pos.x - half, stuck.pos.y, stuck.pos.z - half);
    Vec3 mx = mmath_vec3(stuck.pos.x + half, stuck.pos.y + stuck.height, stuck.pos.z + half);
    TEST_ASSERT(player_aabb_solid(w, mn, mx) == false);

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
    bool climbed = false;
    for (int i = 0; i < 120; ++i) {
        player_update(&p, &in, w, 1.0f / 60.0f);
        if (p.grounded && p.pos.y > 63.5f) {
            climbed = true;
            break;
        }
    }
    /* Up on the shore (feet 64), still at the waterline, standing. */
    TEST_ASSERT(climbed == true);
    TEST_ASSERT_FLOAT_EQ(p.pos.y, 64.0f, 0.05f);
    TEST_ASSERT(p.pos.x > 7.0f && p.pos.x < 10.0f);
    TEST_ASSERT(p.grounded == true);
    /* Essentially dry (a toe may still touch the waterline). */
    TEST_ASSERT(player_water_contact(w, &p) < 0.05f);
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
        player_update(&p, &in, w, 1.0f / 60.0f);
    }
    TEST_ASSERT(p.grounded == true);
    TEST_ASSERT(p.last_fall > 9.0f);
    TEST_ASSERT(player_water_contact(w, &p) > 0.0f);
    /* Dry control: the same drop onto stone reports no water. */
    p.pos = mmath_vec3(10.5f, 75.0f, 8.5f);
    p.vel = mmath_vec3(0.0f, 0.0f, 0.0f);
    p.grounded = false;
    p.fall_peak = -1.0f;
    p.last_fall = -1.0f;
    for (int i = 0; i < 400 && !p.grounded; ++i) {
        player_update(&p, &in, w, 1.0f / 60.0f);
    }
    TEST_ASSERT(p.grounded == true);
    TEST_ASSERT(p.last_fall > 9.0f);
    TEST_ASSERT_FLOAT_EQ(player_water_contact(w, &p), 0.0f, 1e-6f);
    world_destroy(w);
    return failures;
}
