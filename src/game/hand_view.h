#pragma once

/* First-person hand and held-item viewmodel, ported from the Java Edition
 * 1.21.11 ItemInHandRenderer / LivingEntity swing / GameRenderer.bobView
 * pipeline. Pure math (no GL), headless-testable.
 *
 * Two clearly separated halves:
 *   - Simulation: HandView advances on the vanilla 20 Hz tick clock (an
 *     internal accumulator turns variable sim dt into whole 0.05 s ticks) and
 *     owns swing progress, equip height, view sway and walk-distance state.
 *   - Presentation: hand_view_frame() interpolates that tick state with the
 *     partial tick (accumulator / 0.05) and composes the exact vanilla
 *     PoseStack transform chain into plain Mat4 values for the renderer.
 *
 * Coordinates are vanilla camera space: +X right, +Y up, -Z forward.
 * Constants are reference-derived (see docs/ANIMATION_PARITY.md); none are
 * tuned by eye.
 */

#include "math/mmath.h"

#include <stdbool.h>
#include <stddef.h>

/* Vanilla tick length (20 TPS). */
#define HAND_TICK_SECONDS 0.05f

/* LivingEntity.getCurrentSwingDuration() base value (no haste/fatigue). */
#define HAND_SWING_DURATION_TICKS 6

/* What the hand is doing with the held item (ItemUseAnimation subset). */
typedef enum HandUse {
    HAND_USE_NONE = 0, /* Not using an item. */
    HAND_USE_EAT,      /* Eating or drinking (applyEatTransform). */
    HAND_USE_BOW       /* Drawing a bow. */
} HandUse;

/* Held-item model category (selects the display transform). */
typedef enum HandItemKind {
    HAND_ITEM_NONE = 0, /* Empty hand: the player arm is drawn. */
    HAND_ITEM_SPRITE,   /* item/generated and item/handheld (flat sprite). */
    HAND_ITEM_BLOCK     /* block/block display (cube). */
} HandItemKind;

/* Per-sim-step facts fed to hand_view_advance(). */
typedef struct HandViewSample {
    int held_key;        /* Visual identity of the main-hand stack (0 = empty). */
    float yaw_deg;       /* View yaw, degrees, vanilla sense (turning right increases). */
    float pitch_deg;     /* View pitch, degrees, vanilla sense (looking down positive). */
    float step_dist;     /* Horizontal distance moved during this step (blocks). */
    bool on_ground;      /* Standing on a surface. */
    bool swimming;       /* Swimming pose (suppresses bob). */
    bool dead;           /* Dead (suppresses bob). */
    bool hands_busy;     /* isHandsBusy(): hand lowers fully. */
    float attack_delay_ticks; /* getCurrentItemAttackStrengthDelay() = 20 / attackSpeed. */
} HandViewSample;

/* Tick-state (field names follow the vanilla fields they mirror). */
typedef struct HandView {
    float accum;            /* Sim seconds toward the next 20 Hz tick. */
    float pending_dist;     /* Horizontal distance since the last tick. */
    HandViewSample last;    /* Most recent sample (consumed at tick time). */
    bool have_sample;       /* last is valid. */

    bool swinging;          /* LivingEntity.swinging. */
    int swing_time;         /* LivingEntity.swingTime. */
    float attack_anim;      /* LivingEntity.attackAnim. */
    float o_attack_anim;    /* LivingEntity.oAttackAnim. */

    float attack_ticker;    /* Player.attackStrengthTicker (ticks). */
    float swap_ticker;      /* LivingEntity.itemSwapTicker (ticks; drives the hand dip). */
    int last_held_key;      /* Player.lastItemInMainHand, by item identity. */
    bool have_last_held;

    float main_hand_height;   /* ItemInHandRenderer.mainHandHeight. */
    float o_main_hand_height; /* ItemInHandRenderer.oMainHandHeight. */
    int shown_key;            /* mainHandItem (what is currently drawn). */

    float x_bob, y_bob;     /* LocalPlayer.xBob / yBob (degrees). */
    float x_bob_o, y_bob_o; /* LocalPlayer.xBobO / yBobO. */
    float bob, o_bob;       /* Player.bob / oBob. */
    float walk_dist, walk_dist_o; /* Entity.walkDist / walkDistO. */

    float yaw_unwrapped;    /* Continuous yaw (degrees) so wraps never spike the sway. */
    bool have_rot;          /* Rotation bob state seeded from the first sample. */
} HandView;

/* Reset to a freshly spawned player (hand fully raised, no swing). */
void hand_view_init(HandView *hv);

/* Advance by dt seconds of simulation time, running whole 20 Hz ticks.
 * Rotation/bob inputs are sampled at tick boundaries; step_dist accumulates
 * across steps. NULL-safe; non-finite or negative dt reads as 0. */
void hand_view_advance(HandView *hv, float dt, const HandViewSample *sample);

/* LivingEntity.swing(MAIN_HAND): start a swing unless one is in its first
 * half. Returns true when a new swing started. */
bool hand_view_swing(HandView *hv);

