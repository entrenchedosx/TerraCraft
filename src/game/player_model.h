#pragma once

/* Third-person player character (classic humanoid): part table and Steve
 * skin mapping reusing the mob cuboid pipeline (MobModelPart boxes,
 * per-face skin rects, mob_model_validate / mob_skin_validate). Geometry
 * is data; pixels stay a renderer concern.
 *
 * Proportions follow the 1.8 m player: legs 0..0.675, torso 0.675..1.35,
 * head 1.35..1.80. Arms hang at the torso sides (shoulder pivots at
 * y=1.35), legs pivot at the hips (y=0.675). +Z faces forward.
 */

#include "game/mob_model.h"

/* Part indexes (stable for the renderer/anim wiring). */
typedef enum PlayerModelPartIndex {
    PLAYER_PART_TORSO = 0,
    PLAYER_PART_HEAD = 1,
    PLAYER_PART_ARM_L = 2,
    PLAYER_PART_ARM_R = 3,
    PLAYER_PART_LEG_L = 4,
    PLAYER_PART_LEG_R = 5,
    PLAYER_PART_COUNT = 6
} PlayerModelPartIndex;

/* The player body model (static storage, do not free; always valid). */
const MobModel *player_body_model(void);

/* The Steve skin mapping for the player model (static storage, do not
 * free; file stem "player", 64x64). Arm/leg side faces use the true
 * outer/inner regions per side (verified against the source pixels).
 * Always valid.
 */
const MobSkin *player_body_skin(void);

/* First-person arm mapping: same skin texels as the body model, with the
 * camera-facing side mapped to Steve's front arm face. */
const MobSkinPart *player_viewmodel_arm_skin_part(void);
