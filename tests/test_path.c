#include "test_main.h"

#include "game/pathfind.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

/* Flat stone floor y 60..64 over chunk (0,0); tests sculpt on top. */
static World *make_path_world(void)
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

/* Every waypoint must be standable (feet+head clear, ground below). */
static int path_all_standable(const World *w, int xyz[][3], int n)
{
    for (int i = 0; i < n; ++i) {
        int x = xyz[i][0], y = xyz[i][1], z = xyz[i][2];
        if (block_is_solid(world_get_block(w, x, y, z))) {
            return 0;
        }
        if (block_is_solid(world_get_block(w, x, y + 1, z))) {
            return 0;
        }
        if (!block_is_solid(world_get_block(w, x, y - 1, z))) {
            return 0;
        }
    }
    return 1;
}

/* Consecutive waypoints must be king-moves without diagonals (|dx|+|dz|
 * <= 1, |dy| <= 1, and not a pure vertical hop).
 */
static int path_steps_legal(int xyz[][3], int n)
{
    for (int i = 1; i < n; ++i) {
        int dx = xyz[i][0] - xyz[i - 1][0];
        int dy = xyz[i][1] - xyz[i - 1][1];
        int dz = xyz[i][2] - xyz[i - 1][2];
        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }
        if (dz < 0) {
            dz = -dz;
        }
        if (dx + dz > 1 || dx + dz < 1 || dy > 1) {
            return 0;
        }
    }
    return 1;
}

/* Test: flat straight path is short, legal, and ends at the target.
 *
 * Returns: failure count.
 */
int test_path_flat(void)
{
    int failures = 0;
    World *w = make_path_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    int out[PATHFIND_MAX_LEN][3];
    int n = pathfind_ground(w, 2, 65, 2, 8, 65, 2, out, PATHFIND_MAX_LEN);
    TEST_ASSERT(n == 6);
    if (n > 0) {
        TEST_ASSERT(out[n - 1][0] == 8 && out[n - 1][1] == 65 && out[n - 1][2] == 2);
        TEST_ASSERT(path_all_standable(w, out, n));
        TEST_ASSERT(path_steps_legal(out, n));
    }
    /* Start == target: zero-length success. */
    TEST_ASSERT(pathfind_ground(w, 2, 65, 2, 2, 65, 2, out, PATHFIND_MAX_LEN) == 0);
    /* Bad args rejected. */
    TEST_ASSERT(pathfind_ground(NULL, 2, 65, 2, 8, 65, 2, out, PATHFIND_MAX_LEN) < 0);
    TEST_ASSERT(pathfind_ground(w, 2, 65, 2, 8, 65, 2, NULL, PATHFIND_MAX_LEN) < 0);
    TEST_ASSERT(pathfind_ground(w, 2, 65, 2, 8, 65, 2, out, 0) < 0);
    TEST_ASSERT(pathfind_ground(w, 2, 65, 2, 8, 65, 2, out, PATHFIND_MAX_LEN + 1) < 0);
    world_destroy(w);
    return failures;
}

/* Test: a wall is routed around (never through), path stays legal.
 *
 * Returns: failure count.
 */
int test_path_wall(void)
{
    int failures = 0;
    World *w = make_path_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Chunk *c = world_get_chunk(w, 0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        world_destroy(w);
        return failures + 1;
    }
    for (int y = 65; y <= 70; ++y) {
        chunk_set_block(c, 5, y, 2, BLOCK_STONE);
    }
    int out[PATHFIND_MAX_LEN][3];
    int n = pathfind_ground(w, 2, 65, 2, 8, 65, 2, out, PATHFIND_MAX_LEN);
    TEST_ASSERT(n > 6); /* Detour costs extra steps. */
    if (n > 0) {
        TEST_ASSERT(out[n - 1][0] == 8 && out[n - 1][1] == 65 && out[n - 1][2] == 2);
        TEST_ASSERT(path_all_standable(w, out, n));
        TEST_ASSERT(path_steps_legal(out, n));
        int through_wall = 0;
        for (int i = 0; i < n; ++i) {
            if (out[i][0] == 5 && out[i][2] == 2 && out[i][1] <= 70) {
                through_wall = 1;
            }
        }
        TEST_ASSERT(through_wall == 0);
    }
    world_destroy(w);
    return failures;
}

