#include "game/mob_model.h"
#include "render/texture_atlas.h"

#include <math.h>
#include <stddef.h>

/* Cow (model 0): classic quadruped proportions. The authored 12x18x10
 * torso is pitched 90 degrees around X so its long axis runs nose-to-tail,
 * not vertically. The head has a projecting muzzle, short horns, broad
 * ears, an udder, and four articulated 4x12x4 legs. +Z faces forward.
 */
static const MobModelPart COW_PARTS[] = {
    /* Torso: 12x18x10 authored box, centered at y=.8125 before rotation. */
    {{-0.375f, 0.25f, -0.3125f}, {0.75f, 1.125f, 0.625f}, TILE_LEATHER, -1, 0.8125f,
     MOB_ANIM_COW_BODY_X90},
    /* Head */
    {{-0.25f, 0.84f, 0.28f}, {0.50f, 0.50f, 0.375f}, TILE_LEATHER, -1, 1.10f, MOB_ANIM_HEAD},
    /* Broad pink muzzle, extending visibly beyond the head. */
    {{-0.15f, 0.90f, 0.61f}, {0.30f, 0.20f, 0.20f}, TILE_LEATHER, -1, 1.0f, MOB_ANIM_NONE},
    /* Short blocky horns at the crown, within the existing 1.4 block height. */
    {{-0.22f, 1.22f, 0.35f}, {0.08f, 0.15f, 0.10f}, TILE_LEATHER, -1, 1.22f, MOB_ANIM_NONE},
    {{0.14f, 1.22f, 0.35f}, {0.08f, 0.15f, 0.10f}, TILE_LEATHER, -1, 1.22f, MOB_ANIM_NONE},
    /* Ears project to either side of the head. */
    {{-0.36f, 1.02f, 0.34f}, {0.15f, 0.08f, 0.23f}, TILE_LEATHER, -1, 1.06f, MOB_ANIM_NONE},
    {{0.21f, 1.02f, 0.34f}, {0.15f, 0.08f, 0.23f}, TILE_LEATHER, -1, 1.06f, MOB_ANIM_NONE},
    /* Front pair (+Z), then hind pair (-Z), for alternating stride phases. */
    {{-0.375f, 0.00f, 0.20f}, {0.25f, 0.73f, 0.25f}, TILE_LEATHER, -1, 0.73f, MOB_ANIM_LEG},
    {{0.125f, 0.00f, 0.20f}, {0.25f, 0.73f, 0.25f}, TILE_LEATHER, -1, 0.73f, MOB_ANIM_LEG},
    {{-0.375f, 0.00f, -0.45f}, {0.25f, 0.73f, 0.25f}, TILE_LEATHER, -1, 0.73f, MOB_ANIM_LEG},
    {{0.125f, 0.00f, -0.45f}, {0.25f, 0.73f, 0.25f}, TILE_LEATHER, -1, 0.73f, MOB_ANIM_LEG},
    /* Visible pink udder between the hind legs, with four short teats. */
    {{-0.15f, 0.34f, -0.30f}, {0.30f, 0.20f, 0.25f}, TILE_LEATHER, -1, 0.44f, MOB_ANIM_NONE},
    {{-0.11f, 0.25f, -0.25f}, {0.07f, 0.11f, 0.07f}, TILE_LEATHER, -1, 0.36f, MOB_ANIM_NONE},
    {{0.04f, 0.25f, -0.25f}, {0.07f, 0.11f, 0.07f}, TILE_LEATHER, -1, 0.36f, MOB_ANIM_NONE},
    {{-0.11f, 0.25f, -0.12f}, {0.07f, 0.11f, 0.07f}, TILE_LEATHER, -1, 0.36f, MOB_ANIM_NONE},
    {{0.04f, 0.25f, -0.12f}, {0.07f, 0.11f, 0.07f}, TILE_LEATHER, -1, 0.36f, MOB_ANIM_NONE},
};

