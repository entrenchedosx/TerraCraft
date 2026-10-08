#pragma once

/* Versioned world metadata (M5): name, seed, mode, player transform, clock,
 * last-played stamp. Simple key=value text, validated on load; malformed
 * files fail safely (never crash). Forward compatibility: unknown future
 * format versions are rejected with a clear error, never migrated silently.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Current metadata format version. Stays 1 for M6: the new keys
 * (health/hunger/spawn/inv) are optional with safe defaults, so M5
 * files load with migration defaults and M5 builds ignore the extras.
 * Bump only on breaking layout change (see BACKLOG policy).
 */
#define WORLD_META_VERSION 1
#define WORLD_TERRAIN_VERSION_CURRENT 4

/* World display-name capacity (incl. terminator). */
#define WORLD_NAME_LEN 64

/* Game modes stored in metadata (survival mechanics beyond physics are
 * future work; the mode gates spawn behavior for now).
 */
typedef enum WorldMode {
    WORLD_MODE_SURVIVAL = 0,
    WORLD_MODE_CREATIVE = 1
} WorldMode;

/* Metadata file name inside a world directory. */
#define WORLD_META_FILE "world.meta"

/* Inventory serialization capacity (36 slots; see game/inventory.h). */
#define WORLD_META_INV_SLOTS 36

/* Full world metadata record. M6 additions (health/hunger/spawn/inv) are
 * optional keys: absent keys yield migration defaults (full vitals,
 * no stored spawn, empty inventory).
 */
typedef struct WorldMeta {
    int version;               /* Format version (must equal WORLD_META_VERSION). */
    char name[WORLD_NAME_LEN]; /* Display name (NUL-terminated). */
    int64_t seed;              /* World seed (stored 64-bit; cast to long in use). */
    int mode;                  /* WorldMode value. */
    int terrain_version;       /* 1..3 frozen; 4 = climate/density worldgen. */
    float px, py, pz;          /* Player feet position. */
    float yaw, pitch;          /* Player look angles, radians. */
    float day;                 /* Time-of-day progress 0..1. */
    int64_t last_played;       /* Unix timestamp of last save. */
    bool has_player;           /* True when player fields are valid. */
    float health;              /* Player health (default: full). */
    float hunger;              /* Player hunger (default: full). */
    float spawn_x, spawn_y, spawn_z; /* Stable world spawn (feet). */
    bool has_spawn;            /* True when spawn fields are valid. */
    uint16_t inv_items[WORLD_META_INV_SLOTS];  /* Item IDs per slot. */
    uint16_t inv_counts[WORLD_META_INV_SLOTS];  /* Counts per slot. */
    /* M7 tool wear per slot (optional key "invdur", compact "slot:dur").
     * Absent entries read as 0 (brand-new); M6 files predate the key and
     * load every tool at full durability. Values are clamped by
     * inv_sanitize on apply, never trusted blindly. */
    uint16_t inv_dur[WORLD_META_INV_SLOTS];
} WorldMeta;

/* Fill safe defaults (version current, name "World", survival, no player).
 *
 * Args:
 *   m: record to initialise (must not be NULL).
 */
void world_meta_defaults(WorldMeta *m);

/* Write metadata to dir/world.meta (creates dir; temp-file + rename).
 *
 * Args:
 *   dir: world directory (must not be NULL).
 *   m: record to persist (must not be NULL).
 *
 * Returns: 0 on success, non-zero on failure (nothing partial left behind
 * except possibly a .tmp file on hard FS errors).
 */
int world_meta_write(const char *dir, const WorldMeta *m);

/* Read + validate dir/world.meta. Unknown keys ignored (forward tolerant
 * within the same version); missing/invalid required fields fail.
 * Future versions (version > CURRENT) are rejected, never guessed.
 *
 * Args:
 *   dir: world directory (must not be NULL).
 *   out: receives the record (must not be NULL).
 *
 * Returns: 0 on success, non-zero on missing/unreadable/corrupt/future data.
 */
int world_meta_read(const char *dir, WorldMeta *out);

/* Parse a metadata buffer (same rules as file read; for tests/tools).
 *
 * Args:
 *   text: NUL-terminated buffer (must not be NULL).
 *   out: receives the record (must not be NULL).
 *
 * Returns: 0 on success, non-zero on corrupt/future data.
 */
int world_meta_parse(const char *text, WorldMeta *out);
