#include "world/block.h"

/* Static lookup table, indexed by BlockType. Must stay in sync with the enum.
 * M1 visualization colors (spec):
 *   Grass (0.2,0.8,0.2), Dirt (0.5,0.3,0.1), Stone (0.5,0.5,0.5),
 *   Bedrock (0.1,0.1,0.1).
 * MC-accurate hardness (Java values): stone 1.5, dirt/grass/sand 0.5,
 * wood/planks 2, workbench 2.5, ores 3, leaves 0.2, glass 0.3, snow 0.1,
 * decor 0 (instant). Break time derives in survival_mine_time via the MC
 * formula (hardness*1.5/speed correct tool, hardness*5 otherwise);
 * pickaxe-class blocks need a sufficient pickaxe to drop at all.
 */
static const BlockInfo BLOCK_TABLE[BLOCK_COUNT] = {
    [BLOCK_AIR] = {.name = "air", .solid = false, .transparent = true, .color_r = 0.0f, .color_g = 0.0f, .color_b = 0.0f, .hardness = 0.0f, .tool = 0, .min_tier = 0, .drop = 0, .drop_count = 0, .unbreakable = true},
    [BLOCK_STONE] = {.name = "stone", .solid = true, .transparent = false, .color_r = 0.5f, .color_g = 0.5f, .color_b = 0.5f, .hardness = 1.5f, .tool = 1, .min_tier = 1, .drop = 1, .drop_count = 1, .unbreakable = false},
    [BLOCK_DIRT] = {.name = "dirt", .solid = true, .transparent = false, .color_r = 0.5f, .color_g = 0.3f, .color_b = 0.1f, .hardness = 0.5f, .tool = 3, .min_tier = 1, .drop = 2, .drop_count = 1, .unbreakable = false},
    [BLOCK_GRASS] = {.name = "grass", .solid = true, .transparent = false, .color_r = 0.2f, .color_g = 0.8f, .color_b = 0.2f, .hardness = 0.5f, .tool = 3, .min_tier = 1, .drop = 2, .drop_count = 1, .unbreakable = false},
    [BLOCK_WATER] = {.name = "water", .solid = false, .transparent = true, .color_r = 0.25f, .color_g = 0.45f, .color_b = 0.9f, .hardness = 0.0f, .tool = 0, .min_tier = 0, .drop = 0, .drop_count = 0, .unbreakable = true},
    [BLOCK_BEDROCK] = {.name = "bedrock", .solid = true, .transparent = false, .color_r = 0.1f, .color_g = 0.1f, .color_b = 0.1f, .hardness = 0.0f, .tool = 0, .min_tier = 0, .drop = 0, .drop_count = 0, .unbreakable = true},
    [BLOCK_WOOD] = {.name = "wood", .solid = true, .transparent = false, .color_r = 0.45f, .color_g = 0.3f, .color_b = 0.15f, .hardness = 2.0f, .tool = 2, .min_tier = 1, .drop = 6, .drop_count = 1, .unbreakable = false},
    [BLOCK_LEAVES] = {.name = "leaves", .solid = true, .transparent = true, .color_r = 0.2f, .color_g = 0.55f, .color_b = 0.2f, .hardness = 0.2f, .tool = 0, .min_tier = 0, .drop = 0, .drop_count = 0, .unbreakable = false},
    [BLOCK_SAND] = {.name = "sand", .solid = true, .transparent = false, .color_r = 0.85f, .color_g = 0.75f, .color_b = 0.5f, .hardness = 0.5f, .tool = 3, .min_tier = 1, .drop = 8, .drop_count = 1, .unbreakable = false},
    [BLOCK_GLASS] = {.name = "glass", .solid = true, .transparent = true, .color_r = 0.75f, .color_g = 0.88f, .color_b = 0.95f, .hardness = 0.3f, .tool = 0, .min_tier = 0, .drop = 0, .drop_count = 0, .unbreakable = false},
    [BLOCK_COAL_ORE] = {.name = "coal_ore", .solid = true, .transparent = false, .color_r = 0.35f, .color_g = 0.35f, .color_b = 0.35f, .hardness = 3.0f, .tool = 1, .min_tier = 1, .drop = 100, .drop_count = 1, .unbreakable = false},
    [BLOCK_IRON_ORE] = {.name = "iron_ore", .solid = true, .transparent = false, .color_r = 0.62f, .color_g = 0.5f, .color_b = 0.42f, .hardness = 3.0f, .tool = 1, .min_tier = 2, .drop = 11, .drop_count = 1, .unbreakable = false},
    [BLOCK_GOLD_ORE] = {.name = "gold_ore", .solid = true, .transparent = false, .color_r = 0.7f, .color_g = 0.62f, .color_b = 0.4f, .hardness = 3.0f, .tool = 1, .min_tier = 2, .drop = 12, .drop_count = 1, .unbreakable = false},
    [BLOCK_DIAMOND_ORE] = {.name = "diamond_ore", .solid = true, .transparent = false, .color_r = 0.5f, .color_g = 0.68f, .color_b = 0.68f, .hardness = 3.0f, .tool = 1, .min_tier = 2, .drop = 13, .drop_count = 1, .unbreakable = false},
    [BLOCK_SNOW] = {.name = "snow", .solid = true, .transparent = false, .color_r = 0.94f, .color_g = 0.96f, .color_b = 0.98f, .hardness = 0.1f, .tool = 3, .min_tier = 1, .drop = 14, .drop_count = 1, .unbreakable = false},
    [BLOCK_GRASS_PLANT] = {.name = "grass_plant", .solid = false, .transparent = true, .color_r = 0.35f, .color_g = 0.7f, .color_b = 0.25f, .hardness = 0.0f, .tool = 0, .min_tier = 0, .drop = 15, .drop_count = 1, .unbreakable = false},
    [BLOCK_FLOWER] = {.name = "flower", .solid = false, .transparent = true, .color_r = 0.9f, .color_g = 0.25f, .color_b = 0.3f, .hardness = 0.0f, .tool = 0, .min_tier = 0, .drop = 16, .drop_count = 1, .unbreakable = false},
    [BLOCK_TORCH] = {.name = "torch", .solid = false, .transparent = true, .color_r = 0.95f, .color_g = 0.75f, .color_b = 0.3f, .hardness = 0.0f, .tool = 0, .min_tier = 0, .drop = 17, .drop_count = 1, .unbreakable = false},
    [BLOCK_WORKBENCH] = {.name = "workbench", .solid = true, .transparent = false, .color_r = 0.55f, .color_g = 0.40f, .color_b = 0.22f, .hardness = 2.5f, .tool = 2, .min_tier = 1, .drop = 18, .drop_count = 1, .unbreakable = false},
    [BLOCK_PLANKS] = {.name = "planks", .solid = true, .transparent = false, .color_r = 0.62f, .color_g = 0.47f, .color_b = 0.26f, .hardness = 2.0f, .tool = 2, .min_tier = 1, .drop = 19, .drop_count = 1, .unbreakable = false},
};

