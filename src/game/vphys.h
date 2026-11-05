#pragma once

/* Java Edition 1.21.11 player movement, ticked at the vanilla 20 TPS.
 *
 * A port of the LivingEntity / Player / LocalPlayer movement path, read from
 * the 1.21.11 client bytecode (see docs/PHYSICS_PARITY.md for the method list
 * and what is not ported):
 *   baseTick fluid state -> LocalPlayer.aiStep input (modifyInput, sprint) ->
 *   LivingEntity.aiStep (jump, fluid jump, velocity cutoffs) -> travel (air,
 *   water) -> Entity.move (maybeBackOffFromEdge, collide with step-up,
 *   fall distance) -> updatePlayerPose.
 *
 * State is double precision, velocities are blocks per tick, and the world
 * coordinate system matches TerraCraft (feet position, +Y up). Yaw is given in
 * vanilla degrees (0 faces +Z, increases clockwise seen from above).
 */

#include <stdbool.h>

typedef struct World World;

typedef enum VPose { VPOSE_STANDING = 0, VPOSE_CROUCHING, VPOSE_SWIMMING } VPose;

typedef struct VInput {
    float forward;   /* -1..1 (W positive). */
    float left;      /* -1..1 (A positive). */
    bool jump;
    bool shift;      /* Sneak key. */
    bool sprint_key; /* Sprint key held. */
} VInput;

typedef struct VPhys {
    double x, y, z;        /* Feet. */
    double px, py, pz;     /* Feet at the previous tick (render interpolation). */
    double vx, vy, vz;     /* Delta movement, blocks per tick. */
    bool on_ground;
    bool horizontal_collision;
    bool vertical_collision;
    bool vertical_collision_below;
    bool in_water;         /* wasTouchingWater. */
    bool eye_in_water;     /* wasEyeInWater. */
    bool swimming;
    bool sprinting;
    bool shift_key;        /* isShiftKeyDown (this tick's key). */
    bool jumping;          /* Jump key this tick. */
    double water_height;   /* getFluidHeight(WATER) of the last fluid update. */
    double fall_distance;
    int no_jump_delay;
    VPose pose;
    double width, height, eye_height;
    double landed_fall;    /* Fall distance at the last landing (>= 0), else -1; consumed by the caller. */
    bool flying;           /* abilities.flying (creative flight, collides with blocks, no gravity). */
    bool may_fly;          /* abilities.mayfly: double-tap jump toggles flying. */
    int jump_trigger;      /* LocalPlayer.jumpTriggerTime: ticks left for the second jump tap. */
    bool was_jumping;      /* LocalPlayer.wasJumping (jump key last tick). */
    float xxa, zza;        /* Processed input impulses of the last tick. */
    unsigned long long ticks;
} VPhys;

/* Reset to standing at (x, y, z) with zero velocity. */
void vphys_init(VPhys *s, double x, double y, double z);

/* One vanilla game tick. yaw_deg/pitch_deg in vanilla degrees (pitch positive
 * looking down). w may be NULL (no collision, no fluids). */
void vphys_tick(VPhys *s, const VInput *in, const World *w, float yaw_deg, float pitch_deg);

/* Pose dimensions (width, height, eye height). */
void vphys_pose_dims(VPose pose, double *width, double *height, double *eye);

/* Mth.sin / Mth.cos (65536-entry table) in float, for tests. */
float vphys_mth_sin(float radians);
float vphys_mth_cos(float radians);

/* Constants verified against the 1.21.11 bytecode. */
#define VPHYS_GRAVITY 0.08
#define VPHYS_DRAG_VERTICAL 0.98f
#define VPHYS_BASE_SPEED 0.10000000149011612
#define VPHYS_JUMP_POWER 0.42f
#define VPHYS_STEP_HEIGHT 0.6f
#define VPHYS_TICK_SECONDS 0.05f
