#pragma once

/* Block/item particle pool (M7): small bounded CPU simulation for break,
 * hit, and pickup feedback. Velocities derive from a spawn counter hash
 * (deterministic, no RNG stream), so effects are stable and testable.
 * Rendering reads the pool (transient cubes); visuals never drive logic.
 * Pure CPU, headless-testable.
 */

#include "math/mmath.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pool capacity (a break burst uses ~12; storms stay bounded). */
#define PARTICLE_MAX 256

/* One particle: world-space motion, finite life, shrinking cube. */
typedef struct Particle {
    bool active;   /* True while alive. */
    Vec3 pos;      /* Center position (world). */
    Vec3 vel;      /* Velocity m/s (gravity applies when gravity != 0). */
    float age;     /* Seconds since spawn. */
    float life;    /* Total lifetime seconds (> 0 while active). */
    float size;    /* Edge length at birth (shrinks linearly to 0). */
    float r, g, b; /* Tint color 0..1 (block/item base color). */
    int tile;      /* Atlas tile for the cube faces. */
    float gravity; /* Downward accel m/s^2 (0 = drifting). */
} Particle;

/* Fixed pool (owned by AppContext by value; no per-frame allocation). */
typedef struct ParticlePool {
    Particle items[PARTICLE_MAX];
    uint32_t seq; /* Spawn counter (deterministic velocity hash). */
} ParticlePool;

/* Deactivate every slot + reset the sequence (NULL-safe no-op).
 *
 * Args:
 *   pool: pool to clear (may be NULL).
 */
void particle_pool_clear(ParticlePool *pool);

/* Count active particles.
 *
 * Args:
 *   pool: pool (may be NULL).
 *
 * Returns: active count.
 */
int particle_active_count(const ParticlePool *pool);

/* Spawn one particle (first free slot). No-op with zero/negative life or
 * size, or NULL pool.
 *
 * Args:
 *   pool: pool (must not be NULL for effect).
 *   pos, vel: initial state.
 *   life: lifetime seconds (> 0).
 *   size: birth edge length (> 0).
 *   r, g, b: tint color.
 *   tile: atlas tile index.
 *   gravity: downward accel (0 = none).
 *
 * Returns: slot index, or -1 when the pool is full (callers drop extras).
 */
int particle_spawn(ParticlePool *pool, Vec3 pos, Vec3 vel, float life, float size, float r, float g,
                   float b, int tile, float gravity);

/* Burst n block-colored cubes from a block volume (up-biased spray).
 * Unknown blocks fall back to stone color/tile. Spawns min(n, free).
 *
 * Args:
 *   pool: pool (must not be NULL for effect).
 *   block: broken block ID.
 *   cx, cy, cz: block cell coords.
 *   n: particles to attempt (> 0).
 *
 * Returns: particles actually spawned.
 */
int particle_burst_block(ParticlePool *pool, uint16_t block, int cx, int cy, int cz, int n);

/* Burst n item-colored cubes around a point (pickup poof).
 *
 * Args:
 *   pool: pool (must not be NULL for effect).
 *   item: item ID (unknown IDs spawn nothing).
 *   at: burst center.
 *   n: particles to attempt (> 0).
 *
 * Returns: particles actually spawned.
 */
int particle_burst_item(ParticlePool *pool, uint16_t item, Vec3 at, int n);

/* Simulate one frame: integrate, apply gravity, expire at life end.
 * Single dt step (particles are small and short-lived; no substeps).
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   dt: frame seconds (>= 0).
 */
void particle_update(ParticlePool *pool, float dt);
