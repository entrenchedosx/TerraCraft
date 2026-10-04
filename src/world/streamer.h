#pragma once

/* Chunk streaming manager (M2): keeps a square of chunks loaded around the
 * player and unloads the rest. Operates on a World (CPU chunks only); GPU
 * buffer pruning lives in the renderer (`renderer_prune_world`), called
 * after each update by the app loop.
 *
 * Loading uses noise terrain (`world_gen`); borders of newly loaded chunks
 * mark existing 4-neighbours dirty so shared faces remesh correctly.
 * Unloading frees CPU chunks via `world_remove_chunk`.
 */

#include "math/mmath.h"

#include <stdbool.h>
#include <stddef.h>

/* Forward declaration (full type in world.h). */
typedef struct World World;

/* Streaming state. Owned by AppContext (by value); the World is borrowed. */
typedef struct Streamer {
    World *world; /* Borrowed world (must outlive the streamer). */
    long seed; /* Generation seed (mirrored into world on update). */
    int render_distance; /* Chunk radius R (Chebyshev); loads (2R+1)^2. */
    int center_cx; /* Last streamed player chunk X. */
    int center_cz; /* Last streamed player chunk Z. */
    bool has_center; /* False until the first update runs. */
    size_t generated_total; /* Lifetime generated chunk count. */
    size_t unloaded_total; /* Lifetime unloaded chunk count. */
    double gen_ms_total; /* Lifetime generation time (ms). */
} Streamer;

/* Initialise a streamer (no chunks are loaded yet).
 *
 * Args:
 *   s: streamer to init (must not be NULL).
 *   world: world to manage (must not be NULL).
 *   seed: generation seed (stored in world on first update).
 *   render_distance: chunk radius >= 0 (0 = single chunk; clamped to [0,15]).
 *
 * Returns: 0 on success, non-zero on bad args.
 */
int streamer_init(Streamer *s, World *world, long seed, int render_distance);

/* Reset statistics and center (does not unload chunks; world untouched).
 *
 * Args:
 *   s: streamer (must not be NULL).
 */
void streamer_reset(Streamer *s);

/* Update loaded chunks around the player.
 * 1. Compute the player chunk (floor(x/16), floor(z/16)).
 * 2. Generate missing chunks in the square (budgeted: at most max_new;
 *    max_new <= 0 means unlimited, used for initial fill).
 * 3. Unload every loaded chunk outside the square.
 * Neighbours of newly generated chunks are marked dirty.
 *
 * Args:
 *   s: streamer (must not be NULL, must be initialised).
 *   player_pos: player world position.
 *   max_new: max chunks to generate this call (<= 0 = unlimited).
 */
void streamer_update(Streamer *s, Vec3 player_pos, int max_new);

/* Number of chunks currently loaded (== world count when exclusively managed).
 *
 * Args:
 *   s: streamer (may be NULL).
 *
 * Returns: loaded count.
 */
size_t streamer_loaded_count(const Streamer *s);
