#pragma once

/* ItemStack + Inventory (M6): validated stack arithmetic and a 36-slot
 * player inventory (slots 0..8 are the hotbar). All operations preserve
 * the canonical empty {ITEM_NONE, 0} and per-item max stacks; invalid
 * states are rejected, never created. Pure CPU, headless-testable.
 */

#include "game/item.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Inventory layout (no magic numbers at call sites). */
#define INV_SIZE 36
#define INV_HOTBAR 9
#define INV_MAIN_START 9

/* One stack: canonical empty is {ITEM_NONE, 0, 0}. Durability is wear
 * (uses consumed): 0 reads as brand-new, and a damageable tool breaks
 * when wear reaches its max_durability. Non-damageable stacks always
 * carry 0 (enforced by sanitize); wear never affects max stacks.
 */
typedef struct ItemStack {
    ItemId item;       /* ItemId (ITEM_NONE when empty). */
    uint16_t count;    /* 0 when empty, else 1..max_stack. */
    uint16_t durability; /* Tool wear (uses consumed), 0 when new/unused. */
} ItemStack;

/* Player inventory: slots[0..8] hotbar, slots[9..35] main storage. */
typedef struct Inventory {
    ItemStack slots[INV_SIZE];
} Inventory;

/* True when the stack is canonically empty.
 *
 * Args:
 *   s: stack (must not be NULL).
 *
 * Returns: true when item == NONE (count is forced to 0 by all ops).
 */
bool stack_is_empty(const ItemStack *s);

/* Reset to canonical empty (NULL-safe no-op).
 *
 * Args:
 *   s: stack to clear (may be NULL).
 */
void stack_clear(ItemStack *s);

/* Check whether src can merge into dst (same item, same wear, dst not
 * full). Wear must match so a damaged tool can never repair by merging.
 *
 * Args:
 *   dst, src: stacks (must not be NULL).
 *
 * Returns: true when a merge would move at least one item.
 */
bool stack_can_merge(const ItemStack *dst, const ItemStack *src);

/* Move up to max-move items from src into dst (dst empty accepts any src).
 * Respects dst max stack. Both stay canonical.
 *
 * Args:
 *   dst, src: stacks (must not be NULL).
 *
 * Returns: items actually moved.
 */
uint16_t stack_add(ItemStack *dst, ItemStack *src);

/* Remove up to n items from a stack (canonicalizes emptied stacks).
 *
 * Args:
 *   s: stack (must not be NULL).
 *   n: items to remove.
 *
 * Returns: items actually removed.
 */
uint16_t stack_remove(ItemStack *s, uint16_t n);

/* Consume one tool use (wear + 1). Only for damageable, non-empty stacks;
 * swinging air or using non-tools must never call this. When wear reaches
 * max_durability the tool breaks: the slot is cleared.
 *
 * Args:
 *   s: tool stack (must not be NULL).
 *
 * Returns: true when the tool broke (slot now empty), false otherwise
 * (including empty/non-damageable input, which is a no-op).
 */
bool stack_use_tool(ItemStack *s);

/* Apply ordinary weapon/tool wear after a melee hit that actually dealt
 * damage: swords lose one use, other damageable tools lose two, bows and
 * nondamageable items lose none. Returns true only if the item broke.
 */
bool stack_use_melee_hit(ItemStack *s);

/* Remaining uses on a stack (max_durability - wear). Empty stacks and
 * non-damageable items report 0; a fresh tool reports its maximum.
 *
 * Args:
 *   s: stack (must not be NULL).
 *
 * Returns: uses left (0 when broken/empty/non-damageable).
 */
uint16_t stack_uses_left(const ItemStack *s);
/* Split off ceil(count/2) into out (out cleared first); src keeps the rest.
 * Both stay canonical. No-op when src is empty.
 *
 * Args:
 *   src: stack to split (must not be NULL).
 *   out: receives the split half (must not be NULL).
 */
void stack_split_half(ItemStack *src, ItemStack *out);

/* Zero an inventory (all slots canonical empty).
 *
 * Args:
 *   inv: inventory (must not be NULL).
 */
void inv_init(Inventory *inv);

/* Validate a slot index.
 *
 * Args:
 *   slot: candidate index.
 *
 * Returns: true for 0..INV_SIZE-1.
 */
bool inv_valid_slot(int slot);

/* Insert a stack: fill compatible partials first, then empty slots.
 * The input is consumed accordingly (may become empty).
 *
 * Args:
 *   inv: inventory (must not be NULL).
 *   stack: stack to insert; consumed in place (must not be NULL).
 *
 * Returns: leftover count still in *stack (0 = fully stored).
 */
uint16_t inv_insert(Inventory *inv, ItemStack *stack);

/* Remove up to n items from a slot.
 *
 * Args:
 *   inv: inventory (must not be NULL).
 *   slot: 0..35.
 *   n: items to remove.
 *
 * Returns: items actually removed (0 on bad slot/empty).
 */
uint16_t inv_remove(Inventory *inv, int slot, uint16_t n);

/* Find a merge target for item (non-full matching stack), else -1.
 *
 * Args:
 *   inv: inventory (must not be NULL).
 *   item: item ID.
 *
 * Returns: slot index or -1.
 */
int inv_find_merge(const Inventory *inv, ItemId item);

/* Find the first empty slot, else -1.
 *
 * Args:
 *   inv: inventory (must not be NULL).
 *
 * Returns: slot index or -1.
 */
int inv_find_empty(const Inventory *inv);

/* Swap two slots' contents (both stay canonical).
 *
 * Args:
 *   inv: inventory (must not be NULL).
 *   a, b: slot indices.
 *
 * Returns: 0 on success, non-zero on bad slots (a==b is a no-op success).
 */
int inv_swap(Inventory *inv, int a, int b);

/* Move stack a -> b: merge when compatible, else swap.
 *
 * Args:
 *   inv: inventory (must not be NULL).
 *   a, b: slot indices (a = source).
 */
int inv_move(Inventory *inv, int a, int b);

/* Consume up to n items from a slot (for placement/crafting later).
 *
 * Args:
 *   inv: inventory (must not be NULL).
 *   slot: 0..35.
 *   n: items to consume.
 *
 * Returns: true when n items were available and consumed.
 */
bool inv_consume(Inventory *inv, int slot, uint16_t n);

/* Count total items of one ID across the inventory.
 *
 * Args:
 *   inv: inventory (must not be NULL).
 *   item: item ID.
 *
 * Returns: total count.
 */
uint32_t inv_count_item(const Inventory *inv, ItemId item);

/* Validate + sanitize every slot (for save loading): unknown items and
 * overfull counts are dropped to canonical empty (never crash, never
 * keep ITEM_NONE with count or count > max). Wear: non-damageable stacks
 * are zeroed; damageable wear clamps to max (about-to-break, never
 * deleted by sanitize).
 *
 * Args:
 *   inv: inventory (must not be NULL).
 */
void inv_sanitize(Inventory *inv);
