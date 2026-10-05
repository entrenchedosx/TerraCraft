#pragma once

/* Dropped-item + living-mob persistence (M6.1/M8): one atomic world-level
 * file per world directory. Entities are world-positioned (never
 * chunk-bound), so they save/load as a single list — chunk boundaries can
 * neither duplicate nor lose them. Only live entities are written:
 * despawned/picked-up items and dead/removed mobs simply vanish from the
 * file. Worlds without the file (M5/M6/M7 saves) load zero entities.
 * Pure CPU, headless-testable.
 *
 * Format v2 (all multi-byte fields little-endian, no struct padding ever):
 *   char magic[4]      "MNCE"
 *   u16 version        ENTITY_SAVE_VERSION (currently 2)
 *   u32 count          total records (0..ENTITY_SAVE_MAX_RECORDS)
 *   per record:
 *     u8 kind          1 = item, 2 = cow, 3 = zombie, 4 = skeleton
 *     item payload (38 bytes, M6.1 layout):
 *       u16 item       ItemId (must be a valid item)
 *       u16 count      1..item max stack
 *       f32 x,y,z      world position (finite, |v| <= 1e6)
 *       f32 vx,vy,vz   velocity (finite, |v| <= 100)
 *       u16 durability tool wear (0 unless damageable, <= max)
 *       f32 age        seconds since spawn (0..ENTITY_LIFETIME)
 *       f32 pickup_t   pickup delay remaining (0..ENTITY_PICKUP_DELAY)
 *     mob payload (28 bytes):
 *       u8 type        EntityType (2..4, must match kind)
 *       u8 state       MobState (0..6, never DEAD on disk)
 *       u8 reserved[2] zeros (must be 0)
 *       f32 x,y,z      feet position (finite, |v| <= 1e6)
 *       f32 yaw        facing radians (finite)
 *       f32 hp         health (0 < hp <= type max)
 *       f32 state_t    seconds in state (>= 0, finite)
 *
 * Version 1 files (M6.1/M7: items only, 38-byte records without the kind
 * byte) still load. Any violation (bad magic/version/count, truncation,
 * trailing bytes, invalid field, unknown kind) rejects the whole file
 * safely: pools are left cleared and a clear log is emitted. Runtime
 * EntityId handles, velocities (mobs restart at rest), AI targets, and
 * cached paths are intentionally NOT persisted (rebuilt live).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declarations (full types in their headers). */
typedef struct EntityPool EntityPool;
typedef struct MobPool MobPool;

/* Entity save file name inside a world directory. */
#define ENTITY_SAVE_FILE "entities.bin"

/* Current entity save format version (v1 = M6.1 items only). */
#define ENTITY_SAVE_VERSION 2

/* Maximum records per file (128 item drops + 64 living mobs). */
#define ENTITY_SAVE_MAX_RECORDS 192

/* Write all live entities to dir/entities.bin (temp-file + rename,
 * same atomicity pattern as the meta writer). An empty world writes a
 * valid zero-count file. A NULL item pool writes no items; a NULL mob
 * pool writes no mobs.
 *
 * Args:
 *   dir: world directory (must not be NULL).
 *   pool: item-drop pool (may be NULL).
 *   mobs: living-mob pool (may be NULL; only alive mobs persist).
 *
 * Returns: 0 on success, non-zero on I/O failure (old file kept).
 */
int entity_save_write(const char *dir, const EntityPool *pool, const MobPool *mobs);

/* Read dir/entities.bin into the pools (both cleared first). A missing
 * file is normal for M5/M6/M7 worlds: pools cleared, success returned.
 * Version 1 files (items only) still load. Corrupt files are rejected
 * safely (pools cleared, non-zero returned, reason logged).
 *
 * Args:
 *   dir: world directory (must not be NULL).
 *   pool: item-drop pool to fill (must not be NULL).
 *   mobs: living-mob pool to fill (may be NULL; mob records skipped).
 *
 * Returns: 0 on success (including missing file), non-zero on corrupt/
 * unreadable data (pools left cleared).
 */
int entity_save_read(const char *dir, EntityPool *pool, MobPool *mobs);