/* Fallback entry for invalid IDs (aliases AIR). */
static const BlockInfo BLOCK_INVALID = {.name = "air", .solid = false, .transparent = true, .color_r = 0.0f, .color_g = 0.0f, .color_b = 0.0f};

/* Get metadata for a block ID.
 *
 * Args:
 *   type: block ID.
 *
 * Returns: static BlockInfo pointer.
 */
const BlockInfo *block_get_info(uint16_t type)
{
    if (type >= (uint16_t)BLOCK_COUNT) {
        return &BLOCK_INVALID;
    }
    return &BLOCK_TABLE[type];
}

/* Check solidity.
 *
 * Args:
 *   type: block ID.
 *
 * Returns: true if solid.
 */
bool block_is_solid(uint16_t type)
{
    return block_get_info(type)->solid;
}

/* Check transparency.
 *
 * Args:
 *   type: block ID.
 *
 * Returns: true if transparent.
 */
bool block_is_transparent(uint16_t type)
{
    return block_get_info(type)->transparent;
}

/* Check blended-pass membership (WATER, GLASS only).
 *
 * Args:
 *   type: block ID.
 *
 * Returns: true for blended-pass blocks.
 */
bool block_is_blended(uint16_t type)
{
    return type == BLOCK_WATER || type == BLOCK_GLASS;
}

/* Cross-sprite blocks (non-solid decor rendered as X quads). */
bool block_is_cross(uint16_t type)
{
    return type == BLOCK_GRASS_PLANT || type == BLOCK_FLOWER || type == BLOCK_TORCH;
}

/* Check face visibility.
 *
 * Args:
 *   current: current block ID.
 *   neighbor: neighboring block ID.
 *
 * Returns: true if the face should be meshed.
 */
bool block_is_face_visible(uint16_t current, uint16_t neighbor)
{
    if (current == BLOCK_AIR) {
        return false;
    }
    if (neighbor == BLOCK_AIR) {
        return true;
    }
    /* Opaque current vs transparent neighbor: visible (e.g. stone next to
     * water/leaves). Transparent current vs opaque neighbor: handled from the
     * opaque side; still emit to avoid holes for water surfaces. */
    if (block_is_transparent(neighbor) && !block_is_transparent(current)) {
        return true;
    }
    if (block_is_transparent(current)) {
        /* Same transparent type touching itself: cull inner faces
         * (leaves clusters, water bodies, glass panes). */
        if (current == neighbor) {
            return false;
        }
        return !block_is_transparent(neighbor) ? true : neighbor != current;
    }
    return false;
}
