#include "test_main.h"

#include "game/inventory.h"
#include "game/item.h"
#include "render/texture_atlas.h"
#include "world/block.h"

#include <stdlib.h>
#include <string.h>

typedef struct ItemTextureExpectation {
    ItemId item;
    int tile;
    const char *stem;
} ItemTextureExpectation;

/* Test every registered item-to-atlas mapping and ensure its procedural
 * fallback tile contains drawable pixels. This protects the item icons,
 * cursors, and dropped-item sprites, which share this mapping.
 */
int test_item_texture_mapping(void)
{
    static const ItemTextureExpectation expected[] = {
        {BLOCK_STONE, TILE_STONE, "stone"},
        {BLOCK_DIRT, TILE_DIRT, "dirt"},
        {BLOCK_GRASS, TILE_GRASS_SIDE, "grass_side"},
        {BLOCK_BEDROCK, TILE_BEDROCK, "bedrock"},
        {BLOCK_WOOD, TILE_WOOD, "wood"},
        {BLOCK_LEAVES, TILE_LEAVES, "leaves"},
        {BLOCK_SAND, TILE_SAND, "sand"},
        {BLOCK_GLASS, TILE_GLASS, "glass"},
        {BLOCK_COAL_ORE, TILE_COAL_ORE, "coal_ore"},
        {BLOCK_IRON_ORE, TILE_IRON_ORE, "iron_ore"},
        {BLOCK_GOLD_ORE, TILE_GOLD_ORE, "gold_ore"},
        {BLOCK_DIAMOND_ORE, TILE_DIAMOND_ORE, "diamond_ore"},
        {BLOCK_SNOW, TILE_SNOW, "snow"},
        {BLOCK_GRASS_PLANT, TILE_PLANT, "plant"},
        {BLOCK_FLOWER, TILE_FLOWER, "flower"},
        {BLOCK_TORCH, TILE_TORCH, "torch"},
        {BLOCK_WORKBENCH, TILE_WORKBENCH, "workbench"},
        {BLOCK_PLANKS, TILE_PLANKS, "planks"},
        {ITEM_COAL, TILE_COAL, "coal"},
        {ITEM_APPLE, TILE_APPLE, "apple"},
        {ITEM_STICK, TILE_STICK, "stick"},
        {ITEM_ARROW, TILE_ARROW, "arrow"},
        {ITEM_BONE, TILE_BONE, "bone"},
        {ITEM_RAW_BEEF, TILE_BEEF, "beef"},
        {ITEM_LEATHER, TILE_LEATHER, "leather"},
        {ITEM_WOOD_PICKAXE, TILE_WOOD_PICKAXE, "wood_pickaxe"},
        {ITEM_STONE_PICKAXE, TILE_STONE_PICKAXE, "stone_pickaxe"},
        {ITEM_WOOD_AXE, TILE_WOOD_AXE, "wood_axe"},
        {ITEM_STONE_AXE, TILE_STONE_AXE, "stone_axe"},
        {ITEM_WOOD_SHOVEL, TILE_WOOD_SHOVEL, "wood_shovel"},
        {ITEM_STONE_SHOVEL, TILE_STONE_SHOVEL, "stone_shovel"},
        {ITEM_BOW, TILE_BOW, "bow"},
    };
    int failures = 0;
    const size_t atlas_bytes = (size_t)ATLAS_SIZE * ATLAS_SIZE * ATLAS_BYTES;
    unsigned char *pixels = (unsigned char *)malloc(atlas_bytes);
    TEST_ASSERT(pixels != NULL);
    if (pixels == NULL) {
        return failures + 1;
    }
    texture_atlas_fill_rgba(pixels);
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        const ItemTextureExpectation *want = &expected[i];
        const ItemInfo *info = item_get_info(want->item);
        TEST_ASSERT(item_is_valid(want->item));
        TEST_ASSERT(info->tile == want->tile);
        TEST_ASSERT(strcmp(texture_atlas_tile_file(want->tile), want->stem) == 0);
        size_t visible = 0;
        int col = want->tile % ATLAS_TILES;
        int row = want->tile / ATLAS_TILES;
        for (int y = 0; y < ATLAS_TILE_PX; ++y) {
            for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                size_t offset = ((size_t)(row * ATLAS_TILE_PX + y) * ATLAS_SIZE +
                                 (size_t)(col * ATLAS_TILE_PX + x)) * ATLAS_BYTES;
                if (pixels[offset + 3] >= 13) {
                    ++visible;
                }
            }
        }
        TEST_ASSERT(visible > 0);
    }
    free(pixels);
    return failures;
}

/* Test: item registry validity, block mapping, tool data.
 *
 * Returns: failure count.
 */
