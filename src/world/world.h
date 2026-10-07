#pragma once

/* World container (M2): heap chunks with O(1) hashmap lookup + dense array
 * for iteration. Right-handed, Y-up. X East, Z South. Chunk (cx,cz) covers
 * world x in [cx*16, cx*16+15], z in [cz*16, cz*16+15].
 *
 * The M1 sine-based `world_generate_stub` (3x3) is retained frozen for
 * unit-test determinism; streaming uses `world_gen` (noise terrain).
 */

#include "core/hashmap.h"
#include "world/gravity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* World display-name capacity (mirror of WORLD_NAME_LEN, no meta dep). */
#define WORLD_NAME_LEN 64

/* Maximum simultaneously loaded chunks. 1024 covers render distance 15
 * ((2*15+1)^2 = 961); the M2 default distance is far smaller. */
#define WORLD_MAX_CHUNKS 1024
#define WORLD_WATER_QUEUE_CAP 32768
#define WORLD_GRAVITY_QUEUE_CAP 8192
#define WORLD_FALLING_BLOCK_CAP 256

/* Sea level for water fill (M2 terrain fills water up to this height). */
#define WORLD_SEA_LEVEL 64

/* Forward declaration (full type in chunk.h). */
typedef struct Chunk Chunk;
typedef struct WorldWaterUpdate WorldWaterUpdate;
typedef void (*WorldBlockChangeCallback)(void *context, int wx, int wy, int wz, uint16_t block_id);
/* Return false to defer the transition (for example, when a reliable LAN
 * event queue is full); the physics scheduler retries without losing state. */
typedef bool (*WorldGravityEventCallback)(void *context, bool landed, int wx, int wy, int wz,
                                          uint16_t block_id);

/* World: owns up to WORLD_MAX_CHUNKS heap chunks. The compact array is the
 * iteration order; chunk_map mirrors it for O(1) lookup by chunk key.
 * name/mode describe the open session; save_dir (empty = session-only,
 * no disk I/O) roots chunk persistence for load-first generation and
 * save-on-unload. */
typedef struct World {
    Chunk *chunks[WORLD_MAX_CHUNKS]; /* Owned pointers in [0,count); remainder is NULL. */
    size_t count;                    /* Number of live chunks. */
    long seed;                       /* Generation seed. */
    int terrain_version;             /* 1 = legacy, 2 = warped landforms, 3 = modern-style landforms. */
    WorldWaterUpdate *water_updates;  /* Bounded coordinate queue; no chunk pointers. */
    size_t water_head;
    size_t water_count;
    float water_accumulator;
    size_t water_scan_chunk;
    size_t water_scan_cell;
    bool water_rescan_needed;
    bool water_rescan_repeat;
    WorldGravityUpdate *gravity_updates; /* Bounded coordinate queue; no chunk pointers. */
    size_t gravity_head;
    size_t gravity_count;
    float gravity_accumulator;
    size_t gravity_scan_chunk;
    size_t gravity_scan_cell;
    bool gravity_rescan_needed;
    bool gravity_rescan_repeat;
    WorldFallingBlock *falling_blocks; /* Fixed-capacity, world-coordinate physics records. */
    size_t falling_active;
    HashMap chunk_map;               /* Maps hashmap_chunk_key(cx,cz) -> Chunk*. */
    char name[WORLD_NAME_LEN];        /* Session display name. */
    int mode;                        /* Session game mode (WorldMode value). */
    char save_dir[512];              /* World directory ("" = no persistence). */
    WorldBlockChangeCallback block_change_callback; /* Optional synchronous replication hook. */
    void *block_change_context;       /* Borrowed callback context. */
    WorldGravityEventCallback gravity_event_callback; /* Optional falling start/landing replication hook. */
    void *gravity_event_context;      /* Borrowed callback context. */
    bool gravity_replicate_changes;   /* Whether save/unload settling is authoritative. */
} World;

/* Create an empty world (no chunks yet).
 *
 * Returns: owned World on success, NULL on OOM.
 */
World *world_create(void);

/* Destroy a world and all its chunks. NULL-safe.
 *
 * Args:
 *   w: world to destroy (may be NULL).
 */
