#pragma once

/* Living-entity framework (M8): generation-safe handles, a fixed mob
 * pool coexisting with (never replacing) the M6 item-drop pool, data-driven
 * mob definitions, and per-frame simulation (10 Hz AI decisions, per-frame
 * physics). Pure CPU except rendering hooks; headless-testable.
 *
 * Identity: EntityId packs a slot index (low 8 bits) + generation (high
 * 24 bits). Slots recycle with a bumped generation, so a stale handle can
 * never alias an unrelated new mob. ENTITY_ID_NULL (0) is never issued
 * (slot 0 starts at generation 1); ENTITY_PLAYER_ID is reserved to mean
 * "the player" as an AI/combat target and never resolves to a mob.
 */

#include "game/inventory.h"
#include "math/mmath.h"

#include "game/audio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Opaque-at-a-glance entity handle (never a raw pointer). */
typedef uint32_t EntityId;

/* Null handle (resolves to nothing, always). */
#define ENTITY_ID_NULL 0u

/* Reserved target meaning "the player" (never resolves to a mob). */
#define ENTITY_PLAYER_ID 0xFFFFFFFEu

/* Stable entity type IDs (persistent: never reorder, never reuse).
 * Kind 2 was the M8 "mossling" placeholder; it is now the cow (same
 * value, so old saves load their grazers as cows). Kind 3 was the
 * "gloomstalker" placeholder; it is now the zombie (same value, old
 * night stalkers load as zombies — documented).
 */
typedef enum EntityType {
    ENTITY_NONE = 0,      /* No entity. */
    ENTITY_ITEM_DROP = 1,  /* M6 dropped-item entity (separate pool). */
    ENTITY_COW = 2,        /* Passive cow (real MC animal). */
    ENTITY_ZOMBIE = 3,     /* Hostile zombie (real MC monster). */
    ENTITY_SKELETON = 4   /* M9 hostile ranged creature. */
} EntityType;

/* AI states (finite-state machine, one decision tick at a time).
 * AIM is appended after DEAD (never renumber: saved states persist).
 */
typedef enum MobState {
    MOB_STATE_IDLE = 0, /* Standing, deciding. */
    MOB_STATE_WANDER,   /* Moving along a casual heading. */
    MOB_STATE_CHASE,    /* Pursuing the target (hostile). */
    MOB_STATE_ATTACK,   /* In range, swinging/firing on cooldown (hostile). */
    MOB_STATE_HURT,     /* Brief stagger after taking damage. */
    MOB_STATE_DEAD,     /* Death timer running, then removed. */
    MOB_STATE_AIM       /* Ranged: holding position, drawing on the target. */
} MobState;

/* Damage sources (M9: distinguishes melee, projectile, fall, and mob
 * attacks for future fireballs/thrown items/magic — stored per mob,
 * no behavior differences yet beyond debug logging).
 */
typedef enum DamageSource {
    DAMAGE_MELEE = 0,     /* Player melee swing. */
    DAMAGE_PROJECTILE = 1, /* Arrow (player- or mob-fired). */
    DAMAGE_FALL = 2,      /* Fall impact. */
    DAMAGE_MOB = 3        /* Mob melee strike. */
} DamageSource;

