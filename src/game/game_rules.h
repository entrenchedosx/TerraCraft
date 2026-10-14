#pragma once

/* TerraCraft game rules / cheats (world-scoped).
 *
 * Single home for world creation toggles so UI, persistence, LAN sync,
 * and gameplay gates never scatter ad-hoc booleans. Pure CPU,
 * headless-testable. All rules are versioned through world.meta with
 * backward-compatible defaults (missing keys read as defaults).
 *
 * Supported rules (only systems TerraCraft actually implements):
 *   allow_cheats      - cheat-only commands/actions permitted.
 *   keep_inventory    - inventory/hotbar/cursor survive death.
 *   mob_spawning      - natural mob spawns enabled.
 *   daylight_cycle    - day/night clock advances.
 *   natural_regen     - hunger-driven health regen (and starve damage).
 *   fall_damage       - fall impact damages players (and mobs).
 *   pvp               - players can damage other players.
 *
 * Deliberately omitted (no such system exists; no fake toggles):
 *   weather cycle (no weather), fire spread (no fire).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct GameRules {
    bool allow_cheats;
    bool keep_inventory;
    bool mob_spawning;
    bool daylight_cycle;
    bool natural_regen;
    bool fall_damage;
    bool pvp;
} GameRules;

/* Fill Minecraft-like defaults matching existing TerraCraft behaviour:
 * cheats off, keep-inventory off, mobs on, daylight on, regen on,
 * fall damage on, pvp on. */
void game_rules_defaults(GameRules *rules);

/* Parse helpers for world.meta key=value lines. Unknown values read as
 * false for allow_cheats/keep_inventory, true for the rest (fail-safe
 * towards vanilla behaviour). NULL-safe. */
bool game_rules_parse_bool(const char *value, bool default_value);

/* Serialize one rule as "0"/"1" (never NULL). */
const char *game_rules_bool_text(bool value);

/* Compare two rule sets (NULL reads as defaults). */
bool game_rules_equal(const GameRules *a, const GameRules *b);

/* Gameplay policy queries (NULL reads as defaults). Pure predicates so
 * death/respawn/combat call sites stay one-liners and unit-testable. */
bool game_rules_keep_inventory_drops(const GameRules *rules);
bool game_rules_pvp_allowed(const GameRules *rules);
bool game_rules_fall_damage_enabled(const GameRules *rules);
bool game_rules_mob_spawning_enabled(const GameRules *rules);
bool game_rules_daylight_enabled(const GameRules *rules);
bool game_rules_regen_enabled(const GameRules *rules);
bool game_rules_cheats_allowed(const GameRules *rules);
