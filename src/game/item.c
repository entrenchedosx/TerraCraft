#include "game/item.h"
#include "render/texture_atlas.h"
#include "world/block.h"

#include <stddef.h>

/* Fallback entry for unknown IDs (aliases NONE). */
static const ItemInfo ITEM_INVALID = {
    ITEM_NONE, "none", 64, 0, TOOL_NONE, TOOL_TIER_NONE, 3, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0.0f,
};

/* Static registry. Block items mirror BlockType values (frozen); tools and
 * materials use explicit standalone IDs. Ordered for readability only —
 * lookup is by explicit ID match, never by position.
 *
 * Durability values are original TerraCraft tuning (uses per tool): wood 64,
 * stone 160, iron 250, diamond 1561; swords use the familiar vanilla tiers
 * (wood 59, stone 131, iron 250, diamond 1561). Food values are hunger points
 * restored on a completed eat.
 */
static const ItemInfo ITEM_TABLE[] = {
    {BLOCK_GRAVEL, "Gravel", 64, BLOCK_GRAVEL, TOOL_NONE, TOOL_TIER_NONE, TILE_GRAVEL, 0.55f, 0.54f, 0.52f, 0, 0, 1, .4f},
    {BLOCK_SANDSTONE, "Sandstone", 64, BLOCK_SANDSTONE, TOOL_NONE, TOOL_TIER_NONE, TILE_SANDSTONE, 0.81f, 0.72f, 0.48f, 0, 0, 1, .4f},
    {BLOCK_DEEPSLATE, "Deepslate", 64, BLOCK_DEEPSLATE, TOOL_NONE, TOOL_TIER_NONE, TILE_DEEPSLATE, 0.28f, 0.28f, 0.3f, 0, 0, 1, .4f},
    {BLOCK_COPPER_ORE, "Copper Ore", 64, BLOCK_COPPER_ORE, TOOL_NONE, TOOL_TIER_NONE, TILE_COPPER_ORE, 0.65f, 0.43f, 0.28f, 0, 0, 1, .4f},
    {BLOCK_LAPIS_ORE, "Lapis Ore", 64, BLOCK_LAPIS_ORE, TOOL_NONE, TOOL_TIER_NONE, TILE_LAPIS_ORE, 0.25f, 0.35f, 0.75f, 0, 0, 1, .4f},
    {BLOCK_REDSTONE_ORE, "Redstone Ore", 64, BLOCK_REDSTONE_ORE, TOOL_NONE, TOOL_TIER_NONE, TILE_REDSTONE_ORE, 0.7f, 0.16f, 0.16f, 0, 0, 1, .4f},
    {BLOCK_EMERALD_ORE, "Emerald Ore", 64, BLOCK_EMERALD_ORE, TOOL_NONE, TOOL_TIER_NONE, TILE_EMERALD_ORE, 0.15f, 0.7f, 0.35f, 0, 0, 1, .4f},
    {BLOCK_TUFF, "Tuff", 64, BLOCK_TUFF, TOOL_NONE, TOOL_TIER_NONE, TILE_TUFF, 0.4f, 0.42f, 0.37f, 0, 0, 1, .4f},
    {BLOCK_GRANITE, "Granite", 64, BLOCK_GRANITE, TOOL_NONE, TOOL_TIER_NONE, TILE_GRANITE, 0.62f, 0.43f, 0.36f, 0, 0, 1, .4f},
    {BLOCK_RAW_IRON, "Raw Iron Block", 64, BLOCK_RAW_IRON, TOOL_NONE, TOOL_TIER_NONE, TILE_RAW_IRON, 0.65f, 0.52f, 0.4f, 0, 0, 1, .4f},
    {BLOCK_RAW_COPPER, "Raw Copper Block", 64, BLOCK_RAW_COPPER, TOOL_NONE, TOOL_TIER_NONE, TILE_RAW_COPPER, 0.65f, 0.42f, 0.28f, 0, 0, 1, .4f},
    {3, "Grass Block", 64, 3, TOOL_NONE, TOOL_TIER_NONE, TILE_GRASS_SIDE, 0.20f, 0.80f, 0.20f, 0, 0, 1, 0.4f},
    {2, "Dirt", 64, 2, TOOL_NONE, TOOL_TIER_NONE, TILE_DIRT, 0.50f, 0.30f, 0.10f, 0, 0, 1, 0.4f},
    {1, "Stone", 64, 1, TOOL_NONE, TOOL_TIER_NONE, TILE_STONE, 0.50f, 0.50f, 0.50f, 0, 0, 1, 0.4f},
    {8, "Sand", 64, 8, TOOL_NONE, TOOL_TIER_NONE, TILE_SAND, 0.85f, 0.75f, 0.50f, 0, 0, 1, 0.4f},
    {6, "Wood", 64, 6, TOOL_NONE, TOOL_TIER_NONE, TILE_WOOD, 0.45f, 0.30f, 0.15f, 0, 0, 1, 0.4f},
    {7, "Leaves", 64, 7, TOOL_NONE, TOOL_TIER_NONE, TILE_LEAVES, 0.20f, 0.55f, 0.20f, 0, 0, 1, 0.4f},
    {9, "Glass", 64, 9, TOOL_NONE, TOOL_TIER_NONE, TILE_GLASS, 0.75f, 0.88f, 0.95f, 0, 0, 1, 0.4f},
    {5, "Bedrock", 64, 5, TOOL_NONE, TOOL_TIER_NONE, TILE_BEDROCK, 0.10f, 0.10f, 0.10f, 0, 0, 1, 0.4f},
    {10, "Coal Ore", 64, 10, TOOL_NONE, TOOL_TIER_NONE, TILE_COAL_ORE, 0.35f, 0.35f, 0.35f, 0, 0, 1, 0.4f},
    {11, "Iron Ore", 64, 11, TOOL_NONE, TOOL_TIER_NONE, TILE_IRON_ORE, 0.62f, 0.50f, 0.42f, 0, 0, 1, 0.4f},
    {12, "Gold Ore", 64, 12, TOOL_NONE, TOOL_TIER_NONE, TILE_GOLD_ORE, 0.70f, 0.62f, 0.40f, 0, 0, 1, 0.4f},
    {13, "Diamond Ore", 64, 13, TOOL_NONE, TOOL_TIER_NONE, TILE_DIAMOND_ORE, 0.50f, 0.68f, 0.68f, 0, 0, 1, 0.4f},
    {14, "Snow", 64, 14, TOOL_NONE, TOOL_TIER_NONE, TILE_SNOW, 0.94f, 0.96f, 0.98f, 0, 0, 1, 0.4f},
    {15, "Grass Plant", 64, 15, TOOL_NONE, TOOL_TIER_NONE, TILE_PLANT, 0.35f, 0.70f, 0.25f, 0, 0, 1, 0.4f},
    {16, "Flower", 64, 16, TOOL_NONE, TOOL_TIER_NONE, TILE_FLOWER, 0.90f, 0.25f, 0.30f, 0, 0, 1, 0.4f},
    {17, "Torch", 64, 17, TOOL_NONE, TOOL_TIER_NONE, TILE_TORCH, 0.95f, 0.75f, 0.30f, 0, 0, 1, 0.4f},
    {18, "Workbench", 64, 18, TOOL_NONE, TOOL_TIER_NONE, TILE_WORKBENCH, 0.55f, 0.40f, 0.22f, 0, 0, 1, 0.4f},
    {19, "Planks", 64, 19, TOOL_NONE, TOOL_TIER_NONE, TILE_PLANKS, 0.62f, 0.47f, 0.26f, 0, 0, 1, 0.4f},
    {ITEM_COAL, "Coal", 64, 0, TOOL_NONE, TOOL_TIER_NONE, TILE_COAL, 0.15f, 0.15f, 0.15f, 0, 0, 1, 0.4f},
    {ITEM_APPLE, "Apple", 64, 0, TOOL_NONE, TOOL_TIER_NONE, TILE_APPLE, 0.85f, 0.15f, 0.15f, 0, 4, 0,
     0.0f},
    {ITEM_STICK, "Stick", 64, 0, TOOL_NONE, TOOL_TIER_NONE, TILE_STICK, 0.55f, 0.42f, 0.25f, 0, 0, 1, 0.4f},
    {ITEM_ARROW, "Arrow", 64, 0, TOOL_NONE, TOOL_TIER_NONE, TILE_ARROW, 0.80f, 0.75f, 0.60f, 0, 0, 1,
     0.4f},
    {ITEM_BONE, "Bone", 64, 0, TOOL_NONE, TOOL_TIER_NONE, TILE_BONE, 0.85f, 0.83f, 0.75f, 0, 0, 1,
     0.4f},
    {ITEM_RAW_BEEF, "Raw Beef", 64, 0, TOOL_NONE, TOOL_TIER_NONE, TILE_BEEF, 0.72f, 0.35f, 0.25f, 0,
     3, 1, 0.4f},
    {ITEM_LEATHER, "Leather", 64, 0, TOOL_NONE, TOOL_TIER_NONE, TILE_LEATHER, 0.65f, 0.42f, 0.25f,
     0, 0, 1, 0.4f},
    /* Rotten flesh (inedible here: no status-effect system exists, and a
     * fake free meal would lie — documented gap). */
    {ITEM_ROTTEN_FLESH, "Rotten Flesh", 64, 0, TOOL_NONE, TOOL_TIER_NONE, TILE_FLESH, 0.45f, 0.35f,
     0.22f, 0, 0, 1, 0.4f},
    {ITEM_IRON_INGOT, "Iron Ingot", 64, 0, TOOL_NONE, TOOL_TIER_NONE, TILE_IRON_INGOT, 0.82f, 0.82f, 0.85f,
     0, 0, 1, 0.4f},
    {ITEM_DIAMOND, "Diamond", 64, 0, TOOL_NONE, TOOL_TIER_NONE, TILE_DIAMOND, 0.36f, 0.85f, 0.82f,
     0, 0, 1, 0.4f},
    {ITEM_WOOD_PICKAXE, "Wood Pickaxe", 1, 0, TOOL_PICKAXE, TOOL_TIER_WOOD, TILE_WOOD_PICKAXE, 0.55f, 0.42f,
     0.25f, 64, 0, 3, 0.5f},
    {ITEM_STONE_PICKAXE, "Stone Pickaxe", 1, 0, TOOL_PICKAXE, TOOL_TIER_STONE, TILE_STONE_PICKAXE, 0.55f, 0.55f,
     0.58f, 160, 0, 3, 0.5f},
    {ITEM_WOOD_AXE, "Wood Axe", 1, 0, TOOL_AXE, TOOL_TIER_WOOD, TILE_WOOD_AXE, 0.55f, 0.42f, 0.25f, 64, 0, 4, 0.8f},
    {ITEM_STONE_AXE, "Stone Axe", 1, 0, TOOL_AXE, TOOL_TIER_STONE, TILE_STONE_AXE, 0.55f, 0.55f, 0.58f, 160,
     0, 4, 0.8f},
    {ITEM_WOOD_SHOVEL, "Wood Shovel", 1, 0, TOOL_SHOVEL, TOOL_TIER_WOOD, TILE_WOOD_SHOVEL, 0.55f, 0.42f,
     0.25f, 64, 0, 2, 0.5f},
    {ITEM_STONE_SHOVEL, "Stone Shovel", 1, 0, TOOL_SHOVEL, TOOL_TIER_STONE, TILE_STONE_SHOVEL, 0.55f, 0.55f,
     0.58f, 160, 0, 2, 0.5f},
    /* Bow (M9 ranged weapon): unstackable, 128 shots per bow, mines at
     * hand speed (no block prefers TOOL_BOW), melees as fists. */
    {ITEM_BOW, "Bow", 1, 0, TOOL_BOW, TOOL_TIER_NONE, TILE_BOW, 0.55f, 0.42f, 0.25f, 128, 0, 0, 0.0f},
    /* Wooden and stone sword values follow familiar vanilla tiers.
     * Higher tiers await ingot/gem acquisition systems. */
    {ITEM_WOOD_SWORD, "Wooden Sword", 1, 0, TOOL_SWORD, TOOL_TIER_WOOD, TILE_WOOD_SWORD, 0.55f, 0.42f,
     0.25f, 59, 0, 4, 0.625f},
{ITEM_STONE_SWORD, "Stone Sword", 1, 0, TOOL_SWORD, TOOL_TIER_STONE, TILE_STONE_SWORD, 0.55f, 0.55f,
     0.58f, 131, 0, 5, 0.625f},
    /* Iron and diamond complete the vanilla progression ladder: durability
     * 250/1561 uses, melee damage +1 per tier over stone. Mining speed comes
     * from the tier (6x / 8x) in survival_mine_time. */
    {ITEM_IRON_PICKAXE, "Iron Pickaxe", 1, 0, TOOL_PICKAXE, TOOL_TIER_IRON, TILE_IRON_PICKAXE, 0.82f, 0.82f,
     0.85f, 250, 0, 4, 0.5f},
    {ITEM_IRON_AXE, "Iron Axe", 1, 0, TOOL_AXE, TOOL_TIER_IRON, TILE_IRON_AXE, 0.82f, 0.82f, 0.85f, 250, 0, 5,
     0.8f},
    {ITEM_IRON_SHOVEL, "Iron Shovel", 1, 0, TOOL_SHOVEL, TOOL_TIER_IRON, TILE_IRON_SHOVEL, 0.82f, 0.82f,
     0.85f, 250, 0, 3, 0.5f},
    {ITEM_DIAMOND_PICKAXE, "Diamond Pickaxe", 1, 0, TOOL_PICKAXE, TOOL_TIER_DIAMOND, TILE_DIAMOND_PICKAXE, 0.36f,
     0.85f, 0.82f, 1561, 0, 5, 0.5f},
    {ITEM_DIAMOND_AXE, "Diamond Axe", 1, 0, TOOL_AXE, TOOL_TIER_DIAMOND, TILE_DIAMOND_AXE, 0.36f, 0.85f, 0.82f,
     1561, 0, 6, 0.8f},
    {ITEM_DIAMOND_SHOVEL, "Diamond Shovel", 1, 0, TOOL_SHOVEL, TOOL_TIER_DIAMOND, TILE_DIAMOND_SHOVEL, 0.36f,
     0.85f, 0.82f, 1561, 0, 3, 0.5f},
    {ITEM_IRON_SWORD, "Iron Sword", 1, 0, TOOL_SWORD, TOOL_TIER_IRON, TILE_IRON_SWORD, 0.82f, 0.82f, 0.85f, 250,
     0, 6, 0.625f},
    {ITEM_DIAMOND_SWORD, "Diamond Sword", 1, 0, TOOL_SWORD, TOOL_TIER_DIAMOND, TILE_DIAMOND_SWORD, 0.36f, 0.85f,
     0.82f, 1561, 0, 7, 0.625f},
};
#define ITEM_TABLE_COUNT (sizeof(ITEM_TABLE) / sizeof(ITEM_TABLE[0]))

/* Look up an item definition. */
const ItemInfo *item_get_info(ItemId id)
{
    for (size_t i = 0; i < ITEM_TABLE_COUNT; ++i) {
        if (ITEM_TABLE[i].id == id) {
            return &ITEM_TABLE[i];
        }
    }
    return &ITEM_INVALID;
}

/* Check whether an ID names a real item. */
bool item_is_valid(ItemId id)
{
    return item_get_info(id)->id != ITEM_NONE;
}

/* Check whether an item places a block. */
bool item_is_block(ItemId id)
{
    return item_get_info(id)->place_block != 0;
}

/* Block placed by an item (0 when not placeable). */
uint16_t item_to_block(ItemId id)
{
    return item_get_info(id)->place_block;
}

/* Check whether an item can be eaten. */
bool item_is_edible(ItemId id)
{
    return item_get_info(id)->food > 0;
}