/* Pool + tuning constants (documented M8 values). */
#define MOB_MAX 64               /* Living pool slots (caps below stay under). */
#define MOB_MAX_PASSIVE 10       /* Natural passive cap. */
#define MOB_MAX_HOSTILE 10       /* Natural hostile cap. */
#define MOB_SIM_RANGE 64.0f      /* Beyond: frozen (persisted, not simulated). */
#define MOB_DESPAWN_RANGE 80.0f  /* Hostiles past this are removed. */
#define MOB_SPAWN_MIN_DIST 24.0f /* Natural spawns no closer than this. */
#define MOB_SPAWN_RING 25        /* Ring width: spawns land 24..48 out. */
#define MOB_SPAWN_PERIOD 5.0f    /* Seconds between spawn attempts. */
#define MOB_AI_TICK 0.1f         /* AI decisions at 10 Hz. */
#define MOB_GRAVITY 32.0f        /* Same gravity as the player. */
#define MOB_TERMINAL_VEL -78.4f  /* Same terminal velocity as the player. */
#define MOB_STEP_HEIGHT 1        /* Auto step-up height in blocks. */
#define MOB_PATH_MAX_NODES 48    /* Waypoint buffer per mob. */
#define MOB_REPATH_COOLDOWN 2.0f /* Seconds between path requests. */
#define MOB_DEATH_TIME 1.0f      /* Corpse lifetime before removal. */
#define MOB_HURT_WINDOW 0.4f     /* Post-hit invulnerability (seconds). */
#define PLAYER_HURT_WINDOW 0.5f  /* Post-hit mob-damage immunity (seconds). */
#define PLAYER_ATTACK_REACH 3.0f /* Default Java entity interaction reach. */

/* One drop-table entry (data-driven mob loot). */
typedef struct MobDrop {
    ItemId item;       /* ItemId to drop. */
    uint16_t min_count; /* Minimum count (>= 1 when chance hits). */
    uint16_t max_count; /* Maximum count (>= min_count). */
    float chance;       /* 0..1 roll per kill. */
} MobDrop;

/* Static per-type definition (data, not code). */
typedef struct MobDefinition {
    EntityType type;         /* Stable type ID. */
    const char *name;        /* Display/debug name (never NULL). */
    float max_health;        /* Spawn/current health. */
    float speed;             /* Ground speed m/s. */
    float width;             /* Collision width/depth. */
    float height;            /* Collision height. */
    float eye_height;        /* Eye above feet (sight origin). */
    bool hostile;            /* True: hunts the player. */
    float damage;            /* Melee damage per hit. */
    float attack_range;      /* Melee reach in blocks. */
    float attack_cooldown;   /* Seconds between mob attacks. */
    float knockback_resist;  /* 0..1 fraction of knockback ignored. */
    float detect_range;      /* Player detection radius (hostile). */
    float lose_range;        /* Target lost past this radius (hostile). */
    MobDrop drops[3];        /* Loot table. */
    int ndrops;              /* Loot entries used (0..3). */
    int model;               /* MobModel index (see game/mob_model.h). */
    bool ranged;             /* True: ranged AI (range management + arrow fire). */
    float prefer_min;        /* Preferred combat range inner edge (ranged). */
    float prefer_max;        /* Preferred combat range outer edge (ranged). */
    float fire_cooldown;     /* Seconds between shots (ranged). */
    float aim_time;          /* Seconds of aimed draw before firing (ranged). */
    float arrow_speed;       /* Arrow launch speed m/s (ranged). */
    float arrow_damage;      /* Arrow damage per hit (ranged). */
    float inaccuracy;        /* Base aim jitter radians, scaled by distance. */
} MobDefinition;

