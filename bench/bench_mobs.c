/* Mob + projectile simulation benchmark: AI + physics + pathfinding +
 * swept arrow collision over representative workloads. Prints per-case
 * milliseconds (average over repeats) plus counters. Headless CPU numbers
 * for regression sniffing only — not universal FPS claims.
 * Build: terracraft_bench_mobs target (not part of ctest).
 */

#include "game/entity.h"
#include "game/mob.h"
#include "game/pathfind.h"
#include "game/projectile.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <stdio.h>
#include <time.h>

/* Timed repeats per case. */
#define MOB_BENCH_REPEATS 20

/* Flat stone floor y 60..64 over a 3x3 chunk area. */
static World *bench_world(void)
{
    World *w = world_create();
    if (w == NULL) {
        return NULL;
    }
    w->seed = 424242L;
    for (int cx = -1; cx <= 1; ++cx) {
        for (int cz = -1; cz <= 1; ++cz) {
            Chunk *c = chunk_create(cx, cz);
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
            /* One wall (with a 3-wide gap) to force pathfinding in the
             * chase/path cases. */
            if (cx == 0 && cz == 0) {
                for (int y = 65; y <= 72; ++y) {
                    for (int z = 2; z <= 13; ++z) {
                        if (z >= 7 && z <= 9) {
                            continue;
                        }
                        chunk_set_block(c, 8, y, z, BLOCK_STONE);
                    }
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

static void bench_player(MobPlayerInfo *pi, float x, float y, float z)
{
    pi->pos = mmath_vec3(x, y, z);
    pi->eye_height = 1.62f;
    pi->alive = true;
    pi->creative = false;
    pi->day_progress = 0.5f;
}

/* Time N full mob_update_all ticks; report average ms + counters. */
static double bench_ticks(MobPool *pool, EntityPool *drops, World *w, MobPlayerInfo *pi, int ticks,
                          int repeats)
{
    MobFrameEvents ev;
    clock_t start = clock();
    for (int r = 0; r < repeats; ++r) {
        for (int t = 0; t < ticks; ++t) {
            mob_update_all(pool, drops, w, pi, 1.0f / 60.0f, &ev);
        }
    }
    clock_t end = clock();
    return ((double)(end - start) / (double)CLOCKS_PER_SEC) * 1000.0 / (double)repeats;
}

int main(void)
{
    World *w = bench_world();
    if (w == NULL) {
        printf("bench: world OOM\n");
        return 1;
    }
    MobPlayerInfo pi;
    bench_player(&pi, 0.5f, 65.0f, 0.5f);

    /* Case 1: 100 idle cows (AI think + gravity settle). */
    {
        MobPool pool;
        mob_pool_init(&pool, 1u);
        EntityPool drops;
        entity_pool_clear(&drops);
        for (int i = 0; i < 100; ++i) {
            float x = -20.0f + (float)(i % 10) * 2.0f;
            float z = -20.0f + (float)(i / 10) * 2.0f;
            if (mob_spawn(&pool, ENTITY_COW, mmath_vec3(x, 65.0f, z), 0.0f) == ENTITY_ID_NULL) {
                break;
            }
        }
        /* Pool caps at MOB_MAX (64): report actual. */
        double ms = bench_ticks(&pool, &drops, w, &pi, 60, MOB_BENCH_REPEATS);
        printf("idle x%d: %.3f ms/tick-set (60 ticks), thinks %u\n", mob_active_count(&pool), ms,
               (unsigned)pool.ai_thinks);
    }

    /* Case 2: chasing gloomstalkers east of a gapped wall, player west in
     * range and visible through the gap (paths + follow + physics). */
    {
        MobPool pool;
        mob_pool_init(&pool, 2u);
        EntityPool drops;
        entity_pool_clear(&drops);
        bench_player(&pi, 1.5f, 65.0f, 8.5f);
        for (int i = 0; i < 20; ++i) {
            float z = 2.0f + (float)i * 0.5f;
            if (mob_spawn(&pool, ENTITY_ZOMBIE, mmath_vec3(12.5f, 65.0f, z), 0.0f) ==
                ENTITY_ID_NULL) {
                break;
            }
        }
        double ms = bench_ticks(&pool, &drops, w, &pi, 60, MOB_BENCH_REPEATS);
        printf("chase x%d: %.3f ms/tick-set (60 ticks), paths %u\n", mob_active_count(&pool), ms,
               (unsigned)pool.path_reqs);
    }

    /* Case 3: raw pathfinding throughput (blocked straight lines across
     * the wall; start/end 14 apart, inside the 16-cell radius). */
    {
        int out[PATHFIND_MAX_LEN][3];
        clock_t start = clock();
        int found = 0;
        for (int r = 0; r < 200; ++r) {
            int n = pathfind_ground(w, -4, 65, 2 + (r % 10), 10, 65, 2 + (r % 10), out,
                                    PATHFIND_MAX_LEN);
            if (n > 0) {
                ++found;
            }
        }
        clock_t end = clock();
        double ms = ((double)(end - start) / (double)CLOCKS_PER_SEC) * 1000.0 / 200.0;
        printf("pathfind: %.3f ms/request (200 requests, %d found)\n", ms, found);
    }

    /* Case 4: 32 arrows in flight (swept DDA + entity scans over the
     * idle mob field from case 1's layout, re-spawned fresh). */
    {
        MobPool pool;
        mob_pool_init(&pool, 4u);
        EntityPool drops;
        entity_pool_clear(&drops);
        for (int i = 0; i < 40; ++i) {
            float x = -20.0f + (float)(i % 10) * 2.0f;
            float z = -20.0f + (float)(i / 10) * 2.0f;
            if (mob_spawn(&pool, ENTITY_COW, mmath_vec3(x, 65.0f, z), 0.0f) ==
                ENTITY_ID_NULL) {
                break;
            }
        }
        ProjectilePool parrows;
        projectile_pool_clear(&parrows);
        for (int i = 0; i < PROJECTILE_MAX; ++i) {
            float z = -20.0f + (float)i * 1.2f;
            projectile_fire(&parrows, PROJECTILE_ARROW, mmath_vec3(-24.0f, 70.0f, z),
                            mmath_vec3(1.0f, -0.05f, 0.0f), 35.0f, 4.0f, 5.0f, ENTITY_PLAYER_ID);
        }
        ProjectilePlayer pp;
        pp.pos = mmath_vec3(0.5f, 65.0f, 0.5f);
        pp.height = 1.8f;
        pp.width = 0.6f;
        pp.alive = true;
        pp.creative = false;
        ProjectileFrameEvents pev;
        clock_t start = clock();
        for (int r = 0; r < MOB_BENCH_REPEATS; ++r) {
            for (int t = 0; t < 60; ++t) {
                projectile_update(&parrows, &pool, &drops, w, &pp, 1.0f / 60.0f, &pev);
            }
            /* Re-fire whatever landed so every repeat is equally loaded. */
            for (int i = 0; i < PROJECTILE_MAX; ++i) {
                if (!parrows.projs[i].active) {
                    float z = -20.0f + (float)i * 1.2f;
                    projectile_fire(&parrows, PROJECTILE_ARROW, mmath_vec3(-24.0f, 70.0f, z),
                                    mmath_vec3(1.0f, -0.05f, 0.0f), 35.0f, 4.0f, 5.0f,
                                    ENTITY_PLAYER_ID);
                }
            }
        }
        clock_t end = clock();
        double ms = ((double)(end - start) / (double)CLOCKS_PER_SEC) * 1000.0 / MOB_BENCH_REPEATS;
        printf("arrows x%d vs mobs x%d: %.3f ms/tick-set (60 ticks), impacts %u\n",
               projectile_active_count(&parrows), mob_active_count(&pool), ms,
               (unsigned)parrows.impacts);
    }

    /* Case 5: 10 skeletons aiming at the player (ranged AI: range
     * management + LOS + draw/fire cadence through the shot queue). */
    {
        MobPool pool;
        mob_pool_init(&pool, 5u);
        EntityPool drops;
        entity_pool_clear(&drops);
        bench_player(&pi, 1.5f, 65.0f, 8.5f);
        pi.day_progress = 0.0f; /* Midnight: hostiles engage. */
        for (int i = 0; i < 10; ++i) {
            float z = 4.0f + (float)i * 0.9f;
            if (mob_spawn(&pool, ENTITY_SKELETON, mmath_vec3(12.5f, 65.0f, z), 0.0f) ==
                ENTITY_ID_NULL) {
                break;
            }
        }
        double ms = bench_ticks(&pool, &drops, w, &pi, 60, MOB_BENCH_REPEATS);
        printf("skeletons x%d: %.3f ms/tick-set (60 ticks), paths %u\n", mob_active_count(&pool),
               ms, (unsigned)pool.path_reqs);
    }

    world_destroy(w);
    printf("bench: all cases passed\n");
    return 0;
}
