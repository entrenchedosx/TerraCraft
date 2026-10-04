#pragma once

/* Physical player entity (M3): AABB body, fixed-step Newtonian physics,
 * axis-separated collision vs the voxel grid. Pure CPU, headless-testable.
 *
 * Conventions: pos is the FEET position. Yaw=0 faces -Z (matches camera).
 * Horizontal velocity is input-driven (arcade); vertical integrates gravity.
 * drag_air/drag_ground are per-physics-step velocity retention applied when
 * no move input is present (MC-style multipliers at 60 Hz).
 */

#include "math/mmath.h"

#include "game/inventory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration (full type in world.h). */
typedef struct World World;

/* Fixed physics step (seconds). The accumulator in PlayerUpdate splits
 * variable frame dt into these steps; excess past the step cap is dropped
 * (documented spiral-of-death guard). */
#define PLAYER_STEP_DT (1.0f / 60.0f)
#define PLAYER_MAX_STEPS 5

/* Per-frame input snapshot consumed by player_update. */
typedef struct PlayerInput {
    float fwd;    /* Forward input -1..1 (W positive). */
    float strafe; /* Right input -1..1 (D positive). */
    bool jump;    /* Jump / fly-up held. */
    bool sneak;   /* Sneak / fly-down held. */
    bool sprint;  /* Sprint held. */
} PlayerInput;

/* Player entity: body state, dimensions, tuning, inventory, survival.
 * Slots inv.slots[0..8] are the hotbar (hotbar_sel indexes them).
 */
typedef struct Player {
    Vec3 pos;         /* Feet position (world). */
    Vec3 vel;         /* Velocity (m/s). */
    Vec3 acc;         /* Last computed acceleration (debug/inspection). */
    float yaw;        /* Horizontal rotation, radians (0 faces -Z). */
    float pitch;      /* Vertical rotation, radians (clamped +/-89 deg). */
    float width;      /* Body width/depth (0.6). */
    float height;     /* Body height (1.8). */
    float eye_height; /* Eye above feet (1.62). */
    bool grounded;    /* Standing on solid (set by Y collision). */
    bool flying;      /* Creative flight (no gravity, no-clip). */
    bool sprinting;   /* Sprint flag from input (informational). */
    bool sneaking;    /* Sneak flag from input (half speed on ground). */
    float walk_speed;   /* 4.317 */
    float sprint_speed; /* 5.612 */
    float fly_speed;    /* 10.0 */
    float jump_vel;     /* 8.8 (~1.2 blocks with gravity 32). */
    float gravity;      /* 32.0 */
    float drag_air;     /* 0.91 retention/step with no input (airborne). */
    float drag_ground;  /* 0.6 retention/step with no input (grounded). */
    float step_accum;   /* Fixed-step time accumulator (internal). */
    int mode;           /* WorldMode value (0 survival, 1 creative). */
    Inventory inv;      /* 36 slots; slots 0..8 are the hotbar. */
    int hotbar_sel;     /* Selected hotbar slot 0..8. */
    float health;       /* Current health 0..max_health. */
    float max_health;   /* 20. */
    float hunger;       /* Current hunger 0..max_hunger. */
    float max_hunger;   /* 20. */
    bool dead;          /* True once health hits 0 (until respawn). */
    float hunger_t;     /* Hunger depletion accumulator (seconds). */
    float regen_t;      /* Regen/starve tick accumulator (seconds). */
    float exhaustion;   /* Activity strain 0..40 (M7: 4 points burn 1 hunger). */
    float hurt_t;       /* Mob-damage immunity remaining (M8, seconds). */
    float attack_cd;    /* Melee swing cooldown remaining (M8, seconds). */
    bool eat_active;    /* Eating in progress (RMB held on edible). */
    int eat_slot;       /* Hotbar slot being eaten from (0..8). */
    float eat_t;        /* Seconds into the current eat (EAT_TIME completes). */
    bool bow_drawing;   /* Bow draw in progress (RMB held on a bow). */
    int bow_slot;       /* Hotbar slot being drawn from (0..8). */
    float bow_t;        /* Seconds into the current draw (FULL_DRAW = full). */
    bool bow_full;      /* Full-draw feedback already played this draw. */
    float fall_peak;    /* Highest feet Y while airborne (<0 = none). */
    float last_fall;    /* Fall distance of the last landing (<0 = none). */
    bool mine_active;   /* Survival mining in progress. */
    int mine_bx, mine_by, mine_bz; /* Mining target cell. */
    uint16_t mine_block; /* Block ID at target when started. */
    ItemId mine_held;   /* Held item when started (slot switch resets). */
    float mine_progress; /* Seconds spent on target. */
    float mine_need;    /* Seconds required (from survival policy). */
} Player;

/* Initialise a player with M3 defaults (survival walk mode, slot defaults).
 * Position is left at origin; call player_find_spawn (or set pos) after.
 *
 * Args:
 *   p: player to init (must not be NULL).
 */
void player_init(Player *p);

/* Scan down from the top for a safe spawn near (sx,sz): first y with solid
 * footing and two free blocks above. Feet are placed on top of the footing.
 *
 * Args:
 *   w: world (must not be NULL).
 *   sx, sz: desired world column.
 *   out: receives feet position (must not be NULL).
 *
 * Returns: true when a spawn was found (false leaves out untouched).
 */
bool player_find_spawn(const World *w, int sx, int sz, Vec3 *out);

/* Eye (camera) position: pos + eye_height.
 *
 * Args:
 *   p: player (must not be NULL).
 *
 * Returns: eye position (origin on bad args).
 */
Vec3 player_eye_pos(const Player *p);

/* Apply mouse-look deltas to yaw/pitch (dx right+, dy down+).
 * yaw -= dx*sensitivity; pitch clamped to +/-89 deg.
 *
 * Args:
 *   p: player (must not be NULL).
 *   dx, dy: mouse motion in pixels.
 *   sensitivity: radians per pixel (e.g. 0.0025).
 */
void player_add_look(Player *p, float dx, float dy, float sensitivity);

/* AABB solidity test: true when any solid (collidable) block overlaps the
 * box [mn,mx]. Water/air never collide. Touching faces exactly does NOT
 * count (epsilon shrink on the max side).
 *
 * Args:
 *   w: world (NULL reads as empty).
 *   mn, mx: box corners (feet-based for the player).
 *
 * Returns: true on overlap with a solid block.
 */
bool player_aabb_solid(const World *w, Vec3 mn, Vec3 mx);

/* Advance the player by dt seconds using fixed 1/60 s substeps.
 * Flying: velocity set directly from input (incl. vertical), no gravity,
 * no collision (creative no-clip). Walking: horizontal velocity set from
 * wish direction * speed; vertical integrates gravity with terminal clamp;
 * axes resolve X, Z, then Y (down hits set grounded).
 *
 * Args:
 *   p: player (must not be NULL).
 *   in: input snapshot (NULL reads as no input).
 *   w: world (must not be NULL for collision; NULL skips collision).
 *   dt: frame time seconds (>= 0; clamped internally).
 */
void player_update(Player *p, const PlayerInput *in, World *w, float dt);
