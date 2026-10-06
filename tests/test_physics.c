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
    TEST_ASSERT(dry != NULL && wet != NULL);
    if (dry == NULL || wet == NULL) {
        world_destroy(dry);
        world_destroy(wet);
        return failures + 1;
    }
    TEST_ASSERT(world_set_block(wet, 8, 65, 8, BLOCK_WATER));
    Player dry_player, wet_player;
    player_init(&dry_player);
    player_init(&wet_player);
    dry_player.pos = wet_player.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    dry_player.grounded = wet_player.grounded = true;
    PlayerInput in = no_input();
    in.fwd = 1.0f;
    in.jump = true;
    player_update(&dry_player, &in, dry, PLAYER_STEP_DT);
    player_update(&wet_player, &in, wet, PLAYER_STEP_DT);
    TEST_ASSERT(fabsf(wet_player.vel.z) < fabsf(dry_player.vel.z));
    TEST_ASSERT(wet_player.vel.y > 2.0f);
    world_destroy(dry);
    world_destroy(wet);
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
