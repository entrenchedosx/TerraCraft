#include "game/recipe.h"

#include "game/item.h"
#include "world/block.h"

#include <stddef.h>

/* Shaped pattern legend: W = wood log, P = planks, S = stick, C = coal,
 * T = stone, . = required empty. 2-wide/tall patterns also fit the 2x2
 * player grid; 3-wide/tall patterns need the workbench. Bow limbs are
 * planks (no string in the game: an original TerraCraft recipe); arrows
 * are coal-tipped hunting shafts (no flint/feather either).
 */
#define W ((ItemId)BLOCK_WOOD)
#define P ((ItemId)BLOCK_PLANKS)
#define S ((ItemId)ITEM_STICK)
#define C ((ItemId)ITEM_COAL)
#define T ((ItemId)BLOCK_STONE)
#define I ((ItemId)ITEM_IRON_INGOT)
#define D ((ItemId)ITEM_DIAMOND)
#define N ((ItemId)ITEM_NONE)

/* Registry (first match wins: list specific patterns before general ones;
 * all outputs are valid items with count >= 1).
 */
static const Recipe RECIPES[] = {
    /* 2x2-grid recipes (also craftable on the bench by offset). */
    {"planks", false, 0, 0, {W, N, N, N, N, N, N, N, N}, 1, (ItemId)BLOCK_PLANKS, 4},
    {"stick", true, 1, 2, {P, P, N, N, N, N, N, N, N}, 0, ITEM_STICK, 4},
    {"torch", true, 1, 2, {C, S, N, N, N, N, N, N, N}, 0, (ItemId)BLOCK_TORCH, 4},
    {"workbench", true, 2, 2, {P, P, P, P, N, N, N, N, N}, 0, (ItemId)BLOCK_WORKBENCH, 1},
    {"wood_shovel_2", true, 1, 2, {P, S, N, N, N, N, N, N, N}, 0, ITEM_WOOD_SHOVEL, 1},
    {"stone_shovel_2", true, 1, 2, {T, S, N, N, N, N, N, N, N}, 0, ITEM_STONE_SHOVEL, 1},
    /* Bench-only 3-wide/tall tool patterns (asymmetric: no mirroring). */
    {"wood_pick", true, 3, 3, {P, P, P, N, S, N, N, S, N}, 0, ITEM_WOOD_PICKAXE, 1},
    {"wood_axe", true, 3, 3, {P, P, N, P, S, N, N, S, N}, 0, ITEM_WOOD_AXE, 1},
    {"wood_shovel", true, 3, 3, {N, P, N, N, S, N, N, S, N}, 0, ITEM_WOOD_SHOVEL, 1},
    {"stone_pick", true, 3, 3, {T, T, T, N, S, N, N, S, N}, 0, ITEM_STONE_PICKAXE, 1},
    {"stone_axe", true, 3, 3, {T, T, N, T, S, N, N, S, N}, 0, ITEM_STONE_AXE, 1},
    {"stone_shovel", true, 3, 3, {N, T, N, N, S, N, N, S, N}, 0, ITEM_STONE_SHOVEL, 1},
    {"wood_sword", true, 1, 3, {P, P, S, N, N, N, N, N, N}, 0, ITEM_WOOD_SWORD, 1},
    {"stone_sword", true, 1, 3, {T, T, S, N, N, N, N, N, N}, 0, ITEM_STONE_SWORD, 1},
    /* M9 ranged gear (bench-only): bow limbs + coal-tipped arrows
     * (shaped 3-tall so the 2x2 player grid never collides). */
    {"bow", true, 3, 3, {P, S, N, P, N, S, P, S, N}, 0, ITEM_BOW, 1},
    {"arrow", true, 1, 3, {C, S, S, N, N, N, N, N, N}, 0, ITEM_ARROW, 4},
    /* Iron and diamond gear: same silhouettes as the stone/wood patterns,
     * swapped to refined materials. Diamond ore needs an iron pickaxe, so
     * these recipes sit behind the iron tier in the progression. */
    {"iron_pick", true, 3, 3, {I, I, I, N, S, N, N, S, N}, 0, ITEM_IRON_PICKAXE, 1},
    {"iron_axe", true, 3, 3, {I, I, N, I, S, N, N, S, N}, 0, ITEM_IRON_AXE, 1},
    {"iron_shovel", true, 3, 3, {N, I, N, N, S, N, N, S, N}, 0, ITEM_IRON_SHOVEL, 1},
    {"iron_sword", true, 1, 3, {I, I, S, N, N, N, N, N, N}, 0, ITEM_IRON_SWORD, 1},
    {"diamond_pick", true, 3, 3, {D, D, D, N, S, N, N, S, N}, 0, ITEM_DIAMOND_PICKAXE, 1},
    {"diamond_axe", true, 3, 3, {D, D, N, D, S, N, N, S, N}, 0, ITEM_DIAMOND_AXE, 1},
    {"diamond_shovel", true, 3, 3, {N, D, N, N, S, N, N, S, N}, 0, ITEM_DIAMOND_SHOVEL, 1},
    {"diamond_sword", true, 1, 3, {D, D, S, N, N, N, N, N, N}, 0, ITEM_DIAMOND_SWORD, 1},
};
#define RECIPE_COUNT (sizeof(RECIPES) / sizeof(RECIPES[0]))

