#include "game/raycast.h"
#include "render/camera.h"
#include "world/block.h"
#include "world/world.h"

#include <math.h>
#include <stddef.h>

/* Direction from yaw/pitch (matches camera convention). */
static Vec3 ray_dir(float yaw, float pitch)
{
    float cp = cosf(pitch);
    Vec3 d;
    d.x = -sinf(yaw) * cp;
    d.y = sinf(pitch);
    d.z = -cosf(yaw) * cp;
    return d; /* Not normalized when cp==0; DDA handles any scale via t. */
}

/* Targetable cells: anything with a breakable presence. Air is empty and
 * water is passed through (unbreakable, never a mining/placement target);
 * plants/flowers/torches are non-solid but must still be hittable (they
 * have hardness + drops and interaction_break accepts them).
 */
static bool raycast_targetable(uint16_t id)
{
    return id != (uint16_t)BLOCK_AIR && id != (uint16_t)BLOCK_WATER;
}

/* Cast a ray with Amanatides & Woo traversal.
 *
 * Args:
 *   eye: origin.
 *   yaw, pitch: direction.
 *   w: world.
 *   max_dist: range.
 *
 * Returns: hit result.
 */
HitResult raycast_from_eye(Vec3 eye, float yaw, float pitch, const World *w, float max_dist)
{
    HitResult r;
    r.hit = false;
    r.block[0] = r.block[1] = r.block[2] = 0;
    r.normal[0] = r.normal[1] = r.normal[2] = 0;
    r.point = eye;
    r.dist = 0.0f;
    if (w == NULL || max_dist <= 0.0f) {
        return r;
    }

    Vec3 d = ray_dir(yaw, pitch);
    if (fabsf(d.x) < 1e-8f && fabsf(d.y) < 1e-8f && fabsf(d.z) < 1e-8f) {
        return r;
    }

    int x = (int)floorf(eye.x);
    int y = (int)floorf(eye.y);
    int z = (int)floorf(eye.z);

    /* Eye inside a targetable cell (shouldn't happen): report immediately. */
    if (raycast_targetable(world_get_block(w, x, y, z))) {
        r.hit = true;
        r.block[0] = x;
        r.block[1] = y;
        r.block[2] = z;
        return r;
    }

    int step_x = (d.x > 0.0f) ? 1 : -1;
    int step_y = (d.y > 0.0f) ? 1 : -1;
    int step_z = (d.z > 0.0f) ? 1 : -1;

    const float inf = 1e30f;
    float t_delta_x = (fabsf(d.x) > 1e-8f) ? fabsf(1.0f / d.x) : inf;
    float t_delta_y = (fabsf(d.y) > 1e-8f) ? fabsf(1.0f / d.y) : inf;
    float t_delta_z = (fabsf(d.z) > 1e-8f) ? fabsf(1.0f / d.z) : inf;

    float fx = eye.x - (float)x;
    float fy = eye.y - (float)y;
    float fz = eye.z - (float)z;
    float t_max_x = (fabsf(d.x) > 1e-8f) ? ((step_x > 0 ? (1.0f - fx) : fx) * t_delta_x) : inf;
    float t_max_y = (fabsf(d.y) > 1e-8f) ? ((step_y > 0 ? (1.0f - fy) : fy) * t_delta_y) : inf;
    float t_max_z = (fabsf(d.z) > 1e-8f) ? ((step_z > 0 ? (1.0f - fz) : fz) * t_delta_z) : inf;

    float t = 0.0f;
    int nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < 256; ++i) {
        if (t_max_x < t_max_y && t_max_x < t_max_z) {
            x += step_x;
            t = t_max_x;
            t_max_x += t_delta_x;
            nx = -step_x;
            ny = 0;
            nz = 0;
        } else if (t_max_y < t_max_z) {
            y += step_y;
            t = t_max_y;
            t_max_y += t_delta_y;
            nx = 0;
            ny = -step_y;
            nz = 0;
        } else {
            z += step_z;
            t = t_max_z;
            t_max_z += t_delta_z;
            nx = 0;
            ny = 0;
            nz = -step_z;
        }
        if (t > max_dist) {
            return r;
        }
        if (raycast_targetable(world_get_block(w, x, y, z))) {
            r.hit = true;
            r.block[0] = x;
            r.block[1] = y;
            r.block[2] = z;
            r.normal[0] = nx;
            r.normal[1] = ny;
            r.normal[2] = nz;
            r.point = mmath_vec3(eye.x + d.x * t, eye.y + d.y * t, eye.z + d.z * t);
            r.dist = t;
            return r;
        }
    }
    return r;
}

/* Cast from a camera.
 *
 * Args:
 *   cam: camera.
 *   w: world.
 *   max_dist: range.
 *
 * Returns: hit result.
 */
HitResult raycast_from_camera(const Camera *cam, const World *w, float max_dist)
{
    HitResult miss;
    miss.hit = false;
    miss.block[0] = miss.block[1] = miss.block[2] = 0;
    miss.normal[0] = miss.normal[1] = miss.normal[2] = 0;
    miss.point = mmath_vec3(0.0f, 0.0f, 0.0f);
    miss.dist = 0.0f;
    if (cam == NULL) {
        return miss;
    }
    float yaw = 0.0f;
    float pitch = 0.0f;
    camera_get_yaw_pitch(cam, &yaw, &pitch);
    return raycast_from_eye(camera_get_position(cam), yaw, pitch, w, max_dist);
}