/* One living mob (fixed pool slot, addressed by EntityId handle). */
typedef struct Mob {
    bool active;        /* True while in the world. */
    uint32_t gen;       /* Generation (handle validation). */
    EntityType type;    /* Cow / gloomstalker / skeleton. */
    Vec3 pos;           /* Feet position (world). */
    Vec3 render_pos;    /* Interpolated feet for rendering (smoothed). */
    Vec3 prev_pos;      /* Previous tick position (stuck detect + interp). */
    Vec3 vel;           /* Velocity m/s. */
    float yaw;          /* Facing, radians. */
    float width;        /* Collision width (cached from definition). */
    float height;       /* Collision height (cached from definition). */
    float health;       /* Current health. */
    float max_health;   /* Maximum health. */
    float hurt_t;       /* Invulnerability remaining (seconds). */
    float attack_cd;    /* Attack cooldown remaining (seconds). */
    bool dead;          /* True once health hits 0 (until removed). */
    float dead_t;       /* Death timer (removal at MOB_DEATH_TIME). */
    bool grounded;      /* Standing on solid. */
    int state;          /* MobState value. */
    float state_t;      /* Seconds in the current state. */
    float ai_t;         /* AI tick accumulator (decisions at MOB_AI_TICK). */
    EntityId target;    /* ENTITY_ID_NULL = none (player = ENTITY_PLAYER_ID). */
    Vec3 wish_dir;       /* AI movement intent (unit-ish xz). */
    float wish_speed;    /* AI movement intent speed (0 = stand). */
    float wander_yaw;    /* Current wander heading. */
    float wander_t;      /* Wander leg timer. */
    int path_len;        /* Cached path waypoints. */
    int path_i;          /* Next waypoint index. */
    int path_xyz[MOB_PATH_MAX_NODES][3]; /* Waypoint cells (feet coords). */
    float repath_t;      /* Path request cooldown. */
    Vec3 stuck_pos;      /* Stuck-detection anchor. */
    float stuck_t;       /* Seconds without meaningful progress. */
    float walk_phase;    /* Animation phase (advanced while moving). */
    float fall_peak;     /* Highest feet Y while airborne (<0 = none). */
    float last_fall;     /* Fall distance of the last landing (<0 = none). */
    Vec3 hurt_from;      /* Horizontal dir TO the last attacker (flee). */
    int last_source;     /* DamageSource of the last damaging hit (transient). */
} Mob;

/* Fixed living pool (owned by AppContext by value). */
typedef struct MobPool {
    Mob mobs[MOB_MAX];
    uint32_t rng;    /* xorshift state (no global rand()). */
    float spawn_t;   /* Natural-spawn timer. */
    uint32_t ai_thinks; /* Cumulative AI decision ticks (debug). */
    uint32_t path_reqs; /* Cumulative path requests (debug). */
} MobPool;

/* Forward declarations (full types in their headers). */
typedef struct World World;
typedef struct EntityPool EntityPool;
typedef struct Player Player;

/* Slot index of a handle (-1 for null/reserved/foreign handles).
 *
 * Args:
 *   id: entity handle.
 *
 * Returns: slot 0..MOB_MAX-1, or -1.
 */
int entity_id_index(EntityId id);

/* Generation of a handle (0 for null).
 *
 * Args:
 *   id: entity handle.
 *
 * Returns: generation.
 */
uint32_t entity_id_gen(EntityId id);

/* Build a handle (index must be valid; generation 0 maps to 1 so the
 * result is never null for a real slot).
 *
 * Args:
 *   index: slot 0..MOB_MAX-1.
 *   gen: generation.
 *
 * Returns: handle (ENTITY_ID_NULL on bad index).
 */
EntityId entity_id_make(int index, uint32_t gen);

/* Initialise a pool (clears all slots, seeds the RNG).
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   seed: RNG seed (world seed works).
 */
void mob_pool_init(MobPool *pool, uint32_t seed);

/* Count active mobs (NULL-safe).
 *
 * Args:
 *   pool: pool (may be NULL).
 *
 * Returns: active count.
 */
int mob_active_count(const MobPool *pool);

/* Count active mobs of one type.
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   type: entity type.
 *
 * Returns: matching active count.
 */
int mob_count_type(const MobPool *pool, EntityType type);

/* Look up a mob definition (never NULL; unknown types yield a safe
 * inert definition so callers never branch on NULL).
 *
 * Args:
 *   type: entity type.
 *
 * Returns: static definition (do not free).
 */
const MobDefinition *mob_definition(EntityType type);

/* Spawn a mob (first free slot, fresh generation, full health, idle).
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   type: a tabled living type (cow/gloomstalker/skeleton;
 *     others rejected).
 *   pos: feet spawn position.
 *   yaw: initial facing.
 *
 * Returns: handle, or ENTITY_ID_NULL when full/invalid.
 */
