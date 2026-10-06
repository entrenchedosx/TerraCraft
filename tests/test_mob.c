#include "test_main.h"

#include "core/path.h"
#include "game/entity.h"
#include "game/inventory.h"
#include "game/mob.h"
#include "game/mob_model.h"
#include "game/player_animation.h"
#include "game/player_model.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/entity_save.h"
#include "world/world.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Flat stone floor fixture: y 60..64 solid over chunk (0,0), plus a
 * one-block step ledge (a 65-high pillar row at x=10) and a wall (x=12,
 * y 60..70). Eye/feet heights must account for mob dimensions.
 */
static World *make_mob_world(void)
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
    chunk_set_block(c, 10, 65, 8, BLOCK_STONE); /* Step ledge. */
    for (int y = 60; y <= 70; ++y) {
        chunk_set_block(c, 12, y, 8, BLOCK_STONE); /* Wall. */
    }
    if (world_add_chunk(w, c) != 0) {
        chunk_destroy(c);
        world_destroy(w);
        return NULL;
    }
    return w;
}

/* Test: handle create/resolve/remove/reuse/generation safety.
 *
 * Returns: failure count.
 */
int test_mob_handles(void)
{
    int failures = 0;
    MobPool pool;
    mob_pool_init(&pool, 1234u);
    TEST_ASSERT(mob_active_count(&pool) == 0);
    TEST_ASSERT(mob_active_count(NULL) == 0);
    EntityId a = mob_spawn(&pool, ENTITY_COW, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    TEST_ASSERT(a != ENTITY_ID_NULL);
    TEST_ASSERT(mob_active_count(&pool) == 1);
    TEST_ASSERT(mob_count_type(&pool, ENTITY_COW) == 1);
    TEST_ASSERT(mob_count_type(&pool, ENTITY_ZOMBIE) == 0);
    Mob *m = mob_resolve(&pool, a);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        TEST_ASSERT(m->type == ENTITY_COW);
        TEST_ASSERT_FLOAT_EQ(m->health, 10.0f, 1e-4f);
        TEST_ASSERT(m->dead == false && m->state == MOB_STATE_IDLE);
        /* Spawn zeroes all runtime state (Release stack garbage killed
         * mobs via phantom fall damage before this was pinned). */
        TEST_ASSERT(m->last_fall < 0.0f && m->fall_peak < 0.0f);
        TEST_ASSERT(m->hurt_t == 0.0f && m->attack_cd == 0.0f);
        TEST_ASSERT(m->hurt_from.x == 0.0f && m->hurt_from.y == 0.0f && m->hurt_from.z == 0.0f);
        TEST_ASSERT(m->path_len == 0 && m->repath_t == 0.0f);
    }
    /* Bad handles never resolve. */
    TEST_ASSERT(mob_resolve(&pool, ENTITY_ID_NULL) == NULL);
    TEST_ASSERT(mob_resolve(&pool, ENTITY_PLAYER_ID) == NULL);
    TEST_ASSERT(mob_resolve(NULL, a) == NULL);
    TEST_ASSERT(mob_resolve(&pool, entity_id_make(99, 1)) == NULL);
    TEST_ASSERT(entity_id_index(ENTITY_ID_NULL) < 0);
    /* Invalid spawns rejected. */
    TEST_ASSERT(mob_spawn(&pool, ENTITY_NONE, mmath_vec3(0.0f, 70.0f, 0.0f), 0.0f) == ENTITY_ID_NULL);
    TEST_ASSERT(mob_spawn(NULL, ENTITY_COW, mmath_vec3(0.0f, 70.0f, 0.0f), 0.0f) ==
                ENTITY_ID_NULL);
    TEST_ASSERT(entity_id_make(-1, 1) == ENTITY_ID_NULL);
    TEST_ASSERT(entity_id_make(MOB_MAX, 1) == ENTITY_ID_NULL);
    /* Removal invalidates; slot reuse bumps generation (no aliasing). */
    mob_remove(&pool, a);
    TEST_ASSERT(mob_active_count(&pool) == 0);
    TEST_ASSERT(mob_resolve(&pool, a) == NULL);
    mob_remove(&pool, a); /* Stale remove: silent no-op. */
    EntityId b = mob_spawn(&pool, ENTITY_ZOMBIE, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    TEST_ASSERT(b != ENTITY_ID_NULL && b != a);
    TEST_ASSERT(entity_id_index(b) == entity_id_index(a));
    TEST_ASSERT(entity_id_gen(b) != entity_id_gen(a));
    TEST_ASSERT(mob_resolve(&pool, b) != NULL);
    TEST_ASSERT(mob_resolve(&pool, a) == NULL);
    /* Fill the pool: further spawns fail cleanly, never overflow. */
    int extra = 0;
    for (int i = 0; i < MOB_MAX + 4; ++i) {
        if (mob_spawn(&pool, ENTITY_COW, mmath_vec3(1.5f, 65.0f, 1.5f), 0.0f) !=
            ENTITY_ID_NULL) {
            ++extra;
        }
    }
    TEST_ASSERT(mob_active_count(&pool) == MOB_MAX);
    TEST_ASSERT(extra == MOB_MAX - 1);
    mob_pool_init(NULL, 0); /* Safe. */
    return failures;
}

/* Test: falls onto the floor, lands grounded, reports fall distance.
 *
 * Returns: failure count.
 */
int test_mob_physics_fall(void)
{
    int failures = 0;
    World *w = make_mob_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool pool;
    mob_pool_init(&pool, 42u);
    EntityId id = mob_spawn(&pool, ENTITY_COW, mmath_vec3(4.5f, 75.0f, 4.5f), 0.0f);
    Mob *m = mob_resolve(&pool, id);
    TEST_ASSERT(m != NULL);
    if (m == NULL) {
        world_destroy(w);
        return failures + 1;
    }
    for (int i = 0; i < 240 && !m->grounded; ++i) {
        m->prev_pos = m->pos;
        mob_physics_step(m, w, 1.0f / 60.0f);
    }
    TEST_ASSERT(m->grounded == true);
    TEST_ASSERT_FLOAT_EQ(m->pos.y, 65.0f, 1e-3f);
    TEST_ASSERT(m->last_fall > 9.0f && m->last_fall < 11.0f);
    /* Null/world/dt guards. */
    mob_physics_step(NULL, w, 0.016f);
    mob_physics_step(m, NULL, 0.016f);
    mob_physics_step(m, w, 0.0f);
    mob_physics_step(m, w, -1.0f);
    world_destroy(w);
    return failures;
}

/* Test: wall blocks, one-block step climbs, no tunneling at speed.
 *
 * Returns: failure count.
 */
int test_mob_physics_wall_step(void)
{
    int failures = 0;
    World *w = make_mob_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool pool;
    mob_pool_init(&pool, 7u);
    /* Drive +X into the x=12 wall from (8.5, 65, 8.5): must stop short. */
    EntityId id = mob_spawn(&pool, ENTITY_COW, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    Mob *m = mob_resolve(&pool, id);
    TEST_ASSERT(m != NULL);
    if (m == NULL) {
        world_destroy(w);
        return failures + 1;
    }
    m->grounded = true;
    m->wish_dir = mmath_vec3(1.0f, 0.0f, 0.0f);
    m->wish_speed = 3.0f;
    for (int i = 0; i < 180; ++i) {
        m->prev_pos = m->pos;
        mob_physics_step(m, w, 1.0f / 60.0f);
    }
    TEST_ASSERT(m->pos.x < 12.0f - 0.3f); /* Wall face at x=12. */
    TEST_ASSERT(m->pos.x > 10.0f);
    /* Drive +X toward the x=10 step ledge from (6.5, 65, 8.5): climbs onto
     * it (peak 66), crosses, and continues to the wall. */
    m->pos = mmath_vec3(6.5f, 65.0f, 8.5f);
    m->vel = mmath_vec3(0.0f, 0.0f, 0.0f);
    m->grounded = true;
    float peak = 65.0f;
    for (int i = 0; i < 240; ++i) {
        m->prev_pos = m->pos;
        mob_physics_step(m, w, 1.0f / 60.0f);
        if (m->pos.y > peak) {
            peak = m->pos.y;
        }
    }
    TEST_ASSERT(peak >= 65.9f);       /* Stepped up onto the ledge. */
    TEST_ASSERT(m->pos.x > 11.0f);    /* Crossed it and kept walking. */
    TEST_ASSERT(m->pos.x < 12.0f - 0.3f); /* Stopped at the wall. */
    world_destroy(w);
    return failures;
}

/* Shared fixtures for AI/combat/spawn tests: flat world + pools + a
 * daytime player snapshot standing on the floor.
 */
static void test_mob_player(MobPlayerInfo *pi, float x, float y, float z)
{
    pi->pos = mmath_vec3(x, y, z);
    pi->eye_height = 1.62f;
    pi->alive = true;
    pi->creative = false;
    pi->day_progress = 0.5f;
}

/* Test: mob definitions (stats, drops, inert fallback).
 *
 * Returns: failure count.
 */
int test_mob_definitions(void)
{
    int failures = 0;
    const MobDefinition *moss = mob_definition(ENTITY_COW);
    TEST_ASSERT(moss->type == ENTITY_COW && moss->hostile == false);
    TEST_ASSERT_FLOAT_EQ(moss->max_health, 10.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(moss->width, 0.9f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(moss->height, 1.4f, 1e-4f);
    TEST_ASSERT(moss->ndrops == 2 && moss->drops[0].item == ITEM_RAW_BEEF &&
                moss->drops[1].item == ITEM_LEATHER);
    TEST_ASSERT(moss->name != NULL);
    const MobDefinition *zomb = mob_definition(ENTITY_ZOMBIE);
    TEST_ASSERT(zomb->type == ENTITY_ZOMBIE && zomb->hostile == true && zomb->ranged == false);
    TEST_ASSERT_FLOAT_EQ(zomb->damage, 3.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(zomb->detect_range, 14.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(zomb->max_health, 20.0f, 1e-4f);
    TEST_ASSERT(zomb->ndrops == 1 && zomb->drops[0].item == ITEM_ROTTEN_FLESH);
    TEST_ASSERT(zomb->drops[0].min_count == 0 && zomb->drops[0].max_count == 2);
    TEST_ASSERT(zomb->model == 1);
    const MobDefinition *bad = mob_definition((EntityType)99);
    TEST_ASSERT(bad->type == ENTITY_NONE && bad->max_health > 0.0f);
    return failures;
}

/* Test: passive idle/wander cycle moves the mob; cliff turns it around.
 *
 * Returns: failure count.
 */
int test_mob_ai_passive(void)
{
    int failures = 0;
    World *w = make_mob_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool pool;
    mob_pool_init(&pool, 999u);
    EntityPool drops;
    entity_pool_clear(&drops);
    MobPlayerInfo pi;
    test_mob_player(&pi, 4.5f, 65.0f, 4.5f);
    MobFrameEvents ev;
    EntityId id = mob_spawn(&pool, ENTITY_COW, mmath_vec3(4.5f, 65.0f, 4.5f), 0.0f);
    TEST_ASSERT(id != ENTITY_ID_NULL);
    /* Simulate 20 s in AI-tick steps: must wander (move) at some point. */
    Vec3 start = mmath_vec3(4.5f, 65.0f, 4.5f);
    (void)start;
    float moved = 0.0f;
    int wandered = 0;
    for (int i = 0; i < 200; ++i) {
        mob_update_all(&pool, &drops, w, &pi, 0.1f, &ev);
        Mob *m = mob_resolve(&pool, id);
        TEST_ASSERT(m != NULL);
        if (m == NULL) {
            break;
        }
        if (m->state == MOB_STATE_WANDER) {
            wandered = 1;
        }
        float dx = m->pos.x - 4.5f;
        float dz = m->pos.z - 4.5f;
        float d = dx * dx + dz * dz;
        if (d > moved) {
            moved = d;
        }
        TEST_ASSERT(m->dead == false);
    }
    TEST_ASSERT(wandered == 1);
    TEST_ASSERT(moved > 1.0f);
    /* Cliff: teleport near the world edge (x=15.5, air beyond chunk) and
     * face outward; wandering must turn, not march into the void. */
    Mob *m = mob_resolve(&pool, id);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        m->pos = mmath_vec3(15.5f, 65.0f, 8.5f);
        m->prev_pos = m->pos;
        m->stuck_pos = m->pos;
        m->state = MOB_STATE_WANDER;
        m->state_t = 0.0f;
        m->wander_yaw = -1.5707963f; /* Facing +X (off the edge). */
        m->wander_t = 5.0f;
        float yaw0 = m->wander_yaw;
        for (int i = 0; i < 60; ++i) {
            mob_update_all(&pool, &drops, w, &pi, 0.1f, &ev);
        }
        Mob *m2 = mob_resolve(&pool, id);
        TEST_ASSERT(m2 != NULL);
        if (m2 != NULL) {
            TEST_ASSERT(m2->wander_yaw != yaw0); /* Turned around. */
            TEST_ASSERT(m2->pos.x < 16.5f);     /* Never left the world. */
        }
    }
    world_destroy(w);
    return failures;
}

/* Test: hostile acquire/chase/attack/lose + creative/dead gating.
 *
 * Returns: failure count.
 */
int test_mob_ai_hostile(void)
{
    int failures = 0;
    World *w = make_mob_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool pool;
    mob_pool_init(&pool, 31u);
    EntityPool drops;
    entity_pool_clear(&drops);
    MobPlayerInfo pi;
    test_mob_player(&pi, 4.5f, 65.0f, 4.5f);
    MobFrameEvents ev;
    EntityId id = mob_spawn(&pool, ENTITY_ZOMBIE, mmath_vec3(9.5f, 65.0f, 4.5f), 0.0f);
    TEST_ASSERT(id != ENTITY_ID_NULL);
    /* Player 5 away: acquired within a second, then approaches. */
    int chased = 0;
    for (int i = 0; i < 30; ++i) {
        mob_update_all(&pool, &drops, w, &pi, 0.1f, &ev);
        Mob *m = mob_resolve(&pool, id);
        if (m != NULL && (m->state == MOB_STATE_CHASE || m->state == MOB_STATE_ATTACK)) {
            chased = 1;
        }
    }
    TEST_ASSERT(chased == 1);
    Mob *m = mob_resolve(&pool, id);
    TEST_ASSERT(m != NULL && m->target == ENTITY_PLAYER_ID);
    /* Point-blank: attacks land damage on cooldown (not per frame).
     * NOTE: mob_update_all clears ev each call, so accumulate across ticks. */
    pi.pos = mmath_vec3(m->pos.x + 1.0f, 65.0f, m->pos.z);
    int hits = 0;
    float damage = 0.0f;
    for (int i = 0; i < 30; ++i) {
        mob_update_all(&pool, &drops, w, &pi, 0.1f, &ev);
        hits += ev.player_hits;
        damage += ev.player_damage;
    }
    TEST_ASSERT(hits >= 1 && hits <= 4); /* 3 s at 1.2 s cooldown. */
    TEST_ASSERT_FLOAT_EQ(damage, (float)hits * 3.0f, 1e-3f);
    /* Run away past lose range: target dropped. */
    pi.pos = mmath_vec3(40.0f, 65.0f, 4.5f);
    for (int i = 0; i < 30; ++i) {
        mob_update_all(&pool, &drops, w, &pi, 0.1f, &ev);
    }
    m = mob_resolve(&pool, id);
    TEST_ASSERT(m != NULL && m->target == ENTITY_ID_NULL);
    TEST_ASSERT(m->state == MOB_STATE_IDLE || m->state == MOB_STATE_WANDER);
    /* Creative players are never acquired. */
    pi.creative = true;
    pi.pos = mmath_vec3(m->pos.x + 1.0f, 65.0f, m->pos.z);
    for (int i = 0; i < 30; ++i) {
        mob_update_all(&pool, &drops, w, &pi, 0.1f, &ev);
    }
    m = mob_resolve(&pool, id);
    TEST_ASSERT(m != NULL && m->target == ENTITY_ID_NULL);
    pi.creative = false;
    /* Dead players drop targets too. */
    m->target = ENTITY_PLAYER_ID;
    m->state = MOB_STATE_CHASE;
    pi.alive = false;
    mob_update_all(&pool, &drops, w, &pi, 0.5f, &ev);
    m = mob_resolve(&pool, id);
    TEST_ASSERT(m != NULL && m->target == ENTITY_ID_NULL);
    world_destroy(w);
    return failures;
}

/* Count cow-loot items across active drop entities (helper). */
static int test_drop_cow(const EntityPool *drops)
{
    uint32_t total = 0;
    for (int i = 0; i < ENTITY_MAX; ++i) {
        if (drops->items[i].active && (drops->items[i].stack.item == ITEM_RAW_BEEF ||
                                       drops->items[i].stack.item == ITEM_LEATHER)) {
            total += drops->items[i].stack.count;
        }
    }
    return (int)total;
}

/* Test: hurt window, death drops exactly once, knockback clamp/resist.
 *
 * Returns: failure count.
 */
int test_mob_damage(void)
{
    int failures = 0;
    MobPool pool;
    mob_pool_init(&pool, 5u);
    EntityPool drops;
    entity_pool_clear(&drops);
    EntityId id = mob_spawn(&pool, ENTITY_COW, mmath_vec3(0.0f, 70.0f, 0.0f), 0.0f);
    Vec3 east = mmath_vec3(1.0f, 0.0f, 0.0f);
    /* Non-lethal hit: damage, knockback, hurt window opens. */
    TEST_ASSERT(living_entity_damage(&pool, &drops, id, 2.0f, east, 10.0f, &east) == false);
    Mob *m = mob_resolve(&pool, id);
    TEST_ASSERT(m != NULL);
    if (m == NULL) {
        return failures + 1;
    }
    TEST_ASSERT_FLOAT_EQ(m->health, 8.0f, 1e-4f);
    TEST_ASSERT(m->state == MOB_STATE_HURT);
    TEST_ASSERT(m->hurt_t > 0.0f);
    TEST_ASSERT_FLOAT_EQ(m->vel.x, 10.0f, 1e-3f);
    /* Hurt window: immediate second hit ignored. */
    TEST_ASSERT(living_entity_damage(&pool, &drops, id, 2.0f, east, 10.0f, &east) == false);
    TEST_ASSERT_FLOAT_EQ(m->health, 8.0f, 1e-4f);
    /* Knockback clamp: absurd power stays bounded. */
    m->hurt_t = 0.0f;
    m->vel = mmath_vec3(0.0f, 0.0f, 0.0f);
    TEST_ASSERT(living_entity_damage(&pool, &drops, id, 1.0f, east, 100.0f, &east) == false);
    {
        float hs = sqrtf(m->vel.x * m->vel.x + m->vel.z * m->vel.z);
        TEST_ASSERT(hs <= 12.001f);
    }
    TEST_ASSERT(m->vel.y <= 6.001f);
    /* Lethal hit: death, drops exactly once, further hits silent. */
    m->hurt_t = 0.0f;
    int drops0 = entity_active_count(&drops);
    TEST_ASSERT(living_entity_damage(&pool, &drops, id, 50.0f, east, 5.0f, &east) == true);
    TEST_ASSERT(m->dead == true && m->state == MOB_STATE_DEAD);
    int gained = entity_active_count(&drops) - drops0;
    TEST_ASSERT(gained >= 1 && gained <= 2); /* Beef 1-3 + leather 0-2. */
    TEST_ASSERT(test_drop_cow(&drops) >= 1);
    TEST_ASSERT(living_entity_damage(&pool, &drops, id, 50.0f, east, 5.0f, &east) == false);
    TEST_ASSERT(entity_active_count(&drops) == drops0 + gained); /* Nothing more. */
    /* Bad handles silent. */
    TEST_ASSERT(living_entity_damage(&pool, &drops, ENTITY_ID_NULL, 5.0f, east, 5.0f, &east) ==
                false);
    TEST_ASSERT(living_entity_damage(NULL, &drops, id, 5.0f, east, 5.0f, &east) == false);
    return failures;
}

/* Test: fall-damage frame events (hurt vs death, gated ticks silent).
 *
 * Returns: failure count.
 */
int test_mob_fall_events(void)
{
    int failures = 0;
    World *w = make_mob_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool pool;
    mob_pool_init(&pool, 77u);
    EntityPool drops;
    entity_pool_clear(&drops);
    MobPlayerInfo pi;
    test_mob_player(&pi, 0.5f, 66.0f, 0.5f); /* Far from drops, noon. */
    MobFrameEvents ev;
    /* Zombie HP 20, 10-block fall (~7 damage): survives, exactly one hurt. */
    EntityId id = mob_spawn(&pool, ENTITY_ZOMBIE, mmath_vec3(4.5f, 75.0f, 4.5f), 0.0f);
    TEST_ASSERT(id != ENTITY_ID_NULL);
    int hurt_total = 0;
    int died_total = 0;
    for (int i = 0; i < 240; ++i) {
        mob_update_all(&pool, &drops, w, &pi, 1.0f / 60.0f, &ev);
        hurt_total += ev.mobs_hurt;
        died_total += ev.mobs_died;
    }
    Mob *m = mob_resolve(&pool, id);
    TEST_ASSERT(m != NULL && m->dead == false);
    TEST_ASSERT(died_total == 0);
    TEST_ASSERT(hurt_total == 1);
    if (m != NULL) {
        TEST_ASSERT(m->health < 20.0f && m->health > 0.0f);
    }
    /* Cow HP 10, 30-block fall: dies, exactly one death, no hurt. */
    EntityId doomed = mob_spawn(&pool, ENTITY_COW, mmath_vec3(6.5f, 95.0f, 6.5f), 0.0f);
    TEST_ASSERT(doomed != ENTITY_ID_NULL);
    hurt_total = 0;
    died_total = 0;
    for (int i = 0; i < 400; ++i) {
        mob_update_all(&pool, &drops, w, &pi, 1.0f / 60.0f, &ev);
        hurt_total += ev.mobs_hurt;
        died_total += ev.mobs_died;
    }
    TEST_ASSERT(mob_resolve(&pool, doomed) == NULL); /* Corpse reaped. */
    TEST_ASSERT(died_total == 1);
    TEST_ASSERT(hurt_total == 0);
    world_destroy(w);
    return failures;
}

/* Test: entity raycast hit/miss/nearest/dead-skip/block-closer logic.
 *
 * Returns: failure count.
 */
int test_mob_raycast(void)
{
    int failures = 0;
    TEST_ASSERT_FLOAT_EQ(PLAYER_ATTACK_REACH, 3.0f, 1e-6f);
    MobPool pool;
    mob_pool_init(&pool, 11u);
    Vec3 eye = mmath_vec3(0.0f, 66.0f, 0.0f);
    Vec3 fwd = mmath_vec3(1.0f, 0.0f, 0.0f);
    float dist = 0.0f;
    EntityId hit = ENTITY_ID_NULL;
    /* Empty pool: miss. */
    TEST_ASSERT(mob_raycast(&pool, eye, fwd, 5.0f, &dist, &hit) == false);
    TEST_ASSERT(hit == ENTITY_ID_NULL);
    /* Two mobs ahead: nearest wins. */
    EntityId near_id =
        mob_spawn(&pool, ENTITY_COW, mmath_vec3(3.0f, 65.0f, 0.0f), 0.0f);
    EntityId far_id =
        mob_spawn(&pool, ENTITY_ZOMBIE, mmath_vec3(4.0f, 65.0f, 0.0f), 0.0f);
    TEST_ASSERT(near_id != ENTITY_ID_NULL && far_id != ENTITY_ID_NULL);
    Vec3 aim = mmath_vec3(1.0f, -0.3f, 0.0f);
    TEST_ASSERT(mob_raycast(&pool, eye, aim, 8.0f, &dist, &hit) == true);
    TEST_ASSERT(hit == near_id);
    TEST_ASSERT(dist > 2.0f && dist < 4.0f);
    /* Dead mobs are ignored. */
    Mob *dead = mob_resolve(&pool, far_id);
    TEST_ASSERT(dead != NULL);
    if (dead != NULL) {
        dead->dead = true;
    }
    Mob *nm = mob_resolve(&pool, near_id);
    TEST_ASSERT(nm != NULL);
    if (nm != NULL) {
        nm->dead = true;
    }
    TEST_ASSERT(mob_raycast(&pool, eye, aim, 8.0f, NULL, NULL) == false);
    /* Bad args safe. */
    TEST_ASSERT(mob_raycast(NULL, eye, aim, 8.0f, &dist, &hit) == false);
    TEST_ASSERT(mob_raycast(&pool, eye, mmath_vec3(0.0f, 0.0f, 0.0f), 8.0f, &dist, &hit) == false);
    TEST_ASSERT(mob_raycast(&pool, eye, aim, 0.0f, &dist, &hit) == false);
    /* A camera inside a mob's model still registers the immediate hit. */
    MobPool inside_pool;
    mob_pool_init(&inside_pool, 12u);
    EntityId inside_id = mob_spawn(&inside_pool, ENTITY_COW, mmath_vec3(0.0f, 65.0f, 0.0f), 0.0f);
    TEST_ASSERT(inside_id != ENTITY_ID_NULL);
    TEST_ASSERT(mob_raycast(&inside_pool, mmath_vec3(0.0f, 65.5f, 0.0f), fwd, 3.0f, &dist, &hit));
    TEST_ASSERT(hit == inside_id);
    TEST_ASSERT_FLOAT_EQ(dist, 0.0f, 1e-6f);
    /* The visible cow muzzle extends beyond the legacy square hitbox and
     * remains hittable as its root yaw rotates the whole model. */
    const float cow_yaws[3] = {0.0f, 1.57079632679f, 3.14159265359f};
    for (int i = 0; i < 3; ++i) {
        MobPool muzzle_pool;
        mob_pool_init(&muzzle_pool, 13u + (uint32_t)i);
        EntityId muzzle_id = mob_spawn(&muzzle_pool, ENTITY_COW, mmath_vec3(3.0f, 65.0f, 0.0f),
                                       cow_yaws[i]);
        TEST_ASSERT(muzzle_id != ENTITY_ID_NULL);
        float facing = cow_yaws[i] + 3.14159265359f;
        Vec3 muzzle = mmath_vec3(3.0f + sinf(facing) * 0.45f, 66.1f,
                                 cosf(facing) * 0.45f);
        Vec3 ray = mmath_vec3_sub(muzzle, eye);
        TEST_ASSERT(mob_raycast(&muzzle_pool, eye, ray, 4.0f, &dist, &hit));
        TEST_ASSERT(hit == muzzle_id);
    }
    /* The ray follows the geometrically rendered mob: its model-space
     * bounds (collision box plus the protruding muzzle box the model
     * raycast covers), so visible geometry is always hittable. */
    MobPool visual_pool;
    mob_pool_init(&visual_pool, 17u);
    EntityId visual_id = mob_spawn(&visual_pool, ENTITY_COW, mmath_vec3(12.0f, 65.0f, 0.0f), 0.0f);
    Mob *visual_mob = mob_resolve(&visual_pool, visual_id);
    TEST_ASSERT(visual_mob != NULL);
    if (visual_mob != NULL) {
        visual_mob->render_pos = visual_mob->pos;
    }
    TEST_ASSERT(mob_raycast(&visual_pool, eye, mmath_vec3(1.0f, 0.0f, -0.018f), 20.0f, &dist, &hit));
    TEST_ASSERT(hit == visual_id);
    /* The skeleton's visible arm also reaches beyond its 0.6-wide body box. */
    MobPool arm_pool;
    mob_pool_init(&arm_pool, 18u);
    EntityId arm_id = mob_spawn(&arm_pool, ENTITY_SKELETON, mmath_vec3(3.0f, 65.0f, 0.0f), 0.0f);
    TEST_ASSERT(arm_id != ENTITY_ID_NULL);
    TEST_ASSERT(mob_raycast(&arm_pool, eye, mmath_vec3(1.0f, 0.0f, 0.0f), 4.0f, &dist, &hit));
    TEST_ASSERT(hit == arm_id);
    /* Swing shielding: solid blocks stop a swing, decor never does
     * (swinging through a flower at a mob must connect). */
    TEST_ASSERT(mob_block_shields(BLOCK_STONE) == true);
    TEST_ASSERT(mob_block_shields(BLOCK_GLASS) == true);
    TEST_ASSERT(mob_block_shields(BLOCK_LEAVES) == true);
    TEST_ASSERT(mob_block_shields(BLOCK_GRASS_PLANT) == false);
    TEST_ASSERT(mob_block_shields(BLOCK_FLOWER) == false);
    TEST_ASSERT(mob_block_shields(BLOCK_TORCH) == false);
    TEST_ASSERT(mob_block_shields(BLOCK_WATER) == false);
    TEST_ASSERT(mob_block_shields(BLOCK_AIR) == false);
    /* Per-type hurt/die sounds: cows moo, everyone else thuds. */
    TEST_ASSERT(mob_hurt_sound((int)ENTITY_COW) == AUDIO_COW_HURT);
    TEST_ASSERT(mob_die_sound((int)ENTITY_COW) == AUDIO_COW_DIE);
    TEST_ASSERT(mob_hurt_sound((int)ENTITY_ZOMBIE) == AUDIO_MOB_HURT);
    TEST_ASSERT(mob_die_sound((int)ENTITY_ZOMBIE) == AUDIO_MOB_DIE);
    TEST_ASSERT(mob_hurt_sound((int)ENTITY_SKELETON) == AUDIO_MOB_HURT);
    TEST_ASSERT(mob_die_sound((int)ENTITY_SKELETON) == AUDIO_MOB_DIE);
    TEST_ASSERT(mob_hurt_sound(99) == AUDIO_MOB_HURT);
    return failures;
}

/* Test: melee tool stats (original tuning) + NULL safety.
 *
 * Returns: failure count.
 */
int test_mob_tool_stats(void)
{
    int failures = 0;
    float dmg = 0.0f, cd = 0.0f;
    mob_tool_stats(ITEM_NONE, &dmg, &cd);
    TEST_ASSERT_FLOAT_EQ(dmg, 1.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(cd, 0.4f, 1e-6f);
    mob_tool_stats(ITEM_WOOD_SHOVEL, &dmg, &cd);
    TEST_ASSERT_FLOAT_EQ(dmg, 2.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(cd, 0.5f, 1e-6f);
    mob_tool_stats(ITEM_STONE_PICKAXE, &dmg, &cd);
    TEST_ASSERT_FLOAT_EQ(dmg, 3.0f, 1e-6f);
    mob_tool_stats(ITEM_STONE_AXE, &dmg, &cd);
    TEST_ASSERT_FLOAT_EQ(dmg, 4.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(cd, 0.8f, 1e-6f);
    mob_tool_stats((ItemId)BLOCK_STONE, &dmg, &cd);
    TEST_ASSERT_FLOAT_EQ(dmg, 1.0f, 1e-6f); /* Blocks hit as fists. */
    mob_tool_stats(ITEM_APPLE, NULL, NULL); /* Safe. */
    return failures;
}

/* 5x5-chunk grassland fixture (stone 60..63, grass top 64) for spawner
 * tests. NULL on OOM.
 */
static World *make_spawn_world(void)
{
    World *w = world_create();
    if (w == NULL) {
        return NULL;
    }
    w->seed = 777L;
    for (int cx = -2; cx <= 2; ++cx) {
        for (int cz = -2; cz <= 2; ++cz) {
            Chunk *c = chunk_create(cx, cz);
            if (c == NULL) {
                world_destroy(w);
                return NULL;
            }
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    for (int y = 60; y <= 63; ++y) {
                        chunk_set_block(c, x, y, z, BLOCK_STONE);
                    }
                    chunk_set_block(c, x, 64, z, BLOCK_GRASS);
                }
            }
            if (world_add_chunk(w, c) != 0) {
                chunk_destroy(c);
                world_destroy(w);
                return NULL;
            }
        }
    }
    return w;
}

/* Run the spawner up to `steps` 0.5 s ticks, stopping early when pred is
 * true. Returns ticks run.
 */
static int run_spawner(MobPool *pool, EntityPool *drops, World *w, MobPlayerInfo *pi, int steps)
{
    MobFrameEvents ev;
    int i = 0;
    for (; i < steps; ++i) {
        mob_update_all(pool, drops, w, pi, 0.5f, &ev);
        if (mob_active_count(pool) > 0) {
            ++i;
            break;
        }
    }
    return i;
}

/* Test: daylight spawns passives on grass within the 24..48 ring.
 *
 * Returns: failure count.
 */
int test_mob_spawn_passive(void)
{
    int failures = 0;
    World *w = make_spawn_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool pool;
    mob_pool_init(&pool, 12345u);
    EntityPool drops;
    entity_pool_clear(&drops);
    MobPlayerInfo pi;
    test_mob_player(&pi, 0.5f, 66.0f, 0.5f); /* Noon. */
    int used = run_spawner(&pool, &drops, w, &pi, 120);
    TEST_ASSERT(mob_count_type(&pool, ENTITY_COW) >= 1);
    TEST_ASSERT(mob_count_type(&pool, ENTITY_ZOMBIE) == 0);
    TEST_ASSERT(used < 120);
    /* Spawn distance ring honored. */
    for (int i = 0; i < MOB_MAX; ++i) {
        if (!pool.mobs[i].active) {
            continue;
        }
        float dx = pool.mobs[i].pos.x - pi.pos.x;
        float dz = pool.mobs[i].pos.z - pi.pos.z;
        float d = sqrtf(dx * dx + dz * dz);
        TEST_ASSERT(d >= MOB_SPAWN_MIN_DIST - 1.0f && d <= 60.0f);
    }
    world_destroy(w);
    return failures;
}

/* Test: night spawns hostiles (never passives); day spawns no hostiles.
 *
 * Returns: failure count.
 */
int test_mob_spawn_hostile(void)
{
    int failures = 0;
    World *w = make_spawn_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool pool;
    mob_pool_init(&pool, 54321u);
    EntityPool drops;
    entity_pool_clear(&drops);
    MobPlayerInfo pi;
    test_mob_player(&pi, 0.5f, 66.0f, 0.5f);
    pi.day_progress = 0.0f; /* Midnight. */
    run_spawner(&pool, &drops, w, &pi, 120);
    /* Night spawns hostiles of either kind (skeletons share the night
     * rotation); never passives. */
    TEST_ASSERT(mob_count_type(&pool, ENTITY_ZOMBIE) +
                    mob_count_type(&pool, ENTITY_SKELETON) >=
                1);
    TEST_ASSERT(mob_count_type(&pool, ENTITY_COW) == 0);
    world_destroy(w);
    return failures;
}

/* Test: caps enforced; water/headroom columns rejected; far hostile
 * despawns while passives persist.
 *
 * Returns: failure count.
 */
int test_mob_spawn_rules(void)
{
    int failures = 0;
    World *w = make_spawn_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool pool;
    mob_pool_init(&pool, 9999u);
    EntityPool drops;
    entity_pool_clear(&drops);
    MobPlayerInfo pi;
    test_mob_player(&pi, 0.5f, 66.0f, 0.5f);
    MobFrameEvents ev;
    /* Prefill to the passive cap: spawner adds nothing more. */
    for (int i = 0; i < MOB_MAX_PASSIVE; ++i) {
        TEST_ASSERT(mob_spawn(&pool, ENTITY_COW, mmath_vec3(2.5f, 65.0f, 2.5f), 0.0f) !=
                    ENTITY_ID_NULL);
    }
    for (int i = 0; i < 40; ++i) {
        mob_update_all(&pool, &drops, w, &pi, 0.5f, &ev);
    }
    TEST_ASSERT(mob_count_type(&pool, ENTITY_COW) == MOB_MAX_PASSIVE);
    /* Far hostile (> 80) despawns; far passive persists. */
    EntityId far_hostile =
        mob_spawn(&pool, ENTITY_ZOMBIE, mmath_vec3(100.5f, 65.0f, 0.5f), 0.0f);
    EntityId far_passive =
        mob_spawn(&pool, ENTITY_COW, mmath_vec3(-100.5f, 65.0f, 0.5f), 0.0f);
    TEST_ASSERT(far_hostile != ENTITY_ID_NULL && far_passive != ENTITY_ID_NULL);
    mob_update_all(&pool, &drops, w, &pi, 0.5f, &ev);
    TEST_ASSERT(mob_resolve(&pool, far_hostile) == NULL);
    TEST_ASSERT(mob_resolve(&pool, far_passive) != NULL);
    world_destroy(w);
    /* Water world: nothing spawns (no valid columns). */
    World *ocean = world_create();    TEST_ASSERT(ocean != NULL);
    if (ocean == NULL) {
        return failures + 1;
    }
    ocean->seed = 1L;
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        world_destroy(ocean);
        return failures + 1;
    }
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 60; y <= 63; ++y) {
                chunk_set_block(c, x, y, z, BLOCK_STONE);
            }
            chunk_set_block(c, x, 64, z, BLOCK_WATER);
            chunk_set_block(c, x, 65, z, BLOCK_WATER);
        }
    }
    if (world_add_chunk(ocean, c) != 0) {
        chunk_destroy(c);
        world_destroy(ocean);
        return failures + 1;
    }
    MobPool pool2;
    mob_pool_init(&pool2, 111u);
    MobPlayerInfo pi2;
    test_mob_player(&pi2, 8.5f, 70.0f, 8.5f);
    for (int i = 0; i < 40; ++i) {
        mob_update_all(&pool2, &drops, ocean, &pi2, 0.5f, &ev);
    }
    TEST_ASSERT(mob_active_count(&pool2) == 0);
    world_destroy(ocean);
    return failures;
}

/* Scratch dir cleanup for mob-save tests. */
static void mob_save_cleanup(const char *dir)
{
    char full[PATH_MAX_LEN];
    if (path_join(full, sizeof(full), dir, ENTITY_SAVE_FILE) == 0) {
        path_remove_file(full);
    }
    path_remove_dir(dir);
    path_remove_dir("test_tmp_mobsave");
}

/* Little-endian writers for hand-crafted save buffers. */
static void mob_raw_u16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void mob_raw_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static void mob_raw_f32(unsigned char *p, float v)
{
    uint32_t u = 0;
    memcpy(&u, &v, sizeof(u));
    mob_raw_u32(p, u);
}

static int mob_write_raw(const char *dir, const unsigned char *buf, size_t n)
{
    char full[PATH_MAX_LEN];
    if (path_join(full, sizeof(full), dir, ENTITY_SAVE_FILE) != 0) {
        return -1;
    }
    if (path_mkdir_p(dir) != 0) {
        return -1;
    }
    FILE *f = fopen(full, "wb");
    if (f == NULL) {
        return -1;
    }
    size_t wrote = n > 0 ? fwrite(buf, 1, n, f) : 0;
    fclose(f);
    return wrote == n ? 0 : -1;
}

/* Test: mixed item + mob round trip (type/pos/yaw/hp/state survive;
 * velocity/targets/paths reset by design).
 *
 * Returns: failure count.
 */
int test_save_mob_roundtrip(void)
{
    int failures = 0;
    const char *dir = "test_tmp_mobsave/rt";
    MobPool pool;
    mob_pool_init(&pool, 77u);
    EntityPool drops;
    entity_pool_clear(&drops);
    EntityId moss = mob_spawn(&pool, ENTITY_COW, mmath_vec3(-12.5f, 66.0f, 7.25f), 1.0f);
    EntityId zomb = mob_spawn(&pool, ENTITY_ZOMBIE, mmath_vec3(30.0f, 70.0f, -40.0f), 2.0f);
    TEST_ASSERT(moss != ENTITY_ID_NULL && zomb != ENTITY_ID_NULL);
    Mob *mm = mob_resolve(&pool, moss);
    Mob *gm = mob_resolve(&pool, zomb);
    TEST_ASSERT(mm != NULL && gm != NULL);
    if (mm == NULL || gm == NULL) {
        return failures + 1;
    }
    mm->health = 5.0f;
    mm->state = MOB_STATE_WANDER;
    mm->state_t = 1.25f;
    gm->health = 17.0f;
    gm->state = MOB_STATE_CHASE;
    gm->state_t = 0.5f;
    gm->target = ENTITY_PLAYER_ID; /* Targets never persist. */
    gm->path_len = 3;
    ItemStack drop = {(ItemId)BLOCK_STONE, 4, 0};
    TEST_ASSERT(entity_spawn(&drops, mmath_vec3(1.0f, 66.0f, 1.0f), &drop) >= 0);
    TEST_ASSERT(entity_save_write(dir, &drops, &pool) == 0);

    EntityPool back_drops;
    MobPool back_mobs;
    TEST_ASSERT(entity_save_read(dir, &back_drops, &back_mobs) == 0);
    TEST_ASSERT(entity_active_count(&back_drops) == 1);
    TEST_ASSERT(mob_active_count(&back_mobs) == 2);
    TEST_ASSERT(mob_count_type(&back_mobs, ENTITY_COW) == 1);
    TEST_ASSERT(mob_count_type(&back_mobs, ENTITY_ZOMBIE) == 1);
    int found_cow = 0;
    int found_zomb = 0;
    for (int i = 0; i < MOB_MAX; ++i) {
        Mob *m = &back_mobs.mobs[i];
        if (!m->active) {
            continue;
        }
        if (m->type == ENTITY_COW) {
            found_cow = 1;
            TEST_ASSERT_FLOAT_EQ(m->pos.x, -12.5f, 1e-4f);
            TEST_ASSERT_FLOAT_EQ(m->pos.y, 66.0f, 1e-4f);
            TEST_ASSERT_FLOAT_EQ(m->pos.z, 7.25f, 1e-4f);
            TEST_ASSERT_FLOAT_EQ(m->yaw, 1.0f, 1e-4f);
            TEST_ASSERT_FLOAT_EQ(m->health, 5.0f, 1e-4f);
            TEST_ASSERT(m->state == MOB_STATE_WANDER);
            TEST_ASSERT_FLOAT_EQ(m->state_t, 1.25f, 1e-4f);
            TEST_ASSERT(m->dead == false);
        } else if (m->type == ENTITY_ZOMBIE) {
            found_zomb = 1;
            TEST_ASSERT_FLOAT_EQ(m->health, 17.0f, 1e-4f);
            TEST_ASSERT(m->state == MOB_STATE_CHASE);
            TEST_ASSERT(m->target == ENTITY_ID_NULL); /* Reset by design. */
            TEST_ASSERT(m->path_len == 0);             /* Rebuilt live. */
            TEST_ASSERT_FLOAT_EQ(m->vel.x, 0.0f, 1e-6f); /* At rest. */
        }
    }
    TEST_ASSERT(found_cow == 1 && found_zomb == 1);
    mob_save_cleanup(dir);
    return failures;
}

/* Test: v1 files (M6.1/M7 items only) still load; mobs stay empty.
 *
 * Returns: failure count.
 */
int test_save_mob_v1_compat(void)
{
    int failures = 0;
    const char *dir = "test_tmp_mobsave/v1";
    unsigned char buf[10 + 38];
    buf[0] = 'M';
    buf[1] = 'N';
    buf[2] = 'C';
    buf[3] = 'E';
    mob_raw_u16(buf + 4, 1); /* Version 1. */
    mob_raw_u32(buf + 6, 1);
    mob_raw_u16(buf + 10, (uint16_t)BLOCK_DIRT);
    mob_raw_u16(buf + 12, 2);
    mob_raw_f32(buf + 14, 5.0f);
    mob_raw_f32(buf + 18, 66.0f);
    mob_raw_f32(buf + 22, 5.0f);
    mob_raw_f32(buf + 26, 0.0f);
    mob_raw_f32(buf + 30, 0.0f);
    mob_raw_f32(buf + 34, 0.0f);
    mob_raw_u16(buf + 38, 0);
    mob_raw_f32(buf + 40, 0.0f);
    mob_raw_f32(buf + 44, 0.0f);
    TEST_ASSERT(mob_write_raw(dir, buf, sizeof(buf)) == 0);
    EntityPool drops;
    MobPool mobs;
    TEST_ASSERT(entity_save_read(dir, &drops, &mobs) == 0);
    TEST_ASSERT(entity_active_count(&drops) == 1);
    TEST_ASSERT(mob_active_count(&mobs) == 0);
    mob_save_cleanup(dir);
    return failures;
}

/* Test: corrupt v2 files rejected (bad kind/type/hp/pos/truncation/
 * count/dead-state); pools left cleared.
 *
 * Returns: failure count.
 */
int test_save_mob_corrupt(void)
{
    int failures = 0;
    const char *dir = "test_tmp_mobsave/corrupt";
    /* Valid 28-byte mob body builder (cow at origin, full hp). */
    unsigned char good[28];
    memset(good, 0, sizeof(good));
    good[0] = (unsigned char)ENTITY_COW;
    good[1] = (unsigned char)MOB_STATE_IDLE;
    mob_raw_f32(good + 4, 1.0f);
    mob_raw_f32(good + 8, 66.0f);
    mob_raw_f32(good + 12, 2.0f);
    mob_raw_f32(good + 16, 0.5f);
    mob_raw_f32(good + 20, 8.0f);
    mob_raw_f32(good + 24, 0.0f);
    unsigned char hdr[10];
    hdr[0] = 'M';
    hdr[1] = 'N';
    hdr[2] = 'C';
    hdr[3] = 'E';
    mob_raw_u16(hdr + 4, 2);
    mob_raw_u32(hdr + 6, 1);
    /* Unknown kind byte. */
    {
        unsigned char buf[10 + 1 + 28];
        memcpy(buf, hdr, 10);
        buf[10] = 99;
        memcpy(buf + 11, good, 28);
        TEST_ASSERT(mob_write_raw(dir, buf, sizeof(buf)) == 0);
        EntityPool drops;
        MobPool mobs;
        TEST_ASSERT(entity_save_read(dir, &drops, &mobs) != 0);
        TEST_ASSERT(mob_active_count(&mobs) == 0 && entity_active_count(&drops) == 0);
    }
    /* Unknown mob type + dead state + zero hp + NaN pos (each alone). */
    {
        unsigned char bad[28];
        memcpy(bad, good, 28);
        bad[0] = 9;
        unsigned char buf[10 + 1 + 28];
        memcpy(buf, hdr, 10);
        buf[10] = 9;
        memcpy(buf + 11, bad, 28);
        EntityPool drops;
        MobPool mobs;
        TEST_ASSERT(mob_write_raw(dir, buf, sizeof(buf)) == 0);
        TEST_ASSERT(entity_save_read(dir, &drops, &mobs) != 0);
        memcpy(bad, good, 28);
        bad[1] = (unsigned char)MOB_STATE_DEAD;
        memcpy(buf + 11, bad, 28);
        buf[10] = (unsigned char)ENTITY_COW;
        TEST_ASSERT(mob_write_raw(dir, buf, sizeof(buf)) == 0);
        TEST_ASSERT(entity_save_read(dir, &drops, &mobs) != 0);
        memcpy(bad, good, 28);
        mob_raw_f32(bad + 20, 0.0f);
        memcpy(buf + 11, bad, 28);
        TEST_ASSERT(mob_write_raw(dir, buf, sizeof(buf)) == 0);
        TEST_ASSERT(entity_save_read(dir, &drops, &mobs) != 0);
        memcpy(bad, good, 28);
        mob_raw_u32(bad + 4, 0x7FC00000u); /* NaN x. */
        memcpy(buf + 11, bad, 28);
        TEST_ASSERT(mob_write_raw(dir, buf, sizeof(buf)) == 0);
        TEST_ASSERT(entity_save_read(dir, &drops, &mobs) != 0);
    }
    /* Truncated mob + excessive count. */
    {
        unsigned char buf[10 + 1 + 10];
        memcpy(buf, hdr, 10);
        buf[10] = (unsigned char)ENTITY_COW;
        memset(buf + 11, 0, 10);
        EntityPool drops;
        MobPool mobs;
        TEST_ASSERT(mob_write_raw(dir, buf, sizeof(buf)) == 0);
        TEST_ASSERT(entity_save_read(dir, &drops, &mobs) != 0);
        unsigned char big[10];
        memcpy(big, hdr, 10);
        mob_raw_u32(big + 6, 500);
        TEST_ASSERT(mob_write_raw(dir, big, sizeof(big)) == 0);
        TEST_ASSERT(entity_save_read(dir, &drops, &mobs) != 0);
    }
    mob_save_cleanup(dir);
    return failures;
}

/* Test: dead mobs never persist (killed before save stay gone).
 *
 * Returns: failure count.
 */
int test_save_mob_dead_excluded(void)
{
    int failures = 0;
    const char *dir = "test_tmp_mobsave/dead";
    MobPool pool;
    mob_pool_init(&pool, 3u);
    EntityPool drops;
    entity_pool_clear(&drops);
    EntityId id = mob_spawn(&pool, ENTITY_COW, mmath_vec3(0.0f, 70.0f, 0.0f), 0.0f);
    TEST_ASSERT(id != ENTITY_ID_NULL);
    Vec3 east = mmath_vec3(1.0f, 0.0f, 0.0f);
    TEST_ASSERT(living_entity_damage(&pool, &drops, id, 100.0f, east, 1.0f, &east) == true);
    TEST_ASSERT(entity_save_write(dir, &drops, &pool) == 0);
    EntityPool back_drops;
    MobPool back_mobs;
    TEST_ASSERT(entity_save_read(dir, &back_drops, &back_mobs) == 0);
    TEST_ASSERT(mob_active_count(&back_mobs) == 0);
    mob_save_cleanup(dir);
    return failures;
}

/* Test: shipped models validate; malformed definitions rejected.
 *
 * Returns: failure count.
 */
int test_mob_models(void)
{
    int failures = 0;
    const MobModel *moss = mob_model_for(0);
    const MobModel *zomb = mob_model_for(1);
    const MobModel *skel = mob_model_for(2);
    TEST_ASSERT(moss != NULL && mob_model_validate(moss) == true);
    TEST_ASSERT(zomb != NULL && mob_model_validate(zomb) == true);
    TEST_ASSERT(skel != NULL && mob_model_validate(skel) == true);
    TEST_ASSERT(mob_model_for(-1) == NULL);
    TEST_ASSERT(mob_model_for(3) == NULL);
    TEST_ASSERT(mob_model_validate(NULL) == false);
    if (zomb != NULL) {
        TEST_ASSERT(zomb->nparts == 6);
        TEST_ASSERT_FLOAT_EQ(zomb->height, 1.9f, 1e-4f);
    }
    /* Skins: cow + zombie + skeleton all wear real art (no placeholders
     * left in the roster). */
    const MobSkin *zbsk = mob_skin_for(1);
    TEST_ASSERT(zbsk != NULL);
    if (zbsk != NULL && zomb != NULL) {
        TEST_ASSERT(strcmp(mob_skin_file(1), "zombie") == 0);
        TEST_ASSERT(zbsk->width == 64 && zbsk->height == 64);
        TEST_ASSERT(mob_skin_validate(zbsk, zomb->nparts) == true);
        TEST_ASSERT(mob_skin_validate(zbsk, zomb->nparts + 1) == false);
    }
    const MobSkin *cowsk = mob_skin_for(0);
    TEST_ASSERT(cowsk != NULL);
    if (cowsk != NULL && moss != NULL) {
        TEST_ASSERT(strcmp(mob_skin_file(0), "cow") == 0);
        TEST_ASSERT(cowsk->width == 64 && cowsk->height == 64);
        TEST_ASSERT(mob_skin_validate(cowsk, moss->nparts) == true);
        TEST_ASSERT(mob_skin_validate(cowsk, moss->nparts + 1) == false);
        MobSkinRect body_side = cowsk->parts[0].faces[0];
        MobSkinRect body_end = cowsk->parts[0].faces[4];
        MobSkinRect head_side = cowsk->parts[1].faces[0];
        TEST_ASSERT(body_side.x == 18 && body_side.y == 14 && body_side.w == 10 && body_side.h == 18);
        TEST_ASSERT(body_end.x == 50 && body_end.y == 14 && body_end.w == 12 && body_end.h == 18);
        TEST_ASSERT(head_side.x == 0 && head_side.y == 6 && head_side.w == 6 && head_side.h == 8);
    }
    const MobSkin *sk = mob_skin_for(2);
    TEST_ASSERT(sk != NULL);
    if (sk != NULL && skel != NULL) {
        TEST_ASSERT(strcmp(mob_skin_file(2), "skeleton") == 0);
        TEST_ASSERT(sk->width == 64 && sk->height == 32);
        TEST_ASSERT(mob_skin_validate(sk, skel->nparts) == true);
        TEST_ASSERT(mob_skin_validate(sk, skel->nparts + 1) == false);
    }
    TEST_ASSERT(mob_skin_for(-1) == NULL);
    TEST_ASSERT(mob_skin_for(3) == NULL);
    TEST_ASSERT(mob_skin_file(3) == NULL);
    TEST_ASSERT(mob_skin_validate(NULL, 6) == false);
    /* Malformed skin: rect outside the 64x32 bounds rejected. */
    MobSkinPart bad_rect[1] = {{{{0, 0, 200, 8},
                                 {0, 0, 8, 8},
                                 {0, 0, 8, 8},
                                 {0, 0, 8, 8},
                                 {0, 0, 8, 8},
                                 {0, 0, 8, 8}}}};
    MobSkin bad_skin = {"skeleton", 64, 32, bad_rect, 1};
    TEST_ASSERT(mob_skin_validate(&bad_skin, 1) == false);
    if (moss != NULL) {
        TEST_ASSERT(moss->nparts == 6);
        TEST_ASSERT_FLOAT_EQ(moss->parts[0].size.x, 0.75f, 1e-6f);
        TEST_ASSERT_FLOAT_EQ(moss->parts[0].size.y, 1.125f, 1e-6f);
        TEST_ASSERT(moss->parts[1].size.x < moss->parts[0].size.x);
        /* Legs swing, body does not. */
        int legs = 0;
        for (int i = 0; i < moss->nparts; ++i) {
            if (moss->parts[i].anim == MOB_ANIM_LEG) {
                ++legs;
            }
        }
        TEST_ASSERT(legs == 4);
    }
    /* Malformed: bad parent, oversized part, bad tile, empty. */
    MobModelPart bad_parent[2] = {
        {{0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, 3, -1, 0.0f, MOB_ANIM_NONE},
        {{0.0f, 0.5f, 0.0f}, {0.4f, 0.4f, 0.4f}, 3, 5, 0.0f, MOB_ANIM_NONE},
    };
    MobModel m1 = {bad_parent, 2, 1.0f};
    TEST_ASSERT(mob_model_validate(&m1) == false);
    MobModelPart bad_size[1] = {
        {{0.0f, 0.0f, 0.0f}, {9.0f, 0.5f, 0.5f}, 3, -1, 0.0f, MOB_ANIM_NONE},
    };
    MobModel m2 = {bad_size, 1, 1.0f};
    TEST_ASSERT(mob_model_validate(&m2) == false);
    MobModelPart bad_tile[1] = {
        {{0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, 999, -1, 0.0f, MOB_ANIM_NONE},
    };
    MobModel m3 = {bad_tile, 1, 1.0f};
    TEST_ASSERT(mob_model_validate(&m3) == false);
    MobModel m4 = {NULL, 0, 1.0f};
    TEST_ASSERT(mob_model_validate(&m4) == false);
    MobModelPart tall[1] = {
        {{0.0f, 0.0f, 0.0f}, {0.5f, 5.0f, 0.5f}, 3, -1, 0.0f, MOB_ANIM_NONE},
    };
    MobModel m5 = {tall, 1, 1.0f};
    TEST_ASSERT(mob_model_validate(&m5) == false);
    return failures;
}

/* Test: first-person swing timing is finite, clamped, and smooth. */
int test_player_swing_animation(void)
{
    int failures = 0;
    TEST_ASSERT_FLOAT_EQ(player_swing_phase(-1.0f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(player_swing_phase(0.0f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(player_swing_phase(PLAYER_SWING_DURATION * 0.5f), 0.5f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(player_swing_phase(PLAYER_SWING_DURATION * 2.0f), 1.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(player_swing_phase(NAN), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(player_swing_weight(0.0f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(player_swing_weight(0.5f), 1.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(player_swing_weight(1.0f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(player_swing_weight(2.0f), 0.0f, 1e-6f);
    return failures;
}

/* Drive a controller with a fixed input for n 1/60 s steps. */
static void test_anim_drive(PlayerAnim *a, const PlayerAnimInput *in, int n)
{
    for (int i = 0; i < n; ++i) {
        player_anim_update(a, in, 1.0f / 60.0f);
    }
}

/* Test: hand controller states, priorities, and one-shot expiry. */
int test_player_anim_states(void)
{
    int failures = 0;
    PlayerAnim a;
    player_anim_init(&a);
    TEST_ASSERT(a.state == PLAYER_ANIM_IDLE);
    PlayerAnimPose zp = player_anim_pose(&a);
    TEST_ASSERT_FLOAT_EQ(zp.bob_x, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(zp.punch, 0.0f, 1e-6f);

    PlayerAnimInput walk = {true, false, true, false, false};
    test_anim_drive(&a, &walk, 30);
    TEST_ASSERT(a.state == PLAYER_ANIM_WALK);
    TEST_ASSERT(a.stride > 0.0f);
    float peak = 0.0f;
    for (int i = 0; i < 120; ++i) {
        player_anim_update(&a, &walk, 1.0f / 60.0f);
        PlayerAnimPose p = player_anim_pose(&a);
        float m = p.bob_x < 0.0f ? -p.bob_x : p.bob_x;
        if (m > peak) {
            peak = m;
        }
    }
    TEST_ASSERT(peak > PLAYER_ANIM_BOB_WALK * 0.9f);

    PlayerAnimInput sprint = {true, true, true, false, false};
    player_anim_update(&a, &sprint, 1.0f / 60.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_SPRINT);

    PlayerAnimInput air = {true, false, false, false, false};
    player_anim_update(&a, &air, 1.0f / 60.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_AIR);

    /* Small hop settles straight to base (no dip theatre). */
    player_anim_notify_landed(&a, 0.5f);
    player_anim_update(&a, &walk, 1.0f / 60.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_WALK);

    /* Real fall dips, then releases to base. */
    player_anim_notify_landed(&a, 4.0f);
    player_anim_update(&a, &walk, 1.0f / 60.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_LAND);
    TEST_ASSERT_FLOAT_EQ(a.land_mag, 4.0f / PLAYER_ANIM_LAND_FULL_DIST, 1e-5f);
    float dip_peak = 0.0f;
    for (int i = 0; i < 30; ++i) {
        player_anim_update(&a, &walk, 1.0f / 60.0f);
        PlayerAnimPose p = player_anim_pose(&a);
        if (p.dip > dip_peak) {
            dip_peak = p.dip;
        }
    }
    TEST_ASSERT(dip_peak > 0.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_WALK);

    /* Attack punches with the swing envelope, then releases. */
    player_anim_notify_attacked(&a);
    player_anim_update(&a, &walk, 1.0f / 60.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_ATTACK);
    float punch_peak = 0.0f;
    for (int i = 0; i < 30; ++i) {
        player_anim_update(&a, &walk, 1.0f / 60.0f);
        PlayerAnimPose p = player_anim_pose(&a);
        if (p.punch > punch_peak) {
            punch_peak = p.punch;
        }
    }
    TEST_ASSERT(punch_peak > 0.9f);
    TEST_ASSERT(a.state == PLAYER_ANIM_WALK);
    TEST_ASSERT_FLOAT_EQ(player_anim_pose(&a).punch, 0.0f, 1e-6f);

    /* Hurt interrupts the swing and lifts the hand. */
    player_anim_notify_attacked(&a);
    player_anim_update(&a, &walk, 1.0f / 60.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_ATTACK);
    player_anim_notify_hurt(&a);
    player_anim_update(&a, &walk, 1.0f / 60.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_HURT);
    float lift = 0.0f;
    for (int i = 0; i < 25; ++i) {
        player_anim_update(&a, &walk, 1.0f / 60.0f);
        PlayerAnimPose p = player_anim_pose(&a);
        if (p.dip < lift) {
            lift = p.dip;
        }
    }
    TEST_ASSERT(lift < 0.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_WALK);

    /* Held use raises the hand; sneak crouches it. */
    PlayerAnimInput use = {false, false, true, false, true};
    player_anim_update(&a, &use, 1.0f / 60.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_USE);
    TEST_ASSERT_FLOAT_EQ(player_anim_pose(&a).raise, 0.10f, 1e-6f);
    PlayerAnimInput sneak_idle = {false, false, true, true, false};
    player_anim_update(&a, &sneak_idle, 1.0f / 60.0f);
    TEST_ASSERT(a.state == PLAYER_ANIM_IDLE);
    TEST_ASSERT_FLOAT_EQ(player_anim_pose(&a).dip, 0.025f, 1e-6f);

    /* Deterministic: same drive, same result. */
    PlayerAnim b;
    player_anim_init(&b);
    player_anim_notify_landed(&b, 4.0f);
    test_anim_drive(&b, &walk, 10);
    PlayerAnim c;
    player_anim_init(&c);
    player_anim_notify_landed(&c, 4.0f);
    test_anim_drive(&c, &walk, 10);
    TEST_ASSERT(b.state == c.state);
    TEST_ASSERT_FLOAT_EQ(b.stride, c.stride, 1e-6f);

    /* Bad inputs never crash. */
    player_anim_init(NULL);
    player_anim_notify_attacked(NULL);
    player_anim_notify_hurt(NULL);
    player_anim_notify_landed(NULL, 3.0f);
    player_anim_notify_landed(&a, NAN);
    player_anim_update(NULL, &walk, 0.05f);
    player_anim_update(&a, NULL, 0.05f);
    player_anim_update(&a, &walk, NAN);
    player_anim_update(&a, &walk, -1.0f);
    player_anim_update(&a, &walk, 100.0f);
    PlayerAnimPose np = player_anim_pose(NULL);
    TEST_ASSERT_FLOAT_EQ(np.punch, 0.0f, 1e-6f);
    return failures;
}

/* Test: third-person body model + skin are valid, Steve-mapped, 1.8 m. */
int test_player_body_model(void)
{
    int failures = 0;
    const MobModel *m = player_body_model();
    TEST_ASSERT(m != NULL);
    TEST_ASSERT(mob_model_validate(m));
    TEST_ASSERT(m->nparts == PLAYER_PART_COUNT);
    TEST_ASSERT_FLOAT_EQ(m->height, 1.8f, 1e-6f);
    /* Head sits on top, limbs pivot at shoulder/hip, feet at zero. */
    TEST_ASSERT_FLOAT_EQ(m->parts[PLAYER_PART_HEAD].offset.y, 1.35f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(m->parts[PLAYER_PART_ARM_L].pivot_y, 1.35f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(m->parts[PLAYER_PART_LEG_L].pivot_y, 0.675f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(m->parts[PLAYER_PART_LEG_R].offset.y, 0.0f, 1e-6f);
    /* Left limbs sit on +X (model's left facing +Z), right on -X. */
    TEST_ASSERT(m->parts[PLAYER_PART_ARM_L].offset.x > 0.0f);
    TEST_ASSERT(m->parts[PLAYER_PART_ARM_R].offset.x < 0.0f);
    const MobSkin *s = player_body_skin();
    TEST_ASSERT(s != NULL);
    TEST_ASSERT(mob_skin_validate(s, m->nparts));
    TEST_ASSERT(strcmp(s->file, "player") == 0);
    TEST_ASSERT(s->width == 64 && s->height == 64);
    /* Spot-check Steve net regions: head front, torso front, arm front. */
    MobSkinRect hf = s->parts[PLAYER_PART_HEAD].faces[5];
    TEST_ASSERT(hf.x == 8 && hf.y == 8 && hf.w == 8 && hf.h == 8);
    MobSkinRect tf = s->parts[PLAYER_PART_TORSO].faces[5];
    TEST_ASSERT(tf.x == 20 && tf.y == 20 && tf.w == 8 && tf.h == 12);
    MobSkinRect af = s->parts[PLAYER_PART_ARM_R].faces[5];
    TEST_ASSERT(af.x == 44 && af.y == 20 && af.w == 4 && af.h == 12);
    return failures;
}

/* Test: melee swipe curve (windup, release, rest) + zombie arm roles. */
int test_mob_strike_pitch(void)
{
    int failures = 0;
    TEST_ASSERT_FLOAT_EQ(mob_strike_pitch(0.0f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(mob_strike_pitch(-1.0f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(mob_strike_pitch(NAN), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(mob_strike_pitch(0.125f), -0.6f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(mob_strike_pitch(0.25f), -1.2f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(mob_strike_pitch(0.45f), -0.6f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(mob_strike_pitch(0.65f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(mob_strike_pitch(5.0f), 0.0f, 1e-6f);
    /* Monotonic windup, monotonic release (no mid-swing pops). */
    float prev = 0.0f;
    for (int i = 1; i <= 10; ++i) {
        float v = mob_strike_pitch((float)i * 0.025f);
        TEST_ASSERT(v <= prev + 1e-6f);
        prev = v;
    }
    for (int i = 1; i <= 8; ++i) {
        float v = mob_strike_pitch(0.25f + (float)i * 0.05f);
        TEST_ASSERT(v >= prev - 1e-6f);
        prev = v;
    }
    /* Zombie arms swipe; validator accepts the new role. */
    const MobModel *z = mob_model_for(1);
    TEST_ASSERT(z != NULL);
    TEST_ASSERT(mob_model_validate(z));
    TEST_ASSERT(z->parts[2].anim == MOB_ANIM_STRIKE_ARM);
    TEST_ASSERT(z->parts[3].anim == MOB_ANIM_STRIKE_ARM);
    TEST_ASSERT(z->parts[4].anim == MOB_ANIM_LEG);
    MobModelPart copy[6];
    memcpy(copy, z->parts, sizeof(copy));
    MobModel bad = {copy, z->nparts, z->height};
    TEST_ASSERT(mob_model_validate(&bad));
    copy[0].anim = 99;
    TEST_ASSERT(mob_model_validate(&bad) == false);
    return failures;
}
