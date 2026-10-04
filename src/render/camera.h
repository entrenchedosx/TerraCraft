#pragma once

/* First-person fly camera (M1): position + yaw/pitch, WASD + mouse look.
 * Pure math (no SDL) so unit tests stay headless; SDL polling lives in app.c.
 * Right-handed, Y-up. Yaw=0 faces -Z; positive yaw turns left (-X at yaw=+90).
 */

#include "math/mmath.h"

#include <stdbool.h>

/* Pitch clamp in radians (+/-89 deg). */
#define CAMERA_PITCH_LIMIT 1.55334306f

/* Opaque camera handle. */
typedef struct Camera Camera;

/* Create a camera above the M1 terrain (spawn ~ (8,72,8), yaw facing -Z).
 *
 * Returns: owned Camera on success, NULL on OOM.
 */
Camera *camera_create(void);

/* Destroy a camera. NULL-safe.
 *
 * Args:
 *   cam: camera to destroy (may be NULL).
 */
void camera_destroy(Camera *cam);

/* Get the view matrix (forward derived from yaw/pitch).
 *
 * Args:
 *   cam: camera (must not be NULL).
 *
 * Returns: view matrix (identity on bad args).
 */
Mat4 camera_get_view(const Camera *cam);

/* Get the perspective projection matrix.
 *
 * Args:
 *   cam: camera (must not be NULL).
 *   aspect: viewport width/height (> 0; falls back to 16/9).
 *
 * Returns: projection matrix.
 */
Mat4 camera_get_proj(const Camera *cam, float aspect);

/* Get the camera position.
 *
 * Args:
 *   cam: camera (must not be NULL).
 *
 * Returns: position (origin on bad args).
 */
Vec3 camera_get_position(const Camera *cam);

/* Set the camera position.
 *
 * Args:
 *   cam: camera (must not be NULL).
 *   pos: new position.
 */
void camera_set_position(Camera *cam, Vec3 pos);

/* Set yaw/pitch in radians (pitch clamped to +/-89 deg).
 *
 * Args:
 *   cam: camera (must not be NULL).
 *   yaw: rotation around Y.
 *   pitch: rotation around X.
 */
void camera_set_yaw_pitch(Camera *cam, float yaw, float pitch);

/* Get yaw/pitch in radians (outputs optional, may be NULL).
 *
 * Args:
 *   cam: camera (must not be NULL).
 *   out_yaw/out_pitch: receive angles (each may be NULL).
 */
void camera_get_yaw_pitch(const Camera *cam, float *out_yaw, float *out_pitch);

/* Set the vertical field of view in degrees (clamped to 40..120).
 *
 * Args:
 *   cam: camera (must not be NULL).
 *   degrees: vertical FOV (invalid values ignored).
 */
void camera_set_fov_y(Camera *cam, float degrees);

/* Apply mouse deltas to yaw/pitch (dx right+, dy down+).
 * yaw -= dx*sensitivity; pitch -= dy*sensitivity (clamped).
 *
 * Args:
 *   cam: camera (must not be NULL).
 *   dx, dy: mouse motion in pixels.
 *   sensitivity: radians per pixel (e.g. 0.0025).
 */
void camera_add_look(Camera *cam, float dx, float dy, float sensitivity);

/* Forward direction derived from yaw/pitch (unit length).
 *
 * Args:
 *   cam: camera (must not be NULL).
 *
 * Returns: forward vector (0,0,-1 on bad args).
 */
Vec3 camera_get_forward(const Camera *cam);

/* Right direction (forward x up, normalized; ignores pitch roll).
 *
 * Args:
 *   cam: camera (must not be NULL).
 *
 * Returns: right vector.
 */
Vec3 camera_get_right(const Camera *cam);

/* Translate the camera by a world-space delta.
 *
 * Args:
 *   cam: camera (must not be NULL).
 *   delta: movement vector.
 */
void camera_translate(Camera *cam, Vec3 delta);

/* Fly-move helper: delta = forward*fwd + right*strafe + up*lift.
 *
 * Args:
 *   cam: camera (must not be NULL).
 *   fwd_amount: forward input (-1..1, W positive).
 *   strafe_amount: right input (-1..1, D positive).
 *   lift_amount: up input (-1..1, Space positive).
 *   speed: units per second.
 *   dt: frame time in seconds.
 */
void camera_fly_move(Camera *cam, float fwd_amount, float strafe_amount, float lift_amount, float speed, float dt);

/* Extract the 6 frustum planes (left, right, bottom, top, near, far) from
 * the camera's combined view-projection matrix. Each plane is (a,b,c,d) with
 * a normalized (a,b,c) normal pointing INSIDE the frustum: a point p is
 * inside when a*p.x + b*p.y + c*p.z + d >= 0 for all planes.
 * Pure math (no GL), headless-testable.
 *
 * Args:
 *   cam: camera (must not be NULL).
 *   aspect: viewport width/height (> 0; falls back to 16/9).
 *   out_planes: receives 6 planes x 4 floats (must not be NULL).
 */
void camera_get_frustum_planes(const Camera *cam, float aspect, float out_planes[6][4]);

/* Test an axis-aligned bounding box against frustum planes (positive-vertex
 * method). Boxes touching a plane count as visible.
 *
 * Args:
 *   planes: 6 normalized planes from camera_get_frustum_planes.
 *   mn: AABB minimum corner.
 *   mx: AABB maximum corner.
 *
 * Returns: true when the box intersects (or is inside) the frustum.
 */
bool camera_aabb_visible(const float planes[6][4], Vec3 mn, Vec3 mx);