EntityId mob_spawn(MobPool *pool, EntityType type, Vec3 pos, float yaw);

/* Resolve a handle to a live mob (generation + active checked; dead
 * mobs still resolve until removed so callers can read death state).
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   id: handle.
 *
 * Returns: mob or NULL (null/stale/freed/out-of-range handles).
 */
Mob *mob_resolve(MobPool *pool, EntityId id);

/* Remove a mob (frees the slot with a bumped generation; paths and
 * targets pointing at it go stale-safe automatically).
 *
 * Args:
 *   pool: pool (must not be NULL).
 *   id: handle (stale handles are a silent no-op).
 */
void mob_remove(MobPool *pool, EntityId id);

/* Player snapshot for mob AI/combat (explicit parameter — AI never
 * reaches for globals, which keeps a future multiplayer door open).
 */
typedef struct MobPlayerInfo {
    Vec3 pos;         /* Player feet position. */
    float eye_height; /* Player eye above feet. */
    bool alive;       /* False when dead (targets drop). */
    bool creative;    /* Hostiles never target creative players. */
    float day_progress; /* 0..1 clock (spawn + detection rules). */
} MobPlayerInfo;

/* Maximum arrow-fire requests per mob tick (hostile cap is 10, so 16
 * can never overflow — extras would drop loudly instead of silently).
 */
#define MOB_MAX_SHOTS 16

/* One requested arrow shot (ranged mob AI asks, the app fires through
 * the shared projectile pool — mobs never touch projectiles directly).
 */
typedef struct ProjectileShot {
    Vec3 origin;      /* Arrow spawn center (already clear of the shooter). */
    Vec3 dir;         /* Aim direction (normalized by projectile_fire). */
    float speed;      /* Launch speed m/s. */
    float damage;     /* Hit damage. */
    float knock_power; /* Hit knockback impulse. */
    EntityId owner;   /* Shooter handle (owner-grace applies). */
} ProjectileShot;

/* Per-frame mob-side effects for the app to realize (sounds, particles,
 * player damage application, requested arrow shots — mob.c stays
 * headless and audio-free).
 */
typedef struct MobFrameEvents {
    int player_hits;    /* Mob strikes landed this tick. */
    float player_damage; /* Total player damage to apply (gated by hurt_t). */
    Vec3 player_knock;  /* Knockback impulse for the player (may be zero). */
    int mobs_died;      /* Deaths this tick (drops already spawned). */
    int mobs_hurt;      /* Non-lethal damage ticks (fall damage; melee
                         * feedback plays at the strike site instead). */
    Vec3 last_death_pos; /* Feet position of the last death (FX anchor). */
    ItemId last_death_item; /* First drop item of the last death (FX tint). */
    int last_died_type;  /* EntityType of the last death (sound select). */
    int last_hurt_type;  /* EntityType of the last non-lethal hit (sound). */
    int fire_requests;  /* Requested arrow shots this tick (<= MOB_MAX_SHOTS). */
    ProjectileShot shots[MOB_MAX_SHOTS]; /* Fired via projectile_fire by the app. */
    int shots_dropped;  /* Requests dropped past the cap (never expected). */
} MobFrameEvents;

/* Simulate every mob: AI think (10 Hz), physics, stuck detection, fall
 * damage, death timers + drops, hostile despawn, natural spawning.
 * Mobs past MOB_SIM_RANGE freeze (persisted, not simulated).
 *
 * Args:
 *   pool: mob pool (must not be NULL).
 *   drops: item-drop pool for death loot (must not be NULL).
 *   w: world (must not be NULL).
 *   pi: player snapshot (must not be NULL).
 *   dt: frame seconds (>= 0).
 *   ev: receives frame effects, cleared first (must not be NULL).
 */
void mob_update_all(MobPool *pool, EntityPool *drops, World *w, const MobPlayerInfo *pi, float dt,
                    MobFrameEvents *ev);