/* Zombie (model 1): classic humanoid — box torso, cube head, hanging
 * arms, straight legs. Collision 0.6 x 1.9 (+Z faces forward). Arms
 * swipe on ATTACK (STRIKE_ARM), swing with the stride otherwise.
 */
static const MobModelPart ZOMBIE_PARTS[] = {
    {{-0.25f, 0.70f, -0.125f}, {0.50f, 0.70f, 0.25f}, TILE_LEATHER, -1, 0.0f, MOB_ANIM_NONE},
    {{-0.25f, 1.40f, -0.25f}, {0.50f, 0.50f, 0.50f}, TILE_LEATHER, -1, 0.0f, MOB_ANIM_HEAD},
    {{-0.50f, 0.70f, -0.125f}, {0.25f, 0.70f, 0.25f}, TILE_LEATHER, -1, 1.40f, MOB_ANIM_STRIKE_ARM},
    {{0.25f, 0.70f, -0.125f}, {0.25f, 0.70f, 0.25f}, TILE_LEATHER, -1, 1.40f, MOB_ANIM_STRIKE_ARM},
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
    {COW_PARTS, (int)(sizeof(COW_PARTS) / sizeof(COW_PARTS[0])), 1.40f},
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

/* Reuse a small, existing skin patch for simple added cow details. */
#define COW_SKIN_PATCH(x, y, w, h) \
    {{{x, y, w, h}, {x, y, w, h}, {x, y, w, h}, {x, y, w, h}, {x, y, w, h}, {x, y, w, h}}}

/* Cow skin (temperate_cow.png, 64x64, top-left pixel origin). Rectangles
 * follow the authored 12x18x10 torso, 8x8x6 head, and 4x12x4 leg nets.
 * The base head art supplies eyes and ears; the pink patch is reused on
 * the new muzzle and udder parts. Face order: -X,+X,-Y,+Y,-Z,+Z. The
 * model faces +Z forward, so the skin's standard -Z/front maps to +Z.
 */
static const MobSkinPart COW_SKIN_PARTS[] = {
    /* Body box UV base (18,4), dimensions 12x18x10. */
    {{{18, 14, 10, 18}, {40, 14, 10, 18}, {40, 4, 12, 10}, {28, 4, 12, 10}, {50, 14, 12, 18}, {28, 14, 12, 18}}},
    /* Head box UV base (0,0), dimensions 8x8x6. */
    {{{0, 6, 6, 8}, {14, 6, 6, 8}, {14, 0, 8, 6}, {6, 0, 8, 6}, {20, 6, 8, 8}, {6, 6, 8, 8}}},
    /* Pink nose patch. */
    COW_SKIN_PATCH(52, 0, 8, 8),
    /* Horns use the pale hide patch; ears use the corresponding head sides. */
    COW_SKIN_PATCH(50, 14, 4, 4),
    COW_SKIN_PATCH(50, 14, 4, 4),
    COW_SKIN_PATCH(0, 6, 6, 8),
    COW_SKIN_PATCH(14, 6, 6, 8),
    /* The four legs share the authored 4x12x4 UV region at (0,16). */
    {{{0, 20, 4, 12}, {8, 20, 4, 12}, {8, 16, 4, 4}, {4, 16, 4, 4}, {12, 20, 4, 12}, {4, 20, 4, 12}}},
    {{{0, 20, 4, 12}, {8, 20, 4, 12}, {8, 16, 4, 4}, {4, 16, 4, 4}, {12, 20, 4, 12}, {4, 20, 4, 12}}},
    {{{0, 20, 4, 12}, {8, 20, 4, 12}, {8, 16, 4, 4}, {4, 16, 4, 4}, {12, 20, 4, 12}, {4, 20, 4, 12}}},
    {{{0, 20, 4, 12}, {8, 20, 4, 12}, {8, 16, 4, 4}, {4, 16, 4, 4}, {12, 20, 4, 12}, {4, 20, 4, 12}}},
    /* Udder and teats use the existing pink pixels from the cow skin. */
    COW_SKIN_PATCH(52, 0, 8, 8),
    COW_SKIN_PATCH(52, 0, 8, 8),
    COW_SKIN_PATCH(52, 0, 8, 8),
    COW_SKIN_PATCH(52, 0, 8, 8),
    COW_SKIN_PATCH(52, 0, 8, 8),
};
#undef COW_SKIN_PATCH

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
    {"cow", 64, 64, COW_SKIN_PARTS,
     (int)(sizeof(COW_SKIN_PARTS) / sizeof(COW_SKIN_PARTS[0]))},
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
    if (m->nparts < 1 || m->nparts > MOB_MODEL_MAX_PARTS) {
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
        if (p->anim < MOB_ANIM_NONE || p->anim > MOB_ANIM_COW_BODY_X90) {
            return false;
        }
        if (p->offset.y + p->size.y > m->height + 0.01f) {
            return false; /* Parts must fit the declared height. */
        }
    }
    return true;
}

