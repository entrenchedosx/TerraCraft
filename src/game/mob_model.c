#include "game/mob_model.h"
#include "render/texture_atlas.h"

#include <math.h>
#include <stddef.h>

/* Cow (model 0): compact 12x18x10 body, 8x8x6 head, and four 4x12x4
 * legs in the skin's authored proportions. +Z faces forward. The head
 * joins the chest without burying the muzzle; face details come from the
 * head skin instead of separate boxes sampling unrelated texture regions.
 */
static const MobModelPart COW_PARTS[] = {
    {{-0.375f, 0.275f, -0.3125f}, {0.75f, 1.125f, 0.625f}, TILE_LEATHER, -1, 0.0f, MOB_ANIM_NONE},
    {{-0.25f, 0.85f, 0.25f}, {0.50f, 0.50f, 0.375f}, TILE_LEATHER, -1, 1.10f, MOB_ANIM_HEAD},
    {{-0.375f, 0.00f, 0.20f}, {0.25f, 0.73f, 0.25f}, TILE_LEATHER, -1, 0.73f, MOB_ANIM_LEG},
    {{0.125f, 0.00f, 0.20f}, {0.25f, 0.73f, 0.25f}, TILE_LEATHER, -1, 0.73f, MOB_ANIM_LEG},
    {{-0.375f, 0.00f, -0.45f}, {0.25f, 0.73f, 0.25f}, TILE_LEATHER, -1, 0.73f, MOB_ANIM_LEG},
    {{0.125f, 0.00f, -0.45f}, {0.25f, 0.73f, 0.25f}, TILE_LEATHER, -1, 0.73f, MOB_ANIM_LEG},
};

/* Zombie (model 1): classic humanoid — box torso, cube head, hanging
 * arms, straight legs. Collision 0.6 x 1.9 (+Z faces forward).
 */
static const MobModelPart ZOMBIE_PARTS[] = {
    {{-0.25f, 0.70f, -0.125f}, {0.50f, 0.70f, 0.25f}, TILE_LEATHER, -1, 0.0f, MOB_ANIM_NONE},
    {{-0.25f, 1.40f, -0.25f}, {0.50f, 0.50f, 0.50f}, TILE_LEATHER, -1, 0.0f, MOB_ANIM_HEAD},
    {{-0.50f, 0.70f, -0.125f}, {0.25f, 0.70f, 0.25f}, TILE_LEATHER, -1, 1.40f, MOB_ANIM_LEG},
    {{0.25f, 0.70f, -0.125f}, {0.25f, 0.70f, 0.25f}, TILE_LEATHER, -1, 1.40f, MOB_ANIM_LEG},
    {{-0.25f, 0.00f, -0.125f}, {0.25f, 0.70f, 0.25f}, TILE_LEATHER, -1, 0.70f, MOB_ANIM_LEG},
    {{0.00f, 0.00f, -0.125f}, {0.25f, 0.70f, 0.25f}, TILE_LEATHER, -1, 0.70f, MOB_ANIM_LEG},
};

/* Skeleton (model 2): pale archer — snow-bone limbs, stone ribs/skull,
 * right arm rigged as the bow arm. Collision 0.6 x 1.9.
 */
static const MobModelPart SKELETON_PARTS[] = {
    {{-0.15f, 0.85f, -0.10f}, {0.30f, 0.65f, 0.20f}, TILE_STONE, -1, 0.0f, MOB_ANIM_NONE},
    {{-0.19f, 1.50f, -0.19f}, {0.38f, 0.40f, 0.38f}, TILE_SNOW, -1, 0.0f, MOB_ANIM_HEAD},
    {{-0.27f, 0.85f, -0.07f}, {0.12f, 0.65f, 0.16f}, TILE_SNOW, -1, 1.50f, MOB_ANIM_LEG},
    {{0.15f, 0.85f, -0.07f}, {0.12f, 0.65f, 0.16f}, TILE_WOOD, -1, 1.50f, MOB_ANIM_AIM_ARM},
    {{-0.19f, 0.00f, -0.09f}, {0.14f, 0.85f, 0.18f}, TILE_SNOW, -1, 0.85f, MOB_ANIM_LEG},
    {{0.05f, 0.00f, -0.09f}, {0.14f, 0.85f, 0.18f}, TILE_SNOW, -1, 0.85f, MOB_ANIM_LEG},
};

static const MobModel MOB_MODELS[] = {
    {COW_PARTS, 6, 1.40f},
    {ZOMBIE_PARTS, 6, 1.9f},
    {SKELETON_PARTS, 6, 1.9f},
};
#define MOB_MODEL_COUNT (sizeof(MOB_MODELS) / sizeof(MOB_MODELS[0]))

