#pragma once

/* Projectile framework (M9): a reusable ranged-combat capability shared by
 * player-fired and mob-fired shots (no separate PlayerArrow/SkeletonArrow
 * systems — differences come from owner, launch params, and aim only).
 *
 * A projectile is a lightweight fixed-pool entity (never chunk-bound, never
 * persisted): position/velocity integrate in fixed 1/60 s substeps with
 * gravity + drag, and every substep collides along the swept segment
 * (previous -> new position) so fast shots cannot tunnel. The nearest
 * valid impact wins (block beats entity on exact ties: never shoot
 * through walls). Damage flows through living_entity_damage; the owner
 * is an M8 EntityId handle (mob generation-safe, or ENTITY_PLAYER_ID),
 * ignored while the owner-grace timer runs so fresh shots never clip
 * their shooter. Grace expiry re-arms the owner as a valid target
 * (future reflection mechanics stay possible).
 *
 * Pure CPU, headless-testable. The app realizes frame events (sounds,
 * particles, player damage) exactly like MobFrameEvents.
 */

#include "math/mmath.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declarations (full types in their headers; projectile.c
 * includes them — this header stays light so mob.h never needs it). */
typedef struct World World;
typedef struct MobPool MobPool;
typedef struct EntityPool EntityPool;

/* Opaque-at-a-glance entity handle (defined in game/mob.h; repeated here
 * as uint32_t so this header compiles standalone). Values follow the M8
 * packing: slot index in the low 8 bits, generation above.
 */
typedef uint32_t ProjectileOwnerId;

/* ItemId for drop/death reporting (defined in game/inventory.h). */
typedef uint16_t ProjectileItemId;

/* Stable projectile type IDs (persistent-meaningful: never reorder,
 * never reuse; only appended to).
 */
typedef enum ProjectileType {
    PROJECTILE_NONE = 0, /* No projectile. */
    PROJECTILE_ARROW = 1 /* M9 stick-and-flint arrow (player + skeleton). */
} ProjectileType;

/* Projectile flight states. */
typedef enum ProjectileState {
    PROJECTILE_FLYING = 0,  /* Ballistic, colliding. */
    PROJECTILE_EMBEDDED = 1 /* Stuck in terrain, counting down. */
} ProjectileState;

/* Pool + tuning constants (original TerraCraft values, documented). */
#define PROJECTILE_MAX 32             /* Hard cap (spawn fails cleanly past it). */
#define PROJECTILE_SUBSTEP (1.0f / 60.0f) /* Fixed physics substep (never frame-rate scaled). */
#define PROJECTILE_OWNER_GRACE 0.2f  /* Owner ignored this long after firing. */
#define PROJECTILE_MAX_SHOT_DIST 128.0f /* Segment clamp per substep (sanity bound). */

/* Static per-type definition (data, not code — no arrow magic constants
 * in AI, physics, or combat call sites).
 */
typedef struct ProjectileDefinition {
    ProjectileType type; /* Stable type ID. */
    const char *name;    /* Display/debug name (never NULL). */
    float gravity;       /* Downward acceleration m/s^2. */
    float drag;          /* Per-second velocity retention (0..1). */
    float radius;        /* Collision radius (AABB expansion). */
    float life_fly;      /* Seconds flying before despawn. */
    float life_embed;    /* Seconds embedded before despawn. */
    bool can_embed;      /* True: block hits stick instead of despawning. */
} ProjectileDefinition;

/* One projectile (fixed pool slot; no heap, no handles — nothing
 * references a projectile after firing except the pool scan).
 */
typedef struct Projectile {
    bool active;          /* True while flying or embedded. */
    ProjectileType type;  /* Arrow (only type in M9). */
    int state;            /* ProjectileState value. */
    Vec3 pos;             /* Center position (world). */
    Vec3 prev_pos;        /* Start of the current swept segment. */
    Vec3 vel;             /* Velocity m/s. */
    ProjectileOwnerId owner; /* Shooter (mob handle or ENTITY_PLAYER_ID). */
    float damage;         /* Hit damage (charge-scaled at fire time). */
    float knock_power;    /* Hit knockback impulse. */
    float age;            /* Seconds since firing (flying lifetime). */
    float grace_t;        /* Owner-ignore remaining (seconds). */
    float embed_t;        /* Seconds embedded (embedded lifetime). */
    float embed_yaw;      /* Retained orientation when embedded. */
    float embed_pitch;    /* Retained orientation when embedded. */
} Projectile;

