#pragma once

/* Survival gameplay policy (M6): single home for mode-gated rules so app.c
 * never scatters `if (creative)` checks. Covers mining times, drops, fall
 * damage, health/hunger ticks, and respawn. Pure CPU, headless-testable.
 * Creative immunity is enforced inside the damage/mine entry points.
 */

#include "game/inventory.h"
#include "math/mmath.h"

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations (full types in their headers). */
typedef struct Player Player;
typedef struct World World;

/* Fall damage tuning (MC): no damage for the first 3 blocks, then 1 HP
 * (half a heart) per extra block. Normal jumps (~1.2) are always safe.
 */
#define SURVIVAL_FALL_FREE 3.0f
#define SURVIVAL_FALL_PER_BLOCK 1.0f

/* Hunger tuning (MC): no passive drain — standing still never starves.
 * Regen +1 per 4 s at hunger >= 18, starve 1 per 4 s at hunger 0.
 * (hunger_t is retained unused; kept so Player/save layouts stay stable.)
 */
#define SURVIVAL_REGEN_PERIOD 4.0f
#define SURVIVAL_REGEN_MIN_HUNGER 18.0f
#define SURVIVAL_STARVE_PERIOD 4.0f

/* M7 activity tuning (MC rates): strain accumulates from sprinting,
 * jumping, and regeneration; every 4 points burn 1 hunger. Sprint is
 * per-second at the MC per-meter rate (0.1/m * 5.612 m/s); jump and
 * regen are per-event. Regen deliberately costs less than MC's 6.0:
 * without a saturation system the full rate starves healers net-negative
 * (documented simplification, see DECISIONS).
 */
#define SURVIVAL_EXHAUST_CAP 40.0f
#define SURVIVAL_EXHAUST_PER_HUNGER 4.0f
#define SURVIVAL_EXHAUST_SPRINT 0.56f
#define SURVIVAL_EXHAUST_JUMP 0.05f
#define SURVIVAL_EXHAUST_REGEN 1.5f

/* Eating: hold-to-consume time for one food item (MC: 1.61 s). */
#define SURVIVAL_EAT_TIME 1.61f

/* Bow tuning (M9, original TerraCraft): full draw in 1.0 s; releasing
 * below 0.15 charge cancels the shot; launch speed follows a 0.75
 * curve from 10 to 45 m/s; damage scales 2..6 HP; knockback is flat;
 * drawing slows movement to 0.5x in both modes.
 */
#define SURVIVAL_BOW_FULL_DRAW 1.0f
#define SURVIVAL_BOW_MIN_CHARGE 0.15f
#define SURVIVAL_BOW_SPEED_MIN 10.0f
#define SURVIVAL_BOW_SPEED_MAX 45.0f
#define SURVIVAL_BOW_DMG_MIN 2.0f
#define SURVIVAL_BOW_DMG_MAX 6.0f
#define SURVIVAL_BOW_KNOCK 6.0f
#define SURVIVAL_BOW_MOVE_SCALE 0.5f

/* Block reach in blocks (MC: 4.5 survival, 5.0 creative). */
#define SURVIVAL_REACH_SURVIVAL 4.5f
#define SURVIVAL_REACH_CREATIVE 5.0f

/* Eat update outcome (for sound/logging at the call site). */
typedef enum SurvivalEatResult {
    SURVIVAL_EAT_NONE = 0, /* No state change (not eating / still chewing). */
    SURVIVAL_EAT_DONE      /* An item was fully eaten this tick. */
} SurvivalEatResult;

/* Leaf apple bonus: deterministic 1-in-8 per leaves cell (no RNG stream,
 * so drops are stable across reloads and testable).
 */
#define SURVIVAL_LEAF_BONUS_ONE_IN 8

/* Mining completion event (returned, never allocated). */
typedef struct MineResult {
    bool finished;    /* True when the block broke this update. */
    uint16_t block;   /* Block ID that broke (valid when finished). */
    int bx, by, bz;   /* Cell that broke (valid when finished). */
} MineResult;

/* True when the player is in creative mode (fly/no-clip/infinite).
 *
 * Args:
 *   p: player (NULL reads as survival).
 *
 * Returns: true in creative.
 */
bool survival_is_creative(const Player *p);

/* Break time in seconds for a block with a held item (MC formula:
 * hardness * 1.5 / dig speed when harvestable with the held item
 * (wood 2, stone 4), hardness * 5 only for pickaxe-class blocks mined
 * without a sufficient pickaxe; hardness 0 breaks instantly.
 * Dirt/wood/leaves/glass/etc. mine at the fast rate even by hand
 * (dirt 0.75 s, wood 3 s) — only stone/ores punish bare hands.
 * Unbreakable blocks (bedrock) return -1.0 (infinite).
 *
 * Args:
 *   block: block ID being mined.
 *   held: held item ID (ITEM_NONE = bare hands).
 *
 * Returns: seconds required, 0.0 for instant, or -1.0 when unbreakable.
 */
float survival_mine_time(uint16_t block, ItemId held);

/* Drop produced by breaking a block with a held item (MC harvest rule:
 * pickaxe-class blocks — stone, ores — drop nothing without a pickaxe
 * of sufficient tier; glass never drops; everything else drops
 * regardless of tool).
 *
 * Args:
 *   block: broken block ID.
 *   held: held item ID at the break (ITEM_NONE = bare hands).
 *
 * Returns: canonical drop stack (empty when nothing drops).
 */
ItemStack survival_block_drop(uint16_t block, ItemId held);

/* Block reach for the current mode (see SURVIVAL_REACH_*).
 *
 * Args:
 *   creative: true in creative mode.
 *
 * Returns: raycast distance for block targeting.
 */
float survival_reach(bool creative);