/* Damage a mob through the hurt-window gate (knockback with resistance,
 * HURT state, death with exactly-once drops into the item pool).
 *
 * Args:
 *   pool: mob pool (must not be NULL).
 *   drops: item-drop pool for death loot (must not be NULL).
 *   id: target handle (stale/dead handles are a silent no-op).
 *   amount: damage (> 0).
 *   knock_dir: horizontal knockback direction (need not be normalized).
 *   knock_power: knockback impulse strength.
 *   from_dir: direction TO the attacker, stored for flee (may be NULL).
 *
 * Returns: true when this hit killed the mob, false otherwise.
 */
bool living_entity_damage(MobPool *pool, EntityPool *drops, EntityId id, float amount, Vec3 knock_dir,
                           float knock_power, const Vec3 *from_dir);

/* Damage a mob with an explicit source (same gate/knockback/drops as
 * living_entity_damage, which calls this with DAMAGE_MELEE).
 *
 * Args:
 *   pool, drops, id, amount, knock_dir, knock_power, from_dir: as above.
 *   source: DamageSource value (stored on the mob for debug).
 *
 * Returns: true when this hit killed the mob, false otherwise.
 */
bool living_entity_damage_src(MobPool *pool, EntityPool *drops, EntityId id, float amount,
                              Vec3 knock_dir, float knock_power, const Vec3 *from_dir, int source);

/* Hurt sound for a mob type (cows moo, everyone else uses the generic
 * thud — resolved at the call site so game logic stays audio-free).
 *
 * Args:
 *   type: EntityType value.
 *
 * Returns: AudioEvent to play.
 */
AudioEvent mob_hurt_sound(int type);

/* Death sound for a mob type (same rule as mob_hurt_sound).
 *
 * Args:
 *   type: EntityType value.
 *
 * Returns: AudioEvent to play.
 */
AudioEvent mob_die_sound(int type);

/* Ray vs yaw-aligned living-mob model bounds (nearest alive hit for melee
 * targeting). The bounds include a small animation margin and a ray starting
 * inside a mob counts as an immediate hit.
 *
 * Args:
 *   pool: mob pool (must not be NULL).
 *   eye: ray origin.
 *   dir: ray direction (normalized internally; zero vector never hits).
 *   max_dist: reach limit (> 0).
 *   out_dist: receives hit distance (may be NULL).
 *   out_id: receives hit handle (may be NULL).
 *
 * Returns: true on hit, false otherwise.
 */
bool mob_raycast(const MobPool *pool, Vec3 eye, Vec3 dir, float max_dist, float *out_dist,
                 EntityId *out_id);

/* True when a block cell shields a melee swing aimed past it (solid
 * blocks only — grass, flowers, torches, and water never block a
 * swing, though the mining ray still stops on plants by design).
 *
 * Args:
 *   block: block ID of the hit cell.
 *
 * Returns: true when the cell blocks the swing.
 */
bool mob_block_shields(uint16_t block);

/* Melee stats for a held item (original TerraCraft tuning: fist 1/0.4s,
 * shovel 2/0.5s, pickaxe 3/0.5s, axe 4/0.8s; non-tools hit as fists).
 *
 * Args:
 *   held: held item ID (ITEM_NONE = bare hands).
 *   out_damage: receives damage (may be NULL).
 *   out_cooldown: receives cooldown seconds (may be NULL).
 */
void mob_tool_stats(ItemId held, float *out_damage, float *out_cooldown);
/* Advance one mob's physics (gravity, wish-dir horizontal velocity,
 * axis-separated collision with 1-block step-up, grounded + fall
 * tracking). Pure CPU; the caller clamps dt.
 *
 * Args:
 *   m: mob (must not be NULL).
 *   w: world for collision (must not be NULL).
 *   dt: seconds to advance (> 0, already clamped).
 */
void mob_physics_step(Mob *m, World *w, float dt);
