#include "game/player_model.h"

#include "render/texture_atlas.h"

#include <math.h>
#include <string.h>

/* Player body (feet origin, 1.8 m tall, 0.6 m collision width; arms
 * swing clear outside the body). Tiles are the fallback path when the
 * Steve skin file is missing (e.g. CI without mcassets/).
 */
static const MobModelPart PLAYER_PARTS[] = {
    /* Torso (pivots at the hips so sneak/sprint lean bends the upper body). */
    {{-0.25f, 0.675f, -0.125f}, {0.50f, 0.675f, 0.25f}, TILE_PLAYER_SKIN, -1, 0.675f, MOB_ANIM_NONE},
    /* Head. */
    {{-0.25f, 1.35f, -0.225f}, {0.50f, 0.45f, 0.45f}, TILE_PLAYER_SKIN, -1, 1.35f, MOB_ANIM_NONE},
    /* Arm L (model's left = +X when facing +Z; mirrors arm R faces). */
    {{0.25f, 0.675f, -0.125f}, {0.25f, 0.675f, 0.25f}, TILE_PLAYER_SLEEVE, -1, 1.35f, MOB_ANIM_LEG},
    /* Arm R. */
    {{-0.50f, 0.675f, -0.125f}, {0.25f, 0.675f, 0.25f}, TILE_PLAYER_SLEEVE, -1, 1.35f, MOB_ANIM_LEG},
    /* Leg L (mirrors leg R faces). */
    {{0.00f, 0.00f, -0.125f}, {0.25f, 0.675f, 0.25f}, TILE_PLAYER_SKIN, -1, 0.675f, MOB_ANIM_LEG},
    /* Leg R. */
    {{-0.25f, 0.00f, -0.125f}, {0.25f, 0.675f, 0.25f}, TILE_PLAYER_SKIN, -1, 0.675f, MOB_ANIM_LEG},
};

static const MobModel PLAYER_MODEL = {PLAYER_PARTS, PLAYER_PART_COUNT, 1.8f};

/* Steve 64x64 net regions (top-left origin). Face order per entry:
 * -X, +X, -Y, +Y, -Z, +Z. All rects sit on the opaque base layer. */
static const MobSkinPart PLAYER_SKIN_PARTS[] = {
    /* Torso: front (20,20,8x12), back (32,20,8x12), sides 4x12. */
    {{{16, 20, 4, 12}, {28, 20, 4, 12}, {28, 16, 8, 4}, {20, 16, 8, 4}, {32, 20, 8, 12}, {20, 20, 8, 12}}},
    /* Head 8x8x8. */
    {{{0, 8, 8, 8}, {16, 8, 8, 8}, {16, 0, 8, 8}, {8, 0, 8, 8}, {24, 8, 8, 8}, {8, 8, 8, 8}}},
    /* Arm L (model's left = +X when facing +Z): outer face is +X. */
    {{{48, 20, 4, 12}, {40, 20, 4, 12}, {48, 16, 4, 4}, {44, 16, 4, 4}, {52, 20, 4, 12}, {44, 20, 4, 12}}},
    /* Arm R: outer face is -X. */
    {{{40, 20, 4, 12}, {48, 20, 4, 12}, {48, 16, 4, 4}, {44, 16, 4, 4}, {52, 20, 4, 12}, {44, 20, 4, 12}}},
    /* Leg L (mirrors leg R faces). */
    {{{8, 20, 4, 12}, {0, 20, 4, 12}, {8, 16, 4, 4}, {4, 16, 4, 4}, {12, 20, 4, 12}, {4, 20, 4, 12}}},
    /* Leg R: outer (0,20), inner (8,20). */
    {{{0, 20, 4, 12}, {8, 20, 4, 12}, {8, 16, 4, 4}, {4, 16, 4, 4}, {12, 20, 4, 12}, {4, 20, 4, 12}}},
};

/* View-model geometry is already camera-local and has no mob-facing yaw.
 * The camera sees its -Z side, which carries Steve's authored front strip. */
static const MobSkinPart PLAYER_VIEWMODEL_ARM = {{{40, 20, 4, 12}, {48, 20, 4, 12}, {48, 16, 4, 4}, {44, 16, 4, 4},
                                                   {44, 20, 4, 12}, {52, 20, 4, 12}}};

