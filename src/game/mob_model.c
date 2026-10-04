#include "game/mob_model.h"
#include "render/texture_atlas.h"

#include <math.h>
#include <stddef.h>

/* Cow (model 0): real MC proportions — deep body, head at the front,
 * four legs, two horns. Collision 0.9 x 1.4 (+Z faces forward).
 */
static const MobModelPart COW_PARTS[] = {
    {{-0.45f, 0.70f, -0.70f}, {0.90f, 0.70f, 1.40f}, TILE_LEATHER, -1, 0.0f, MOB_ANIM_NONE},
    {{-0.25f, 1.00f, 0.65f}, {0.50f, 0.50f, 0.50f}, TILE_LEATHER, -1, 0.0f, MOB_ANIM_HEAD},
    {{-0.41f, 0.00f, 0.35f}, {0.22f, 0.70f, 0.22f}, TILE_LEATHER, -1, 0.70f, MOB_ANIM_LEG},
    {{0.19f, 0.00f, 0.35f}, {0.22f, 0.70f, 0.22f}, TILE_LEATHER, -1, 0.70f, MOB_ANIM_LEG},
    {{-0.41f, 0.00f, -0.65f}, {0.22f, 0.70f, 0.22f}, TILE_LEATHER, -1, 0.70f, MOB_ANIM_LEG},
    {{0.19f, 0.00f, -0.65f}, {0.22f, 0.70f, 0.22f}, TILE_LEATHER, -1, 0.70f, MOB_ANIM_LEG},
    {{-0.34f, 1.50f, 0.75f}, {0.12f, 0.12f, 0.12f}, TILE_BONE, -1, 0.0f, MOB_ANIM_NONE},
    {{0.22f, 1.50f, 0.75f}, {0.12f, 0.12f, 0.12f}, TILE_BONE, -1, 0.0f, MOB_ANIM_NONE},
};

/* Gloomstalker (model 1): lanky night hunter — dark ore body, stone
 * head/arms, long legs. Collision 0.6 x 1.7.
 */
static const MobModelPart GLOOM_PARTS[] = {
    {{-0.25f, 0.60f, -0.20f}, {0.50f, 0.70f, 0.40f}, TILE_COAL_ORE, -1, 0.0f, MOB_ANIM_NONE},
    {{-0.20f, 1.30f, -0.20f}, {0.40f, 0.40f, 0.40f}, TILE_STONE, -1, 0.0f, MOB_ANIM_HEAD},
    {{-0.40f, 0.70f, -0.10f}, {0.15f, 0.60f, 0.15f}, TILE_STONE, -1, 1.20f, MOB_ANIM_LEG},
    {{0.25f, 0.70f, -0.10f}, {0.15f, 0.60f, 0.15f}, TILE_STONE, -1, 1.20f, MOB_ANIM_LEG},
    {{-0.22f, 0.00f, -0.10f}, {0.20f, 0.60f, 0.20f}, TILE_COAL_ORE, -1, 0.60f, MOB_ANIM_LEG},
    {{0.02f, 0.00f, -0.10f}, {0.20f, 0.60f, 0.20f}, TILE_COAL_ORE, -1, 0.60f, MOB_ANIM_LEG},
};

/* Skeleton (model 2): pale archer — snow-bone limbs, stone ribs/skull,
 * right arm rigged as the bow arm. Collision 0.6 x 1.9.
 */
static const MobModelPart SKELETON_PARTS[] = {
    {{-0.20f, 0.85f, -0.10f}, {0.40f, 0.65f, 0.20f}, TILE_STONE, -1, 0.0f, MOB_ANIM_NONE},
    {{-0.19f, 1.50f, -0.19f}, {0.38f, 0.40f, 0.38f}, TILE_SNOW, -1, 0.0f, MOB_ANIM_HEAD},
    {{-0.38f, 0.85f, -0.07f}, {0.16f, 0.65f, 0.16f}, TILE_SNOW, -1, 1.50f, MOB_ANIM_LEG},
    {{0.22f, 0.85f, -0.07f}, {0.16f, 0.65f, 0.16f}, TILE_WOOD, -1, 1.50f, MOB_ANIM_AIM_ARM},
    {{-0.20f, 0.00f, -0.09f}, {0.18f, 0.85f, 0.18f}, TILE_SNOW, -1, 0.85f, MOB_ANIM_LEG},
    {{0.02f, 0.00f, -0.09f}, {0.18f, 0.85f, 0.18f}, TILE_SNOW, -1, 0.85f, MOB_ANIM_LEG},
};

static const MobModel MOB_MODELS[] = {
    {COW_PARTS, 8, 1.62f},
    {GLOOM_PARTS, 6, 1.7f},
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

/* Cow skin (temperate_cow.png, 64x64, top-left pixel origin; regions
 * verified against the source alpha map — every rect below sits fully
 * inside opaque art). Face order per entry: -X, +X, -Y, +Y, -Z, +Z.
 * The head front carries the muzzle; horns sample a solid horn blob;
 * legs each take one hide column.
 */
static const MobSkinPart COW_SKIN_PARTS[] = {
    /* Body. Sides sample the long hide, ends the rump/chest. */
    {{{30, 20, 16, 12},
      {30, 20, 16, 12},
      {20, 14, 24, 6},
      {20, 14, 24, 6},
      {18, 20, 10, 12},
      {18, 20, 10, 12}}},
    /* Head (muzzle forward on +Z; bottom reuses the top — unseen). */
    {{{0, 6, 8, 8},
      {16, 6, 8, 8},
      {8, 0, 8, 8},
      {8, 0, 8, 8},
      {24, 6, 8, 8},
      {8, 6, 8, 8}}},
    /* Legs (hide columns). */
    {{{0, 20, 4, 12},
      {0, 20, 4, 12},
      {0, 20, 4, 2},
      {0, 20, 4, 2},
      {0, 20, 4, 12},
      {0, 20, 4, 12}}},
    {{{4, 20, 4, 12},
      {4, 20, 4, 12},
      {4, 20, 4, 2},
      {4, 20, 4, 2},
      {4, 20, 4, 12},
      {4, 20, 4, 12}}},
    {{{8, 20, 4, 12},
      {8, 20, 4, 12},
      {8, 20, 4, 2},
      {8, 20, 4, 2},
      {8, 20, 4, 12},
      {8, 20, 4, 12}}},
    {{{12, 20, 4, 12},
      {12, 20, 4, 12},
      {12, 20, 4, 2},
      {12, 20, 4, 2},
      {12, 20, 4, 12},
      {12, 20, 4, 12}}},
    /* Horns (solid horn blob). */
    {{{52, 1, 6, 6},
      {52, 1, 6, 6},
      {52, 1, 6, 6},
      {52, 1, 6, 6},
      {52, 1, 6, 6},
      {52, 1, 6, 6}}},
    {{{52, 1, 6, 6},
      {52, 1, 6, 6},
      {52, 1, 6, 6},
      {52, 1, 6, 6},
      {52, 1, 6, 6},
      {52, 1, 6, 6}}},
};

static const MobSkin MOB_SKINS[] = {
    {"cow", 64, 64, COW_SKIN_PARTS, 8},
    {NULL, 0, 0, NULL, 0},              /* Gloomstalker: original, tile path. */
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