int test_item_registry(void)
{
    int failures = 0;
    TEST_ASSERT(item_is_valid(1) == true);
    TEST_ASSERT(item_is_valid(17) == true);
    TEST_ASSERT(item_is_valid(ITEM_COAL) == true);
    TEST_ASSERT(item_is_valid(ITEM_STONE_PICKAXE) == true);
    TEST_ASSERT(item_is_valid(ITEM_NONE) == false);
    TEST_ASSERT(item_is_valid(0) == false);
    TEST_ASSERT(item_is_valid(4) == false); /* Water: not an item. */
    TEST_ASSERT(item_is_valid(18) == true); /* Workbench (M7 block). */
    TEST_ASSERT(item_is_valid(19) == true); /* Planks (M7 block). */
    TEST_ASSERT(item_is_valid(20) == false);
    TEST_ASSERT(item_is_valid(199) == false);
    TEST_ASSERT(item_is_valid(207) == false);
    TEST_ASSERT(item_is_valid(999) == false);
    TEST_ASSERT(item_is_valid(ITEM_RAW_BEEF) == true);
    TEST_ASSERT(item_is_valid(ITEM_LEATHER) == true);
    TEST_ASSERT(item_get_info(ITEM_RAW_BEEF)->food == 3);
    TEST_ASSERT(item_is_valid(ITEM_ARROW) == true);
    TEST_ASSERT(item_is_valid(ITEM_BOW) == true);
    TEST_ASSERT(item_is_block(3) == true);
    TEST_ASSERT(item_is_block(ITEM_COAL) == false);
    TEST_ASSERT(item_is_block(ITEM_WOOD_AXE) == false);
    TEST_ASSERT(item_to_block(3) == 3);
    TEST_ASSERT(item_to_block(ITEM_COAL) == 0);
    const ItemInfo *pick = item_get_info(ITEM_STONE_PICKAXE);
    TEST_ASSERT(pick->tool == TOOL_PICKAXE && pick->tier == TOOL_TIER_STONE);
    TEST_ASSERT(pick->max_stack == 1);
    TEST_ASSERT(pick->attack_damage == 3);
    TEST_ASSERT_FLOAT_EQ(pick->attack_cooldown, 0.5f, 1e-6f);
    const ItemInfo *axe = item_get_info(ITEM_WOOD_AXE);
    TEST_ASSERT(axe->attack_damage == 4);
    TEST_ASSERT_FLOAT_EQ(axe->attack_cooldown, 0.8f, 1e-6f);
    TEST_ASSERT(item_get_info((ItemId)BLOCK_STONE)->attack_damage == 1);
    TEST_ASSERT(item_get_info(3)->max_stack == 64);
    TEST_ASSERT(item_get_info(9999)->id == ITEM_NONE);
    TEST_ASSERT(item_get_info(9999)->name != NULL);
    return failures;
}

/* TerraCraft currently registers the Java-style 64-stack set plus damageable
 * tools and a bow. Keep this explicit audit synchronized with ITEM_TABLE:
 * the six tools and bow are the only implemented items whose max stack is 1.
 */
