#pragma once

/* Chunk + session persistence (M5): versioned binary chunk files plus a
 * save-all helper. Only player-modified chunks (save_dirty) hit the disk;
 * untouched terrain regenerates deterministically from the seed.
 *
 * Chunk file layout (all integers little-endian, no struct padding):
 *   magic[4] = "MNC1", u16 version = 1, i32 cx, i32 cz,
 *   65536 x u16 block IDs (y-major chunk_index order).
 * Files live at <world>/chunks/c_<cx>_<cz>.bin.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "math/mmath.h"

/* Chunk file format version. Bump on ANY layout change. */
#define WORLD_CHUNK_VERSION 1

/* Chunk file magic bytes. */
#define WORLD_CHUNK_MAGIC_0 'M'
#define WORLD_CHUNK_MAGIC_1 'N'
#define WORLD_CHUNK_MAGIC_2 'C'
#define WORLD_CHUNK_MAGIC_3 '1'

/* Forward declarations (full types in their headers). */
typedef struct World World;
typedef struct Chunk Chunk;
typedef struct Player Player;

/* Write one chunk's blocks to <dir>/chunks/c_<cx>_<cz>.bin.
 * Creates the chunks/ directory as needed. Does NOT clear save_dirty
 * (the save-all pass owns flag management).
 *
 * Args:
 *   dir: world directory (must not be NULL).
 *   c: chunk to persist (must not be NULL).
 *
 * Returns: 0 on success, non-zero on I/O or validation failure.
 */
int world_save_write_chunk(const char *dir, const Chunk *c);

/* Load <dir>/chunks/c_<cx>_<cz>.bin into an already-created chunk.
 * Validates magic, version, dimensions, and coordinate match. Rejects
 * truncated/corrupt files safely (chunk left untouched on failure).
 *
 * Args:
 *   dir: world directory (must not be NULL).
 *   c: destination chunk with matching (cx,cz) (must not be NULL).
 *
 * Returns: 0 on success, non-zero when missing/corrupt/mismatched.
 */
int world_save_read_chunk(const char *dir, Chunk *c);

/* Persist a whole session: metadata (name/seed/mode from the world; player
 * transform, vitals, spawn, inventory from the player) plus every
 * save-dirty chunk (flags cleared on success). Worlds without a single
 * dirty chunk still get fresh metadata.
 *
 * Args:
 *   dir: world directory (must not be NULL).
 *   w: world (must not be NULL; w->seed/name/mode are stored).
 *   p: player (must not be NULL; pos/yaw/pitch/health/hunger/inv stored).
 *   spawn: stable world spawn feet (stored when has_spawn).
 *   has_spawn: true to persist the spawn point.
 *   day: time-of-day 0..1.
 *
 * Returns: 0 when metadata + all dirty chunks saved, non-zero otherwise
 * (per-chunk failures are logged; metadata failure fails fast).
 */
int world_save_all(const char *dir, World *w, const Player *p, Vec3 spawn, bool has_spawn, float day);

/* Delete a world directory tree (chunk bins, world.meta, dirs).
 * Only removes files inside <dir>/chunks plus the meta file, then the
 * directories themselves. Refuses empty/NULL paths.
 *
 * Args:
 *   dir: world directory (must not be NULL).
 *
 * Returns: 0 on success, non-zero on failure.
 */
int world_save_delete(const char *dir);