/* Test: one-block step up and small step down are traversable.
 *
 * Returns: failure count.
 */
int test_path_steps(void)
{
    int failures = 0;
    World *w = make_path_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Chunk *c = world_get_chunk(w, 0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        world_destroy(w);
        return failures + 1;
    }
    /* Mesa: floor rises to 65 top at x=6..9 (one step up from 65 feet? no:
     * raise a plateau with top at 66 so the path must step up). */
    for (int x = 6; x <= 9; ++x) {
        chunk_set_block(c, x, 65, 2, BLOCK_STONE);
    }
    int out[PATHFIND_MAX_LEN][3];
    int n = pathfind_ground(w, 2, 65, 2, 12, 65, 2, out, PATHFIND_MAX_LEN);
    TEST_ASSERT(n > 0);
    if (n > 0) {
        TEST_ASSERT(out[n - 1][0] == 12 && out[n - 1][1] == 65 && out[n - 1][2] == 2);
        TEST_ASSERT(path_all_standable(w, out, n));
        TEST_ASSERT(path_steps_legal(out, n));
        int stepped = 0;
        for (int i = 0; i < n; ++i) {
            if (out[i][1] == 66) {
                stepped = 1;
            }
        }
        TEST_ASSERT(stepped == 1);
    }
    world_destroy(w);
    return failures;
}

/* Test: sealed target is unreachable; far target exceeds the radius.
 *
 * Returns: failure count.
 */
int test_path_unreachable(void)
{
    int failures = 0;
    World *w = make_path_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Chunk *c = world_get_chunk(w, 0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        world_destroy(w);
        return failures + 1;
    }
    /* Seal (8,65,2) in a 3x3x3 stone box (target cell itself solid). */
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                chunk_set_block(c, 8 + dx, 65 + dy, 2 + dz, BLOCK_STONE);
            }
        }
    }
    int out[PATHFIND_MAX_LEN][3];
    TEST_ASSERT(pathfind_ground(w, 2, 65, 2, 8, 65, 2, out, PATHFIND_MAX_LEN) < 0);
    /* Beyond the search radius: rejected without searching the world. */
    TEST_ASSERT(pathfind_ground(w, 2, 65, 2, 60, 65, 2, out, PATHFIND_MAX_LEN) < 0);
    /* Non-standable start (inside rock) rejected. */
    TEST_ASSERT(pathfind_ground(w, 8, 65, 2, 2, 65, 2, out, PATHFIND_MAX_LEN) < 0);
    world_destroy(w);
    return failures;
}

/* Test: negative coordinates work; results are deterministic.
 *
 * Returns: failure count.
 */
int test_path_negative_deterministic(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Chunk *c = chunk_create(-2, -1);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        world_destroy(w);
        return failures + 1;
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
        return failures + 1;
    }
    /* World coords x -32..-17, z -16..-1; walk from (-30,65,-8) to (-20,65,-8). */
    int a[PATHFIND_MAX_LEN][3];
    int b[PATHFIND_MAX_LEN][3];
    int n1 = pathfind_ground(w, -30, 65, -8, -20, 65, -8, a, PATHFIND_MAX_LEN);
    int n2 = pathfind_ground(w, -30, 65, -8, -20, 65, -8, b, PATHFIND_MAX_LEN);
    TEST_ASSERT(n1 == 10 && n2 == 10);
    int same = 1;
    for (int i = 0; i < n1 && i < n2; ++i) {
        if (a[i][0] != b[i][0] || a[i][1] != b[i][1] || a[i][2] != b[i][2]) {
            same = 0;
        }
    }
    TEST_ASSERT(same == 1);
    if (n1 > 0) {
        TEST_ASSERT(path_all_standable(w, a, n1));
        TEST_ASSERT(path_steps_legal(a, n1));
    }
    world_destroy(w);
    return failures;
}
