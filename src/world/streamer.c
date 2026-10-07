#include "world/streamer.h"
#include "core/log.h"
#include "core/time.h"
#include "world/chunk.h"
#include "world/world.h"
#include "world/world_gen.h"

#include <math.h>
#include <stddef.h>

/* Player world x/z to chunk coords (floor division, chunk size 16). */
static int player_chunk(float v)
{
    return (int)floorf(v / 16.0f);
}

/* Chebyshev distance from (cx,cz) to the center. */
static int chebyshev(int cx, int cz, int ccx, int ccz)
{
    int dx = cx - ccx;
    int dz = cz - ccz;
    if (dx < 0) {
        dx = -dx;
    }
    if (dz < 0) {
        dz = -dz;
    }
    return dx > dz ? dx : dz;
}

/* Mark existing 4-neighbours of (cx,cz) dirty so shared border faces remesh
 * once the new chunk's blocks are visible to the mesher.
 */
static void mark_neighbours_dirty(World *w, int cx, int cz)
{
    static const int OFF[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int i = 0; i < 4; ++i) {
        Chunk *n = world_get_chunk(w, cx + OFF[i][0], cz + OFF[i][1]);
        if (n != NULL) {
            n->dirty = true;
        }
    }
}

/* Initialise a streamer.
 *
 * Args:
 *   s: streamer.
 *   world: world to manage.
 *   seed: generation seed.
 *   render_distance: radius.
 *
 * Returns: 0 on success.
 */
int streamer_init(Streamer *s, World *world, long seed, int render_distance)
{
    if (s == NULL || world == NULL) {
        return -1;
    }
    if (render_distance < 0) {
        render_distance = 0;
    }
    if (render_distance > 15) {
        render_distance = 15;
    }
    s->world = world;
    s->seed = seed;
    s->render_distance = render_distance;
    s->center_cx = 0;
    s->center_cz = 0;
    s->has_center = false;
    s->generated_total = 0;
    s->unloaded_total = 0;
    s->gen_ms_total = 0.0;
    return 0;
}

/* Reset stats/center.
 *
 * Args:
 *   s: streamer.
 */
void streamer_reset(Streamer *s)
{
    if (s == NULL) {
        return;
    }
    s->has_center = false;
    s->generated_total = 0;
    s->unloaded_total = 0;
    s->gen_ms_total = 0.0;
}

/* Update streaming around the player.
 *
 * Args:
 *   s: streamer.
 *   player_pos: player position.
 *   max_new: generation budget (<=0 unlimited).
 */
void streamer_update(Streamer *s, Vec3 player_pos, int max_new)
{
    if (s == NULL || s->world == NULL) {
        return;
    }
    World *w = s->world;
    w->seed = s->seed;
    int ccx = player_chunk(player_pos.x);
    int ccz = player_chunk(player_pos.z);
    int r = s->render_distance;
    bool center_changed = !s->has_center || ccx != s->center_cx || ccz != s->center_cz;
    s->center_cx = ccx;
    s->center_cz = ccz;
    s->has_center = true;

    /* 1. Generate missing chunks (spiral-ish: nearest first for better UX).
     * Iterate rings from the center outward; stop at the budget. */
    int generated = 0;
    double t0 = time_now_seconds();
    for (int ring = 0; ring <= r; ++ring) {
        for (int dx = -ring; dx <= ring; ++dx) {
            for (int dz = -ring; dz <= ring; ++dz) {
                int edge = dx == ring || dx == -ring || dz == ring || dz == -ring;
                if (!edge) {
                    continue;
                }
                int cx = ccx + dx;
                int cz = ccz + dz;
                if (world_get_chunk(w, cx, cz) != NULL) {
                    continue;
                }
                if (max_new > 0 && generated >= max_new) {
                    goto unload_pass;
                }
                if (world_generate_chunk(w, cx, cz) == 0) {
                    generated++;
                    s->generated_total++;
                    mark_neighbours_dirty(w, cx, cz);
                }
            }
        }
    }
unload_pass:
    if (generated > 0) {
        double t1 = time_now_seconds();
        s->gen_ms_total += (t1 - t0) * 1000.0;
        LOG_DEBUG("streamer: +%d chunk(s) around (%d,%d) [total gen %zu]", generated, ccx, ccz, s->generated_total);
    }

    /* 2. Unload everything outside the square. Collect coordinates first
     * because removal compacts the dense array by swapping its last chunk. */
    int unload_list[WORLD_MAX_CHUNKS][2];
    size_t unload_n = 0;
    for (size_t i = 0; i < WORLD_MAX_CHUNKS && unload_n < WORLD_MAX_CHUNKS; ++i) {
        Chunk *c = w->chunks[i];
        if (c == NULL) {
            continue;
        }
        if (chebyshev(c->cx, c->cz, ccx, ccz) > r) {
            unload_list[unload_n][0] = c->cx;
            unload_list[unload_n][1] = c->cz;
            unload_n++;
        }
    }
    for (size_t i = 0; i < unload_n; ++i) {
        if (world_remove_chunk(w, unload_list[i][0], unload_list[i][1])) {
            s->unloaded_total++;
        }
    }
    if (unload_n > 0) {
        LOG_DEBUG("streamer: -%zu chunk(s) outside (%d,%d) [total unload %zu]", unload_n, ccx, ccz,
                  s->unloaded_total);
    }
    if (center_changed) {
        LOG_INFO("streamer: center (%d,%d) r=%d loaded=%zu", ccx, ccz, r, world_chunk_count(w));
    }
}

/* Loaded chunk count.
 *
 * Args:
 *   s: streamer.
 *
 * Returns: count.
 */
size_t streamer_loaded_count(const Streamer *s)
{
    if (s == NULL || s->world == NULL) {
        return 0;
    }
    return world_chunk_count(s->world);
}
