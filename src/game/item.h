#pragma once

/* Item registry (M6): stable explicit IDs, data-driven definitions.
 * Block-placeable items reuse their block's numeric ID (frozen, documented);
 * standalone items (materials, tools) live at >= 100. IDs are persistent:
 * never reorder, never reuse. Pure CPU, headless-testable.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Stable item IDs. Block items mirror BlockType values 1..19 and 28..38 (except
 * WATER, which is not a valid item). Standalone IDs are explicit.
 */
typedef uint16_t ItemId;

#define ITEM_NONE 0u
/* Block items: use the BlockType value directly (AIR=0 and WATER=4 excluded;
 * see item_is_valid). Example: ITEM_GRASS_BLOCK == BLOCK_GRASS == 3. */
#define ITEM_COAL 100u
#define ITEM_APPLE 101u
#define ITEM_STICK 102u
#define ITEM_ARROW 103u
#define ITEM_BONE 104u
#define ITEM_RAW_BEEF 105u
#define ITEM_LEATHER 106u
#define ITEM_ROTTEN_FLESH 107u
#define ITEM_IRON_INGOT 108u
#define ITEM_DIAMOND 109u
#define ITEM_WOOD_PICKAXE 200u
#define ITEM_STONE_PICKAXE 201u
#define ITEM_WOOD_AXE 202u
#define ITEM_STONE_AXE 203u
#define ITEM_WOOD_SHOVEL 204u
#define ITEM_STONE_SHOVEL 205u
#define ITEM_BOW 206u
#define ITEM_WOOD_SWORD 207u
#define ITEM_STONE_SWORD 208u
#define ITEM_IRON_PICKAXE 209u
#define ITEM_IRON_AXE 210u
#define ITEM_IRON_SHOVEL 211u
#define ITEM_DIAMOND_PICKAXE 212u
#define ITEM_DIAMOND_AXE 213u
#define ITEM_DIAMOND_SHOVEL 214u
#define ITEM_IRON_SWORD 215u
#define ITEM_DIAMOND_SWORD 216u

/* Tool categories a block may prefer (bows and swords never match a block tool,
 * so they always mine at hand speed — no struct change needed).
 */
typedef enum ToolType {
    TOOL_NONE = 0,
    TOOL_PICKAXE,
    TOOL_AXE,
    TOOL_SHOVEL,
    TOOL_BOW,
    TOOL_SWORD
} ToolType;

/* Tool tiers (0 = not a tool / bare hands). Higher breaks faster. Mining
 * speed uses the vanilla ladder (wood 2x, stone 4x, iron 6x, diamond 8x);
 * see survival_mine_time.
 */
#define TOOL_TIER_NONE 0
#define TOOL_TIER_WOOD 1
#define TOOL_TIER_STONE 2
#define TOOL_TIER_IRON 3
#define TOOL_TIER_DIAMOND 4

/* Per-item static definition. */
typedef struct ItemInfo {
    ItemId id;            /* Numeric ID (never NULL name for valid IDs). */
    const char *name;     /* Display name, e.g. "Stone Pickaxe". */
    uint16_t max_stack;   /* Maximum stack size (>= 1). */
    uint16_t place_block; /* BlockType to place, or 0 when not placeable. */
    int tool;             /* ToolType value (TOOL_NONE for non-tools). */
    int tier;             /* Tool tier (TOOL_TIER_NONE for non-tools). */
    int tile;             /* Atlas tile index for icons/entities. */
    float color_r, color_g, color_b; /* Flat UI icon color (0..1). */
    uint16_t max_durability; /* Uses before a tool breaks (0 = not damageable). */
    uint8_t food;            /* Hunger restored when eaten (0 = inedible). */
    uint8_t attack_damage;   /* Melee damage (fists use 1, see mob_tool_stats). */
    float attack_cooldown;   /* Seconds between melee swings with this item. */
} ItemInfo;

/* Look up an item definition. Unknown IDs return the NONE entry.
 *
 * Args:
 *   id: item ID.
 *
 * Returns: pointer to static info (never NULL, do not free).
 */
const ItemInfo *item_get_info(ItemId id);

/* Check whether an ID names a real item (NONE counts as invalid here;
 * use item == ITEM_NONE directly for emptiness tests).
 *
 * Args:
 *   id: item ID.
 *
 * Returns: true for registered items (blocks 1..19 except water,
 * 100..109, 200..216).
 */
bool item_is_valid(ItemId id);

/* Check whether an item places a block when used.
 *
 * Args:
 *   id: item ID.
 *
 * Returns: true when place_block != 0.
 */
bool item_is_block(ItemId id);

/* Block placed by an item (0 when not placeable).
 *
 * Args:
 *   id: item ID.
 *
 * Returns: BlockType value or 0.
 */
uint16_t item_to_block(ItemId id);

/* Check whether an item can be eaten (food value > 0).
 *
 * Args:
 *   id: item ID.
 *
 * Returns: true for edible items.
 */
bool item_is_edible(ItemId id);
