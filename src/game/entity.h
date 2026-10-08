#pragma once

/* Dropped-item entities (M6): small bounded pool, no heap churn, stable
 * indices (no realloc/compaction, so no dangling references). Entities are
 * world-positioned (never chunk-bound), so chunk unloads can't eat drops.
 * Pure CPU, headless-testable.
 */

#include "game/inventory.h"
#include "math/mmath.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pool capacity (36-slot death drops always fit with room to spare). */
#define ENTITY_MAX 128

/* Gameplay tuning (original TerraCraft values, documented). */
#define ENTITY_PICKUP_RADIUS 1.5f  /* Center-distance pickup range. */
#define ENTITY_PICKUP_DELAY 0.5f   /* Seconds before a fresh drop is grabbable. */
#define ENTITY_LIFETIME 300.0f     /* Seconds before despawn (5 minutes). */
#define ENTITY_GRAVITY 25.0f       /* Drop gravity (m/s^2). */
#define ENTITY_TERMINAL_VEL -30.0f /* Drop terminal velocity. */
#define ENTITY_HALF 0.125f         /* Drop collision half-extent. */

/* One dropped stack. Active entries simulate; inactive are free slots. */
typedef struct ItemEntity {
    bool active;      /* True while in the world. */
    Vec3 pos;         /* Feet-ish origin (collision box base). */
    Vec3 vel;         /* Velocity (m/s). */
    ItemStack stack;  /* Dropped stack (canonical, non-empty when active). */
    float age;        /* Seconds since spawn (despawn at LIFETIME). */
    float pickup_t;   /* Pickup delay remaining (grabbable at <= 0). */
} ItemEntity;

/* Fixed pool (owned by AppContext by value). */
typedef struct EntityPool {
    ItemEntity items[ENTITY_MAX];
} EntityPool;

/* Forward declaration (full type in world.h). */
typedef struct World World;

/* Forward declaration (full type in game/player.h). */
typedef struct Player Player;

/* Deactivate every slot (NULL-safe no-op).
 *
 * Args:
 *   pool: pool to clear (may be NULL).
 */
void entity_pool_clear(EntityPool *pool);

/* Spawn a drop: first free slot gets pos/stack, small deterministic pop
 * velocity derived from the spawn coords, fresh timers.
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   pos: spawn origin.
 *   stack: stack to drop (must be non-empty and valid).
 *
 * Returns: slot index, or -1 when the pool is full (caller keeps leftovers).
 */
int entity_spawn(EntityPool *pool, Vec3 pos, const ItemStack *stack);

/* Merge a stack into a nearby active entity holding the same item and wear
 * (within radius of pos). Partial merges leave the remainder in *stack;
 * a full merge empties it. Used as the no-loss fallback when the pool is
 * full: mob loot merges into a sibling drop instead of vanishing.
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   stack: stack to merge; consumed in place (must not be NULL).
 *   pos: merge origin.
 *   radius: merge range in blocks (> 0).
 *
 * Returns: items actually merged (0 when nothing merged).
 */
uint16_t entity_try_merge(EntityPool *pool, ItemStack *stack, Vec3 pos, float radius);

/* Transfer part or all of a stack into a dropped-item entity. The source is
 * changed only after an entity slot is secured; on failure both are intact.
 * Item wear is preserved for damageable single-item stacks.
 * Returns the entity slot, or -1 when arguments are invalid or the pool is full.
 */
int entity_drop_stack(EntityPool *pool, ItemStack *source, uint16_t count, Vec3 pos, Vec3 vel);

/* Count active entities.
 *
 * Args:
 *   pool: pool (may be NULL).
 *
 * Returns: active count.
 */
int entity_active_count(const EntityPool *pool);

/* Simulate one frame: gravity integrate (terminal-clamped), axis-separated
 * collide vs terrain (settle on contact, no bounce), age/pickup timers,
 * lifetime despawn. Fixed internal substeps? No — single dt step clamped
 * internally to 1/30 s slices to prevent tunneling at low fps.
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   w: world (NULL = free fall, still ages/despawns).
 *   dt: frame seconds (>= 0).
 */
void entity_update(EntityPool *pool, World *w, float dt);

/* Try pickup: every active, delay-expired entity within radius of the
 * player center attempts inventory insert. Fully stored entities
 * deactivate; partial stores leave the remainder; full inventory leaves
 * the entity untouched (never destroy items for lack of space).
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   p: player with inventory (must not be NULL).
 *   radius: pickup range in blocks (> 0).
 *   out_last: receives the last fully-collected ItemId this call
 *     (ITEM_NONE when nothing completed; may be NULL).
 *
 * Returns: entities fully collected this call.
 */
int entity_try_pickup(EntityPool *pool, Player *p, float radius, ItemId *out_last);
