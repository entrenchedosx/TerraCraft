#pragma once

/* TerraCraft lightweight header-only math library (M0).
 *
 * - Column-major Mat4 to match OpenGL expectations (m[col*4+row]).
 * - Right-handed coordinates, angles in radians.
 * - All functions are `static inline` so including this header in multiple
 *   translation units is safe.
 *
 * Naming: types `Vec3`, `Mat4`, `Quat`; functions prefixed `mmath_`.
 */

#include <math.h>
#include <string.h>

#ifndef MMATH_PI
#define MMATH_PI 3.14159265358979323846f
#endif

/* 3-component float vector. */
typedef struct Vec3 {
    float x, y, z;
} Vec3;

/* 4x4 column-major float matrix. Element (col,row) is m[col*4+row]. */
typedef struct Mat4 {
    float m[16];
} Mat4;

/* Quaternion (x,y,z vector part, w scalar part). */
typedef struct Quat {
    float x, y, z, w;
} Quat;

/* --- Vec3 --------------------------------------------------------------- */

/* Construct a Vec3.
 * Returns: vector (x,y,z).
 */
static inline Vec3 mmath_vec3(float x, float y, float z)
{
    Vec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

/* Add two vectors. Returns: a + b. */
static inline Vec3 mmath_vec3_add(Vec3 a, Vec3 b)
{
    return mmath_vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

/* Subtract b from a. Returns: a - b. */
static inline Vec3 mmath_vec3_sub(Vec3 a, Vec3 b)
{
    return mmath_vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

/* Scale a vector. Returns: v * s. */
static inline Vec3 mmath_vec3_scale(Vec3 v, float s)
{
    return mmath_vec3(v.x * s, v.y * s, v.z * s);
}

/* Dot product. Returns: a . b. */
static inline float mmath_vec3_dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* Cross product. Returns: a x b. */
static inline Vec3 mmath_vec3_cross(Vec3 a, Vec3 b)
{
    return mmath_vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

/* Squared length (avoids sqrt). Returns: |v|^2. */
static inline float mmath_vec3_length_sq(Vec3 v)
{
    return mmath_vec3_dot(v, v);
}

/* Length. Returns: |v|. */
static inline float mmath_vec3_length(Vec3 v)
{
    return sqrtf(mmath_vec3_length_sq(v));
}

/* Normalise. Returns: v/|v|, or (0,0,0) if |v| is ~0. */
static inline Vec3 mmath_vec3_normalize(Vec3 v)
{
    float len = mmath_vec3_length(v);
    if (len < 1e-8f) {
        return mmath_vec3(0.0f, 0.0f, 0.0f);
    }
    return mmath_vec3_scale(v, 1.0f / len);
}

/* --- Mat4 --------------------------------------------------------------- */

/* Identity matrix. Returns: I. */
static inline Mat4 mmath_mat4_identity(void)
{
    Mat4 out;
    memset(out.m, 0, sizeof(out.m));
    out.m[0] = 1.0f;
    out.m[5] = 1.0f;
    out.m[10] = 1.0f;
    out.m[15] = 1.0f;
    return out;
}

/* Multiply a * b (column-major, vectors are columns).
 * Applies b first, then a. Returns: a*b.
 */
static inline Mat4 mmath_mat4_mul(Mat4 a, Mat4 b)
{
    Mat4 out;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

/* Perspective projection matrix (right-handed, OpenGL NDC [-1,1]).
 *
 * Args:
 *   fov_y_rad: vertical field of view in radians.
 *   aspect: width / height.
 *   near_z: near plane (> 0).
 *   far_z: far plane (> near_z).
 *
 * Returns: perspective matrix (identity if args invalid).
 */
static inline Mat4 mmath_mat4_perspective(float fov_y_rad, float aspect, float near_z, float far_z)
{
    if (aspect <= 0.0f || near_z <= 0.0f || far_z <= near_z) {
        return mmath_mat4_identity();
    }
    float f = 1.0f / tanf(fov_y_rad * 0.5f);
    Mat4 out;
    memset(out.m, 0, sizeof(out.m));
    out.m[0] = f / aspect;
    out.m[5] = f;
    out.m[10] = (far_z + near_z) / (near_z - far_z);
    out.m[11] = -1.0f;
    out.m[14] = (2.0f * far_z * near_z) / (near_z - far_z);
    return out;
}

/* Look-at view matrix.
 *
 * Args:
 *   eye: camera position.
 *   center: look target.
 *   up: world up (should not be parallel to eye->center).
 *
 * Returns: view matrix (identity if eye==center or up degenerate).
 */static inline Mat4 mmath_mat4_lookat(Vec3 eye, Vec3 center, Vec3 up)
{
    Vec3 f = mmath_vec3_normalize(mmath_vec3_sub(center, eye));
    if (mmath_vec3_length_sq(f) < 1e-12f) {
        return mmath_mat4_identity();
    }
    Vec3 s = mmath_vec3_normalize(mmath_vec3_cross(f, up));
    if (mmath_vec3_length_sq(s) < 1e-12f) {
        return mmath_mat4_identity();
    }
    Vec3 u = mmath_vec3_cross(s, f);

    Mat4 out = mmath_mat4_identity();
    out.m[0] = s.x;
    out.m[1] = u.x;
    out.m[2] = -f.x;
    out.m[4] = s.y;
    out.m[5] = u.y;
    out.m[6] = -f.y;
    out.m[8] = s.z;
    out.m[9] = u.z;
    out.m[10] = -f.z;
    out.m[12] = -mmath_vec3_dot(s, eye);
    out.m[13] = -mmath_vec3_dot(u, eye);
    out.m[14] = mmath_vec3_dot(f, eye);
    return out;
}

/* Orthographic projection matrix (for 2D UI passes).
 * Maps [l,r]x[b,t]x[n,f] to NDC. Use ortho(0,w,h,0,-1,1) for y-down pixels
 * (t<b is legal and flips Y via a negative m[5]; the formula handles it).
 *
 * Args:
 *   l, r, b, t: left/right/bottom/top planes (r!=l, t!=b required).
 *   n, f: near/far planes (f!=n required).
 *
 * Returns: ortho matrix (identity if args invalid).
 */
static inline Mat4 mmath_mat4_ortho(float l, float r, float b, float t, float n, float f)
{
    if (r == l || t == b || f == n) {
        return mmath_mat4_identity();
    }
    Mat4 out;
    memset(out.m, 0, sizeof(out.m));
    out.m[0] = 2.0f / (r - l);
    out.m[5] = 2.0f / (t - b);
    out.m[10] = -2.0f / (f - n);
    out.m[12] = -(r + l) / (r - l);
    out.m[13] = -(t + b) / (t - b);
    out.m[14] = -(f + n) / (f - n);
    out.m[15] = 1.0f;
    return out;
}

/* --- Quat (minimal M0 subset) ------------------------------------------- */

/* Identity quaternion. Returns: (0,0,0,1). */
static inline Quat mmath_quat_identity(void)
{
    Quat q;
    q.x = 0.0f;
    q.y = 0.0f;
    q.z = 0.0f;
    q.w = 1.0f;
    return q;
}

/* Axis-angle constructor.
 *
 * Args:
 *   axis: rotation axis (will be normalised).
 *   angle_rad: rotation angle in radians.
 *
 * Returns: rotation quaternion (identity if axis degenerate).
 */
static inline Quat mmath_quat_from_axis_angle(Vec3 axis, float angle_rad)
{
    Vec3 n = mmath_vec3_normalize(axis);
    if (mmath_vec3_length_sq(n) < 1e-12f) {
        return mmath_quat_identity();
    }
    float half = angle_rad * 0.5f;
    float s = sinf(half);
    Quat q;
    q.x = n.x * s;
    q.y = n.y * s;
    q.z = n.z * s;
    q.w = cosf(half);
    return q;
}
