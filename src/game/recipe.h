#pragma once

/* Crafting recipe registry (M7): data-driven shaped + shapeless recipes.
 * Matching is pure (grid in, match out) so it is independently testable
 * without UI or OpenGL; consumption and output routing live with the
 * caller (screens/app), which owns inventories and the cursor.
 *
 * Shaped rules: a recipe pattern matches at any offset inside the craft
 * grid; every non-covered cell must be empty. Patterns are NOT mirrored:
 * an asymmetric pattern only matches the orientation written here.
 * Shapeless rules: the non-empty grid cells must equal the ingredient
 * multiset in any order (duplicates require distinct items).
 */

#include "game/inventory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Maximum pattern/grid footprint (3x3 bench; 2x2 player grid is a subset). */
#define RECIPE_MAX_CELLS 9

/* One recipe definition (static storage, do not free). */
typedef struct Recipe {
    const char *id;          /* Stable ID, e.g. "stick" (never NULL). */
    bool shaped;             /* True: positional pattern; false: multiset. */
    uint8_t w, h;            /* Shaped footprint (1..3 each); else 0. */
    ItemId cells[RECIPE_MAX_CELLS]; /* Shaped: row-major w*h pattern
                                     * (ITEM_NONE = required empty).
                                     * Shapeless: cells[0..count) are the
                                     * required ingredients. */
    uint8_t count;           /* Shapeless ingredient count (0 when shaped). */
    ItemId out_item;         /* Result item (always valid). */
    uint16_t out_count;      /* Result count (>= 1). */
} Recipe;

/* A successful match: which recipe matched and, for shaped recipes, the
 * grid offset of the pattern origin (for consumption).
 */
typedef struct RecipeMatch {
    const Recipe *recipe; /* Matched recipe (never NULL on success). */
    int ox, oy;           /* Pattern origin offset in the grid. */
} RecipeMatch;

/* Number of registered recipes.
 *
 * Returns: recipe count (> 0).
 */
int recipe_count(void);

/* Recipe by index (NULL on out-of-range; for tests/inspection).
 *
 * Args:
 *   index: 0..recipe_count()-1.
 *
 * Returns: recipe or NULL.
 */
const Recipe *recipe_at(int index);

/* Match a craft grid against the registry (first registered match wins).
 * Empty grids never match. Every non-empty cell must hold count >= 1
 * (wear is ignored: damaged ingredients are accepted).
 *
 * Args:
 *   grid: row-major gw*gh stacks (must not be NULL).
 *   gw, gh: grid footprint, 1..3 each.
 *   out: receives the match (must not be NULL).
 *
 * Returns: true on match (out filled), false otherwise.
 */
bool recipe_match(const ItemStack *grid, int gw, int gh, RecipeMatch *out);

/* Consume one craft operation's ingredients from the grid (one item per
 * ingredient cell of a prior match). The grid must still match m (call
 * recipe_match again after any grid change). No-op on bad args.
 *
 * Args:
 *   grid: row-major gw*gh stacks (must not be NULL).
 *   gw, gh: grid footprint (must equal the matched footprint).
 *   m: match from recipe_match (must not be NULL).
 */
void recipe_consume(ItemStack *grid, int gw, int gh, const RecipeMatch *m);