/* Compute a conservative root-relative envelope. Each part is enclosed by a
 * sphere centered on its renderer pivot. Pitch rotation preserves that sphere,
 * while the model's root yaw is covered by its horizontal radius. Death tilt
 * pivots every part at the feet, so that pose additionally uses a sphere around
 * the mob root. */
bool mob_model_culling_bounds(const MobModel *m, bool include_death_pose,
                              float *out_horizontal_radius, float *out_min_y,
                              float *out_max_y)
{
    if (out_horizontal_radius != NULL) {
        *out_horizontal_radius = 0.0f;
    }
    if (out_min_y != NULL) {
        *out_min_y = 0.0f;
    }
    if (out_max_y != NULL) {
        *out_max_y = 0.0f;
    }
    if (m == NULL || !mob_model_validate(m) || out_horizontal_radius == NULL ||
        out_min_y == NULL || out_max_y == NULL) {
        return false;
    }

    float radius = 0.0f;
    float min_y = INFINITY;
    float max_y = -INFINITY;
    float root_radius = 0.0f;
    for (int i = 0; i < m->nparts; ++i) {
        const MobModelPart *part = &m->parts[i];
        float px = part->offset.x + part->size.x * 0.5f;
        float py = part->pivot_y;
        float pz = part->offset.z + part->size.z * 0.5f;
        float part_radius = 0.0f;
        for (int corner = 0; corner < 8; ++corner) {
            float x = part->offset.x + ((corner & 1) ? part->size.x : 0.0f) - px;
            float y = part->offset.y + ((corner & 2) ? part->size.y : 0.0f) - py;
            float z = part->offset.z + ((corner & 4) ? part->size.z : 0.0f) - pz;
            float distance = sqrtf(x * x + y * y + z * z);
            if (distance > part_radius) {
                part_radius = distance;
            }
        }
        float horizontal = sqrtf(px * px + pz * pz) + part_radius;
        if (horizontal > radius) {
            radius = horizontal;
        }
        float from_root = sqrtf(px * px + py * py + pz * pz) + part_radius;
        if (from_root > root_radius) {
            root_radius = from_root;
        }
        if (py - part_radius < min_y) {
            min_y = py - part_radius;
        }
        if (py + part_radius > max_y) {
            max_y = py + part_radius;
        }
    }
    if (include_death_pose) {
        if (root_radius > radius) {
            radius = root_radius;
        }
        if (-root_radius < min_y) {
            min_y = -root_radius;
        }
        if (root_radius > max_y) {
            max_y = root_radius;
        }
    }
    *out_horizontal_radius = radius;
    *out_min_y = min_y;
    *out_max_y = max_y;
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

/* Swipe pitch for STRIKE_ARM parts. */
float mob_strike_pitch(float state_t)
{
    float t = isfinite(state_t) ? state_t : 0.0f;
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t < 0.25f) {
        return -1.2f * (t / 0.25f);
    }
    if (t < 0.65f) {
        return -1.2f * (1.0f - (t - 0.25f) / 0.4f);
    }
    return 0.0f;
}