void world_destroy(World *w);

/* Number of live chunks.
 *
 * Args:
 *   w: world (may be NULL).
 *
 * Returns: chunk count (0 on NULL).
 */
size_t world_chunk_count(const World *w);

/* Find a chunk by chunk coords (O(1) via hashmap).
 *
 * Args:
 *   w: world (must not be NULL).
 *   cx, cz: chunk coordinates.
 *
 * Returns: chunk pointer or NULL when absent.
 */
Chunk *world_get_chunk(const World *w, int cx, int cz);

/* Add an owned heap chunk to the world (takes ownership).
 * Fails when full or when (cx,cz) already present.
 *
 * Args:
 *   w: world (must not be NULL).
 *   c: heap chunk to adopt (must not be NULL, heap-allocated).
 *
 * Returns: 0 on success, non-zero on failure (chunk NOT freed; caller keeps it).
 */
int world_add_chunk(World *w, Chunk *c);

/* Remove and free the chunk at (cx,cz). No-op when absent. Also clears the
 * hashmap entry and compacts the dense array. When the world has a save
 * directory and the chunk is save-dirty, its edits are flushed to disk
 * first so teleports/streaming never lose player changes.
 *
 * Args:
 *   w: world (must not be NULL).
 *   cx, cz: chunk coordinates.
 *
 * Returns: true when a chunk was present and freed.
 */
bool world_remove_chunk(World *w, int cx, int cz);

/* Point the world at a save directory (enables load-first generation and
 * save-on-unload). Empty/NULL clears persistence (pure session world).
 * The directory is NOT created here (world_save_all creates on demand).
 *
 * Args:
 *   w: world (must not be NULL).
 *   dir: world directory path (NULL/empty disables persistence).
 */
void world_set_save_dir(World *w, const char *dir);

/* Get a block by world coordinates (cross-chunk aware).
 * Missing chunks and y outside [0,255] read as AIR.
 *
 * Args:
 *   w: world (must not be NULL).
 *   wx, wy, wz: world coords.
 *
 * Returns: block ID.
 */
uint16_t world_get_block(const World *w, int wx, int wy, int wz);

/* Change a block by world coordinates. The owning mesh and any adjacent
 * chunk meshes are dirtied; nearby fluid cells are scheduled for reevaluation.
 * Returns true only when a loaded cell changed. */
bool world_set_block(World *w, int wx, int wy, int wz, uint16_t id);

/* Apply a local predicted/simulated block change without invoking the
 * multiplayer replication callback. All local invalidation and neighbor
 * notifications still run. */
bool world_set_block_unreplicated(World *w, int wx, int wy, int wz, uint16_t id);

/* Install an optional synchronous block-change hook. Passing NULL disables it.
 * The callback runs after a changed block is stored and water is notified. */
void world_set_block_change_callback(World *w, WorldBlockChangeCallback callback, void *context);

/* Install an optional callback for authoritative falling-block start/landing
 * events. Clients use these events to keep local falling visuals in sync. */
void world_set_gravity_event_callback(World *w, WorldGravityEventCallback callback, void *context);
void world_set_gravity_replication(World *w, bool enabled);

/* Legacy M1 terrain height for a world (x,z) column (deterministic,
 * seed-aware). FROZEN for test determinism; new code prefers world_gen.
 * Formula: h = 64 + sin(x*0.1)*cos(z*0.1)*8 + seed variation.
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *
 * Returns: height in [56..72] clamped to [1..CHUNK_Y-1].
 */
int world_height_at(long seed, int wx, int wz);

/* Legacy M1 generator: 3x3 grid of chunks centered at (0,0): cx,cz in [-1..1].
 * Terrain rule per column (wx,wz) with H = world_height_at(seed,wx,wz):
 *   y == 0: BEDROCK; y < H-4: STONE; y < H: DIRT; y == H: GRASS; else AIR.
 * Existing chunks are kept; missing ones are allocated. All generated
 * chunks are marked dirty. Deterministic for a given seed.
 *
 * Args:
 *   w: world (must not be NULL).
 *   seed: generation seed (stored in w->seed).
 */
void world_generate_stub(World *w, long seed);
