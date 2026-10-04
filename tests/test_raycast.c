#include "test_main.h"

#include "game/player.h"
#include "game/raycast.h"
#include "math/mmath.h"
#include "render/camera.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

/* Shared fixture: floor y 60..64 over chunk (0,0), plus a wall column at
 * world (10, 60..70, 8). Eye heights must account for +1.62 player eye. */

/* Build the fixture world. NULL on OOM. */
static World *make_ray_world(void)
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
    for (int y = 60; y <= 70; ++y) {
        chunk_set_block(c, 10, y, 8, BLOCK_STONE);
    }
    if (world_add_chunk(w, c) != 0) {
        chunk_destroy(c);
        world_destroy(w);
        return NULL;
    }
    return w;
}

/* Test: looking straight down hits the floor top with an up normal.
 *
 * Returns: failure count.
 */
int test_raycast_down(void)
{
    int failures = 0;
    World *w = make_ray_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Vec3 eye = mmath_vec3(8.5f, 70.0f, 8.5f);
    HitResult r = raycast_from_eye(eye, 0.0f, -MMATH_PI * 0.5f, w, RAYCAST_MAX_DIST);
    TEST_ASSERT(r.hit == true);
    TEST_ASSERT(r.block[0] == 8 && r.block[1] == 64 && r.block[2] == 8);
    TEST_ASSERT(r.normal[0] == 0 && r.normal[1] == 1 && r.normal[2] == 0);
    TEST_ASSERT(r.dist > 0.0f && r.dist <= RAYCAST_MAX_DIST);
    world_destroy(w);
    return failures;
}

/* Test: horizontal ray hits the wall side with a -X normal.
 *
 * Returns: failure count.
 */
int test_raycast_wall(void)
{
    int failures = 0;
    World *w = make_ray_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    /* Eye at y=66.5 (inside wall height span), facing +X (yaw=-PI/2). */
    Vec3 eye = mmath_vec3(5.5f, 66.5f, 8.5f);
    HitResult r = raycast_from_eye(eye, -MMATH_PI * 0.5f, 0.0f, w, RAYCAST_MAX_DIST);
    TEST_ASSERT(r.hit == true);
    TEST_ASSERT(r.block[0] == 10 && r.block[1] == 66 && r.block[2] == 8);
    TEST_ASSERT(r.normal[0] == -1 && r.normal[1] == 0 && r.normal[2] == 0);
    TEST_ASSERT_FLOAT_EQ(r.dist, 4.5f, 0.05f);
    world_destroy(w);
    return failures;
}

/* Test: misses (sky, out-of-range, bad args).
 *
 * Returns: failure count.
 */
int test_raycast_miss(void)
{
    int failures = 0;
    World *w = make_ray_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    /* Straight up: nothing but sky. */
    HitResult up = raycast_from_eye(mmath_vec3(8.5f, 70.0f, 8.5f), 0.0f, MMATH_PI * 0.5f, w, RAYCAST_MAX_DIST);
    TEST_ASSERT(up.hit == false);

    /* Wall is 4.5 away: a 2-block reach misses it. */
    HitResult short_reach =
        raycast_from_eye(mmath_vec3(5.5f, 66.5f, 8.5f), -MMATH_PI * 0.5f, 0.0f, w, 2.0f);
    TEST_ASSERT(short_reach.hit == false);

    /* Bad args never hit. */
    HitResult null_world = raycast_from_eye(mmath_vec3(5.5f, 66.5f, 8.5f), 0.0f, 0.0f, NULL, 5.0f);
    TEST_ASSERT(null_world.hit == false);
    HitResult bad_dist = raycast_from_eye(mmath_vec3(5.5f, 66.5f, 8.5f), 0.0f, 0.0f, w, 0.0f);
    TEST_ASSERT(bad_dist.hit == false);

    world_destroy(w);
    return failures;
}

/* Test: camera wrapper agrees with the eye-level cast.
 *
 * Returns: failure count.
 */
int test_raycast_camera(void)
{
    int failures = 0;
    World *w = make_ray_world();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Camera *cam = camera_create();
    TEST_ASSERT(cam != NULL);
    if (cam == NULL) {
        world_destroy(w);
        return failures + 1;
    }
    camera_set_position(cam, mmath_vec3(5.5f, 66.5f, 8.5f));
    camera_set_yaw_pitch(cam, -MMATH_PI * 0.5f, 0.0f);
    HitResult r = raycast_from_camera(cam, w, RAYCAST_MAX_DIST);
    TEST_ASSERT(r.hit == true);
    TEST_ASSERT(r.block[0] == 10 && r.block[1] == 66 && r.block[2] == 8);

    HitResult null_cam = raycast_from_camera(NULL, w, RAYCAST_MAX_DIST);
    TEST_ASSERT(null_cam.hit == false);

    camera_destroy(cam);
    world_destroy(w);
    return failures;
}
