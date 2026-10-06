#include "game/survival.h"
#include "core/log.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/player.h"
#include "world/block.h"
#include "world/world.h"

#include <math.h>
#include <stddef.h>

/* Creative check (mode 1 == WORLD_MODE_CREATIVE, int to avoid header weight;
 * kept in sync with world_meta.h by the M6 policy note in DECISIONS.md).
 */
bool survival_is_creative(const Player *p)
{
    return p != NULL && p->mode == 1;
}

/* Break time for a block with a held item (MC formula: damage per tick
 * is dig speed / hardness / divisor, 20 ticks/s — i.e. seconds =
 * hardness * divisor / 20 / speed. The divisor is 30 when the block is
 * harvestable with the held item, 100 when it is not. Only pickaxe-class
 * blocks (stone, ores) demand a real tool: anything else (dirt, wood,
 * leaves, glass, …) mines at the /30 rate even by hand, so dirt by hand
 * is 0.75 s, not 2.5 s. Too-weak pickaxe tier counts as wrong tool.
 */
float survival_mine_time(uint16_t block, ItemId held)
{
    const BlockInfo *info = block_get_info(block);
    if (info->unbreakable) {
        return -1.0f;
    }
    if (info->hardness <= 0.0f) {
        return 0.0f; /* Instant (torches, flowers, grass tufts). */
    }
    bool correct = false;
    float speed = 1.0f;
    if (info->tool != TOOL_NONE && held != ITEM_NONE) {
        const ItemInfo *tool = item_get_info(held);
        if (tool->tool == info->tool && tool->tier >= info->min_tier) {
            correct = true;
            speed = tool->tier >= TOOL_TIER_STONE ? 4.0f : 2.0f;
        }
    }
    if (info->tool == (int)TOOL_PICKAXE && !correct) {
        return info->hardness * 5.0f; /* Unharvestable: /100 divisor. */
    }
    return info->hardness * 1.5f / speed; /* Harvestable: /30 divisor. */
}

/* Drop stack for a broken block with a held item (MC harvest rule). */
ItemStack survival_block_drop(uint16_t block, ItemId held)
{
    ItemStack out = {ITEM_NONE, 0, 0};
    const BlockInfo *info = block_get_info(block);
    if (info->drop == 0 || info->drop_count == 0) {
        return out;
    }
    if (!item_is_valid((ItemId)info->drop)) {
        return out;
    }
    /* Pickaxe-class blocks (stone, ores) drop nothing without a pickaxe
     * of sufficient tier — punching stone yields rubble, not cubes. */
    if (info->tool == (int)TOOL_PICKAXE) {
        const ItemInfo *tool = item_get_info(held);
        if (tool->tool != TOOL_PICKAXE || tool->tier < info->min_tier) {
            return out;
        }
    }
    out.item = (ItemId)info->drop;
    out.count = info->drop_count;
    const ItemInfo *ii = item_get_info(out.item);
    if (out.count > ii->max_stack) {
        out.count = ii->max_stack;
    }
    return out;
}

/* Block reach for the current mode. */
float survival_reach(bool creative)
{
    return creative ? SURVIVAL_REACH_CREATIVE : SURVIVAL_REACH_SURVIVAL;
}

/* Advance mining state; report completion. */
void survival_mine_update(Player *p, World *w, int bx, int by, int bz, ItemId held, float dt, MineResult *out)
{
    static MineResult none = {false, 0, 0, 0, 0};
    if (out == NULL) {
        return;
    }
    *out = none;
    if (p == NULL || w == NULL || dt < 0.0f) {
        return;
    }
    if (dt == 0.0f) {
        /* Still validate the target so stale state clears promptly. */
    }
    uint16_t cur = world_get_block(w, bx, by, bz);
    float need = survival_mine_time(cur, held);
    bool same = p->mine_active && p->mine_bx == bx && p->mine_by == by && p->mine_bz == bz &&
                p->mine_block == cur && p->mine_held == held;
    if (!same || need < 0.0f) {
        /* Retarget (or unbreakable): reset progress, latch new values. */
        p->mine_active = (need >= 0.0f);
        p->mine_bx = bx;
        p->mine_by = by;
        p->mine_bz = bz;
        p->mine_block = cur;
        p->mine_held = held;
        p->mine_progress = 0.0f;
        p->mine_need = need >= 0.0f ? need : 0.0f;
        if (need < 0.0f) {
            p->mine_active = false;
        }
        return;
    }
    p->mine_progress += dt;
    if (p->mine_progress >= p->mine_need) {
        out->finished = true;
        out->block = cur;
        out->bx = bx;
        out->by = by;
        out->bz = bz;
        survival_mine_reset(p);
    }
}

/* Clear mining state. */
void survival_mine_reset(Player *p)
{
    if (p == NULL) {
        return;
    }
    p->mine_active = false;
    p->mine_bx = 0;
    p->mine_by = 0;
    p->mine_bz = 0;
    p->mine_block = 0;
    p->mine_held = ITEM_NONE;
    p->mine_progress = 0.0f;
    p->mine_need = 0.0f;
}

