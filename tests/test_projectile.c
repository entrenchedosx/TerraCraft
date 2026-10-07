#include "test_main.h"

#include "core/path.h"
#include "game/entity.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/mob.h"
#include "game/player.h"
#include "game/projectile.h"
#include "game/recipe.h"
#include "game/survival.h"
#include "render/texture_atlas.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/entity_save.h"
#include "world/world.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Flat stone floor fixture: y 60..64 solid over chunk (0,0), open air
 * above (y 65+), so tests control walls/targets explicitly.
 */
static World *make_proj_world(void)
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

/* Dead-far player snapshot (never hit, never targeting). */
static void proj_player_far(ProjectilePlayer *pp)
{
    pp->pos = mmath_vec3(-100.5f, 65.0f, -100.5f);
    pp->height = 1.8f;
    pp->width = 0.6f;
    pp->alive = true;
    pp->creative = false;
}

/* Run the sim in 1/60 s slices (deterministic fixed substeps).
 * Frame events accumulate across slices (the updater clears per call,
 * like the app OR-ing effects across a frame).
 */
static void proj_step(ProjectilePool *pool, MobPool *mobs, EntityPool *drops, World *w,
                      ProjectilePlayer *pp, float seconds, ProjectileFrameEvents *ev)
{
    ProjectileFrameEvents total;
    memset(&total, 0, sizeof(total));
    int n = (int)(seconds * 60.0f + 0.5f);
    for (int i = 0; i < n; ++i) {
        ProjectileFrameEvents one;
        projectile_update(pool, mobs, drops, w, pp, 1.0f / 60.0f, &one);
        total.player_hits += one.player_hits;
        total.player_damage += one.player_damage;
        total.player_knock.x += one.player_knock.x;
        total.player_knock.y += one.player_knock.y;
        total.player_knock.z += one.player_knock.z;
        total.mobs_hit += one.mobs_hit;
        total.mobs_died += one.mobs_died;
        if (one.mobs_died > 0) {
            total.last_death_pos = one.last_death_pos;
            total.last_death_item = one.last_death_item;
        }
        total.blocks_hit += one.blocks_hit;
        if (one.blocks_hit > 0) {
            total.last_block_pos = one.last_block_pos;
        }
    }
    *ev = total;
}

/* Test: registry validity (arrow defined, unknown inert), fire/remove
 * basics (pool full fails cleanly, bad args rejected).
 *
 * Returns: failure count.
 */
int test_projectile_basics(void)
{
    int failures = 0;
    const ProjectileDefinition *arrow = projectile_definition(PROJECTILE_ARROW);
    TEST_ASSERT(arrow != NULL && arrow->type == PROJECTILE_ARROW);
    TEST_ASSERT(arrow->gravity > 0.0f && arrow->life_fly > 0.0f && arrow->can_embed == true);
    const ProjectileDefinition *bad = projectile_definition((ProjectileType)99);
    TEST_ASSERT(bad != NULL && bad->type == PROJECTILE_NONE);
    ProjectilePool pool;
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_active_count(&pool) == 0);
    TEST_ASSERT(projectile_active_count(NULL) == 0);
    /* Bad fires rejected. */
    TEST_ASSERT(projectile_fire(NULL, PROJECTILE_ARROW, mmath_vec3(0, 70, 0),
                                mmath_vec3(1, 0, 0), 20.0f, 4.0f, 5.0f, 1) == false);
    TEST_ASSERT(projectile_fire(&pool, (ProjectileType)99, mmath_vec3(0, 70, 0),
                                mmath_vec3(1, 0, 0), 20.0f, 4.0f, 5.0f, 1) == false);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(0, 70, 0),
                                mmath_vec3(0, 0, 0), 20.0f, 4.0f, 5.0f, 1) == false);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(0, 70, 0),
                                mmath_vec3(1, 0, 0), 0.0f, 4.0f, 5.0f, 1) == false);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(0, 70, 0),
                                mmath_vec3(1, 0, 0), 20.0f, 0.0f, 5.0f, 1) == false);
    /* Fill the pool: further fires fail, never overflow. */
    int fired = 0;
    for (int i = 0; i < PROJECTILE_MAX + 4; ++i) {
        if (projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(0.0f, 70.0f, 0.0f),
                            mmath_vec3(1.0f, 0.0f, 0.0f), 20.0f, 4.0f, 5.0f, 1)) {
            ++fired;
        }
    }
    TEST_ASSERT(fired == PROJECTILE_MAX);
    TEST_ASSERT(projectile_active_count(&pool) == PROJECTILE_MAX);
    /* Removal frees (out-of-range silent no-op). */
    projectile_remove(&pool, 0);
    projectile_remove(&pool, -1);
    projectile_remove(&pool, PROJECTILE_MAX);
    projectile_remove(NULL, 0);
    TEST_ASSERT(projectile_active_count(&pool) == PROJECTILE_MAX - 1);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(0.0f, 70.0f, 0.0f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 20.0f, 4.0f, 5.0f, 1) == true);
    /* Grace armed at fire. */
    TEST_ASSERT(pool.projs[0].grace_t > 0.0f);
    projectile_pool_clear(NULL); /* Safe. */
    return failures;
}

