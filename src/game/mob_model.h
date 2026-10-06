#pragma once

/* Blocky creature models (M8): part tables describing articulated cuboids
 * (offset, size, pivot, tile, parent, animation role). Geometry is data;
 * the renderer composes parts (root yaw + per-part procedural pitch) and
 * emits rotated boxes. Pure CPU, headless-testable (validation only —
 * pixels stay a renderer concern).
 */

#include "math/mmath.h"

#include <stdbool.h>
#include <stddef.h>

/* Per-part animation roles (procedural, phase-driven). */
typedef enum MobPartAnim {
    MOB_ANIM_NONE = 0, /* Static part (body, head base). */
    MOB_ANIM_LEG,      /* Limb swing: pitch = sin(walk_phase) * 0.6. */
    MOB_ANIM_HEAD,     /* Head bob: slight pitch with walk phase. */
    MOB_ANIM_AIM_ARM,  /* Bow arm: raised forward in AIM/ATTACK, swings else. */
    MOB_ANIM_STRIKE_ARM /* Melee arm: windup swipe in ATTACK (mob_strike_pitch), swings else. */
} MobPartAnim;

/* One cuboid part (units are blocks, origin at the mob feet center). */
typedef struct MobModelPart {
    Vec3 offset; /* Part minimum corner (mob-local, y-up from feet). */
    Vec3 size;   /* Edge lengths (each 0.05..2.0). */
    int tile;    /* Atlas tile index (procedural fallback path). */
    int parent;  /* Parent part index (-1 = root/mob origin). */
    float pivot_y; /* Rotation pivot height (mob-local Y). */
    int anim;      /* MobPartAnim value. */
} MobModelPart;

/* One skin UV rect (pixels, top-left origin in skin space). */
typedef struct MobSkinRect {
    int x, y; /* Top-left corner in pixels. */
    int w, h; /* Size in pixels (> 0). */
} MobSkinRect;

/* Face order for skin rects (matches the renderer's MOB_FACE order:
 * -X, +X, -Y, +Y, -Z, +Z).
 */
#define MOB_SKIN_FACES 6

/* Per-part skin mapping: one rect per face. A whole skin region maps
 * onto the part face regardless of pixel-size mismatch (stretched to
 * fit — our boxes differ slightly from Mojang's, so exact texel
 * parity is not the goal; recognizable real art is).
 */
typedef struct MobSkinPart {
    MobSkinRect faces[MOB_SKIN_FACES];
} MobSkinPart;

/* One creature skin (static storage, do not free). NULL skin = tile
 * path (procedural atlas art, always available).
 */
typedef struct MobSkin {
    const char *file;          /* Generated skin stem, e.g. "skeleton". */
    int width, height;         /* Native skin size (64x32 or 64x64). */
    const MobSkinPart *parts;  /* Per-part face rects (nparts entries). */
    int nparts;                /* Must equal the model's part count. */
} MobSkin;

/* One creature model (static storage, do not free). */
typedef struct MobModel {
    const MobModelPart *parts; /* Part table (never NULL when valid). */
    int nparts;                /* Part count (1..16). */
    float height;              /* Total height (must cover collision). */
} MobModel;

/* Model for a mob definition index (see MobDefinition.model).
 *
 * Args:
 *   index: 0 = cow, 1 = zombie, 2 = skeleton.
 *
 * Returns: model, or NULL on bad index.
 */
const MobModel *mob_model_for(int index);

/* Skin for a mob definition index (same indexing as mob_model_for).
 * Only mobs with a real Minecraft counterpart have skins (cow, zombie,
 * skeleton); anything else renders with atlas tiles.
 *
 * Args:
 *   index: 0 = cow, 1 = zombie, 2 = skeleton.
 *
 * Returns: skin, or NULL (tile path) on bad index or unskinned model.
 */
const MobSkin *mob_skin_for(int index);

/* Generated skin file stem for an override lookup (NULL when the model
 * has no skin). Files live at <mcassets>/generated/mobs/<stem>.bmp.
 *
 * Args:
 *   index: model index (see mob_model_for).
 *
 * Returns: file stem or NULL.
 */
const char *mob_skin_file(int index);

/* Validate a skin definition (rects inside the skin, part count sane).
 * Malformed skins fall back to the tile path (never crash on bad data).
 *
 * Args:
 *   s: skin (must not be NULL).
 *   nparts: expected part count (the model's count).
 *
 * Returns: true when usable.
 */
bool mob_skin_validate(const MobSkin *s, int nparts);

/* Validate a model definition (part count, parent indexes acyclic and
 * in-range, bounded dimensions, tile range sane). Rejects malformed
 * external definitions safely (resource packs must never crash this).
 *
 * Args:
 *   m: model (must not be NULL).
 *
 * Returns: true when usable.
 */
bool mob_model_validate(const MobModel *m);

/* Swipe pitch for STRIKE_ARM parts (radians, <= 0): 0.25 s windup to
 * -1.2, 0.4 s release back to rest. Pure function of time-in-attack so
 * the animation never drives the damage event (that stays on the AI
 * cooldown). Headless-testable.
 *
 * Args:
 *   state_t: seconds in MOB_STATE_ATTACK (NaN/negative reads as 0).
 *
 * Returns: pitch (0 at rest).
 */
float mob_strike_pitch(float state_t);