/* Skeleton skin (verified against the source alpha map — every rect
 * below sits fully inside opaque art; the ribcage holes are deliberately
 * NOT sampled: a solid cuboid with hole-Uvs would show through the
 * torso). Face order per entry: -X, +X, -Y, +Y, -Z, +Z. Left limbs
 * mirror the right regions (matches the 64x32 format).
 */
static const MobSkinPart SKELETON_SKIN_PARTS[] = {
    /* Torso (solid bone shafts — same tone as the limbs). */
    {{{0, 18, 2, 12},
      {6, 18, 2, 12},
      {2, 16, 4, 2},
      {2, 16, 4, 2},
      {2, 18, 2, 12},
      {4, 18, 2, 12}}},
    /* Head. */
    {{{0, 8, 8, 8},
      {16, 8, 8, 8},
      {8, 0, 8, 8},
      {8, 0, 8, 8},
      {24, 8, 8, 8},
      {8, 8, 8, 8}}},
    /* Arm L (mirrors arm @ x40-47 y18-29). */
    {{{40, 18, 2, 12},
      {46, 18, 2, 12},
      {42, 16, 2, 2},
      {42, 16, 2, 2},
      {42, 18, 2, 12},
      {44, 18, 2, 12}}},
    /* Arm R (bow arm, same region). */
    {{{40, 18, 2, 12},
      {46, 18, 2, 12},
      {42, 16, 2, 2},
      {42, 16, 2, 2},
      {42, 18, 2, 12},
      {44, 18, 2, 12}}},
    /* Leg L (mirrors leg @ x0-7 y18-29). */
    {{{0, 18, 2, 12},
      {6, 18, 2, 12},
      {2, 16, 2, 2},
      {2, 16, 2, 2},
      {2, 18, 2, 12},
      {4, 18, 2, 12}}},
    /* Leg R. */
    {{{0, 18, 2, 12},
      {6, 18, 2, 12},
      {2, 16, 2, 2},
      {2, 16, 2, 2},
      {2, 18, 2, 12},
      {4, 18, 2, 12}}},
};

/* Cow skin (temperate_cow.png, 64x64, top-left pixel origin). Rectangles
 * follow the skin's box-unfold layout for the 12x18x10 body, 8x8x6 head,
 * and 4x12x4 legs. Face order: -X,+X,-Y,+Y,-Z,+Z. The model faces +Z
 * forward, so the skin's standard -Z/front region maps to the +Z face.
 */
static const MobSkinPart COW_SKIN_PARTS[] = {
    /* Body box UV base (18,4), dimensions 12x18x10. */
    {{{18, 14, 10, 18}, {40, 14, 10, 18}, {40, 4, 12, 10}, {28, 4, 12, 10}, {50, 14, 12, 18}, {28, 14, 12, 18}}},
    /* Head box UV base (0,0), dimensions 8x8x6. */
    {{{0, 6, 6, 8}, {14, 6, 6, 8}, {14, 0, 8, 6}, {6, 0, 8, 6}, {20, 6, 8, 8}, {6, 6, 8, 8}}},
    /* The four legs share the authored 4x12x4 UV region at (0,16). */
    {{{0, 20, 4, 12}, {8, 20, 4, 12}, {8, 16, 4, 4}, {4, 16, 4, 4}, {12, 20, 4, 12}, {4, 20, 4, 12}}},
    {{{0, 20, 4, 12}, {8, 20, 4, 12}, {8, 16, 4, 4}, {4, 16, 4, 4}, {12, 20, 4, 12}, {4, 20, 4, 12}}},
    {{{0, 20, 4, 12}, {8, 20, 4, 12}, {8, 16, 4, 4}, {4, 16, 4, 4}, {12, 20, 4, 12}, {4, 20, 4, 12}}},
    {{{0, 20, 4, 12}, {8, 20, 4, 12}, {8, 16, 4, 4}, {4, 16, 4, 4}, {12, 20, 4, 12}, {4, 20, 4, 12}}},
};

/* Zombie skin (standard 64x64 humanoid layout, top-left pixel origin;
 * every rect verified fully opaque against the source alpha map).
 * Face order per entry: -X, +X, -Y, +Y, -Z, +Z. Left limbs mirror the
 * right regions (same convention as the 64x32 skeleton).
 */