/* Advance survival mining state by dt. Call only while the mine button is
 * held with a live raycast target; call survival_mine_reset otherwise.
 * Resets automatically on target/block/held change. Unbreakable targets
 * never finish (progress still resets on change).
 *
 * Args:
 *   p: player with mine_* state (must not be NULL).
 *   w: world (must not be NULL; used to verify the target is unchanged).
 *   bx, by, bz: current raycast target cell.
 *   held: currently held item ID.
 *   dt: seconds to advance (>= 0).
 *   out: receives the completion event (must not be NULL).
 */
void survival_mine_update(Player *p, World *w, int bx, int by, int bz, ItemId held, float dt, MineResult *out);

/* Clear mining state (button release, target lost, menu opened, death).
 *
 * Args:
 *   p: player (must not be NULL).
 */
void survival_mine_reset(Player *p);

/* Apply damage through the creative-immunity + death gate.
 * No-op when dead/NULL/creative/non-positive. Sets dead at zero.
 *
 * Args:
 *   p: player (must not be NULL for effect).
 *   amount: damage (> 0).
 */
void survival_damage_player(Player *p, float amount);

/* Heal clamped to [0, max_health]. No-op when dead/NULL.
 *
 * Args:
 *   p: player (must not be NULL for effect).
 *   amount: healing (> 0).
 */
void survival_heal_player(Player *p, float amount);

/* Fall damage for a landing distance (0 when within the free allowance).
 *
 * Args:
 *   dist: fall distance in blocks (>= 0).
 *
 * Returns: damage amount.
 */
float survival_fall_damage(float dist);

/* Hunger/regen/starve timers (call each PLAYING tick in survival only;
 * timers live on the player so pause/menu time never advances them).
 *
 * Args:
 *   p: player (must not be NULL).
 *   dt: seconds to advance (>= 0).
 */
void survival_hunger_update(Player *p, float dt);

/* Respawn: restore health/hunger, clear velocity/fall/mining/dead flags,
 * teleport to spawn. Inventory is handled by the caller (death drops).
 *
 * Args:
 *   p: player (must not be NULL).
 *   spawn: feet respawn position.
 */
void survival_respawn(Player *p, Vec3 spawn);

/* Add activity strain (sprinting/jumping/regen). Clamped to
 * [0, SURVIVAL_EXHAUST_CAP]; every SURVIVAL_EXHAUST_PER_HUNGER points burn
 * 1 hunger on the next hunger update. No-op on NULL/dead/creative.
 *
 * Args:
 *   p: player (must not be NULL for effect).
 *   amount: strain to add (>= 0).
 */
void survival_add_exhaustion(Player *p, float amount);

/* Normalized bow charge from draw time (frame-rate independent: the
 * caller accumulates dt; this only maps).
 *
 * Args:
 *   draw_t: seconds drawn (>= 0).
 *
 * Returns: charge 0..1 (1 at FULL_DRAW and beyond).
 */
float survival_bow_charge(float draw_t);

/* Launch parameters for a charge (smooth 0.75 power curve on speed,
 * linear damage; both clamped to their tuning ranges).
 *
 * Args:
 *   charge: normalized charge 0..1.
 *   out_speed: receives launch m/s (may be NULL).
 *   out_damage: receives hit damage (may be NULL).
 */
void survival_bow_launch(float charge, float *out_speed, float *out_damage);

/* True when a held item is a bow (TOOL_BOW registry flag).
 *
 * Args:
 *   held: held item ID (ITEM_NONE reads as false).
 *
 * Returns: true for bows.
 */
bool survival_bow_is_bow(ItemId held);

/* First inventory slot holding arrows (-1 when none). Scans the full
 * 36 slots in order (hotbar first, matching player intuition).
 *
 * Args:
 *   inv: inventory (must not be NULL).
 *
 * Returns: slot index or -1.
 */
int survival_bow_find_arrow(const Inventory *inv);

/* Consume exactly one arrow from the inventory (first stack found).
 *
 * Args:
 *   inv: inventory (must not be NULL).
 *
 * Returns: true when an arrow was removed, false when none existed.
 */
bool survival_bow_consume_arrow(Inventory *inv);

/* Cancel any in-progress bow draw (slot switches, menus, death, unload
 * all funnel here — cancellations never consume ammo or durability).
 * No-op on NULL.
 *
 * Args:
 *   p: player (must not be NULL for effect).
 */
void survival_bow_reset(Player *p);

/* Deterministic bonus drop for a broken block (currently: leaves cells
 * yield an apple 1-in-8 by position hash — stable, no RNG stream).
 * Never a weapon for duplication: at most one stack per call.
 *
 * Args:
 *   seed: world seed (variety between worlds).
 *   block: broken block ID.
 *   x, y, z: broken cell.
 *   out: receives the bonus stack (empty when none; must not be NULL).
 *
 * Returns: true when a bonus dropped (out filled), false otherwise.
 */
bool survival_bonus_drop(long seed, uint16_t block, int x, int y, int z, ItemStack *out);

/* Hold-to-eat state machine (pure CPU: `holding` is the caller's input
 * edge/level, so this is fully unit-testable). Uses the selected hotbar
 * slot: starting requires a held edible below full hunger; completion
 * consumes exactly one item and restores its food value (clamped).
 * Release, slot switch, inedible selection, full hunger, UI/death
 * (caller clears eat_active on those paths) interrupt with nothing
 * consumed. Creative and dead players never eat.
 *
 * Args:
 *   p: player with eat/hunger/inventory state (must not be NULL).
 *   holding: use button held this tick.
 *   dt: seconds to advance (>= 0).
 *
 * Returns: SURVIVAL_EAT_DONE when an item completed this tick, else NONE.
 */
SurvivalEatResult survival_eat_update(Player *p, bool holding, float dt);
