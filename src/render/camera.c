#include "render/camera.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>

/* Concrete FPS camera. */
struct Camera {
    Vec3 position;
    float yaw; /* Radians around Y. 0 faces -Z. */
    float pitch; /* Radians around X, clamped. */
    float fov_y_rad;
    float near_z;
    float far_z;
};

/* Clamp pitch to +/-89 deg. */
static float clamp_pitch(float p)
{
    if (p > CAMERA_PITCH_LIMIT) {
        return CAMERA_PITCH_LIMIT;
    }
    if (p < -CAMERA_PITCH_LIMIT) {
        return -CAMERA_PITCH_LIMIT;
    }
    return p;
}

/* Create a camera.
 *
 * Returns: owned Camera or NULL.
 */
Camera *camera_create(void)
{
    Camera *cam = (Camera *)calloc(1, sizeof(Camera));
    if (cam == NULL) {
        LOG_ERROR("camera_create: out of memory");
        return NULL;
    }
    /* M1 spawn: above terrain center (8,~72,8); terrain H ~64..72. */
    cam->position = mmath_vec3(8.0f, 74.0f, 24.0f);
    cam->yaw = 0.0f;
    cam->pitch = -0.25f;
    cam->fov_y_rad = 70.0f * (MMATH_PI / 180.0f);
    cam->near_z = 0.1f;
    cam->far_z = 1000.0f;
    return cam;
}

/* Destroy a camera.
 *
 * Args:
 *   cam: camera to destroy.
 */
void camera_destroy(Camera *cam)
{
    free(cam);
}

/* Set the vertical FOV in degrees (clamped 40..120).
 *
 * Args:
 *   cam: camera.
 *   degrees: vertical FOV.
 */
void camera_set_fov_y(Camera *cam, float degrees)
{
    if (cam == NULL || !(degrees >= 40.0f) || !(degrees <= 120.0f)) {
        return;
    }
    cam->fov_y_rad = degrees * (MMATH_PI / 180.0f);
}

/* Forward from yaw/pitch (unit). */
static Vec3 yaw_pitch_forward(float yaw, float pitch)
{
    float cp = cosf(pitch);
    Vec3 f;
    f.x = -sinf(yaw) * cp;
    f.y = sinf(pitch);
    f.z = -cosf(yaw) * cp;
    return mmath_vec3_normalize(f);
}

/* Get view matrix.
 *
 * Args:
 *   cam: camera.
 *
 * Returns: look-at matrix.
 */
Mat4 camera_get_view(const Camera *cam)
{
    if (cam == NULL) {
        return mmath_mat4_identity();
    }
    Vec3 fwd = yaw_pitch_forward(cam->yaw, cam->pitch);
    Vec3 center = mmath_vec3_add(cam->position, fwd);
    return mmath_mat4_lookat(cam->position, center, mmath_vec3(0.0f, 1.0f, 0.0f));
}

/* Get projection matrix.
 *
 * Args:
 *   cam: camera.
 *   aspect: width/height.
 *
 * Returns: perspective matrix.
 */
Mat4 camera_get_proj(const Camera *cam, float aspect)
{
    if (cam == NULL) {
        return mmath_mat4_identity();
    }
    if (aspect <= 0.0f) {
        aspect = 16.0f / 9.0f;
    }
    return mmath_mat4_perspective(cam->fov_y_rad, aspect, cam->near_z, cam->far_z);
}

/* Get position.
 *
 * Args:
 *   cam: camera.
 *
 * Returns: position.
 */
Vec3 camera_get_position(const Camera *cam)
{
    if (cam == NULL) {
        return mmath_vec3(0.0f, 0.0f, 0.0f);
    }
    return cam->position;
}

/* Set position.
 *
 * Args:
 *   cam: camera.
 *   pos: new position.
 */
void camera_set_position(Camera *cam, Vec3 pos)
{
    if (cam == NULL) {
        return;
    }
    cam->position = pos;
}

/* Set yaw/pitch.
 *
 * Args:
 *   cam: camera.
 *   yaw, pitch: angles in radians.
 */
void camera_set_yaw_pitch(Camera *cam, float yaw, float pitch)
{
    if (cam == NULL) {
        return;
    }
    cam->yaw = yaw;
    cam->pitch = clamp_pitch(pitch);
}

/* Get yaw/pitch.
 *
 * Args:
 *   cam: camera.
 *   out_yaw/out_pitch: receivers.
 */
void camera_get_yaw_pitch(const Camera *cam, float *out_yaw, float *out_pitch)
{
    if (cam == NULL) {
        return;
    }
    if (out_yaw != NULL) {
        *out_yaw = cam->yaw;
    }
    if (out_pitch != NULL) {
        *out_pitch = cam->pitch;
    }
}

/* Apply mouse look.
 *
 * Args:
 *   cam: camera.
 *   dx, dy: pixels.
 *   sensitivity: rad/px.
 */
void camera_add_look(Camera *cam, float dx, float dy, float sensitivity)
{
    if (cam == NULL) {
        return;
    }
    cam->yaw -= dx * sensitivity;
    cam->pitch = clamp_pitch(cam->pitch - dy * sensitivity);
}

/* Get forward.
 *
 * Args:
 *   cam: camera.
 *
 * Returns: forward.
 */