/* Damage through the creative/dead gate. */
void survival_damage_player(Player *p, float amount)
{
    if (p == NULL || amount <= 0.0f || p->dead || survival_is_creative(p)) {
        return;
    }
    if (p->max_health <= 0.0f) {
        return;
    }
    p->health -= amount;
    if (p->health <= 0.0f) {
        p->health = 0.0f;
        p->dead = true;
        LOG_INFO("player died (damage %.1f)", (double)amount);
    }
}

/* Heal clamped. */
void survival_heal_player(Player *p, float amount)
{
    if (p == NULL || amount <= 0.0f || p->dead) {
        return;
    }
    p->health += amount;
    if (p->health > p->max_health) {
        p->health = p->max_health;
    }
}

/* Fall damage for a landing distance. */
float survival_fall_damage(float dist)
{
    if (!(dist > SURVIVAL_FALL_FREE)) {
        return 0.0f;
    }
    return (dist - SURVIVAL_FALL_FREE) * SURVIVAL_FALL_PER_BLOCK;
}

/* Hunger/regen/starve timers. */
void survival_hunger_update(Player *p, float dt)
{
    if (p == NULL || dt <= 0.0f || p->dead || survival_is_creative(p)) {
        return;
    }
    if (p->max_hunger <= 0.0f) {
        return;
    }
    /* No passive drain (MC parity): standing still never starves; only
     * activity (sprint/jump/regen) burns hunger via exhaustion below. */
    /* Activity burn: every 4 strain points cost 1 hunger (then reset the
     * counter by the same amount, keeping any overflow). */
    while (p->exhaustion >= SURVIVAL_EXHAUST_PER_HUNGER) {
        p->exhaustion -= SURVIVAL_EXHAUST_PER_HUNGER;
        if (p->hunger > 0.0f) {
            p->hunger -= 1.0f;
        }
    }
    p->regen_t += dt;
    while (p->regen_t >= SURVIVAL_REGEN_PERIOD) {
        p->regen_t -= SURVIVAL_REGEN_PERIOD;
        if (p->hunger <= 0.0f) {
            survival_damage_player(p, 1.0f); /* Starving. */
        } else if (p->hunger >= SURVIVAL_REGEN_MIN_HUNGER) {
            /* No heal, no cost (MC): full health never burns hunger. */
            float before = p->health;
            survival_heal_player(p, 1.0f);
            if (p->health > before) {
                survival_add_exhaustion(p, SURVIVAL_EXHAUST_REGEN);
            }
        }
    }
}

/* Add activity strain (clamped, gated). */
void survival_add_exhaustion(Player *p, float amount)
{
    if (p == NULL || !(amount > 0.0f) || p->dead || survival_is_creative(p)) {
        return;
    }
    p->exhaustion += amount;
    if (p->exhaustion > SURVIVAL_EXHAUST_CAP) {
        p->exhaustion = SURVIVAL_EXHAUST_CAP;
    }
}

/* Position hash (FNV-1a over x/y/z/seed): deterministic per cell. */
static uint32_t survival_cell_hash(long seed, int x, int y, int z)
{
    uint32_t h = 2166136261u;
    uint32_t parts[4];
    parts[0] = (uint32_t)(x * 1 + 0x9e3779b9u);
    parts[1] = (uint32_t)(y * 1 + 0x85ebca6bu);
    parts[2] = (uint32_t)(z * 1 + 0xc2b2ae35u);
    parts[3] = (uint32_t)((uint64_t)seed ^ ((uint64_t)seed >> 32));
    for (int i = 0; i < 4; ++i) {
        uint32_t v = parts[i];
        for (int b = 0; b < 4; ++b) {
            h ^= (v >> (b * 8)) & 0xFFu;
            h *= 16777619u;
        }
    }
    return h;
}

/* Bonus drop roll (leaves -> apple 1-in-8 by cell hash). */
bool survival_bonus_drop(long seed, uint16_t block, int x, int y, int z, ItemStack *out)
{
    ItemStack none = {ITEM_NONE, 0, 0};
    if (out == NULL) {
        return false;
    }
    *out = none;
    if (block != (uint16_t)BLOCK_LEAVES) {
        return false;
    }
    if (survival_cell_hash(seed, x, y, z) % (uint32_t)SURVIVAL_LEAF_BONUS_ONE_IN != 0u) {
        return false;
    }
    out->item = ITEM_APPLE;
    out->count = 1;
    return true;
}