static const MobSkin PLAYER_SKIN = {"player", 64, 64, PLAYER_SKIN_PARTS, PLAYER_PART_COUNT};

const MobModel *player_body_model(void)
{
    return &PLAYER_MODEL;
}

const MobSkin *player_body_skin(void)
{
    return &PLAYER_SKIN;
}

const MobSkinPart *player_viewmodel_arm_skin_part(void)
{
    return &PLAYER_VIEWMODEL_ARM;
}

/* Clamp a scalar into 0..1 (NaN reads as 0). */
static float body_clamp01(float v)
{
    if (!isfinite(v) || v <= 0.0f) {
        return 0.0f;
    }
    return v < 1.0f ? v : 1.0f;
}

/* Clamp a scalar into -1..1 (NaN reads as 0). */
static float body_clamp_pm1(float v)
{
    if (!isfinite(v)) {
        return 0.0f;
    }
    if (v < -1.0f) {
        return -1.0f;
    }
    return v > 1.0f ? 1.0f : v;
}

void player_body_pose(const PlayerBodyAnimInput *in, PlayerBodyPartPose out[PLAYER_PART_COUNT])
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(PlayerBodyPartPose) * (size_t)PLAYER_PART_COUNT);
    if (in == NULL) {
        return;
    }
    const float pi = 3.14159265358979323846f;
    const float hip_y = 0.675f;
    float blend = body_clamp01(in->move_blend);
    bool moving = blend > 0.02f;
    float stride = isfinite(in->walk_phase) ? in->walk_phase : 0.0f;
    float water = body_clamp01(in->water_contact);
    bool swim = water > 0.3f;
    bool air = !in->grounded && !swim;
    float attack = body_clamp01(in->attack_weight);
    float hurt = body_clamp01(in->hurt_weight);
    float land = body_clamp01(in->land_weight);

    float amp_leg = (in->sprinting ? 0.85f : 0.55f) * blend;
    float amp_arm = (in->sprinting ? 0.75f : 0.45f) * blend;

    /* Base upper-body lean: prone while swimming, hunched while sneaking,
     * a slight forward drive while sprinting. */
    float torso_pitch = 0.0f;
    Vec3 upper = mmath_vec3(0.0f, 0.0f, 0.0f);
    if (swim) {
        torso_pitch = 0.95f * (0.5f + 0.5f * water);
        upper.y = -0.06f;
        amp_leg = 0.25f;
        amp_arm = 0.0f;
    } else if (in->sneaking) {
        torso_pitch = in->vanilla ? 0.5f : 0.58f; /* HumanoidModel crouch: body.xRot = 0.5 */
        upper.y = -0.15f;
        amp_leg *= 0.70f;
        amp_arm *= 0.60f;
    } else if (in->sprinting && moving) {
        torso_pitch = 0.14f * blend;
    }

    /* Idle breathing (calmer while walking), plus flinch/absorb/attack
     * contributions to the torso. */
    float breathe_amt = moving ? 0.25f : 1.0f;
    float breathe = sinf(isfinite(in->idle_time) ? in->idle_time * 1.9f : 0.0f);
    torso_pitch += 0.010f * breathe * breathe_amt;
    torso_pitch += -0.12f * hurt + 0.20f * land - 0.06f * attack;
    upper.y -= 0.075f * land;
    upper.y += 0.002f * (1.0f - cosf(isfinite(in->idle_time) ? in->idle_time * 1.9f : 0.0f)) *
               breathe_amt;

    /* Shoulders/head ride the hip-pivoted torso lean. Arms hang from the
     * leaned shoulders instead of folding with the chest. */
    float c = cosf(torso_pitch);
    float s = sinf(torso_pitch);
    upper.y += -hip_y * (1.0f - c);
    upper.z += hip_y * s;

    float head_pitch = isfinite(in->pitch) ? in->pitch : 0.0f;
    if (head_pitch > 1.55334306f) {
        head_pitch = 1.55334306f;
    } else if (head_pitch < -1.55334306f) {
        head_pitch = -1.55334306f;
    }
    out[PLAYER_PART_TORSO].pitch = torso_pitch;
    out[PLAYER_PART_HEAD].pitch = -head_pitch;
    if (in->vanilla) {
        float limit = 50.0f * 0.017453292519943295f;
        float hy = isfinite(in->head_yaw) ? in->head_yaw : 0.0f;
        out[PLAYER_PART_HEAD].yaw = hy < -limit ? -limit : (hy > limit ? limit : hy);
    }
    out[PLAYER_PART_HEAD].offset = upper;
    out[PLAYER_PART_ARM_L].offset = upper;
    out[PLAYER_PART_ARM_R].offset = upper;

    /* Legs: opposite-phase stride on foot, flutter kick in water, a
     * tucked/split pose in the air. */
    if (swim) {
        out[PLAYER_PART_LEG_L].pitch = 0.30f * sinf(stride * 1.6f);
        out[PLAYER_PART_LEG_R].pitch = 0.30f * sinf(stride * 1.6f + pi);
    } else if (in->vanilla) {
        /* HumanoidModel: legs swing cos(limbSwing * 0.6662) * 1.4 * limbSwingAmount, in opposite phase. */
        float lp = (isfinite(in->limb_pos) ? in->limb_pos : 0.0f) * 0.6662f;
        float amt = body_clamp01(in->limb_amount);
        out[PLAYER_PART_LEG_L].pitch = cosf(lp + pi) * 1.4f * amt;
        out[PLAYER_PART_LEG_R].pitch = cosf(lp) * 1.4f * amt;
    } else if (air) {
        float rise = body_clamp_pm1(in->vertical_speed / 8.0f);
        float split = 0.26f - 0.10f * rise;
        out[PLAYER_PART_LEG_L].pitch = split;
        out[PLAYER_PART_LEG_R].pitch = -split * 0.8f;
    } else {
        out[PLAYER_PART_LEG_L].pitch = sinf(stride) * amp_leg;
        out[PLAYER_PART_LEG_R].pitch = sinf(stride + pi) * amp_leg;
    }

    /* Arms counter-swing each same-side leg (arm L matches leg R). */
    if (swim) {
        out[PLAYER_PART_ARM_L].pitch = -1.05f + 0.75f * sinf(stride);
        out[PLAYER_PART_ARM_R].pitch = -1.05f + 0.75f * sinf(stride + pi);
    } else {
        float arm_l = sinf(stride + pi) * amp_arm;
        float arm_r = sinf(stride) * amp_arm;
        if (in->vanilla) {
            /* cos(limbSwing * 0.6662) * 2 * limbSwingAmount * 0.5, the arm opposite its leg. */
            float lp = (isfinite(in->limb_pos) ? in->limb_pos : 0.0f) * 0.6662f;
            float amt = body_clamp01(in->limb_amount);
            arm_l = cosf(lp) * amt;
            arm_r = cosf(lp + pi) * amt;
            if (in->sneaking) {
                arm_l += 0.4f;
                arm_r += 0.4f;
            }
            if (in->holding_item && !in->use_hold) {
                arm_r = arm_r * 0.5f - 0.31415927f; /* ArmPose.ITEM */
            }
            air = false;
        }
        float drive = (in->sprinting && moving && !in->vanilla) ? -0.12f : 0.0f;
        arm_l += drive;
        arm_r += drive;
        if (air) {
            float drift = 0.05f * sinf(isfinite(in->idle_time) ? in->idle_time : 0.0f);
            arm_l = -0.22f + drift;
            arm_r = -0.22f - drift;
        }
        if (land > 0.0f) {
            arm_l += 0.25f * land;
            arm_r += 0.25f * land;
        }
        if (in->use_hold && attack <= 0.0f) {
            arm_r = -0.90f;
            arm_l = -0.20f;
        }
        float arm_roll_l = 0.0f, arm_roll_r = 0.0f; /* vanilla zRot values */
        if (in->vanilla && in->attack_active) {
            /* HumanoidModel.setupAttackAnimation (right arm). */
            float t = body_clamp01(in->attack_progress);
            float twist = sinf(sqrtf(t) * 2.0f * pi) * 0.2f; /* body.yRot */
            float f = 1.0f - t;
            f *= f;
            f *= f;
            f = 1.0f - f;
            float g = sinf(f * pi);
            float head_x = -head_pitch; /* vanilla head.xRot: positive looks down */
            float h = sinf(t * pi) * -(head_x - 0.7f) * 0.75f;
            out[PLAYER_PART_TORSO].yaw = -twist;
            out[PLAYER_PART_ARM_R].orbit = -twist;
            out[PLAYER_PART_ARM_L].orbit = -twist;
            arm_l += twist; /* leftArm.xRot += body.yRot */
            arm_r -= g * 1.2f + h;
            out[PLAYER_PART_ARM_R].yaw = -(twist + twist * 2.0f);
            out[PLAYER_PART_ARM_L].yaw = -twist;
            arm_roll_r += sinf(t * pi) * -0.4f;
        } else if (attack > 0.0f) {
            /* Right-hand jab: wind back, then swipe forward. */
            arm_r = 0.35f - 1.95f * attack;
            arm_l -= 0.25f * attack;
        }
        if (in->vanilla) {
            /* AnimationUtils.bobArms: the idle sway of both arms. */
            float age = isfinite(in->age_ticks) ? in->age_ticks : 0.0f;
            float sway_z = cosf(age * 0.09f) * 0.05f + 0.05f;
            float sway_x = sinf(age * 0.067f) * 0.05f;
            arm_roll_r += sway_z;
            arm_roll_l -= sway_z;
            arm_r += sway_x;
            arm_l -= sway_x;
            out[PLAYER_PART_ARM_R].roll = -arm_roll_r; /* engine roll = -zRot */
            out[PLAYER_PART_ARM_L].roll = -arm_roll_l;
        }
        if (hurt > 0.0f) {
            arm_l += 0.30f * hurt;
            arm_r += 0.30f * hurt;
        }
        out[PLAYER_PART_ARM_L].pitch = arm_l;
        out[PLAYER_PART_ARM_R].pitch = arm_r;
    }
}

