#pragma once

/* Voxel raycasting (M3): Amanatides & Woo grid traversal. Pure CPU,
 * headless-testable. Selectable blocks are non-air, non-water cells
 * (plants/torches count; water/air are passed through).
 */

#include "math/mmath.h"

#include <stdbool.h>

/* Forward declarations (full types in their headers). */
typedef struct World World;
typedef struct Camera Camera;

/* Maximum block reach for crosshair targeting. */
#define RAYCAST_MAX_DIST 5.0f

/* Ray hit result. block/normal use int[3] (no ivec3 type in mmath). */
typedef struct HitResult {
    bool hit;      /* True when a targetable block was found in range. */
    int block[3];  /* Integer coords of the hit block (x,y,z). */
    int normal[3]; /* Face normal of entry (e.g. {0,1,0} for top). */
    Vec3 point;    /* Approximate world intersection point. */
    float dist;    /* Ray distance to the hit (<= max_dist). */
} HitResult;

/* Cast a ray from an eye position along a yaw/pitch direction.
 *
 * Args:
 *   eye: ray origin (world).
 *   yaw, pitch: direction angles, radians (same convention as camera).
 *   w: world (NULL never hits).
 *   max_dist: range limit (> 0).
 *
 * Returns: hit result (hit=false when nothing in range).
 */
HitResult raycast_from_eye(Vec3 eye, float yaw, float pitch, const World *w, float max_dist);

/* Cast a ray from a camera's eye along its view direction.
 *
 * Args:
 *   cam: camera (must not be NULL).
 *   w: world.
 *   max_dist: range limit.
 *
 * Returns: hit result (miss on bad args).
 */
HitResult raycast_from_camera(const Camera *cam, const World *w, float max_dist);
