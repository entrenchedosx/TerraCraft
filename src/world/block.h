#pragma once

/* Block definitions (M5): stable IDs + lookup table (name, solidity, color).
 * Original placeholder set; not Minecraft assets.
 *
 * Canonical IDs: AIR=0, STONE=1, DIRT=2, GRASS=3, WATER=4, BEDROCK=5,
 * WOOD=6, LEAVES=7, SAND=8 (M0/M1), GLASS=9, COAL_ORE=10, IRON_ORE=11,
 * GOLD_ORE=12, DIAMOND_ORE=13, SNOW=14 (M2), GRASS_PLANT=15, FLOWER=16,
 * TORCH=17 (M5: vegetation + light placeholder).
 * Append-only; never reorder or reuse (saves depend on values).
 * See docs/BACKLOG.md "Save format forward compatibility".
 */

#include <stdbool.h>
#include <stdint.h>

/* Stable block IDs. */
typedef enum BlockType {
    BLOCK_AIR = 0,
    BLOCK_STONE = 1,
    BLOCK_DIRT = 2,
    BLOCK_GRASS = 3,
    BLOCK_WATER = 4,
    BLOCK_BEDROCK = 5,
    BLOCK_WOOD = 6,
    BLOCK_LEAVES = 7,
    BLOCK_SAND = 8,
    BLOCK_GLASS = 9,
    BLOCK_COAL_ORE = 10,
    BLOCK_IRON_ORE = 11,
    BLOCK_GOLD_ORE = 12,
    BLOCK_DIAMOND_ORE = 13,
    BLOCK_SNOW = 14,
    BLOCK_GRASS_PLANT = 15,
    BLOCK_FLOWER = 16,
    BLOCK_TORCH = 17,
    BLOCK_WORKBENCH = 18, /* M7 crafting station (axe-preferred, drops itself). */
    BLOCK_PLANKS = 19,    /* M7 processed wood (axe-preferred, drops itself). */
    BLOCK_WATER_FLOW_1 = 20, /* Flowing-water depth states, persisted as block IDs. */
    BLOCK_WATER_FLOW_2 = 21,
    BLOCK_WATER_FLOW_3 = 22,
    BLOCK_WATER_FLOW_4 = 23,
    BLOCK_WATER_FLOW_5 = 24,
    BLOCK_WATER_FLOW_6 = 25,
    BLOCK_WATER_FLOW_7 = 26,
    BLOCK_WATER_FALLING = 27,
    BLOCK_COUNT /* Sentinel: number of block types. */
} BlockType;

/* Static block metadata. Gameplay fields (hardness/tool/drop) drive the
 * M6 survival policy in game/survival.h; tool/tier integers match the
 * ToolType enum and TOOL_TIER_* constants there (kept as ints here to
 * avoid a header cycle).
 */
typedef struct BlockInfo {
    const char *name; /* Display name (never NULL for valid IDs). */
    bool solid;       /* Collidable / face-culling occluder. */
    bool transparent; /* Renders in transparent pass (water/leaves). */
    float color_r, color_g, color_b; /* Placeholder base color (0..1). */
    float hardness;   /* MC hardness (break time derives in survival_mine_time). */
    int tool;         /* Preferred tool (0 none, 1 pickaxe, 2 axe, 3 shovel). */
    int min_tier;     /* Minimum tier for full tool speed (0..2). */
    uint16_t drop;    /* Dropped ItemId (0 = none). */
    uint8_t drop_count; /* Dropped count (>= 1 when drop != 0). */
    bool unbreakable; /* True: survival mining never finishes (bedrock). */
    bool gravity_affected; /* True when unsupported blocks are simulated as falling. */
} BlockInfo;

/* Get metadata for a block ID. Out-of-range IDs return the AIR entry.
 *
 * Args:
 *   type: block ID (0..65535; values >= BLOCK_COUNT map to AIR info).
 *
 * Returns: pointer to static BlockInfo (never NULL, do not free).
 */
const BlockInfo *block_get_info(uint16_t type);

/* Check solidity (air and water are non-solid for physics purposes).
 *
 * Args:
 *   type: block ID.
 *
 * Returns: true if solid.
 */
bool block_is_solid(uint16_t type);

/* Check transparency.
 *
 * Args:
 *   type: block ID.
 *
 * Returns: true if transparent.
 */
bool block_is_transparent(uint16_t type);

/* Check whether a block's faces belong in the blended transparent pass
 * (WATER, GLASS). Cutout vegetation (leaves, plants, flowers, torches)
 * renders in the opaque pass with an alpha discard.
 *
 * Args:
 *   type: block ID.
 *
 * Returns: true for blended-pass blocks.
 */
bool block_is_blended(uint16_t type);

/* Fluid-state helpers. BLOCK_WATER is a source; horizontal flow is level
 * 1..7 and falling water keeps full height while fed from above. */
bool block_is_water(uint16_t type);
int block_water_level(uint16_t type);
bool block_water_is_falling(uint16_t type);
uint16_t block_water_flowing(int level, bool falling);
float block_water_height(uint16_t type);

/* True for registry-defined falling blocks (currently sand). */
bool block_has_gravity(uint16_t type);

/* Check whether a block renders as crossed sprite quads (plants, flowers,
 * torches) instead of a full cube. Sprites are always cutout-opaque and
 * double-sided (both windings emitted).
 *
 * Args:
 *   type: block ID.
 *
 * Returns: true for cross-sprite blocks.
 */
bool block_is_cross(uint16_t type);

/* Check whether a face between `a` (current) and `b` (neighbor) is visible.
 * A face is visible when the neighbor is air or transparent while the
 * current block is opaque, or more generally when the neighbor does not
 * fully occlude. M1 rule: visible if neighbor is AIR or transparent and
 * current is not AIR.
 *
 * Args:
 *   current: current block ID.
 *   neighbor: neighboring block ID.
 *
 * Returns: true if the face should be meshed.
 */
bool block_is_face_visible(uint16_t current, uint16_t neighbor);
