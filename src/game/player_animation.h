#pragma once

/* First-person hand animation controller (deterministic state machine).
 *
 * The viewmodel arm is a real camera-relative model, so it gets a real
 * controller: locomotion states (IDLE/WALK/SPRINT), airborne (AIR), and
 * one-shot events (LAND/ATTACK/HURT) plus a held USE pose (eating, bow
 * draw) and a sneak modifier. Transitions are deterministic, one-shots
 * are interruptible by higher-priority events, and all timing runs on
 * simulation dt (frame-rate independent). Gameplay never reads this;
 * it only poses the hand.
 */

#include <stdbool.h>
#include <stddef.h>

/* Total time from the start of a punch to its resting pose. */
#define PLAYER_SWING_DURATION 0.42f

/* One-shot durations (seconds of simulation time). */
#define PLAYER_ANIM_LAND_DURATION 0.25f
#define PLAYER_ANIM_HURT_DURATION 0.30f

/* Stride rates (radians of bob phase per second) and amplitudes (blocks
 * of camera-space hand travel). */
#define PLAYER_ANIM_STRIDE_WALK 8.0f
#define PLAYER_ANIM_STRIDE_SPRINT 11.0f
#define PLAYER_ANIM_BOB_WALK 0.018f
#define PLAYER_ANIM_BOB_SPRINT 0.030f

/* Hard landing reference: this many blocks of fall = full dip. */
#define PLAYER_ANIM_LAND_FULL_DIST 6.0f
#define PLAYER_ANIM_LAND_MIN_DIST 1.0f
#define PLAYER_ANIM_LAND_MAX_DIP 0.09f

/* Return a clamped 0..1 position through a swing for elapsed seconds. */
float player_swing_phase(float elapsed_seconds);

/* Return the smooth 0..1 swing envelope for a clamped phase. */
float player_swing_weight(float phase);

/* Controller states. LAND/ATTACK/HURT are one-shots; the rest hold. */
typedef enum PlayerAnimState {
    PLAYER_ANIM_IDLE = 0,
    PLAYER_ANIM_WALK,
    PLAYER_ANIM_SPRINT,
    PLAYER_ANIM_AIR,
    PLAYER_ANIM_LAND,
    PLAYER_ANIM_ATTACK,
    PLAYER_ANIM_USE,
    PLAYER_ANIM_HURT
} PlayerAnimState;

/* Per-tick movement snapshot (all plain facts, no history). */
typedef struct PlayerAnimInput {
    bool moving;   /* Horizontal speed above the stride threshold. */
    bool sprinting; /* Sprint key held (only matters when moving). */
    bool grounded; /* Standing on solid. */
    bool sneaking; /* Sneak held (crouch dip + calmer bob). */
    bool use_hold; /* Eating or drawing the bow (steady raised pose). */
} PlayerAnimInput;

/* Controller: state + timers + edge latches (notify, then update). */
typedef struct PlayerAnim {
    PlayerAnimState state; /* Current state. */
    float t;               /* Seconds in the current one-shot (else 0). */
    float stride;          /* Bob phase (radians, advances in locomotion). */
    float land_mag;        /* 0..1 landing strength (LAND only). */
    bool sneak;            /* Sneak held (crouch dip + calmer bob). */
    bool ev_attack;        /* Latched swing start (consumed by update). */
    bool ev_hurt;          /* Latched flinch (consumed by update). */
    bool ev_land;          /* Latched touchdown (consumed by update). */
    float ev_land_dist;    /* Fall distance for the latched touchdown. */
} PlayerAnim;

/* Zero a controller (IDLE, no latches). */
void player_anim_init(PlayerAnim *a);

/* Latch a swing start (consumed by the next update). NULL-safe. */
void player_anim_notify_attacked(PlayerAnim *a);

/* Latch a flinch (consumed by the next update). NULL-safe. */
void player_anim_notify_hurt(PlayerAnim *a);

/* Latch a touchdown (consumed by the next update). Small hops
 * (dist <= PLAYER_ANIM_LAND_MIN_DIST) settle straight to base. */
void player_anim_notify_landed(PlayerAnim *a, float dist);

/* Advance the controller by dt seconds of simulation time (clamped
 * 0..0.25 internally; NaN reads as 0). NULL-safe. */
void player_anim_update(PlayerAnim *a, const PlayerAnimInput *in, float dt);

/* Camera-space hand pose for the current state (blocks of travel;
 * punch is the 0..1 swing envelope, 0 outside ATTACK). */
typedef struct PlayerAnimPose {
    float bob_x; /* Lateral sway. */
    float bob_y; /* Vertical bounce (>= 0). */
    float dip;   /* Downward settle (sneak crouch, landing; HURT lifts). */
    float raise; /* Up/in toward the face (USE hold). */
    float punch; /* Swing envelope (ATTACK only). */
    float arm_roll; /* Camera-plane wrist rotation in radians. */
    float arm_pitch; /* Forward/back forearm rotation in radians. */
    float arm_yaw; /* Side-to-side forearm rotation in radians. */
} PlayerAnimPose;

/* Pose for the controller's current state (zero pose on bad args). */
PlayerAnimPose player_anim_pose(const PlayerAnim *a);