int test_item_stackability_contract(void)
{
    static const ItemId unstackable[] = {
        ITEM_WOOD_PICKAXE, ITEM_STONE_PICKAXE, ITEM_WOOD_AXE,
        ITEM_STONE_AXE, ITEM_WOOD_SHOVEL, ITEM_STONE_SHOVEL, ITEM_BOW,
    };
    int failures = 0;
    int registered = 0;

    /* Exhaustively check each currently allocated registry range, including
     * the intentionally omitted water block and unused ID gaps.
     */
    for (ItemId id = 1; id <= 19; ++id) {
        bool expected_valid = id != 4; /* Water has no inventory item. */
        TEST_ASSERT(item_is_valid(id) == expected_valid);
        if (expected_valid) {
            ++registered;
            TEST_ASSERT(item_get_info(id)->max_stack == 64);
        }
    }
    for (ItemId id = 100; id <= 107; ++id) {
        TEST_ASSERT(item_is_valid(id));
        ++registered;
        TEST_ASSERT(item_get_info(id)->max_stack == 64);
    }
    for (ItemId id = 200; id <= 206; ++id) {
        TEST_ASSERT(item_is_valid(id));
        ++registered;
        TEST_ASSERT(item_get_info(id)->max_stack == 1);
    }
    TEST_ASSERT(registered == 33);
    for (size_t i = 0; i < sizeof(unstackable) / sizeof(unstackable[0]); ++i) {
        TEST_ASSERT(item_get_info(unstackable[i])->max_stack == 1);
    }

    /* Exercise the shared rules used by inventory insertion, GUI merges,
     * right-click splitting, and save-load sanitization for every singleton.
     */
    for (size_t i = 0; i < sizeof(unstackable) / sizeof(unstackable[0]); ++i) {
        ItemId id = unstackable[i];
        ItemStack dst = {id, 1, 0};
        ItemStack src = {id, 1, 0};
        TEST_ASSERT(!stack_can_merge(&dst, &src));
        TEST_ASSERT(stack_add(&dst, &src) == 0);
        TEST_ASSERT(dst.item == id && dst.count == 1);
        TEST_ASSERT(src.item == id && src.count == 1);

        /* Defensive clamp: even a malformed count cannot create a stack. */
        ItemStack oversized = {id, 2, 0};
        ItemStack empty = {ITEM_NONE, 0, 0};
        TEST_ASSERT(stack_add(&empty, &oversized) == 1);
        TEST_ASSERT(empty.item == id && empty.count == 1);
        TEST_ASSERT(oversized.item == id && oversized.count == 1);

        /* Two obtained copies land in distinct slots, never one stack. */
        Inventory inv;
        inv_init(&inv);
        ItemStack pair = {id, 2, 0};
        TEST_ASSERT(inv_insert(&inv, &pair) == 0);
        TEST_ASSERT(stack_is_empty(&pair));
        TEST_ASSERT(inv.slots[0].item == id && inv.slots[0].count == 1);
        TEST_ASSERT(inv.slots[1].item == id && inv.slots[1].count == 1);

        /* A singleton RMB split transfers that one item; it cannot duplicate
         * it. Persisted over-counts are clamped back to one on sanitize.
         */
        ItemStack single = {id, 1, 0};
        ItemStack picked = {ITEM_NONE, 0, 0};
        stack_split_half(&single, &picked);
        TEST_ASSERT(single.item == ITEM_NONE && single.count == 0);
        TEST_ASSERT(picked.item == id && picked.count == 1);
        inv.slots[2].item = id;
        inv.slots[2].count = 64;
        inv_sanitize(&inv);
        TEST_ASSERT(inv.slots[2].item == id && inv.slots[2].count == 1);
    }
    return failures;
}

/* Test: empty/canonical, merge checks, add/remove/split.
 *
 * Returns: failure count.
 */
int test_stack_ops(void)
{
    int failures = 0;
    ItemStack e = {ITEM_NONE, 0, 0};
    TEST_ASSERT(stack_is_empty(&e) == true);
    TEST_ASSERT(stack_is_empty(NULL) == true);
    ItemStack bad = {ITEM_NONE, 27, 0};
    TEST_ASSERT(stack_is_empty(&bad) == true); /* NONE always reads empty. */

    ItemStack a = {3, 60, 0};
    ItemStack b = {3, 10, 0};
    TEST_ASSERT(stack_can_merge(&a, &b) == true);
    uint16_t moved = stack_add(&a, &b);
    TEST_ASSERT(moved == 4 && a.count == 64 && b.count == 6);

    ItemStack full = {3, 64, 0};
    ItemStack extra = {3, 5, 0};
    TEST_ASSERT(stack_can_merge(&full, &extra) == false);
    TEST_ASSERT(stack_add(&full, &extra) == 0 && extra.count == 5);

    ItemStack diff = {2, 5, 0};
    TEST_ASSERT(stack_can_merge(&a, &diff) == false);

    ItemStack dst;
    stack_clear(&dst);
    ItemStack src = {2, 70, 0}; /* Over max: clamps to 64, keeps 6. */
    TEST_ASSERT(stack_add(&dst, &src) == 64);
    TEST_ASSERT(dst.item == 2 && dst.count == 64 && src.count == 6);

    ItemStack junk = {999, 10, 0};
    ItemStack anywhere = {2, 1, 0};
    TEST_ASSERT(stack_add(&anywhere, &junk) == 0 && stack_is_empty(&junk) == true);

    ItemStack r = {2, 10, 0};
    TEST_ASSERT(stack_remove(&r, 4) == 4 && r.count == 6);
    TEST_ASSERT(stack_remove(&r, 99) == 6 && stack_is_empty(&r) == true);
    TEST_ASSERT(stack_remove(&r, 1) == 0);
    TEST_ASSERT(stack_remove(NULL, 1) == 0);

    ItemStack s = {2, 7, 0};
    ItemStack half = {ITEM_NONE, 0, 0};
    stack_split_half(&s, &half);
    TEST_ASSERT(half.item == 2 && half.count == 4 && s.count == 3);
    ItemStack s2 = {2, 8, 0};
    stack_split_half(&s2, &half);
    TEST_ASSERT(half.count == 4 && s2.count == 4);
    ItemStack e2 = {ITEM_NONE, 0, 0};
    stack_split_half(&e2, &half);
    TEST_ASSERT(stack_is_empty(&half) == true);
    stack_clear(NULL);
    return failures;
}

/* Fill an inventory with single junk stacks (leaves one slot free when n=35).
 * Uses stone (id 1) count 1 per slot for determinism.
 */
