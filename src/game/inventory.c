#include "game/inventory.h"

#include <string.h>

/* True when canonically empty. */
bool stack_is_empty(const ItemStack *s)
{
    return s == NULL || s->item == ITEM_NONE || s->count == 0;
}

/* Reset to canonical empty. */
void stack_clear(ItemStack *s)
{
    if (s == NULL) {
        return;
    }
    s->item = ITEM_NONE;
    s->count = 0;
    s->durability = 0;
}

/* Merge possible when both non-empty, same item, same wear, dst not full. */
bool stack_can_merge(const ItemStack *dst, const ItemStack *src)
{
    if (dst == NULL || src == NULL) {
        return false;
    }
    if (stack_is_empty(dst) || stack_is_empty(src)) {
        return false;
    }
    if (dst->item != src->item) {
        return false;
    }
    if (dst->durability != src->durability) {
        return false;
    }
    return dst->count < item_get_info(dst->item)->max_stack;
}

/* Move items src -> dst (dst empty accepts anything valid). */
uint16_t stack_add(ItemStack *dst, ItemStack *src)
{
    if (dst == NULL || src == NULL || stack_is_empty(src)) {
        return 0;
    }
    if (!item_is_valid(src->item)) {
        stack_clear(src);
        return 0;
    }
    if (stack_is_empty(dst)) {
        const ItemInfo *info = item_get_info(src->item);
        uint16_t take = src->count;
        if (take > info->max_stack) {
            take = info->max_stack;
        }
        dst->item = src->item;
        dst->count = take;
        dst->durability = src->durability;
        if (take >= src->count) {
            stack_clear(src);
        } else {
            src->count -= take;
        }
        return take;
    }
    if (dst->item != src->item || dst->durability != src->durability) {
        return 0;
    }
    uint16_t room = 0;
    uint16_t max = item_get_info(dst->item)->max_stack;
    if (dst->count < max) {
        room = (uint16_t)(max - dst->count);
    }
    uint16_t take = src->count < room ? src->count : room;
    dst->count = (uint16_t)(dst->count + take);
    if (take >= src->count) {
        stack_clear(src);
    } else {
        src->count = (uint16_t)(src->count - take);
    }
    return take;
}

/* Remove up to n items. */
uint16_t stack_remove(ItemStack *s, uint16_t n)
{
    if (s == NULL || stack_is_empty(s) || n == 0) {
        return 0;
    }
    uint16_t take = s->count < n ? s->count : n;
    s->count = (uint16_t)(s->count - take);
    if (s->count == 0) {
        stack_clear(s);
    }
    return take;
}

/* Split ceil half into out. */
void stack_split_half(ItemStack *src, ItemStack *out)
{
    if (src == NULL || out == NULL) {
        return;
    }
    stack_clear(out);
    if (stack_is_empty(src)) {
        return;
    }
    uint16_t half = (uint16_t)((src->count + 1) / 2);
    out->item = src->item;
    out->count = half;
    out->durability = src->durability;
    src->count = (uint16_t)(src->count - half);
    if (src->count == 0) {
        stack_clear(src);
    }
}

/* Zero an inventory. */
void inv_init(Inventory *inv)
{
    if (inv == NULL) {
        return;
    }
    for (int i = 0; i < INV_SIZE; ++i) {
        stack_clear(&inv->slots[i]);
    }
}

/* Slot bounds check. */
bool inv_valid_slot(int slot)
{
    return slot >= 0 && slot < INV_SIZE;
}

/* Insert: partials first, then empties; leftovers stay in *stack. */
uint16_t inv_insert(Inventory *inv, ItemStack *stack)
{
    if (inv == NULL || stack == NULL || stack_is_empty(stack)) {
        return stack != NULL ? (stack_is_empty(stack) ? 0 : stack->count) : 0;
    }
    if (!item_is_valid(stack->item)) {
        stack_clear(stack);
        return 0;
    }
    for (int i = 0; i < INV_SIZE && !stack_is_empty(stack); ++i) {
        if (!stack_is_empty(&inv->slots[i]) && inv->slots[i].item == stack->item) {
            stack_add(&inv->slots[i], stack);
        }
    }
    for (int i = 0; i < INV_SIZE && !stack_is_empty(stack); ++i) {
        if (stack_is_empty(&inv->slots[i])) {
            stack_add(&inv->slots[i], stack);
        }
    }
    return stack->count;
}