/* Registry size. */
int recipe_count(void)
{
    return (int)RECIPE_COUNT;
}

/* Recipe by index. */
const Recipe *recipe_at(int index)
{
    if (index < 0 || (size_t)index >= RECIPE_COUNT) {
        return NULL;
    }
    return &RECIPES[index];
}

/* True when a grid cell holds a usable ingredient (non-empty). */
static bool cell_filled(const ItemStack *s)
{
    return !stack_is_empty(s);
}

/* Shaped match attempt at one offset: pattern cells must equal grid items
 * (or both be empty), and every grid cell outside the pattern must be
 * empty.
 */
static bool match_shaped_at(const Recipe *r, const ItemStack *grid, int gw, int gh, int ox, int oy)
{
    for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < gw; ++x) {
            bool in_pattern = x >= ox && x < ox + r->w && y >= oy && y < oy + r->h;
            const ItemStack *cell = &grid[(size_t)y * (size_t)gw + (size_t)x];
            if (!in_pattern) {
                if (cell_filled(cell)) {
                    return false;
                }
                continue;
            }
            ItemId want = r->cells[(size_t)(y - oy) * (size_t)r->w + (size_t)(x - ox)];
            if (want == ITEM_NONE) {
                if (cell_filled(cell)) {
                    return false;
                }
            } else if (!cell_filled(cell) || cell->item != want) {
                return false;
            }
        }
    }
    return true;
}

/* Shapeless match: grid's non-empty items must equal the multiset. */
static bool match_shapeless(const Recipe *r, const ItemStack *grid, int gw, int gh)
{
    ItemId need[RECIPE_MAX_CELLS];
    for (int i = 0; i < r->count; ++i) {
        need[i] = r->cells[i];
    }
    int remaining = r->count;
    for (int i = 0; i < gw * gh; ++i) {
        if (!cell_filled(&grid[i])) {
            continue;
        }
        bool consumed = false;
        for (int k = 0; k < remaining; ++k) {
            if (need[k] == grid[i].item) {
                need[k] = need[remaining - 1];
                --remaining;
                consumed = true;
                break;
            }
        }
        if (!consumed) {
            return false;
        }
    }
    return remaining == 0;
}

/* Match a grid (first registered recipe wins). */
bool recipe_match(const ItemStack *grid, int gw, int gh, RecipeMatch *out)
{
    if (grid == NULL || out == NULL || gw < 1 || gw > 3 || gh < 1 || gh > 3) {
        return false;
    }
    bool any = false;
    for (int i = 0; i < gw * gh; ++i) {
        if (cell_filled(&grid[i])) {
            any = true;
            break;
        }
    }
    if (!any) {
        return false;
    }
    for (size_t ri = 0; ri < RECIPE_COUNT; ++ri) {
        const Recipe *r = &RECIPES[ri];
        if (r->shaped) {
            if (r->w > gw || r->h > gh) {
                continue;
            }
            for (int oy = 0; oy + r->h <= gh; ++oy) {
                for (int ox = 0; ox + r->w <= gw; ++ox) {
                    if (match_shaped_at(r, grid, gw, gh, ox, oy)) {
                        out->recipe = r;
                        out->ox = ox;
                        out->oy = oy;
                        return true;
                    }
                }
            }
        } else if (match_shapeless(r, grid, gw, gh)) {
            out->recipe = r;
            out->ox = 0;
            out->oy = 0;
            return true;
        }
    }
    return false;
}

/* Consume one set of ingredients for a match. */
void recipe_consume(ItemStack *grid, int gw, int gh, const RecipeMatch *m)
{
    if (grid == NULL || m == NULL || m->recipe == NULL || gw < 1 || gw > 3 || gh < 1 || gh > 3) {
        return;
    }
    const Recipe *r = m->recipe;
    if (r->shaped) {
        if (r->w > gw || r->h > gh || m->ox < 0 || m->oy < 0 || m->ox + r->w > gw || m->oy + r->h > gh) {
            return;
        }
        for (int y = 0; y < r->h; ++y) {
            for (int x = 0; x < r->w; ++x) {
                if (r->cells[(size_t)y * (size_t)r->w + (size_t)x] == ITEM_NONE) {
                    continue;
                }
                ItemStack *cell = &grid[(size_t)(m->oy + y) * (size_t)gw + (size_t)(m->ox + x)];
                stack_remove(cell, 1);
            }
        }
        return;
    }
    /* Shapeless: remove one matching item per requirement, in grid order. */
    for (int k = 0; k < r->count; ++k) {
        for (int i = 0; i < gw * gh; ++i) {
            if (cell_filled(&grid[i]) && grid[i].item == r->cells[k]) {
                stack_remove(&grid[i], 1);
                break;
            }
        }
    }
}