/* Hold-to-eat state machine (see header for the contract). */
SurvivalEatResult survival_eat_update(Player *p, bool holding, float dt)
{
    if (p == NULL || p->dead || survival_is_creative(p)) {
        return SURVIVAL_EAT_NONE;
    }
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (p->hotbar_sel < 0 || p->hotbar_sel >= INV_HOTBAR) {
        p->eat_active = false;
        p->eat_t = 0.0f;
        return SURVIVAL_EAT_NONE;
    }
    ItemStack *sel = &p->inv.slots[p->hotbar_sel];
    bool edible = !stack_is_empty(sel) && item_is_edible(sel->item);
    if (p->eat_active) {
        /* Any change interrupts with nothing consumed. */
        if (!holding || p->hotbar_sel != p->eat_slot || !edible) {
            p->eat_active = false;
            p->eat_t = 0.0f;
            return SURVIVAL_EAT_NONE;
        }
        p->eat_t += dt;
        if (p->eat_t < SURVIVAL_EAT_TIME) {
            return SURVIVAL_EAT_NONE;
        }
        p->eat_active = false;
        p->eat_t = 0.0f;
        const ItemInfo *info = item_get_info(sel->item);
        float gain = (float)info->food;
        if (!inv_consume(&p->inv, p->eat_slot, 1)) {
            return SURVIVAL_EAT_NONE;
        }
        p->hunger += gain;
        if (p->hunger > p->max_hunger) {
            p->hunger = p->max_hunger;
        }
        return SURVIVAL_EAT_DONE;
    }
    /* Start only below full hunger (no wasteful eating at max). */
    if (holding && edible && p->hunger < p->max_hunger) {
        p->eat_active = true;
        p->eat_slot = p->hotbar_sel;
        p->eat_t = 0.0f;
    }
    return SURVIVAL_EAT_NONE;
}

/* Normalized bow charge from draw time. */
float survival_bow_charge(float draw_t)
{
    if (!(draw_t > 0.0f)) {
        return 0.0f;
    }
    float c = draw_t / SURVIVAL_BOW_FULL_DRAW;
    if (c > 1.0f) {
        c = 1.0f;
    }
    return c;
}

/* Launch parameters for a charge. */
void survival_bow_launch(float charge, float *out_speed, float *out_damage)
{
    if (charge < 0.0f) {
        charge = 0.0f;
    }
    if (charge > 1.0f) {
        charge = 1.0f;
    }
    /* Speed rides a 0.75 power curve (weak taps still loft); damage is
     * linear across the tuning range. */
    float speed =
        SURVIVAL_BOW_SPEED_MIN +
        (SURVIVAL_BOW_SPEED_MAX - SURVIVAL_BOW_SPEED_MIN) * powf(charge, 0.75f);
    float damage =
        SURVIVAL_BOW_DMG_MIN + (SURVIVAL_BOW_DMG_MAX - SURVIVAL_BOW_DMG_MIN) * charge;
    if (out_speed != NULL) {
        *out_speed = speed;
    }
    if (out_damage != NULL) {
        *out_damage = damage;
    }
}

/* True when a held item is a bow. */
bool survival_bow_is_bow(ItemId held)
{
    if (held == ITEM_NONE) {
        return false;
    }
    return item_get_info(held)->tool == TOOL_BOW;
}

/* First inventory slot holding arrows (-1 when none). */
int survival_bow_find_arrow(const Inventory *inv)
{
    if (inv == NULL) {
        return -1;
    }
    for (int i = 0; i < INV_SIZE; ++i) {
        if (!stack_is_empty(&inv->slots[i]) && inv->slots[i].item == ITEM_ARROW) {
            return i;
        }
    }
    return -1;
}

/* Consume exactly one arrow (first stack found). */
bool survival_bow_consume_arrow(Inventory *inv)
{
    if (inv == NULL) {
        return false;
    }
    int slot = survival_bow_find_arrow(inv);
    if (slot < 0) {
        return false;
    }
    return stack_remove(&inv->slots[slot], 1) == 1;
}

/* Cancel any in-progress bow draw. */
void survival_bow_reset(Player *p)
{
    if (p == NULL) {
        return;
    }
    p->bow_drawing = false;
    p->bow_slot = 0;
    p->bow_t = 0.0f;
    p->bow_full = false;
}

/* Respawn reset. */
void survival_respawn(Player *p, Vec3 spawn)
{
    if (p == NULL) {
        return;
    }
    p->pos = spawn;
    p->render_pos = spawn;
    p->vel = mmath_vec3(0.0f, 0.0f, 0.0f);
    p->acc = mmath_vec3(0.0f, 0.0f, 0.0f);
    p->health = p->max_health > 0.0f ? p->max_health : 20.0f;
    p->hunger = p->max_hunger > 0.0f ? p->max_hunger : 20.0f;
    p->dead = false;
    p->grounded = false;
    p->hunger_t = 0.0f;
    p->regen_t = 0.0f;
    p->exhaustion = 0.0f;
    p->hurt_t = 0.0f;
    p->attack_cd = 0.0f;
    p->eat_active = false;
    p->eat_slot = 0;
    p->eat_t = 0.0f;
    p->bow_drawing = false;
    p->bow_slot = 0;
    p->bow_t = 0.0f;
    p->bow_full = false;
    p->fall_peak = -1.0f;
    p->last_fall = -1.0f;
    survival_mine_reset(p);
}
