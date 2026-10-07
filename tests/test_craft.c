#include "test_main.h"

#include "core/path.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/player.h"
#include "game/recipe.h"
#include "game/survival.h"
#include "world/block.h"
#include "world/world.h"
#include "world/world_meta.h"
#include "world/world_save.h"

#include <string.h>

/* Fill a gw*gh grid cell (row-major). */
static void craft_set(ItemStack *grid, int gw, int x, int y, ItemId item, uint16_t count)
{
    grid[y * gw + x].item = item;
    grid[y * gw + x].count = count;
    grid[y * gw + x].durability = 0;
}

/* Empty a grid. */
static void craft_clear(ItemStack *grid, int n)
{
    for (int i = 0; i < n; ++i) {
        stack_clear(&grid[i]);
    }
}

/* Test: registry sanity — every recipe has a valid output and sane shape.
 *
 * Returns: failure count.
 */
int test_recipe_registry(void)
{
    int failures = 0;
    TEST_ASSERT(recipe_count() > 0);
    for (int i = 0; i < recipe_count(); ++i) {
        const Recipe *r = recipe_at(i);
        TEST_ASSERT(r != NULL && r->id != NULL);
        if (r == NULL) {
            continue;
        }
        TEST_ASSERT(item_is_valid(r->out_item));
        TEST_ASSERT(r->out_count >= 1);
        if (r->shaped) {
            TEST_ASSERT(r->w >= 1 && r->w <= 3 && r->h >= 1 && r->h <= 3);
        } else {
            TEST_ASSERT(r->count >= 1 && r->count <= 9);
            for (int k = 0; k < r->count; ++k) {
                TEST_ASSERT(item_is_valid(r->cells[k]));
            }
        }
    }
    TEST_ASSERT(recipe_at(-1) == NULL);
    TEST_ASSERT(recipe_at(recipe_count()) == NULL);
    return failures;
}

/* Test: shapeless planks (single wood anywhere) + extra items reject.
 *
 * Returns: failure count.
 */
int test_recipe_shapeless(void)
{
    int failures = 0;
    ItemStack grid[4];
    RecipeMatch m;
    /* Wood in each corner matches planks x4. */
    const int spots[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    for (int s = 0; s < 4; ++s) {
        craft_clear(grid, 4);
        craft_set(grid, 2, spots[s][0], spots[s][1], (ItemId)BLOCK_WOOD, 1);
        TEST_ASSERT(recipe_match(grid, 2, 2, &m) == true);
        TEST_ASSERT(m.recipe->out_item == (ItemId)BLOCK_PLANKS && m.recipe->out_count == 4);
    }
    /* A second item breaks the multiset (exact match required). */
    craft_clear(grid, 4);
    craft_set(grid, 2, 0, 0, (ItemId)BLOCK_WOOD, 1);
    craft_set(grid, 2, 1, 1, (ItemId)BLOCK_WOOD, 1);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == false);
    /* Wrong item: no match. */
    craft_clear(grid, 4);
    craft_set(grid, 2, 0, 0, (ItemId)BLOCK_STONE, 1);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == false);
    /* Empty grid never matches; bad footprints rejected. */
    craft_clear(grid, 4);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == false);
    TEST_ASSERT(recipe_match(NULL, 2, 2, &m) == false);
    TEST_ASSERT(recipe_match(grid, 0, 2, &m) == false);
    TEST_ASSERT(recipe_match(grid, 2, 2, NULL) == false);
    return failures;
}

/* Test: shaped stick (vertical) + orientation strictness.
 *
 * Returns: failure count.
 */