static const MobSkinPart ZOMBIE_SKIN_PARTS[] = {
    /* Torso (body 8x12x4 @ (16,16)). */
    {{{16, 20, 4, 12},
      {28, 20, 4, 12},
      {28, 16, 8, 4},
      {20, 16, 8, 4},
      {32, 20, 8, 12},
      {20, 20, 8, 12}}},
    /* Head (8x8x8 @ (0,0)). */
    {{{0, 8, 8, 8},
      {16, 8, 8, 8},
      {16, 0, 8, 8},
      {8, 0, 8, 8},
      {24, 8, 8, 8},
      {8, 8, 8, 8}}},
    /* Arm L (mirrors arm @ (40,16), 4x12x4). */
    {{{40, 20, 4, 12},
      {48, 20, 4, 12},
      {48, 16, 4, 4},
      {44, 16, 4, 4},
      {52, 20, 4, 12},
      {44, 20, 4, 12}}},
    /* Arm R (@ (40,16), 4x12x4). */
    {{{40, 20, 4, 12},
      {48, 20, 4, 12},
      {48, 16, 4, 4},
      {44, 16, 4, 4},
      {52, 20, 4, 12},
      {44, 20, 4, 12}}},
    /* Leg L (mirrors leg @ (0,16), 4x12x4). */
    {{{0, 20, 4, 12},
      {8, 20, 4, 12},
      {8, 16, 4, 4},
      {4, 16, 4, 4},
      {12, 20, 4, 12},
      {4, 20, 4, 12}}},
    /* Leg R (@ (0,16), 4x12x4). */
    {{{0, 20, 4, 12},
      {8, 20, 4, 12},
      {8, 16, 4, 4},
      {4, 16, 4, 4},
      {12, 20, 4, 12},
      {4, 20, 4, 12}}},
};

static const MobSkin MOB_SKINS[] = {
    {"cow", 64, 64, COW_SKIN_PARTS, 6},
    {"zombie", 64, 64, ZOMBIE_SKIN_PARTS, 6},
    {"skeleton", 64, 32, SKELETON_SKIN_PARTS, 6},
};
#define MOB_SKIN_COUNT (sizeof(MOB_SKINS) / sizeof(MOB_SKINS[0]))

/* Skin by definition index (NULL = tile path). */
const MobSkin *mob_skin_for(int index)
{
    if (index < 0 || (size_t)index >= MOB_SKIN_COUNT) {
        return NULL;
    }
    if (MOB_SKINS[index].file == NULL) {
        return NULL;
    }
    return &MOB_SKINS[index];
}

/* Generated skin file stem (NULL when unskinned). */
const char *mob_skin_file(int index)
{
    const MobSkin *s = (index >= 0 && (size_t)index < MOB_SKIN_COUNT) ? &MOB_SKINS[index] : NULL;
    return (s != NULL) ? s->file : NULL;
}

/* Model by definition index. */
const MobModel *mob_model_for(int index)
{
    if (index < 0 || (size_t)index >= MOB_MODEL_COUNT) {
        return NULL;
    }
    return &MOB_MODELS[index];
}

/* Validate a model definition. */
bool mob_model_validate(const MobModel *m)
{
    if (m == NULL || m->parts == NULL) {
        return false;
    }
    if (m->nparts < 1 || m->nparts > 16) {
        return false;
    }
    if (!(m->height > 0.0f) || m->height > 4.0f) {
        return false;
    }
    for (int i = 0; i < m->nparts; ++i) {
        const MobModelPart *p = &m->parts[i];
        if (p->parent < -1 || p->parent >= i) {
            return false; /* Parents must precede children (acyclic). */
        }
        if (!(p->size.x >= 0.05f && p->size.x <= 2.0f) ||
            !(p->size.y >= 0.05f && p->size.y <= 2.0f) ||
            !(p->size.z >= 0.05f && p->size.z <= 2.0f)) {
            return false;
        }
        if (!(p->offset.x > -4.0f && p->offset.x < 4.0f) ||
            !(p->offset.y > -4.0f && p->offset.y < 4.0f) ||
            !(p->offset.z > -4.0f && p->offset.z < 4.0f)) {
            return false;
        }
        if (p->tile < 0 || p->tile > 255) {
            return false;
        }
        if (p->anim < MOB_ANIM_NONE || p->anim > MOB_ANIM_AIM_ARM) {
            return false;
        }
        if (p->offset.y + p->size.y > m->height + 0.01f) {
            return false; /* Parts must fit the declared height. */
        }
    }
    return true;
}

/* Validate a skin definition (rects inside the skin, part count sane).
 * Malformed skins fall back to the tile path (never crash on bad data).
 *
 * Args:
 *   s: skin (must not be NULL).
 *   nparts: expected part count (the model's count).
 *
 * Returns: true when usable.
 */
bool mob_skin_validate(const MobSkin *s, int nparts)
{
    if (s == NULL || s->file == NULL || s->file[0] == '\0') {
        return false;
    }
    if ((s->width != 64 || (s->height != 32 && s->height != 64)) || s->parts == NULL) {
        return false;
    }
    if (s->nparts != nparts || nparts < 1 || nparts > 16) {
        return false;
    }
    for (int i = 0; i < nparts; ++i) {
        for (int f = 0; f < MOB_SKIN_FACES; ++f) {
            MobSkinRect r = s->parts[i].faces[f];
            if (r.w < 1 || r.h < 1 || r.x < 0 || r.y < 0 || r.x + r.w > s->width ||
                r.y + r.h > s->height) {
                return false;
            }
        }
    }
    return true;
}