/* Consume one tool use; break (clear) at max wear. */
bool stack_use_tool(ItemStack *s)
{
    if (s == NULL || stack_is_empty(s)) {
        return false;
    }
    const ItemInfo *info = item_get_info(s->item);
    if (info->max_durability == 0) {
        return false;
    }
    if (s->durability >= info->max_durability) {
        stack_clear(s); /* Already spent (corrupt save): remove, report break. */
        return true;
    }
    s->durability++;
    if (s->durability >= info->max_durability) {
        stack_clear(s);
        return true;
    }
    return false;
}

/* Remaining uses (0 for empty/non-damageable/broken). */
uint16_t stack_uses_left(const ItemStack *s)
{
    if (s == NULL || stack_is_empty(s)) {
        return 0;
    }
    const ItemInfo *info = item_get_info(s->item);
    if (info->max_durability == 0 || s->durability >= info->max_durability) {
        return 0;
    }
    return (uint16_t)(info->max_durability - s->durability);
}

/* Remove from a slot. */
uint16_t inv_remove(Inventory *inv, int slot, uint16_t n)
{
    if (inv == NULL || !inv_valid_slot(slot)) {
        return 0;
    }
    return stack_remove(&inv->slots[slot], n);
}

/* Find merge target. */
int inv_find_merge(const Inventory *inv, ItemId item)
{
    if (inv == NULL || !item_is_valid(item)) {
        return -1;
    }
    uint16_t max = item_get_info(item)->max_stack;
    for (int i = 0; i < INV_SIZE; ++i) {
        if (!stack_is_empty(&inv->slots[i]) && inv->slots[i].item == item && inv->slots[i].count < max) {
            return i;
        }
    }
    return -1;
}

/* Find first empty slot. */
int inv_find_empty(const Inventory *inv)
{
    if (inv == NULL) {
        return -1;
    }
    for (int i = 0; i < INV_SIZE; ++i) {
        if (stack_is_empty(&inv->slots[i])) {
            return i;
        }
    }
    return -1;
}

/* Swap two slots. */
int inv_swap(Inventory *inv, int a, int b)
{
    if (inv == NULL || !inv_valid_slot(a) || !inv_valid_slot(b)) {
        return -1;
    }
    if (a == b) {
        return 0;
    }
    ItemStack tmp = inv->slots[a];
    inv->slots[a] = inv->slots[b];
    inv->slots[b] = tmp;
    return 0;
}

/* Move a -> b: merge when compatible, else swap. */
int inv_move(Inventory *inv, int a, int b)
{
    if (inv == NULL || !inv_valid_slot(a) || !inv_valid_slot(b)) {
        return -1;
    }
    if (a == b) {
        return 0;
    }
    ItemStack *sa = &inv->slots[a];
    ItemStack *sb = &inv->slots[b];
    if (stack_is_empty(sa)) {
        return 0;
    }
    if (stack_is_empty(sb) || (sb->item == sa->item)) {
        stack_add(sb, sa);
        return 0;
    }
    return inv_swap(inv, a, b);
}

/* Consume n items from a slot (all-or-nothing). */
bool inv_consume(Inventory *inv, int slot, uint16_t n)
{
    if (inv == NULL || !inv_valid_slot(slot) || n == 0) {
        return false;
    }
    ItemStack *s = &inv->slots[slot];
    if (stack_is_empty(s) || s->count < n) {
        return false;
    }
    stack_remove(s, n);
    return true;
}

/* Total count of one item. */
uint32_t inv_count_item(const Inventory *inv, ItemId item)
{
    if (inv == NULL) {
        return 0;
    }
    uint32_t total = 0;
    for (int i = 0; i < INV_SIZE; ++i) {
        if (!stack_is_empty(&inv->slots[i]) && inv->slots[i].item == item) {
            total += inv->slots[i].count;
        }
    }
    return total;
}

/* Sanitize every slot (for save loading). */
void inv_sanitize(Inventory *inv)
{
    if (inv == NULL) {
        return;
    }
    for (int i = 0; i < INV_SIZE; ++i) {
        ItemStack *s = &inv->slots[i];
        if (s->item == ITEM_NONE || s->count == 0) {
            stack_clear(s);
            continue;
        }
        if (!item_is_valid(s->item)) {
            stack_clear(s);
            continue;
        }
        uint16_t max = item_get_info(s->item)->max_stack;
        if (s->count > max) {
            s->count = max;
        }
        /* Wear rules: non-damageable stacks never carry wear; damageable
         * wear clamps to max (about-to-break, never deleted by sanitize). */
        if (item_get_info(s->item)->max_durability == 0) {
            s->durability = 0;
        } else if (s->durability > item_get_info(s->item)->max_durability) {
            s->durability = item_get_info(s->item)->max_durability;
        }
    }
}