int test_recipe_shaped(void)
{
    int failures = 0;
    ItemStack grid[4];
    RecipeMatch m;
    ItemId P = (ItemId)BLOCK_PLANKS;
    /* Vertical pair (either column) matches sticks x4. */
    craft_clear(grid, 4);
    craft_set(grid, 2, 0, 0, P, 1);
    craft_set(grid, 2, 0, 1, P, 1);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == true);
    TEST_ASSERT(m.recipe->out_item == ITEM_STICK && m.recipe->out_count == 4);
    craft_clear(grid, 4);
    craft_set(grid, 2, 1, 0, P, 1);
    craft_set(grid, 2, 1, 1, P, 1);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == true);
    /* Horizontal pair must NOT match (no mirroring/rotation). */
    craft_clear(grid, 4);
    craft_set(grid, 2, 0, 0, P, 1);
    craft_set(grid, 2, 1, 0, P, 1);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == false);
    /* Single plank is insufficient. */
    craft_clear(grid, 4);
    craft_set(grid, 2, 0, 0, P, 1);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == false);
    /* Torch: coal over stick. Stick over coal must NOT match. */
    craft_clear(grid, 4);
    craft_set(grid, 2, 0, 0, ITEM_COAL, 1);
    craft_set(grid, 2, 0, 1, ITEM_STICK, 1);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == true);
    TEST_ASSERT(m.recipe->out_item == (ItemId)BLOCK_TORCH && m.recipe->out_count == 4);
    craft_clear(grid, 4);
    craft_set(grid, 2, 0, 0, ITEM_STICK, 1);
    craft_set(grid, 2, 0, 1, ITEM_COAL, 1);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == false);
    return failures;
}

/* Test: 2x2 workbench + 3x3 pick (offset search in the bench grid).
 *
 * Returns: failure count.
 */
int test_recipe_bench(void)
{
    int failures = 0;
    ItemStack small[4];
    RecipeMatch m;
    ItemId P = (ItemId)BLOCK_PLANKS;
    /* Full 2x2 planks -> workbench. */
    for (int i = 0; i < 4; ++i) {
        small[i].item = P;
        small[i].count = 1;
        small[i].durability = 0;
    }
    TEST_ASSERT(recipe_match(small, 2, 2, &m) == true);
    TEST_ASSERT(m.recipe->out_item == (ItemId)BLOCK_WORKBENCH && m.recipe->out_count == 1);
    /* 3-wide pick pattern cannot fit a 2x2 grid. */
    ItemStack big[9];
    craft_clear(big, 9);
    craft_set(big, 3, 0, 0, P, 1);
    craft_set(big, 3, 1, 0, P, 1);
    craft_set(big, 3, 2, 0, P, 1);
    craft_set(big, 3, 1, 1, ITEM_STICK, 1);
    craft_set(big, 3, 1, 2, ITEM_STICK, 1);
    TEST_ASSERT(recipe_match(big, 3, 3, &m) == true);
    TEST_ASSERT(m.recipe->out_item == ITEM_WOOD_PICKAXE && m.recipe->out_count == 1);
    /* Mirrored axe (flipped head) must NOT match. */
    craft_clear(big, 9);
    craft_set(big, 3, 1, 0, P, 1);
    craft_set(big, 3, 2, 0, P, 1);
    craft_set(big, 3, 2, 1, P, 1);
    craft_set(big, 3, 1, 1, ITEM_STICK, 1);
    craft_set(big, 3, 1, 2, ITEM_STICK, 1);
    TEST_ASSERT(recipe_match(big, 3, 3, &m) == false);
    /* 1x2 stick recipe works at a 3x3 offset (right column). */
    craft_clear(big, 9);
    craft_set(big, 3, 2, 1, P, 1);
    craft_set(big, 3, 2, 2, P, 1);
    TEST_ASSERT(recipe_match(big, 3, 3, &m) == true);
    TEST_ASSERT(m.recipe->out_item == ITEM_STICK);
    TEST_ASSERT(m.ox == 2 && m.oy == 1);

    /* Swords use the 3-tall vertical tool pattern. */
    craft_clear(big, 9);
    craft_set(big, 3, 1, 0, P, 1);
    craft_set(big, 3, 1, 1, P, 1);
    craft_set(big, 3, 1, 2, ITEM_STICK, 1);
    TEST_ASSERT(recipe_match(big, 3, 3, &m));
    TEST_ASSERT(m.recipe->out_item == ITEM_WOOD_SWORD && m.ox == 1 && m.oy == 0);
    recipe_consume(big, 3, 3, &m);
    TEST_ASSERT(recipe_match(big, 3, 3, &m) == false);
    craft_set(big, 3, 1, 0, (ItemId)BLOCK_STONE, 1);
    craft_set(big, 3, 1, 1, (ItemId)BLOCK_STONE, 1);
    craft_set(big, 3, 1, 2, ITEM_STICK, 1);
    TEST_ASSERT(recipe_match(big, 3, 3, &m));
    TEST_ASSERT(m.recipe->out_item == ITEM_STONE_SWORD);
    return failures;
}