/* Test: straight-line flight (zero-gravity mental model fails here —
 * arrows arc), fixed-step determinism (same seed/inputs, same path),
 * flight lifetime despawn.
 *
 * Returns: failure count.
 */
int test_projectile_flight(void)
{
    int failures = 0;
    World *w = make_proj_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool mobs;
    mob_pool_init(&mobs, 7u);
    EntityPool drops;
    entity_pool_clear(&drops);
    ProjectilePlayer pp;
    proj_player_far(&pp);
    ProjectileFrameEvents ev;
    /* Gravity arcs: a flat shot at 20 m/s drops over 1 s. */
    ProjectilePool pool;
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(2.5f, 75.0f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 20.0f, 4.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 1.0f, &ev);
    TEST_ASSERT(projectile_active_count(&pool) == 1);
    TEST_ASSERT(pool.projs[0].pos.x > 2.5f);
    TEST_ASSERT(pool.projs[0].pos.y < 75.0f); /* Arced down. */
    TEST_ASSERT(pool.projs[0].vel.y < 0.0f);
    /* Determinism: replay the same shot, compare positions exactly. */
    Vec3 seen = pool.projs[0].pos;
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(2.5f, 75.0f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 20.0f, 4.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 1.0f, &ev);
    TEST_ASSERT(fabsf(pool.projs[0].pos.x - seen.x) < 1e-5f);
    TEST_ASSERT(fabsf(pool.projs[0].pos.y - seen.y) < 1e-5f);
    TEST_ASSERT(fabsf(pool.projs[0].pos.z - seen.z) < 1e-5f);
    /* Frame-rate independence: 60 x 1/60 vs 600 x 1/600 agree within
     * integration tolerance (semi-implicit Euler at different h is not
     * bit-exact, but must not diverge — the sim stays bounded). */
    Vec3 coarse = pool.projs[0].pos;
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(2.5f, 75.0f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 20.0f, 4.0f, 5.0f, 1) == true);
    for (int i = 0; i < 600; ++i) {
        projectile_update(&pool, &mobs, &drops, w, &pp, 1.0f / 600.0f, &ev);
    }
    TEST_ASSERT(fabsf(pool.projs[0].pos.x - coarse.x) < 0.6f);
    TEST_ASSERT(fabsf(pool.projs[0].pos.y - coarse.y) < 1.2f);
    /* Lifetime: flying arrows die at life_fly even in open sky. */
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(2.5f, 200.0f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 5.0f, 4.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 7.0f, &ev);
    TEST_ASSERT(projectile_active_count(&pool) == 0);
    world_destroy(w);
    return failures;
}

/* Test: swept block collision — fast arrows cannot tunnel; a 40 m/s
 * shot crosses a 1-block wall in one 1/60 s substep and still sticks.
 * Covers single-block, multi-block, negative coords, chunk-edge walls,
 * dead-start grace, and embed decay.
 *
 * Returns: failure count.
 */
int test_projectile_block_swept(void)
{
    int failures = 0;
    World *w = make_proj_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool mobs;
    mob_pool_init(&mobs, 11u);
    EntityPool drops;
    entity_pool_clear(&drops);
    ProjectilePlayer pp;
    proj_player_far(&pp);
    ProjectilePool pool;
    ProjectileFrameEvents ev;
    /* One-block wall at x=8 (y 65..66): 40 m/s crosses it in one substep.
     * Shots fire at y 66.5 flat and fast (0.1 s to the wall, ~0.1 drop):
     * the lane stays inside the wall band. */
    for (int y = 65; y <= 66; ++y) {
        for (int z = 7; z <= 9; ++z) {
            Chunk *c = world_get_chunk(w, 0, 0);
            chunk_set_block(c, 8, y, z, BLOCK_STONE);
        }
    }
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.5f, 66.5f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 40.0f, 4.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 0.25f, &ev);
    TEST_ASSERT(pool.projs[0].state == PROJECTILE_EMBEDDED);
    TEST_ASSERT(pool.projs[0].pos.x < 8.0f); /* Stopped at the face. */
    TEST_ASSERT(ev.blocks_hit >= 1);
    /* Embedded arrows despawn after life_embed (12 s), never simulate. */
    Vec3 stuck = pool.projs[0].pos;
    proj_step(&pool, &mobs, &drops, w, &pp, 6.0f, &ev);
    TEST_ASSERT(projectile_active_count(&pool) == 1);
    TEST_ASSERT(pool.projs[0].pos.x == stuck.x && pool.projs[0].pos.y == stuck.y);
    proj_step(&pool, &mobs, &drops, w, &pp, 7.0f, &ev);
    TEST_ASSERT(projectile_active_count(&pool) == 0);
    TEST_ASSERT(projectile_embedded_count(&pool) == 0);
    /* Long wall run: 3-block wall still stops the same shot. */
    for (int x = 8; x <= 10; ++x) {
        for (int y = 65; y <= 66; ++y) {
            for (int z = 7; z <= 9; ++z) {
                Chunk *c = world_get_chunk(w, 0, 0);
                chunk_set_block(c, x, y, z, BLOCK_STONE);
            }
        }
    }
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.5f, 66.5f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 40.0f, 4.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 0.25f, &ev);
    TEST_ASSERT(pool.projs[0].state == PROJECTILE_EMBEDDED);
    /* Negative coordinates wall: chunk (-1,0) covers x -16..-1,
     * z 0..15; wall plane at world x=-1 (local 15). Floor/void math
     * must hold across the sign boundary. */
    World *wn = world_create();
    TEST_ASSERT(wn != NULL);
    if (wn != NULL) {
        Chunk *cn = chunk_create(-1, 0);
        TEST_ASSERT(cn != NULL);
        if (cn != NULL) {
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    for (int y = 60; y <= 64; ++y) {
                        chunk_set_block(cn, x, y, z, BLOCK_STONE);
                    }
                }
            }
            /* Wall plane at world x=-1 (local 15). */
            for (int y = 65; y <= 66; ++y) {
                for (int z = 0; z < 16; ++z) {
                    chunk_set_block(cn, 15, y, z, BLOCK_STONE);
                }
            }
            if (world_add_chunk(wn, cn) == 0) {
                projectile_pool_clear(&pool);
                TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(-4.5f, 66.5f, 8.5f),
                                            mmath_vec3(1.0f, 0.0f, 0.0f), 40.0f, 4.0f, 5.0f,
                                            1) == true);
                proj_step(&pool, &mobs, &drops, wn, &pp, 0.25f, &ev);
                TEST_ASSERT(pool.projs[0].state == PROJECTILE_EMBEDDED);
            } else {
                chunk_destroy(cn);
            }
        }
        world_destroy(wn);
    }
    world_destroy(w);
    return failures;
}

