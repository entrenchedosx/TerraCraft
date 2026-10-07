#include "test_main.h"

#include "core/mem.h"
#include "core/path.h"
#include "game/player_model.h"
#include "math/mmath.h"
#include "render/camera.h"
#include "world/block.h"
#include "world/chunk.h"

#include <stddef.h>
#include <string.h>

/* Test: vec3 addition.
 *
 * Returns: failure count.
 */
int test_vec3_add(void)
{
    int failures = 0;
    Vec3 a = mmath_vec3(1.0f, 2.0f, 3.0f);
    Vec3 b = mmath_vec3(4.0f, -1.0f, 0.5f);
    Vec3 c = mmath_vec3_add(a, b);
    TEST_ASSERT_FLOAT_EQ(c.x, 5.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(c.y, 1.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(c.z, 3.5f, 1e-6f);
    return failures;
}

/* Camera-local arm X/Y must retain their 70-degree screen projection. */
int test_camera_viewmodel_fov_compensation(void)
{
    int failures = 0;
    Camera *cam = camera_create();
    TEST_ASSERT(cam != NULL);
    if (cam == NULL) {
        return failures;
    }
    const float test_fovs[] = {40.0f, 70.0f, 120.0f};
    const float authored_half_fov = 70.0f * (MMATH_PI / 360.0f);
    const float local_x = 0.41f;
    const float local_y = -0.37f;
    const float local_z = 0.63f;
    for (size_t i = 0; i < sizeof(test_fovs) / sizeof(test_fovs[0]); ++i) {
        camera_set_fov_y(cam, test_fovs[i]);
        float scale = camera_get_viewmodel_xy_scale(cam, 70.0f);
        float current_half_fov = test_fovs[i] * (MMATH_PI / 360.0f);
        float projected_x = (local_x * scale) / (local_z * tanf(current_half_fov));
        float projected_y = (local_y * scale) / (local_z * tanf(current_half_fov));
        float expected_x = local_x / (local_z * tanf(authored_half_fov));
        float expected_y = local_y / (local_z * tanf(authored_half_fov));
        TEST_ASSERT_FLOAT_EQ(projected_x, expected_x, 1e-5f);
        TEST_ASSERT_FLOAT_EQ(projected_y, expected_y, 1e-5f);
    }
    TEST_ASSERT_FLOAT_EQ(camera_get_viewmodel_xy_scale(cam, 121.0f), 1.0f, 1e-6f);
    camera_destroy(cam);
    TEST_ASSERT_FLOAT_EQ(camera_get_viewmodel_xy_scale(NULL, 70.0f), 1.0f, 1e-6f);
    return failures;
}

/* The sleeve stays at the shoulder end, above and to the right of the wrist;
 * the held item remains attached at the wrist through a punch. */
int test_player_viewmodel_arm_pose(void)
{
    int failures = 0;
    Vec3 shoulder = mmath_vec3(0.62f, -0.30f, 0.98f);
    Vec3 sleeve_end = mmath_vec3(0.84f, -0.04f, 1.00f);
    Vec3 wrist_end = mmath_vec3(0.84f, -0.52f, 1.00f);
    const float swings[] = {0.0f, 0.70f};
    /* Centers of the block and tool models authored by the renderer. */
    const Vec3 item_centers[] = {
        {0.81f, -0.43f, 0.87f},
        {0.815f, -0.425f, 0.89f}
    };
    for (size_t i = 0; i < sizeof(swings) / sizeof(swings[0]); ++i) {
        Vec3 sleeve = player_viewmodel_arm_transform_point(sleeve_end, shoulder,
                                                           -0.35f, swings[i], 0.06f * swings[i]);
        Vec3 wrist = player_viewmodel_arm_transform_point(wrist_end, shoulder,
                                                          -0.35f, swings[i], 0.06f * swings[i]);
        TEST_ASSERT(sleeve.y > wrist.y);
        TEST_ASSERT(wrist.x < sleeve.x);
        TEST_ASSERT(wrist.x > 0.45f && sleeve.x < 1.10f);
        TEST_ASSERT(wrist.y > -0.80f && sleeve.y < 0.10f);
        for (size_t item_index = 0; item_index < sizeof(item_centers) / sizeof(item_centers[0]); ++item_index) {
            Vec3 item = player_viewmodel_arm_transform_point(
                item_centers[item_index], shoulder, -0.35f + 0.28f,
                swings[i], 0.06f * swings[i]);
            TEST_ASSERT(mmath_vec3_length(mmath_vec3_sub(item, wrist)) < 0.26f);
        }
    }
    return failures;
}

/* Test: vec3 dot/cross/length/normalize.
 *
 * Returns: failure count.
 */
int test_vec3_ops(void)
{
    int failures = 0;
    Vec3 x = mmath_vec3(1.0f, 0.0f, 0.0f);
    Vec3 y = mmath_vec3(0.0f, 1.0f, 0.0f);
    Vec3 z = mmath_vec3_cross(x, y);
    TEST_ASSERT_FLOAT_EQ(z.x, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(z.y, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(z.z, 1.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(mmath_vec3_dot(x, y), 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(mmath_vec3_length(mmath_vec3(3.0f, 4.0f, 0.0f)), 5.0f, 1e-5f);

    Vec3 n = mmath_vec3_normalize(mmath_vec3(0.0f, 0.0f, 2.0f));
    TEST_ASSERT_FLOAT_EQ(n.x, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(n.y, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(n.z, 1.0f, 1e-6f);
    return failures;
}

/* Test: mat4 identity * identity == identity.
 *
 * Returns: failure count.
 */
int test_mat4_identity(void)
{
    int failures = 0;
    Mat4 I = mmath_mat4_identity();
    Mat4 R = mmath_mat4_mul(I, I);
    for (int i = 0; i < 16; ++i) {
        TEST_ASSERT_FLOAT_EQ(R.m[i], I.m[i], 1e-6f);
    }
    /* Spot-check perspective returns non-degenerate matrix. */
    Mat4 P = mmath_mat4_perspective(70.0f * (MMATH_PI / 180.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    TEST_ASSERT(P.m[0] > 0.0f);
    TEST_ASSERT(P.m[5] > 0.0f);
    return failures;
}

/* Transform a 3D point by a column-major mat4 (w=1), for ortho checks. */
static Vec3 mat4_point(Mat4 m, float x, float y, float z)
{
    Vec3 o;
    o.x = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
    o.y = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
    o.z = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
    return o;
}

/* Test: y-down ortho maps screen corners to NDC (regression: the old
 * t<=b guard rejected this convention and returned identity, clipping
 * every 2D quad and leaving a black screen).
 *
 * Returns: failure count.
 */
int test_mat4_ortho_ydown(void)
{
    int failures = 0;
    Mat4 O = mmath_mat4_ortho(0.0f, 1280.0f, 720.0f, 0.0f, -1.0f, 1.0f);
    /* Must NOT be identity: m[0] and m[5] carry the axis scales. */
    TEST_ASSERT_FLOAT_EQ(O.m[0], 2.0f / 1280.0f, 1e-7f);
    TEST_ASSERT_FLOAT_EQ(O.m[5], -2.0f / 720.0f, 1e-7f);
    Vec3 tl = mat4_point(O, 0.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(tl.x, -1.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(tl.y, 1.0f, 1e-5f);
    Vec3 br = mat4_point(O, 1280.0f, 720.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(br.x, 1.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(br.y, -1.0f, 1e-5f);
    Vec3 mid = mat4_point(O, 640.0f, 360.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(mid.x, 0.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(mid.y, 0.0f, 1e-5f);
    /* Degenerate inputs still fall back to identity (no NaNs). */
    Mat4 bad = mmath_mat4_ortho(0.0f, 0.0f, 720.0f, 0.0f, -1.0f, 1.0f);
    TEST_ASSERT_FLOAT_EQ(bad.m[0], 1.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(bad.m[5], 1.0f, 1e-6f);
    return failures;
}

/* Test: block lookup table sanity.
 *
 * Returns: failure count.
 */
int test_block_table(void)
{
    int failures = 0;
    TEST_ASSERT(block_get_info(BLOCK_AIR)->solid == false);
    TEST_ASSERT(block_get_info(BLOCK_GRASS)->solid == true);
    TEST_ASSERT(block_is_solid(BLOCK_STONE) == true);
    TEST_ASSERT(block_is_solid(BLOCK_AIR) == false);
    TEST_ASSERT(block_is_transparent(BLOCK_WATER) == true);
    TEST_ASSERT(block_is_transparent(BLOCK_DIRT) == false);
    /* Invalid ID maps to AIR info (never NULL). */
    TEST_ASSERT(block_get_info(255) != NULL);
    TEST_ASSERT(block_get_info(255)->solid == false);
    return failures;
}

/* Test: chunk init/get/set/fill/empty.
 *
 * Returns: failure count.
 */
int test_chunk_basic(void)
{
    int failures = 0;
    /* Stack chunk is 64KB; fine for test but keep it static to be safe. */
    static Chunk c;
    chunk_init(&c, 3, -2);
    TEST_ASSERT(c.cx == 3);
    TEST_ASSERT(c.cz == -2);
    TEST_ASSERT(chunk_is_empty(&c) == true);

    TEST_ASSERT(chunk_set(&c, 1, 64, 1, BLOCK_GRASS) == true);
    TEST_ASSERT(chunk_get(&c, 1, 64, 1) == BLOCK_GRASS);
    TEST_ASSERT(chunk_is_empty(&c) == false);

    /* Out of bounds: get returns AIR, set returns false. */
    TEST_ASSERT(chunk_get(&c, -1, 0, 0) == BLOCK_AIR);
    TEST_ASSERT(chunk_get(&c, 0, 256, 0) == BLOCK_AIR);
    TEST_ASSERT(chunk_set(&c, 16, 0, 0, BLOCK_STONE) == false);

    chunk_fill(&c, BLOCK_STONE);
    TEST_ASSERT(chunk_is_empty(&c) == false);
    TEST_ASSERT(chunk_get(&c, 0, 0, 0) == BLOCK_STONE);
    TEST_ASSERT(chunk_get(&c, 15, 255, 15) == BLOCK_STONE);
    return failures;
}

/* Test: arena init/push/reset/destroy.
 *
 * Returns: failure count.
 */
int test_arena(void)
{
    int failures = 0;
    Arena a;
    TEST_ASSERT(arena_init(&a, 256) == 0);
    void *p1 = arena_push(&a, 64);
    TEST_ASSERT(p1 != NULL);
    void *p2 = arena_push(&a, 64);
    TEST_ASSERT(p2 != NULL);
    /* Exhaust: only 128 left, request too much. */
    TEST_ASSERT(arena_push(&a, 1024) == NULL);
    arena_reset(&a);
    TEST_ASSERT(arena_push(&a, 256) != NULL);
    arena_destroy(&a);
    /* Bad args. */
    TEST_ASSERT(arena_init(NULL, 64) != 0);
    return failures;
}

/* Test: exe-dir resolution + mcassets path building.
 *
 * Returns: failure count.
 */
int test_path_dirs(void)
{
    int failures = 0;
    char exe[PATH_MAX_LEN];
    TEST_ASSERT(path_exe_dir(exe, sizeof(exe)) == 0);
    TEST_ASSERT(exe[0] != '\0');
    TEST_ASSERT(path_is_dir(exe) == true);
    TEST_ASSERT(path_exe_dir(NULL, 10) != 0);
    TEST_ASSERT(path_exe_dir(exe, 0) != 0);
    char tiny[4];
    TEST_ASSERT(path_exe_dir(tiny, sizeof(tiny)) != 0);
    char mc[PATH_MAX_LEN];
    TEST_ASSERT(path_mcassets_dir(mc, sizeof(mc), "generated/tiles") == 0);
    /* Ends with mcassets/generated/tiles (either root). */
    size_t n = strlen(mc);
    TEST_ASSERT(n > 15);
    TEST_ASSERT(strcmp(mc + n - 15, "generated/tiles") == 0 ||
                strcmp(mc + n - 15, "generated\\tiles") == 0);
    TEST_ASSERT(path_mcassets_dir(NULL, 10, "x") != 0);
    TEST_ASSERT(path_mcassets_dir(mc, sizeof(mc), NULL) != 0);
    TEST_ASSERT(path_mcassets_dir(mc, sizeof(mc), "") != 0);
    return failures;
}