/* Test: consumption removes exactly one per ingredient cell; repeated
 * crafting drains the grid; full-output accounting is caller-side.
 *
 * Returns: failure count.
 */
int test_recipe_consume(void)
{
    int failures = 0;
    ItemStack grid[4];
    RecipeMatch m;
    ItemId P = (ItemId)BLOCK_PLANKS;
    /* 4 planks -> two consecutive stick crafts (4 sticks each). */
    craft_clear(grid, 4);
    craft_set(grid, 2, 0, 0, P, 2);
    craft_set(grid, 2, 0, 1, P, 2);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == true);
    recipe_consume(grid, 2, 2, &m);
    TEST_ASSERT(grid[0].count == 1 && grid[2].count == 1);
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == true);
    recipe_consume(grid, 2, 2, &m);
    TEST_ASSERT(stack_is_empty(&grid[0]) && stack_is_empty(&grid[2]));
    TEST_ASSERT(recipe_match(grid, 2, 2, &m) == false);
    /* Consume is a safe no-op on bad args. */
    recipe_consume(NULL, 2, 2, &m);
    recipe_consume(grid, 0, 2, &m);
    RecipeMatch bad = {NULL, 0, 0};
    recipe_consume(grid, 2, 2, &bad);
    return failures;
}

/* Test: new tools start full; uses decrement; final use breaks the stack.
 *
 * Returns: failure count.
 */
int test_durability_use(void)
{
    int failures = 0;
    ItemStack tool = {ITEM_WOOD_PICKAXE, 1, 0};
    TEST_ASSERT(stack_uses_left(&tool) == 64);
    for (int i = 0; i < 63; ++i) {
        TEST_ASSERT(stack_use_tool(&tool) == false);
    }
    TEST_ASSERT(stack_uses_left(&tool) == 1);
    TEST_ASSERT(stack_use_tool(&tool) == true);
    TEST_ASSERT(stack_is_empty(&tool));
    TEST_ASSERT(stack_uses_left(&tool) == 0);
    /* Non-tools and empties never wear. */
    ItemStack stone = {(ItemId)BLOCK_STONE, 10, 0};
    TEST_ASSERT(stack_use_tool(&stone) == false);
    TEST_ASSERT(stone.count == 10);
    TEST_ASSERT(stack_uses_left(&stone) == 0);
    ItemStack empty = {ITEM_NONE, 0, 0};
    TEST_ASSERT(stack_use_tool(&empty) == false);
    TEST_ASSERT(stack_use_tool(NULL) == false);
    TEST_ASSERT(stack_uses_left(NULL) == 0);

    /* Successful melee hits wear swords once, other tools twice, and do not
     * wear bows or ordinary block stacks. */
    ItemStack sword = {ITEM_WOOD_SWORD, 1, 0};
    TEST_ASSERT(stack_use_melee_hit(&sword) == false && sword.durability == 1);
    sword.durability = 58;
    TEST_ASSERT(stack_use_melee_hit(&sword) == true && stack_is_empty(&sword));
    ItemStack stone_sword = {ITEM_STONE_SWORD, 1, 130};
    TEST_ASSERT(stack_use_melee_hit(&stone_sword) == true && stack_is_empty(&stone_sword));
    ItemStack pick = {ITEM_WOOD_PICKAXE, 1, 0};
    TEST_ASSERT(stack_use_melee_hit(&pick) == false && pick.durability == 2);
    ItemStack bow = {ITEM_BOW, 1, 0};
    TEST_ASSERT(stack_use_melee_hit(&bow) == false && bow.durability == 0);
    TEST_ASSERT(stack_use_melee_hit(&stone) == false && stone.count == 10);
    TEST_ASSERT(stack_use_melee_hit(NULL) == false);
    return failures;
}