/* Test: nearest-collision ordering — entity-before-block hits the mob,
 * block-before-entity embeds; two mobs pick the nearer one.
 *
 * Returns: failure count.
 */
int test_projectile_ordering(void)
{
    int failures = 0;
    World *w = make_proj_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Chunk *c = world_get_chunk(w, 0, 0);
    MobPool mobs;
    EntityPool drops;
    ProjectilePlayer pp;
    proj_player_far(&pp);
    ProjectilePool pool;
    ProjectileFrameEvents ev;
    /* Mob at x=6 (2.5 out), wall at x=8 (3.5 out): mob wins. Shots run
     * at y 65.4 (cow body (feet 65, height 1.4)): the lane crosses the
     * short body without grazing the floor (top y65). */
    mob_pool_init(&mobs, 21u);
    entity_pool_clear(&drops);
    EntityId mob = mob_spawn(&mobs, ENTITY_COW, mmath_vec3(6.5f, 65.0f, 8.5f), 0.0f);
    TEST_ASSERT(mob != ENTITY_ID_NULL);
    for (int y = 65; y <= 66; ++y) {
        for (int z = 7; z <= 9; ++z) {
            chunk_set_block(c, 8, y, z, BLOCK_STONE);
        }
    }
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.0f, 65.4f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 35.0f, 4.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 0.5f, &ev);
    TEST_ASSERT(ev.mobs_hit == 1 && ev.blocks_hit == 0);
    TEST_ASSERT(projectile_active_count(&pool) == 0); /* Consumed on hit. */
    Mob *m = mob_resolve(&mobs, mob);
    TEST_ASSERT(m != NULL && m->health < 10.0f);
    /* Wall at x=5 ahead of the mob: wall wins, mob untouched. */
    mob_pool_init(&mobs, 22u);
    entity_pool_clear(&drops);
    mob = mob_spawn(&mobs, ENTITY_COW, mmath_vec3(6.5f, 65.0f, 8.5f), 0.0f);
    for (int y = 65; y <= 66; ++y) {
        for (int z = 7; z <= 9; ++z) {
            chunk_set_block(c, 5, y, z, BLOCK_STONE);
        }
    }
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.0f, 65.4f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 35.0f, 4.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 0.5f, &ev);
    TEST_ASSERT(ev.blocks_hit >= 1 && ev.mobs_hit == 0 && ev.mobs_died == 0);
    m = mob_resolve(&mobs, mob);
    TEST_ASSERT(m != NULL && m->health == 10.0f);
    /* Two mobs in line: the nearer takes the hit. */
    chunk_set_block(c, 5, 65, 8, BLOCK_AIR);
    chunk_set_block(c, 5, 66, 8, BLOCK_AIR);
    mob_pool_init(&mobs, 23u);
    entity_pool_clear(&drops);
    EntityId near = mob_spawn(&mobs, ENTITY_COW, mmath_vec3(5.5f, 65.0f, 8.5f), 0.0f);
    EntityId far = mob_spawn(&mobs, ENTITY_COW, mmath_vec3(7.0f, 65.0f, 8.5f), 0.0f);
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.0f, 65.4f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 35.0f, 4.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 0.5f, &ev);
    Mob *mn = mob_resolve(&mobs, near);
    Mob *mf = mob_resolve(&mobs, far);
    TEST_ASSERT(mn != NULL && mf != NULL);
    if (mn != NULL && mf != NULL) {
        TEST_ASSERT(mn->health < 10.0f && mf->health == 10.0f);
    }
    world_destroy(w);
    return failures;
}

