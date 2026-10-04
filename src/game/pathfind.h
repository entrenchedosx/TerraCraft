#pragma once

/* Bounded voxel A* pathfinder (M8): short-range ground navigation for
 * mobs. Nodes are standable block cells (feet clear + head clear + solid
 * ground below); moves are 4-directional with step up/down of at most 1
 * (no diagonals). Strictly bounded (search radius, expanded-node cap,
 * output length cap) so one request can never stall the frame. Pure CPU,
 * headless-testable, deterministic (fixed neighbor order, no RNG).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration (full type in world.h). */
typedef struct World World;

/* Search bounds (documented M8 limits). */
#define PATHFIND_RADIUS 16   /* Chebyshev radius around the start cell. */
#define PATHFIND_MAX_EXPAND 256 /* Max nodes popped from the open set. */
#define PATHFIND_MAX_LEN 48  /* Max waypoints written. */

/* Find a ground path from a feet cell to a target feet cell.
 * Start == target yields length 0 (success, nowhere to go). Unreachable
 * targets, out-of-bounds searches, and cap exhaustion return -1 (caller
 * falls back to direct movement or waits for the repath cooldown).
 *
 * Args:
 *   w: world (must not be NULL).
 *   sx, sy, sz: start feet cell.
 *   tx, ty, tz: target feet cell.
 *   out_xyz: [cap][3] receiver for waypoint feet cells, start-exclusive
 *     (first entry is the first step to take; must not be NULL).
 *   cap: receiver capacity in waypoints (1..PATHFIND_MAX_LEN).
 *
 * Returns: waypoint count (0..cap), or -1 on failure (bad args, no
 * standable start/target, unreachable, or over budget).
 */
int pathfind_ground(const World *w, int sx, int sy, int sz, int tx, int ty, int tz,
                    int out_xyz[][3], int cap);