/* Player.onAttack() -> resetOnlyAttackStrengthTicker(): a melee hit restarts
 * the attack cooldown only. In 1.21.11 this does NOT dip the hand (the dip is
 * driven by the separate item-swap ticker). */
void hand_view_reset_attack_strength(HandView *hv);

/* Player.resetAttackStrengthTicker(): restarts both tickers. Called for an
 * air swing (Minecraft.startAttack MISS) and, inside the tick, whenever the
 * main-hand item type changes. The hand dips and re-raises at a rate set by
 * the item's attack delay (getItemSwapScale). */
void hand_view_reset_swap(HandView *hv);

/* Fraction of a tick elapsed since the last whole tick (0..1). */
float hand_view_partial(const HandView *hv);

/* Same, plus extra_seconds of not-yet-simulated frame time, so the rendered
 * pose advances smoothly between 60 Hz simulation steps and at any frame
 * rate while the tick state itself stays on the 20 Hz clock. */
float hand_view_partial_ahead(const HandView *hv, float extra_seconds);

/* LivingEntity.getAttackAnim(partial): interpolated swing progress 0..1. */
float hand_view_swing_progress(const HandView *hv, float partial);

/* Player.getAttackStrengthScale(partial) in 0..1. */
float hand_view_attack_strength(const HandView *hv, float partial);

/* Player.getItemSwapScale(partial) in 0..1 (what the hand height follows). */
float hand_view_swap_scale(const HandView *hv, float partial);

/* GameRenderer.bobView matrix (applied to both the world view and the hand).
 * Identity when bobbing is disabled. */
Mat4 hand_view_bob_matrix(const HandView *hv, float partial, bool enabled);

/* Use-state for the frame (eat/bow). Ticks follow vanilla: remaining counts
 * down from duration while the item is used. */
typedef struct HandViewUse {
    HandUse kind;
    float remaining_ticks; /* getUseItemRemainingTicks() */
    float duration_ticks;  /* getUseDuration() */
} HandViewUse;

/* Composed matrices for one rendered frame. */
typedef struct HandViewFrame {
    HandItemKind kind; /* NONE draws the arm, otherwise the item. */
    Mat4 arm;          /* Right arm ModelPart origin space -> camera space. */
    Mat4 item;         /* Item model [0,1]^3 space -> camera space. */
    float equipped;    /* 1 - lerp(oMainHandHeight, mainHandHeight). */
    float swing;       /* Swing progress used for this frame. */
} HandViewFrame;

/* Compose the vanilla PoseStack chain for the main (right) hand.
 *
 * Args:
 *   hv: tick state (must not be NULL).
 *   partial: partial tick 0..1 (hand_view_partial()).
 *   view_yaw_deg / view_pitch_deg: current view rotation (vanilla sense).
 *   item_kind: model category of the item that is currently *shown*.
 *   use: eating/bow state (NULL = none).
 *   out: receives the frame (must not be NULL).
 */
void hand_view_frame(const HandView *hv, float partial, float view_yaw_deg, float view_pitch_deg,
                     HandItemKind item_kind, const HandViewUse *use, HandViewFrame *out);

/* ---- Geometry: vanilla ModelPart.Cube ---------------------------------- */

/* One textured quad corner. Positions are in model units / 16 (blocks). */
typedef struct HandVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v; /* Normalised 0..1 texture coordinates. */
} HandVertex;

/* A ModelPart cube: 6 polygons x 4 vertices, in vanilla order
 * DOWN, UP, WEST, NORTH, EAST, SOUTH. */
typedef struct HandCube {
    HandVertex v[6][4];
} HandCube;

/* Build a cube exactly like ModelPart.Cube (no mirror). tex_* in texels;
 * origin/dim in model units (1/16 block); grow is CubeDeformation. */
void hand_cube_build(HandCube *out, int tex_u, int tex_v, float ox, float oy, float oz, float dx,
                     float dy, float dz, float grow, float tex_w, float tex_h);

/* Right player arm (wide / classic Steve) and its sleeve layer. */
void hand_arm_cube(HandCube *out);
void hand_sleeve_cube(HandCube *out);

/* ModelPart pose of the right arm after AvatarRenderer.renderHand: the
 * part-origin translate (-5,2,0)/16 and zRot = 0.1 rad. Multiply onto the
 * arm matrix before drawing arm/sleeve cubes. */
Mat4 hand_arm_part_matrix(void);

/* ItemTransform.apply for firstperson_righthand followed by the model
 * centring translate(-0.5): multiply onto frame.item before drawing the
 * [0,1]^3 item model. Values come from the supplied block/block and
 * item/generated display blocks. */
Mat4 hand_item_display_matrix(HandItemKind kind);

/* Matrix helpers shared with tests/renderer (column-major, post-multiply
 * semantics like PoseStack: out = m * T). */
Mat4 hand_mat_translate(Mat4 m, float x, float y, float z);
Mat4 hand_mat_rotate_deg(Mat4 m, char axis, float degrees);
Mat4 hand_mat_scale(Mat4 m, float x, float y, float z);
Vec3 hand_mat_point(const Mat4 *m, Vec3 p);
Vec3 hand_mat_dir(const Mat4 *m, Vec3 d);
