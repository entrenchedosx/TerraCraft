#pragma once

/* World session management (M5): listing, creation, opening, saving, and
 * closing persistent worlds under saves/. Operates on AppContext (forward
 * declared here to avoid an include cycle; session.c includes core/app.h).
 * Everything here performs real filesystem I/O with validation — no fakes.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration (full type in core/app.h). */
typedef struct AppContext AppContext;

/* Saves root directory (under the process working directory). */
#define SESSION_SAVES_DIR "saves"

/* Maximum worlds enumerated per listing. */
#define SESSION_MAX_WORLDS 48

/* One listed world (metadata snapshot for the select screen). */
typedef struct WorldEntry {
    char name[64];    /* Display name from metadata. */
    char dirname[64]; /* Directory name under saves/ (sanitized). */
    int64_t seed;     /* World seed. */
    int mode;         /* WorldMode value. */
    int64_t played;   /* last_played timestamp (0 when unknown). */
} WorldEntry;

/* Enumerate saved worlds (directories with a readable world.meta),
 * sorted by last-played descending (most recent first).
 *
 * Args:
 *   saves_root: saves directory (usually SESSION_SAVES_DIR).
 *   out: receives entries (must not be NULL).
 *   cap: max entries.
 *
 * Returns: entries stored (may be 0), or -1 on bad args.
 */
int session_list_worlds(const char *saves_root, WorldEntry *out, size_t cap);

/* Create a new world directory + initial metadata (no chunks yet).
 * Sanitizes the name, uniquifies the directory ("Name", "Name (2)", ...),
 * parses the seed field (blank generates one from the wall clock), and
 * writes world.meta. Creates nothing on validation failure.
 *
 * Args:
 *   saves_root: saves directory.
 *   name: display name from the form (must not be NULL).
 *   seed_field: raw seed text (blank generates one).
 *   mode: WorldMode value.
 *   out_dir: receives the world directory path (must not be NULL, 512+ B).
 *
 * Returns: 0 on success, non-zero on failure.
 */
int session_create_world(const char *saves_root, const char *name, const char *seed_field, int mode,
                         char out_dir[512]);

/* Open a world session: reads metadata, builds world/streamer/player/clock,
 * and primes the staged loader counters (caller enters LOADING next).
 * Applies settings (render distance, FOV, sensitivity, vsync, pack).
 * Any previous session must be closed first.
 *
 * Args:
 *   app: application context (must not be NULL).
 *   world_dir: world directory path (must not be NULL).
 *
 * Returns: 0 on success, non-zero on failure (partial state unwound).
 */
int session_open_world(AppContext *app, const char *world_dir);

/* Save (optionally) and close the open session: metadata + dirty chunks,
 * GPU buffers dropped, world freed, flags cleared. Safe with no session.
 *
 * Args:
 *   app: application context (must not be NULL).
 *   save: true to persist before closing.
 */
void session_close_world(AppContext *app, bool save);

/* Persist the open session now (metadata + dirty chunks + dropped items and
 * living mobs). Called by the 30-second autosave, pause entry, and normal
 * world/app close paths. No-op without an open world.
 *
 * Args:
 *   app: application context (must not be NULL).
 *
 * Returns: 0 on success (or no session), non-zero on save failure.
 */
int session_save_now(AppContext *app);

/* Find a safe above-ground spawn near the origin after the initial fill:
 * spiral search for solid footing with air (not water) headroom.
 * Falls back to the first valid column, then to a high drop-in.
 *
 * Args:
 *   app: application context with an open world (must not be NULL).
 *
 * Returns: 0 on success, non-zero when no world is open.
 */
int session_find_spawn(AppContext *app);