/* Test: wear blocks merging (no repair-by-stacking); split keeps wear.
 *
 * Returns: failure count.
 */
int test_durability_merge(void)
{
    int failures = 0;
    ItemStack a = {ITEM_WOOD_AXE, 1, 0};
    ItemStack b = {ITEM_WOOD_AXE, 1, 5};
    TEST_ASSERT(stack_can_merge(&a, &b) == false);
    ItemStack c = {ITEM_WOOD_AXE, 1, 5};
    ItemStack d = {ITEM_WOOD_AXE, 1, 5};
    /* max_stack 1 tools never merge (dst full), but wear must not crash it. */
    TEST_ASSERT(stack_add(&c, &d) == 0 && d.count == 1);
    /* Split preserves wear on both halves. */
    ItemStack pile = {(ItemId)BLOCK_DIRT, 10, 3};
    ItemStack out = {ITEM_NONE, 0, 0};
    stack_split_half(&pile, &out);
    TEST_ASSERT(out.count == 5 && out.durability == 3);
    TEST_ASSERT(pile.count == 5 && pile.durability == 3);
    /* Sanitize: non-damageable wear zeroed, excess wear clamped to max. */
    ItemStack bad = {(ItemId)BLOCK_STONE, 4, 9};
    ItemStack worn = {ITEM_STONE_SHOVEL, 1, 900};
    Inventory inv;
    inv_init(&inv);
    inv.slots[0] = bad;
    inv.slots[1] = worn;
    inv_sanitize(&inv);
    TEST_ASSERT(inv.slots[0].durability == 0 && inv.slots[0].count == 4);
    TEST_ASSERT(inv.slots[1].durability == 160 && inv.slots[1].count == 1);
    return failures;
}

/* Test: tool wear survives save/load; M6 files load tools at full.
 *
 * Returns: failure count.
 */
int test_durability_save(void)
{
    int failures = 0;
    const char *dir = "test_tmp_m6/wear";
    Player pl;
    player_init(&pl);
    pl.inv.slots[0].item = ITEM_WOOD_PICKAXE;
    pl.inv.slots[0].count = 1;
    pl.inv.slots[0].durability = 63; /* One use left. */
    pl.inv.slots[1].item = ITEM_STONE_AXE;
    pl.inv.slots[1].count = 1;
    pl.inv.slots[1].durability = 0; /* Fresh: omitted from invdur. */
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    w->seed = 11L;
    memcpy(w->name, "Wear", 5);
    w->mode = 0;
    Vec3 spawn = mmath_vec3(0.0f, 80.0f, 0.0f);
    TEST_ASSERT(world_save_all(dir, w, &pl, spawn, false, 0.5f) == 0);
    world_destroy(w);
    WorldMeta m;
    TEST_ASSERT(world_meta_read(dir, &m) == 0);
    TEST_ASSERT(m.inv_items[0] == ITEM_WOOD_PICKAXE && m.inv_dur[0] == 63);
    TEST_ASSERT(m.inv_items[1] == ITEM_STONE_AXE && m.inv_dur[1] == 0);
    /* Corrupt wear clamps on sanitize (never deletes the tool). */
    m.inv_dur[0] = 60000;
    Player back;
    player_init(&back);
    for (int i = 0; i < WORLD_META_INV_SLOTS && i < INV_SIZE; ++i) {
        back.inv.slots[i].item = m.inv_items[i];
        back.inv.slots[i].count = m.inv_counts[i];
        back.inv.slots[i].durability = m.inv_dur[i];
    }
    inv_sanitize(&back.inv);
    TEST_ASSERT(back.inv.slots[0].item == ITEM_WOOD_PICKAXE && back.inv.slots[0].durability == 64);
    char meta[PATH_MAX_LEN];
    if (path_join(meta, sizeof(meta), dir, WORLD_META_FILE) == 0) {
        path_remove_file(meta);
    }
    path_remove_dir(dir);
    path_remove_dir("test_tmp_m6");
    /* M6-era buffer (no invdur key): tools load at full durability. */
    static const char m6_meta[] =
        "format_version=1\nworld_name=Old\nseed=3\ngame_mode=survival\n"
        "player_x=1.0\nplayer_y=65.0\nplayer_z=2.0\n"
        "inv=0:200:1\n";
    WorldMeta old;
    TEST_ASSERT(world_meta_parse(m6_meta, &old) == 0);
    TEST_ASSERT(old.inv_items[0] == ITEM_WOOD_PICKAXE && old.inv_dur[0] == 0);
    return failures;
}