Vec3 camera_get_forward(const Camera *cam)
{
    if (cam == NULL) {
        return mmath_vec3(0.0f, 0.0f, -1.0f);
    }
    return yaw_pitch_forward(cam->yaw, cam->pitch);
}

/* Get right (horizontal, normalized).
 *
 * Args:
 *   cam: camera.
 *
 * Returns: right.
 */
Vec3 camera_get_right(const Camera *cam)
{
    Vec3 fwd = camera_get_forward(cam);
    Vec3 right = mmath_vec3_cross(fwd, mmath_vec3(0.0f, 1.0f, 0.0f));
    if (mmath_vec3_length_sq(right) < 1e-12f) {
        return mmath_vec3(1.0f, 0.0f, 0.0f);
    }
    return mmath_vec3_normalize(right);
}

/* Translate by delta.
 *
 * Args:
 *   cam: camera.
 *   delta: movement.
 */
void camera_translate(Camera *cam, Vec3 delta)
{
    if (cam == NULL) {
        return;
    }
    cam->position = mmath_vec3_add(cam->position, delta);
}

/* Fly move.
 *
 * Args:
 *   cam: camera.
 *   fwd_amount, strafe_amount, lift_amount: inputs.
 *   speed: units/sec.
 *   dt: frame time.
 */
void camera_fly_move(Camera *cam, float fwd_amount, float strafe_amount, float lift_amount, float speed, float dt)
{
    if (cam == NULL || dt <= 0.0f) {
        return;
    }
    Vec3 fwd = camera_get_forward(cam);
    Vec3 right = camera_get_right(cam);
    Vec3 up = mmath_vec3(0.0f, 1.0f, 0.0f);
    Vec3 delta = mmath_vec3(0.0f, 0.0f, 0.0f);
    delta = mmath_vec3_add(delta, mmath_vec3_scale(fwd, fwd_amount * speed * dt));
    delta = mmath_vec3_add(delta, mmath_vec3_scale(right, strafe_amount * speed * dt));
    delta = mmath_vec3_add(delta, mmath_vec3_scale(up, lift_amount * speed * dt));
    camera_translate(cam, delta);
}

/* Normalize one plane (a,b,c,d) in place (normal part to unit length). */
static void normalize_plane(float p[4])
{
    float len = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    if (len < 1e-8f) {
        return;
    }
    p[0] /= len;
    p[1] /= len;
    p[2] /= len;
    p[3] /= len;
}

/* Extract frustum planes from the combined view-projection matrix.
 * Column-major m[col*4+row]; rows are gathered across columns (Gribb/Hartmann).
 *
 * Args:
 *   cam: camera.
 *   aspect: viewport aspect.
 *   out_planes: 6x4 receiver (left,right,bottom,top,near,far).
 */
void camera_get_frustum_planes(const Camera *cam, float aspect, float out_planes[6][4])
{
    static const float k_identity[6][4] = {
        {1.0f, 0.0f, 0.0f, 1000.0f},
        {-1.0f, 0.0f, 0.0f, 1000.0f},
        {0.0f, 1.0f, 0.0f, 1000.0f},
        {0.0f, -1.0f, 0.0f, 1000.0f},
        {0.0f, 0.0f, 1.0f, 1000.0f},
        {0.0f, 0.0f, -1.0f, 1000.0f},
    };
    if (out_planes == NULL) {
        return;
    }
    if (cam == NULL) {
        memcpy(out_planes, k_identity, sizeof(k_identity));
        return;
    }
    Mat4 vp = mmath_mat4_mul(camera_get_proj(cam, aspect), camera_get_view(cam));
    const float *m = vp.m;
    /* Rows of the column-major matrix. */
    float row0[4] = {m[0], m[4], m[8], m[12]};
    float row1[4] = {m[1], m[5], m[9], m[13]};
    float row2[4] = {m[2], m[6], m[10], m[14]};
    float row3[4] = {m[3], m[7], m[11], m[15]};
    for (int i = 0; i < 4; ++i) {
        out_planes[0][i] = row3[i] + row0[i]; /* Left. */
        out_planes[1][i] = row3[i] - row0[i]; /* Right. */
        out_planes[2][i] = row3[i] + row1[i]; /* Bottom. */
        out_planes[3][i] = row3[i] - row1[i]; /* Top. */
        out_planes[4][i] = row3[i] + row2[i]; /* Near. */
        out_planes[5][i] = row3[i] - row2[i]; /* Far. */
    }
    for (int p = 0; p < 6; ++p) {
        normalize_plane(out_planes[p]);
    }
}

/* Test an AABB against frustum planes (positive vertex method).
 *
 * Args:
 *   planes: 6 normalized planes (non-const param, see camera.h; read-only).
 *   mn, mx: box corners.
 *
 * Returns: true when intersecting/inside.
 */
bool camera_aabb_visible(float planes[6][4], Vec3 mn, Vec3 mx)
{
    if (planes == NULL) {
        return true;
    }
    for (int p = 0; p < 6; ++p) {
        float px = planes[p][0] >= 0.0f ? mx.x : mn.x;
        float py = planes[p][1] >= 0.0f ? mx.y : mn.y;
        float pz = planes[p][2] >= 0.0f ? mx.z : mn.z;
        if (planes[p][0] * px + planes[p][1] * py + planes[p][2] * pz + planes[p][3] < 0.0f) {
            return false;
        }
    }
    return true;
}