/* Test: owner filter — fresh arrows ignore the shooter (grace), then
 * re-arm; stale owner handles are harmless; other mobs stay hittable.
 *
 * Returns: failure count.
 */
int test_projectile_owner(void)
{
    int failures = 0;
    World *w = make_proj_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool mobs;
    mob_pool_init(&mobs, 31u);
    EntityPool drops;
    entity_pool_clear(&drops);
    ProjectilePlayer pp;
    proj_player_far(&pp);
    ProjectilePool pool;
    ProjectileFrameEvents ev;
    /* Shooter at x=4, victim at x=8: fire THROUGH the shooter from behind
     * (origin ahead of the shooter so the shot starts clear, as the app
     * does) — victim still hit, shooter untouched during grace. */
    EntityId shooter = mob_spawn(&mobs, ENTITY_ZOMBIE, mmath_vec3(4.5f, 65.0f, 8.5f), 0.0f);
    EntityId victim = mob_spawn(&mobs, ENTITY_COW, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(5.5f, 65.4f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 35.0f, 4.0f, 5.0f, shooter) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 0.3f, &ev);
    Mob *s = mob_resolve(&mobs, shooter);
    Mob *v = mob_resolve(&mobs, victim);
    TEST_ASSERT(s != NULL && v != NULL);
    if (s != NULL && v != NULL) {
        TEST_ASSERT(s->health == 20.0f); /* Grace ignored the shooter. */
        TEST_ASSERT(v->health < 10.0f);
    }
    /* Stale owner (shooter removed before impact) never aliases: fire,
     * remove the shooter, let it fly into the victim anyway. */
    mob_pool_init(&mobs, 32u);
    entity_pool_clear(&drops);
    shooter = mob_spawn(&mobs, ENTITY_ZOMBIE, mmath_vec3(4.5f, 65.0f, 8.5f), 0.0f);
    victim = mob_spawn(&mobs, ENTITY_COW, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(5.5f, 65.4f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 35.0f, 4.0f, 5.0f, shooter) == true);
    mob_remove(&mobs, shooter); /* Shooter dies mid-flight. */
    proj_step(&pool, &mobs, &drops, w, &pp, 0.5f, &ev);
    v = mob_resolve(&mobs, victim);
    TEST_ASSERT(v != NULL && v->health < 10.0f); /* Still hits. */
    world_destroy(w);
    return failures;
}

/* Test: entity rules — dead mobs ignored, inactive slots skipped, player
 * hits report (never applied here), creative players immune.
 *
 * Returns: failure count.
 */
int test_projectile_entities(void)
{
    int failures = 0;
    World *w = make_proj_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool mobs;
    mob_pool_init(&mobs, 41u);
    EntityPool drops;
    entity_pool_clear(&drops);
    ProjectilePool pool;
    ProjectileFrameEvents ev;
    /* Dead cow at x=6: the shot must fly THROUGH to the wall. */
    EntityId dead = mob_spawn(&mobs, ENTITY_COW, mmath_vec3(6.5f, 65.0f, 8.5f), 0.0f);
    Mob *d = mob_resolve(&mobs, dead);
    TEST_ASSERT(d != NULL);
    if (d != NULL) {
        d->hurt_t = 0.0f;
        living_entity_damage(&mobs, &drops, dead, 50.0f, mmath_vec3(1, 0, 0), 5.0f, NULL);
    }
    Chunk *c = world_get_chunk(w, 0, 0);
    for (int y = 65; y <= 66; ++y) {
        for (int z = 7; z <= 9; ++z) {
            chunk_set_block(c, 10, y, z, BLOCK_STONE);
        }
    }
    ProjectilePlayer pp;
    proj_player_far(&pp);
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.0f, 65.4f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 35.0f, 4.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 0.5f, &ev);
    TEST_ASSERT(ev.mobs_hit == 0 && ev.mobs_died == 0 && ev.blocks_hit >= 1);
    /* Live player in the lane: reports damage, never applies it here. */
    mob_pool_init(&mobs, 42u);
    entity_pool_clear(&drops);
    ProjectilePlayer near;
    near.pos = mmath_vec3(8.5f, 65.0f, 8.5f);
    near.height = 1.8f;
    near.width = 0.6f;
    near.alive = true;
    near.creative = false;
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.0f, 65.9f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 20.0f, 4.0f, 5.0f, 2) == true);
    proj_step(&pool, &mobs, &drops, w, &near, 0.5f, &ev);
    TEST_ASSERT(ev.player_hits == 1 && ev.player_damage == 4.0f);
    TEST_ASSERT(ev.player_knock.x > 0.0f); /* Downrange push. */
    TEST_ASSERT(projectile_active_count(&pool) == 0);
    /* Creative player: immune (shot continues to the wall instead). */
    near.creative = true;
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.0f, 65.9f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 20.0f, 4.0f, 5.0f, 2) == true);
    proj_step(&pool, &mobs, &drops, w, &near, 0.5f, &ev);
    TEST_ASSERT(ev.player_hits == 0 && ev.blocks_hit >= 1);
    world_destroy(w);
    return failures;
}