/* Test: food values, eat completion, interruption, full-hunger deny.
 *
 * Returns: failure count.
 */
int test_food_eat(void)
{
    int failures = 0;
    TEST_ASSERT(item_is_edible(ITEM_APPLE) == true);
    TEST_ASSERT(item_get_info(ITEM_APPLE)->food == 4);
    TEST_ASSERT(item_is_edible((ItemId)BLOCK_STONE) == false);
    TEST_ASSERT(item_is_edible(ITEM_WOOD_PICKAXE) == false);
    TEST_ASSERT(item_is_edible(ITEM_NONE) == false);

    Player p;
    player_init(&p);
    p.inv.slots[0].item = ITEM_APPLE;
    p.inv.slots[0].count = 3;
    p.hunger = 10.0f;
    /* Hold to start, chew through 1.61 s (MC), complete: exactly one eaten. */
    TEST_ASSERT(survival_eat_update(&p, true, 0.0f) == SURVIVAL_EAT_NONE);
    TEST_ASSERT(p.eat_active == true);
    TEST_ASSERT(survival_eat_update(&p, true, 0.8f) == SURVIVAL_EAT_NONE);
    TEST_ASSERT(survival_eat_update(&p, true, 0.81f) == SURVIVAL_EAT_DONE);
    TEST_ASSERT(p.inv.slots[0].count == 2);
    TEST_ASSERT_FLOAT_EQ(p.hunger, 14.0f, 1e-4f);
    /* Release interrupts with nothing consumed. */
    TEST_ASSERT(survival_eat_update(&p, true, 0.0f) == SURVIVAL_EAT_NONE);
    TEST_ASSERT(survival_eat_update(&p, true, 0.5f) == SURVIVAL_EAT_NONE);
    TEST_ASSERT(survival_eat_update(&p, false, 0.5f) == SURVIVAL_EAT_NONE);
    TEST_ASSERT(p.eat_active == false);
    TEST_ASSERT(p.inv.slots[0].count == 2);
    /* Slot switch interrupts. */
    p.hotbar_sel = 1;
    TEST_ASSERT(survival_eat_update(&p, true, 0.0f) == SURVIVAL_EAT_NONE);
    TEST_ASSERT(p.eat_active == false);
    /* Full hunger: never starts (no waste). */
    p.hotbar_sel = 0;
    p.hunger = 20.0f;
    TEST_ASSERT(survival_eat_update(&p, true, 5.0f) == SURVIVAL_EAT_NONE);
    TEST_ASSERT(p.eat_active == false);
    TEST_ASSERT(p.inv.slots[0].count == 2);
    /* Clamp: 19 + 4 caps at 20. */
    p.hunger = 19.0f;
    TEST_ASSERT(survival_eat_update(&p, true, 0.0f) == SURVIVAL_EAT_NONE);
    TEST_ASSERT(survival_eat_update(&p, true, 2.0f) == SURVIVAL_EAT_DONE);
    TEST_ASSERT_FLOAT_EQ(p.hunger, 20.0f, 1e-4f);
    TEST_ASSERT(p.inv.slots[0].count == 1);
    /* Dead and creative players never eat. */
    p.dead = true;
    TEST_ASSERT(survival_eat_update(&p, true, 5.0f) == SURVIVAL_EAT_NONE);
    p.dead = false;
    p.mode = 1;
    TEST_ASSERT(survival_eat_update(&p, true, 5.0f) == SURVIVAL_EAT_NONE);
    TEST_ASSERT(survival_eat_update(NULL, true, 1.0f) == SURVIVAL_EAT_NONE);
    return failures;
}

