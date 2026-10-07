#pragma once

/* Bounded fixed-tick water simulation. Coordinates are queued instead of
 * chunk pointers so streaming and unloads cannot leave dangling work. */

#include <stdbool.h>
#include <stddef.h>

typedef struct World World;

struct WorldWaterUpdate {
    int x;
    int y;
    int z;
};

/* Seed exposed source/flow cells when a chunk becomes available. */
void world_water_seed_chunk(World *w, int cx, int cz);

/* Recheck fluids at a newly loaded chunk's four boundaries so neighboring
 * loaded water can resume spreading when an unloaded edge becomes available. */
void world_water_seed_chunk_edges(World *w, int cx, int cz);

/* Wake water adjacent to a changed block. */
void world_water_notify_block_changed(World *w, int x, int y, int z);

/* Advance the fixed-rate flow clock; at most a bounded number of cells run
 * per fluid interval, independent of total loaded world size. */
void world_water_tick(World *w, float dt);

/* As above, with an explicit replication policy for deterministic LAN
 * prediction. False still applies all local mesh/save/fluid notifications. */
void world_water_tick_mode(World *w, float dt, bool replicate_changes);

/* Pending work count is exposed for tests and debug inspection. */
size_t world_water_pending(const World *w);
