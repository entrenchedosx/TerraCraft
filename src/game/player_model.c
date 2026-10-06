#include "game/player_model.h"

#include "render/texture_atlas.h"

/* Player body (feet origin, 1.8 m tall, 0.6 m collision width; arms
 * swing clear outside the body). Tiles are the fallback path when the
 * Steve skin file is missing (e.g. CI without mcassets/).
 */
static const MobModelPart PLAYER_PARTS[] = {
    /* Torso. */
    {{-0.25f, 0.675f, -0.125f}, {0.50f, 0.675f, 0.25f}, TILE_PLAYER_SKIN, -1, 0.0f, MOB_ANIM_NONE},
    /* Head. */
    {{-0.25f, 1.35f, -0.225f}, {0.50f, 0.45f, 0.45f}, TILE_PLAYER_SKIN, -1, 0.0f, MOB_ANIM_NONE},
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

static const MobSkin PLAYER_SKIN = {"player", 64, 64, PLAYER_SKIN_PARTS, PLAYER_PART_COUNT};

const MobModel *player_body_model(void)
{
    return &PLAYER_MODEL;
}

const MobSkin *player_body_skin(void)
{
    return &PLAYER_SKIN;
}