PlayerViewmodelPose player_viewmodel_pose(const PlayerAnimPose *pose, float fallback_punch,
                                           float rest_roll)
{
    PlayerViewmodelPose out = {{0.0f, 0.0f, 0.0f}, rest_roll, 0.0f, 0.0f};
    float bob_x = 0.0f;
    float bob_y = 0.0f;
    float dip = 0.0f;
    float raise = 0.0f;
    float punch = fallback_punch;
    float arm_roll = 0.0f;
    float arm_pitch = 0.0f;
    float arm_yaw = 0.0f;
    if (pose != NULL) {
        bob_x = pose->bob_x;
        bob_y = pose->bob_y;
        dip = pose->dip;
        raise = pose->raise;
        punch = pose->punch;
        arm_roll = pose->arm_roll;
        arm_pitch = pose->arm_pitch;
        arm_yaw = pose->arm_yaw;
    }
    out.offset = mmath_vec3(bob_x - raise * 0.5f, bob_y - dip + raise * 0.7f, raise * 0.3f);
    out.roll += arm_roll;
    out.swing_x = 0.70f * punch + arm_pitch;
    out.swing_y = 0.06f * punch + arm_yaw + bob_x * 0.8f;
    return out;
}

Vec3 player_viewmodel_pose_transform_point(Vec3 point, Vec3 base_shoulder,
                                           const PlayerViewmodelPose *pose)
{
    if (pose == NULL) {
        return point;
    }
    Vec3 shoulder = mmath_vec3_add(base_shoulder, pose->offset);
    point = mmath_vec3_add(point, pose->offset);
    return player_viewmodel_arm_transform_point(point, shoulder, pose->roll,
                                                pose->swing_x, pose->swing_y);
}

Vec3 player_viewmodel_arm_transform_point(Vec3 point, Vec3 shoulder, float roll,
                                          float swing_x, float swing_y)
{
    Vec3 rel = mmath_vec3_sub(point, shoulder);
    float c = cosf(roll);
    float s = sinf(roll);
    rel = mmath_vec3(rel.x * c - rel.y * s, rel.x * s + rel.y * c, rel.z);
    c = cosf(swing_x);
    s = sinf(swing_x);
    rel = mmath_vec3(rel.x, rel.y * c - rel.z * s, rel.y * s + rel.z * c);
    c = cosf(swing_y);
    s = sinf(swing_y);
    rel = mmath_vec3(rel.x * c + rel.z * s, rel.y, -rel.x * s + rel.z * c);
    return mmath_vec3_add(shoulder, rel);
}
