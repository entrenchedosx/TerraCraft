#pragma once

/* Fixed-tick, bounded gravity-block scheduling and falling-block physics.
 * Active records contain world coordinates only; chunk pointers are never
 * retained across streaming or save operations. */

#include <stdbool.h>
#include <stdint.h>

typedef struct World World;

typedef struct WorldGravityUpdate {
    int x;
    int y;
    int z;
} WorldGravityUpdate;

typedef struct WorldFallingBlock {
    bool active;
    bool awaiting_authority;
    bool locally_settled;
    uint16_t block_id;
    int x;
    int z;
    int source_y;
    int predicted_landing_y;
    float y;
    float velocity_y;
} WorldFallingBlock;

/* Queue a changed cell and the cell above for support reevaluation. */
void world_gravity_notify_block_changed(World *w, int x, int y, int z);

/* Seed newly adopted chunk gravity blocks without keeping chunk pointers. */
void world_gravity_seed_chunk(World *w, int cx, int cz);

/* Run fixed-step gravity and continuous falling-block collision. The host
 * replicates block transitions; LAN clients predict locally without echoing
 * simulation edits to the host. */
void world_gravity_tick(World *w, float dt, bool replicate_changes);

/* Apply host-authored start/landing events on a LAN client. Start events are
 * idempotent; even if the visual-record pool is full, the authoritative source
 * cell is cleared so a later landing cannot duplicate it. */
bool world_gravity_apply_remote_start(World *w, int x, int y, int z, uint16_t block_id);
bool world_gravity_apply_remote_landing(World *w, int x, int y, int z, uint16_t block_id);

/* Settle active records before save/unload so canonical chunk data remains
 * complete. Returns false if an active block could not be placed safely. */
bool world_gravity_settle_all(World *w, bool replicate_changes);
bool world_gravity_settle_chunk(World *w, int cx, int cz, bool replicate_changes);

/* Number of active continuous falling-block records. */
unsigned world_gravity_active_count(const World *w);