/* Fixed projectile pool (owned by AppContext by value). */
typedef struct ProjectilePool {
    Projectile projs[PROJECTILE_MAX];
    uint32_t fired;   /* Cumulative successful fires (debug/bench). */
    uint32_t impacts; /* Cumulative impacts of any kind (debug/bench). */
} ProjectilePool;

/* Player collision snapshot (explicit parameter — projectiles never
 * reach for globals; mirrors the MobPlayerInfo pattern).
 */
typedef struct ProjectilePlayer {
    Vec3 pos;      /* Player feet position. */
    float height;  /* Player collision height. */
    float width;   /* Player collision width. */
    bool alive;    /* Dead players are never hit. */
    bool creative; /* Creative players are never hit. */
} ProjectilePlayer;

/* Per-update projectile-side effects for the app to realize (sounds,
 * particles, player damage application — projectile.c stays headless
 * and audio-free, exactly like the MobFrameEvents pattern).
 */
typedef struct ProjectileFrameEvents {
    int player_hits;      /* Arrow strikes on the player this update. */
    float player_damage;  /* Total player damage to apply (hurt-gated). */
    Vec3 player_knock;    /* Knockback impulse for the player (may be zero). */
    int mobs_hit;         /* Non-lethal mob strikes this update. */
    int mobs_died;        /* Lethal mob strikes (drops already spawned). */
    Vec3 last_death_pos;  /* Feet position of the last death (FX anchor). */
    ProjectileItemId last_death_item; /* First drop of the last death (FX tint). */
    int blocks_hit;       /* Terrain impacts this update. */
    Vec3 last_block_pos;  /* Last terrain impact point (FX anchor). */
} ProjectileFrameEvents;

/* Look up a projectile definition (never NULL; unknown types yield a
 * safe inert definition so callers never branch on NULL).
 *
 * Args:
 *   type: projectile type.
 *
 * Returns: static definition (do not free).
 */
const ProjectileDefinition *projectile_definition(ProjectileType type);

/* Deactivate every slot and zero counters (NULL-safe no-op).
 *
 * Args:
 *   pool: pool to clear (may be NULL).
 */
void projectile_pool_clear(ProjectilePool *pool);

/* Count active projectiles (NULL-safe).
 *
 * Args:
 *   pool: pool (may be NULL).
 *
 * Returns: active count.
 */
int projectile_active_count(const ProjectilePool *pool);

/* Count embedded projectiles (NULL-safe).
 *
 * Args:
 *   pool: pool (may be NULL).
 *
 * Returns: embedded count.
 */
int projectile_embedded_count(const ProjectilePool *pool);

/* Fire a projectile (first free slot, owner grace armed, prev = origin).
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   type: projectile type (unknown types rejected).
 *   origin: spawn center (must already be clear of the shooter).
 *   dir: launch direction (normalized internally; zero vector rejected).
 *   speed: launch speed m/s (> 0).
 *   damage: hit damage (> 0).
 *   knock_power: hit knockback impulse (>= 0).
 *   owner: shooter handle (mob EntityId or ENTITY_PLAYER_ID).
 *
 * Returns: true on fire, false when the pool is full or args are bad
 *   (caller consumes no ammo/durability on false).
 */
bool projectile_fire(ProjectilePool *pool, ProjectileType type, Vec3 origin, Vec3 dir, float speed,
                     float damage, float knock_power, ProjectileOwnerId owner);

/* Remove a projectile (frees the slot; unknown/out-of-range indexes are
 * a silent no-op; NULL-safe).
 *
 * Args:
 *   pool: pool (may be NULL).
 *   index: slot 0..PROJECTILE_MAX-1.
 */
void projectile_remove(ProjectilePool *pool, int index);

/* Simulate every projectile: fixed-substep integrate, swept block +
 * entity collision (nearest wins), damage/knockback through the M8 APIs,
 * embed/decay/despawn. Mob deaths drop loot exactly once via the shared
 * item-entity system; player hits are reported (never applied here).
 *
 * Args:
 *   pool: projectile pool (must not be NULL).
 *   mobs: living-mob pool for entity hits (must not be NULL).
 *   drops: item-drop pool for death loot (must not be NULL).
 *   w: world for terrain collision (must not be NULL).
 *   player: player collision snapshot (must not be NULL).
 *   dt: frame seconds (>= 0; clamped internally, substepped at 1/60).
 *   ev: receives frame effects, cleared first (must not be NULL).
 */
void projectile_update(ProjectilePool *pool, MobPool *mobs, EntityPool *drops, World *w,
                       const ProjectilePlayer *player, float dt, ProjectileFrameEvents *ev);