/* Test: exhaustion rates, cap, burn, gating.
 *
 * Returns: failure count.
 */
int test_exhaustion(void)
{
    int failures = 0;
    Player p;
    player_init(&p);
    survival_add_exhaustion(&p, 1.5f);
    TEST_ASSERT_FLOAT_EQ(p.exhaustion, 1.5f, 1e-4f);
    /* Cap clamps (never unbounded). */
    survival_add_exhaustion(&p, 100.0f);
    TEST_ASSERT_FLOAT_EQ(p.exhaustion, SURVIVAL_EXHAUST_CAP, 1e-4f);
    /* Non-positive ignored; dead/creative gated. */
    survival_add_exhaustion(&p, -5.0f);
    TEST_ASSERT_FLOAT_EQ(p.exhaustion, SURVIVAL_EXHAUST_CAP, 1e-4f);
    p.dead = true;
    survival_add_exhaustion(&p, 1.0f);
    TEST_ASSERT_FLOAT_EQ(p.exhaustion, SURVIVAL_EXHAUST_CAP, 1e-4f);
    p.dead = false;
    p.mode = 1;
    p.exhaustion = 0.0f;
    survival_add_exhaustion(&p, 1.0f);
    TEST_ASSERT_FLOAT_EQ(p.exhaustion, 0.0f, 1e-4f);
    p.mode = 0;
    survival_add_exhaustion(NULL, 1.0f);
    /* Regen tick costs strain (net drain while healing). */
    player_init(&p);
    p.health = 10.0f;
    p.hunger = 20.0f;
    survival_hunger_update(&p, SURVIVAL_REGEN_PERIOD);
    TEST_ASSERT_FLOAT_EQ(p.exhaustion, SURVIVAL_EXHAUST_REGEN, 1e-4f);
    return failures;
}

/* Test: deterministic leaf apple bonus (stable per cell, seed-varying).
 *
 * Returns: failure count.
 */
int test_leaf_bonus(void)
{
    int failures = 0;
    ItemStack out;
    /* Non-leaves never bonus. */
    TEST_ASSERT(survival_bonus_drop(42L, (uint16_t)BLOCK_STONE, 0, 64, 0, &out) == false);
    TEST_ASSERT(stack_is_empty(&out));
    TEST_ASSERT(survival_bonus_drop(42L, (uint16_t)BLOCK_LEAVES, 0, 64, 0, NULL) == false);
    /* Find one hitting and one missing cell (deterministic search). */
    int hx = -1, mx = -1;
    for (int x = 0; x < 64 && (hx < 0 || mx < 0); ++x) {
        ItemStack probe;
        if (survival_bonus_drop(42L, (uint16_t)BLOCK_LEAVES, x, 64, 0, &probe)) {
            if (hx < 0) {
                hx = x;
            }
        } else if (mx < 0) {
            mx = x;
        }
    }
    TEST_ASSERT(hx >= 0 && mx >= 0);
    /* Hit is stable across calls and yields exactly one apple. */
    ItemStack a;
    ItemStack b;
    TEST_ASSERT(survival_bonus_drop(42L, (uint16_t)BLOCK_LEAVES, hx, 64, 0, &a) == true);
    TEST_ASSERT(survival_bonus_drop(42L, (uint16_t)BLOCK_LEAVES, hx, 64, 0, &b) == true);
    TEST_ASSERT(a.item == ITEM_APPLE && a.count == 1);
    TEST_ASSERT(b.item == ITEM_APPLE && b.count == 1);
    TEST_ASSERT(survival_bonus_drop(42L, (uint16_t)BLOCK_LEAVES, mx, 64, 0, &a) == false);
    return failures;
}