static void inv_fill_n(Inventory *inv, int n)
{
    for (int i = 0; i < n && i < INV_SIZE; ++i) {
        inv->slots[i].item = 1;
        inv->slots[i].count = 1;
    }
}

/* Test: insert merge/spill, leftovers, consume, swap/move, counts.
 *
 * Returns: failure count.
 */
int test_inventory_ops(void)
{
    int failures = 0;
    Inventory inv;
    inv_init(&inv);
    TEST_ASSERT(inv_find_empty(&inv) == 0);
    TEST_ASSERT(inv_find_merge(&inv, 3) == -1);

    ItemStack s = {3, 70, 0};
    TEST_ASSERT(inv_insert(&inv, &s) == 0); /* 64 + 6 across two slots. */
    TEST_ASSERT(inv.slots[0].count == 64 && inv.slots[1].item == 3 && inv.slots[1].count == 6);
    TEST_ASSERT(inv_find_merge(&inv, 3) == 1);
    TEST_ASSERT(inv_count_item(&inv, 3) == 70);

    /* Full inventory returns leftovers untouched. */
    Inventory full;
    inv_init(&full);
    inv_fill_n(&full, 35); /* 35x stone/1. */
    ItemStack big = {2, 130, 0}; /* Dirt: one empty slot takes 64, rest spills. */
    uint16_t left = inv_insert(&full, &big);
    TEST_ASSERT(left == 66 && big.count == 66);
    TEST_ASSERT(inv_count_item(&full, 2) == 64);
    ItemStack more = {2, 1, 0};
    TEST_ASSERT(inv_insert(&full, &more) == 1); /* Nowhere left. */
    TEST_ASSERT(inv_find_empty(&full) == -1);

    /* Consume selected/all-or-nothing. */
    TEST_ASSERT(inv_consume(&inv, 0, 64) == true && stack_is_empty(&inv.slots[0]) == true);
    TEST_ASSERT(inv_consume(&inv, 1, 99) == false && inv.slots[1].count == 6);
    TEST_ASSERT(inv_consume(&inv, 99, 1) == false);
    TEST_ASSERT(inv_consume(NULL, 0, 1) == false);

    /* Swap / move / remove. */
    TEST_ASSERT(inv_swap(&inv, 1, 5) == 0 && inv.slots[5].count == 6 && stack_is_empty(&inv.slots[1]));
    TEST_ASSERT(inv_swap(&inv, 5, 5) == 0);
    TEST_ASSERT(inv_swap(&inv, 5, 99) != 0);
    TEST_ASSERT(inv_move(&inv, 5, 7) == 0 && inv.slots[7].count == 6);
    TEST_ASSERT(inv_move(&inv, 7, 7) == 0);
    TEST_ASSERT(inv_move(NULL, 0, 1) != 0);
    TEST_ASSERT(inv_remove(&inv, 7, 2) == 2 && inv.slots[7].count == 4);
    TEST_ASSERT(inv_remove(&inv, -1, 1) == 0);
    TEST_ASSERT(inv_valid_slot(35) == true && inv_valid_slot(36) == false && inv_valid_slot(-1) == false);

    /* Invalid insert sanitizes. */
    ItemStack bad = {999, 10, 0};
    TEST_ASSERT(inv_insert(&inv, &bad) == 0 && stack_is_empty(&bad) == true);
    ItemStack empty = {ITEM_NONE, 0, 0};
    TEST_ASSERT(inv_insert(&inv, &empty) == 0);
    TEST_ASSERT(inv_insert(NULL, &empty) == 0);
    return failures;
}

/* Test: save-load sanitize drops bad IDs/counts, clamps overfull.
 *
 * Returns: failure count.
 */
int test_inventory_sanitize(void)
{
    int failures = 0;
    Inventory inv;
    inv_init(&inv);
    inv.slots[0].item = 3;
    inv.slots[0].count = 64;
    inv.slots[1].item = 999; /* Unknown ID. */
    inv.slots[1].count = 10;
    inv.slots[2].item = 2;
    inv.slots[2].count = 500; /* Over max. */
    inv.slots[3].item = ITEM_NONE;
    inv.slots[3].count = 27; /* NONE with count. */
    inv.slots[4].item = ITEM_STONE_PICKAXE;
    inv.slots[4].count = 3; /* Tool over max 1. */
    inv_sanitize(&inv);
    TEST_ASSERT(inv.slots[0].item == 3 && inv.slots[0].count == 64);
    TEST_ASSERT(stack_is_empty(&inv.slots[1]) == true);
    TEST_ASSERT(inv.slots[2].count == 64);
    TEST_ASSERT(stack_is_empty(&inv.slots[3]) == true);
    TEST_ASSERT(inv.slots[4].count == 1);
    inv_sanitize(NULL);
    return failures;
}