/* Test: damage rules — charge scales speed/damage, the M8 hurt window
 * gates rapid double-hits, knockback pushes downrange, lethal arrows
 * kill with exactly-once drops.
 *
 * Returns: failure count.
 */
int test_projectile_damage(void)
{
    int failures = 0;
    /* Charge curve pins (speed 0.75 curve, damage linear). */
    float speed = 0.0f, dmg = 0.0f;
    survival_bow_launch(0.0f, &speed, &dmg);
    TEST_ASSERT_FLOAT_EQ(speed, 10.0f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(dmg, 2.0f, 1e-4f);
    survival_bow_launch(1.0f, &speed, &dmg);
    TEST_ASSERT_FLOAT_EQ(speed, 45.0f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(dmg, 6.0f, 1e-4f);
    survival_bow_launch(0.5f, &speed, &dmg);
    TEST_ASSERT(speed > 10.0f && speed < 45.0f && dmg > 2.0f && dmg < 6.0f);
    TEST_ASSERT_FLOAT_EQ(survival_bow_charge(0.0f), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(survival_bow_charge(0.5f), 0.5f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(survival_bow_charge(2.0f), 1.0f, 1e-6f);
    World *w = make_proj_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    MobPool mobs;
    mob_pool_init(&mobs, 51u);
    EntityPool drops;
    entity_pool_clear(&drops);
    ProjectilePlayer pp;
    proj_player_far(&pp);
    ProjectilePool pool;
    ProjectileFrameEvents ev;
    /* Two rapid arrows at one cow: the hurt window eats the second.
     * Fast flat lanes (35 m/s: 0.13 s over 4.5 m, ~0.17 drop) stay in the
     * short body without floor contact. */
    EntityId mob = mob_spawn(&mobs, ENTITY_COW, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.0f, 65.4f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 35.0f, 3.0f, 5.0f, 1) == true);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.0f, 65.4f, 8.7f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 35.0f, 3.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 0.5f, &ev);
    Mob *m = mob_resolve(&mobs, mob);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        TEST_ASSERT_FLOAT_EQ(m->health, 7.0f, 1e-3f); /* One hit landed (3 dmg off 10). */
        TEST_ASSERT(m->vel.x > 0.0f);                 /* Downrange knockback. */
    }
    /* Lethal arrow: death + exactly-once apple drops. */
    mob_pool_init(&mobs, 52u);
    entity_pool_clear(&drops);
    mob = mob_spawn(&mobs, ENTITY_COW, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    int drops0 = entity_active_count(&drops);
    projectile_pool_clear(&pool);
    TEST_ASSERT(projectile_fire(&pool, PROJECTILE_ARROW, mmath_vec3(4.0f, 65.4f, 8.5f),
                                mmath_vec3(1.0f, 0.0f, 0.0f), 35.0f, 50.0f, 5.0f, 1) == true);
    proj_step(&pool, &mobs, &drops, w, &pp, 0.5f, &ev);
    TEST_ASSERT(ev.mobs_died == 1);
    TEST_ASSERT(entity_active_count(&drops) - drops0 >= 1);
    world_destroy(w);
    return failures;
}

/* Test: bow policy — registry (unstackable bow, 64-stack arrows), charge
 * helpers, arrow consume/find, recipes, durability wear per real shot,
 * creative free-fire.
 *
 * Returns: failure count.
 */
int test_bow_policy(void)
{
    int failures = 0;
    const ItemInfo *bow = item_get_info(ITEM_BOW);
    TEST_ASSERT(bow->id == ITEM_BOW && bow->max_stack == 1 && bow->max_durability == 128);
    TEST_ASSERT(bow->tool == TOOL_BOW && bow->tile == TILE_ARROW - 1);
    const ItemInfo *arrow = item_get_info(ITEM_ARROW);
    TEST_ASSERT(arrow->id == ITEM_ARROW && arrow->max_stack == 64 && arrow->max_durability == 0);
    TEST_ASSERT(survival_bow_is_bow(ITEM_BOW) == true);
    TEST_ASSERT(survival_bow_is_bow(ITEM_ARROW) == false);
    TEST_ASSERT(survival_bow_is_bow(ITEM_NONE) == false);
    /* Bow recipe (bench 3x3 limbs) + arrow recipe (bench 1x3 shafts). */
    TEST_ASSERT(recipe_count() == 16);
    ItemStack grid[9];
    RecipeMatch m;
    for (int i = 0; i < 9; ++i) {
        grid[i].item = ITEM_NONE;
        grid[i].count = 0;
        grid[i].durability = 0;
    }
    ItemId P = (ItemId)BLOCK_PLANKS;
    ItemStack S = {ITEM_STICK, 1, 0};
    grid[0].item = P;
    grid[0].count = 1;
    grid[1] = S;
    grid[3].item = P;
    grid[3].count = 1;
    grid[5] = S;
    grid[6].item = P;
    grid[6].count = 1;
    grid[7] = S;
    TEST_ASSERT(recipe_match(grid, 3, 3, &m) == true);
    TEST_ASSERT(m.recipe->out_item == ITEM_BOW);
    for (int i = 0; i < 9; ++i) {
        grid[i].item = ITEM_NONE;
        grid[i].count = 0;
    }
    grid[0].item = ITEM_COAL;
    grid[0].count = 1;
    grid[3] = S;
    grid[6] = S;
    TEST_ASSERT(recipe_match(grid, 3, 3, &m) == true);
    TEST_ASSERT(m.recipe->out_item == ITEM_ARROW && m.recipe->out_count == 4);
    /* Arrow find/consume across the full inventory. */
    Inventory inv;
    inv_init(&inv);
    TEST_ASSERT(survival_bow_find_arrow(&inv) < 0);
    TEST_ASSERT(survival_bow_consume_arrow(&inv) == false);
    inv.slots[30].item = ITEM_ARROW;
    inv.slots[30].count = 5;
    TEST_ASSERT(survival_bow_find_arrow(&inv) == 30);
    TEST_ASSERT(survival_bow_consume_arrow(&inv) == true);
    TEST_ASSERT(inv.slots[30].count == 4);
    /* Draw state: cancel resets everything, consumes nothing. */
    Player p;
    player_init(&p);
    p.bow_drawing = true;
    p.bow_slot = 2;
    p.bow_t = 0.7f;
    survival_bow_reset(&p);
    TEST_ASSERT(p.bow_drawing == false && p.bow_t == 0.0f && p.bow_full == false);
    survival_bow_reset(NULL); /* Safe. */
    return failures;
}

/* Test: hotbar slot switch cancels a draw (the app_tick path is
 * input-bound, so this pins the policy primitive: reset on switch).
 *
 * Returns: failure count.
 */
int test_bow_slot_switch(void)
{
    int failures = 0;
    Player p;
    player_init(&p);
    p.inv.slots[0].item = ITEM_BOW;
    p.inv.slots[0].count = 1;
    p.inv.slots[1].item = ITEM_ARROW;
    p.inv.slots[1].count = 8;
    p.hotbar_sel = 0;
    p.bow_drawing = true;
    p.bow_slot = 0;
    p.bow_t = 0.9f;
    /* Slot switch: draw cancels, ammo and wear untouched. */
    p.hotbar_sel = 1;
    survival_bow_reset(&p);
    TEST_ASSERT(p.bow_drawing == false);
    TEST_ASSERT(p.inv.slots[1].count == 8);
    TEST_ASSERT(p.inv.slots[0].durability == 0);
    return failures;
}

/* Skeleton AI tests (controlled 10 Hz thinks on a flat world):
 * idle->detect, approach, in-band aim, LOS-gated fire, blocked LOS
 * repositions without firing, too-close retreats, cooldown caps rate,
 * owner is the skeleton handle, arrows fly after its death.
 *
 * Returns: failure count.
 */
static void skel_player(MobPlayerInfo *pi, float x, float y, float z)
{
    pi->pos = mmath_vec3(x, y, z);
    pi->eye_height = 1.62f;
    pi->alive = true;
    pi->creative = false;
    pi->day_progress = 0.0f; /* Midnight. */
}

static void skel_think(MobPool *pool, EntityPool *drops, World *w, MobPlayerInfo *pi, int n)
{
    MobFrameEvents ev;
    for (int i = 0; i < n; ++i) {
        mob_update_all(pool, drops, w, pi, 0.1f, &ev);
    }
}

int test_skeleton_ai(void)
{
    int failures = 0;
    World *w = make_proj_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    /* Definition pins (ranged band, cooldown, arrow profile, drops). */
    const MobDefinition *def = mob_definition(ENTITY_SKELETON);
    TEST_ASSERT(def->type == ENTITY_SKELETON && def->hostile == true && def->ranged == true);
    TEST_ASSERT_FLOAT_EQ(def->prefer_min, 6.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(def->prefer_max, 14.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(def->fire_cooldown, 2.2f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(def->arrow_speed, 28.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(def->arrow_damage, 4.0f, 1e-4f);
    TEST_ASSERT(def->ndrops == 2 && def->drops[0].item == ITEM_ARROW &&
                def->drops[1].item == ITEM_BONE);
    TEST_ASSERT(def->model == 2);
    TEST_ASSERT(mob_definition((EntityType)99)->type == ENTITY_NONE);
    MobPool pool;
    EntityPool drops;
    MobPlayerInfo pi;
    MobFrameEvents ev;
    /* Far player (30 out): no detection, stays wandering. */
    mob_pool_init(&pool, 61u);
    entity_pool_clear(&drops);
    skel_player(&pi, 30.5f, 65.0f, 8.5f);
    EntityId id = mob_spawn(&pool, ENTITY_SKELETON, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    TEST_ASSERT(id != ENTITY_ID_NULL);
    skel_think(&pool, &drops, w, &pi, 20);
    Mob *s = mob_resolve(&pool, id);
    TEST_ASSERT(s != NULL && s->target != ENTITY_PLAYER_ID);
    /* Enter range (10 out): detects, aims, fires (cooldown-gated). */
    skel_player(&pi, 18.5f, 65.0f, 8.5f);
    mob_update_all(&pool, &drops, w, &pi, 0.1f, &ev);
    s = mob_resolve(&pool, id);
    TEST_ASSERT(s != NULL && s->target == ENTITY_PLAYER_ID);
    int shots0 = ev.fire_requests;
    skel_think(&pool, &drops, w, &pi, 30);
    s = mob_resolve(&pool, id);
    TEST_ASSERT(s != NULL);
    if (s != NULL) {
        TEST_ASSERT(s->state == MOB_STATE_AIM || s->state == MOB_STATE_ATTACK);
    }
    /* Cooldown: 5 s of thinks yield at most 3 shots (2.2 s cadence). */
    mob_pool_init(&pool, 62u);
    entity_pool_clear(&drops);
    skel_player(&pi, 18.5f, 65.0f, 8.5f);
    id = mob_spawn(&pool, ENTITY_SKELETON, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    s = mob_resolve(&pool, id);
    TEST_ASSERT(s != NULL);
    if (s != NULL) {
        s->target = ENTITY_PLAYER_ID;
        s->state = MOB_STATE_AIM;
        s->attack_cd = 0.0f;
    }
    int total = 0;
    for (int i = 0; i < 50; ++i) {
        mob_update_all(&pool, &drops, w, &pi, 0.1f, &ev);
        total += ev.fire_requests;
        for (int k = 0; k < ev.fire_requests; ++k) {
            TEST_ASSERT(ev.shots[k].owner == id); /* Stable owner handle. */
            TEST_ASSERT(ev.shots[k].speed > 0.0f && ev.shots[k].damage > 0.0f);
        }
    }
    TEST_ASSERT(total >= 1 && total <= 3);
    TEST_ASSERT(ev.shots_dropped == 0);
    /* Blocked LOS: no fire, repositions (path requested, closes in). */
    Chunk *c = world_get_chunk(w, 0, 0);
    for (int y = 65; y <= 70; ++y) {
        for (int z = 6; z <= 10; ++z) {
            chunk_set_block(c, 13, y, z, BLOCK_STONE);
        }
    }
    mob_pool_init(&pool, 63u);
    entity_pool_clear(&drops);
    skel_player(&pi, 18.5f, 65.0f, 8.5f);
    id = mob_spawn(&pool, ENTITY_SKELETON, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    s = mob_resolve(&pool, id);
    TEST_ASSERT(s != NULL);
    if (s != NULL) {
        s->target = ENTITY_PLAYER_ID;
        s->attack_cd = 0.0f;
    }
    float x0 = s != NULL ? s->pos.x : 0.0f;
    total = 0;
    for (int i = 0; i < 30; ++i) {
        mob_update_all(&pool, &drops, w, &pi, 0.1f, &ev);
        total += ev.fire_requests;
    }
    TEST_ASSERT(total == 0); /* Never shoots through the wall. */
    s = mob_resolve(&pool, id);
    if (s != NULL) {
        TEST_ASSERT(s->pos.x != x0 || s->path_len > 0 || s->wish_speed > 0.0f);
    }
    for (int y = 65; y <= 70; ++y) {
        for (int z = 6; z <= 10; ++z) {
            chunk_set_block(c, 13, y, z, BLOCK_AIR);
        }
    }
    /* Too close (3 out): retreats (moves away from the player). */
    mob_pool_init(&pool, 64u);
    entity_pool_clear(&drops);
    skel_player(&pi, 8.5f, 65.0f, 11.5f);
    id = mob_spawn(&pool, ENTITY_SKELETON, mmath_vec3(8.5f, 65.0f, 8.5f), 0.0f);
    s = mob_resolve(&pool, id);
    TEST_ASSERT(s != NULL);
    if (s != NULL) {
        s->target = ENTITY_PLAYER_ID;
    }
    float d0 = 0.0f;
    {
        Mob *t = mob_resolve(&pool, id);
        float dx = pi.pos.x - t->pos.x;
        float dz = pi.pos.z - t->pos.z;
        d0 = sqrtf(dx * dx + dz * dz);
    }
    skel_think(&pool, &drops, w, &pi, 30);
    s = mob_resolve(&pool, id);
    if (s != NULL) {
        float dx = pi.pos.x - s->pos.x;
        float dz = pi.pos.z - s->pos.z;
        float d1 = sqrtf(dx * dx + dz * dz);
        TEST_ASSERT(d1 > d0); /* Created distance. */
    }
    /* Dead skeletons stop AI (no shots from corpses). */
    if (s != NULL) {
        s->hurt_t = 0.0f;
        living_entity_damage(&pool, &drops, id, 99.0f, mmath_vec3(1, 0, 0), 5.0f, NULL);
    }
    mob_update_all(&pool, &drops, w, &pi, 0.5f, &ev);
    TEST_ASSERT(ev.fire_requests == 0);
    (void)shots0;
    world_destroy(w);
    return failures;
}

/* Test: skeleton drops (arrow loot via the shared table) + save
 * roundtrip (skeletons persist like other mobs; arrows never do).
 *
 * Returns: failure count.
 */
int test_skeleton_drops_save(void)
{
    int failures = 0;
    MobPool pool;
    mob_pool_init(&pool, 71u);
    EntityPool drops;
    entity_pool_clear(&drops);
    /* Lethal melee on skeletons: MC 0-2 arrows + 0-2 bones per kill.
     * Zero-rolls drop nothing, so kill several and assert the totals
     * land in range with valid items (no ghosts, no overstacks). */
    int arrows = 0;
    int bones = 0;
    int drops0 = entity_active_count(&drops);
    for (int k = 0; k < 8; ++k) {
        EntityId victim = mob_spawn(&pool, ENTITY_SKELETON, mmath_vec3(0.0f, 70.0f, 0.0f), 0.0f);
        TEST_ASSERT(victim != ENTITY_ID_NULL);
        Mob *sv = mob_resolve(&pool, victim);
        if (sv != NULL) {
            sv->hurt_t = 0.0f;
        }
        TEST_ASSERT(living_entity_damage(&pool, &drops, victim, 99.0f, mmath_vec3(1, 0, 0), 5.0f,
                                         NULL) == true);
    }
    for (int i = 0; i < ENTITY_MAX; ++i) {
        if (!drops.items[i].active) {
            continue;
        }
        if (drops.items[i].stack.item == ITEM_ARROW) {
            arrows += drops.items[i].stack.count;
        } else if (drops.items[i].stack.item == ITEM_BONE) {
            bones += drops.items[i].stack.count;
        } else {
            TEST_ASSERT(false); /* Skeleton drops only arrows + bones. */
        }
        TEST_ASSERT(drops.items[i].stack.count <= 2);
    }
    TEST_ASSERT(entity_active_count(&drops) > drops0);
    TEST_ASSERT(arrows >= 0 && arrows <= 16 && bones >= 0 && bones <= 16);
    TEST_ASSERT(item_is_valid(ITEM_BONE) == true);
    TEST_ASSERT(item_get_info(ITEM_BONE)->max_stack == 64);
    /* Save roundtrip: skeleton persists (kind 4), projectiles do not. */
    const char *dir = "test_tmp_m9save";
    TEST_ASSERT(path_mkdir_p(dir) == 0);
    mob_pool_init(&pool, 72u);
    entity_pool_clear(&drops);
    EntityId id = mob_spawn(&pool, ENTITY_SKELETON, mmath_vec3(1.5f, 66.0f, 1.5f), 1.0f);
    TEST_ASSERT(id != ENTITY_ID_NULL);
    ProjectilePool parrows;
    projectile_pool_clear(&parrows);
    TEST_ASSERT(projectile_fire(&parrows, PROJECTILE_ARROW, mmath_vec3(1.5f, 67.0f, 1.5f),
                                mmath_vec3(1, 0, 0), 20.0f, 4.0f, 5.0f, id) == true);
    TEST_ASSERT(entity_save_write(dir, &drops, &pool) == 0);
    MobPool back;
    mob_pool_init(&back, 0);
    EntityPool dback;
    entity_pool_clear(&dback);
    TEST_ASSERT(entity_save_read(dir, &dback, &back) == 0);
    TEST_ASSERT(mob_count_type(&back, ENTITY_SKELETON) == 1);
    Mob *sb = NULL;
    for (int i = 0; i < MOB_MAX; ++i) {
        if (back.mobs[i].active && back.mobs[i].type == ENTITY_SKELETON) {
            sb = &back.mobs[i];
        }
    }
    TEST_ASSERT(sb != NULL);
    if (sb != NULL) {
        TEST_ASSERT_FLOAT_EQ(sb->health, 20.0f, 1e-4f);
        TEST_ASSERT(sb->target == ENTITY_ID_NULL); /* Transient AI rebuilt. */
    }
    /* M8 worlds (no skeletons) still load: write a cow-only file,
     * read it back, skeleton count zero. */
    mob_pool_init(&pool, 73u);
    entity_pool_clear(&drops);
    TEST_ASSERT(mob_spawn(&pool, ENTITY_COW, mmath_vec3(2.5f, 66.0f, 2.5f), 0.0f) !=
                ENTITY_ID_NULL);
    TEST_ASSERT(entity_save_write(dir, &drops, &pool) == 0);
    mob_pool_init(&back, 0);
    TEST_ASSERT(entity_save_read(dir, &dback, &back) == 0);
    TEST_ASSERT(mob_count_type(&back, ENTITY_SKELETON) == 0);
    TEST_ASSERT(mob_count_type(&back, ENTITY_COW) == 1);
    /* Cleanup: remove the temp world dir contents. */
    char ents[512];
    if (path_join(ents, sizeof(ents), dir, ENTITY_SAVE_FILE) == 0) {
        path_remove_file(ents);
    }
    path_remove_dir(dir);
    return failures;
}
