#include "game/game_rules.h"

#include <string.h>

void game_rules_defaults(GameRules *rules)
{
    if (rules == NULL) {
        return;
    }
    rules->allow_cheats = false;
    rules->keep_inventory = false;
    rules->mob_spawning = true;
    rules->daylight_cycle = true;
    rules->natural_regen = true;
    rules->fall_damage = true;
    rules->pvp = true;
}

bool game_rules_parse_bool(const char *value, bool default_value)
{
    if (value == NULL) {
        return default_value;
    }
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0 ||
        strcmp(value, "yes") == 0) {
        return true;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "off") == 0 ||
        strcmp(value, "no") == 0) {
        return false;
    }
    return default_value;
}

const char *game_rules_bool_text(bool value)
{
    return value ? "1" : "0";
}

static const GameRules *game_rules_or_defaults(const GameRules *rules, GameRules *scratch)
{
    if (rules != NULL) {
        return rules;
    }
    game_rules_defaults(scratch);
    return scratch;
}

bool game_rules_keep_inventory_drops(const GameRules *rules)
{
    GameRules scratch;
    return !game_rules_or_defaults(rules, &scratch)->keep_inventory;
}

bool game_rules_pvp_allowed(const GameRules *rules)
{
    GameRules scratch;
    return game_rules_or_defaults(rules, &scratch)->pvp;
}

bool game_rules_fall_damage_enabled(const GameRules *rules)
{
    GameRules scratch;
    return game_rules_or_defaults(rules, &scratch)->fall_damage;
}

bool game_rules_mob_spawning_enabled(const GameRules *rules)
{
    GameRules scratch;
    return game_rules_or_defaults(rules, &scratch)->mob_spawning;
}

bool game_rules_daylight_enabled(const GameRules *rules)
{
    GameRules scratch;
    return game_rules_or_defaults(rules, &scratch)->daylight_cycle;
}

bool game_rules_regen_enabled(const GameRules *rules)
{
    GameRules scratch;
    return game_rules_or_defaults(rules, &scratch)->natural_regen;
}

bool game_rules_cheats_allowed(const GameRules *rules)
{
    GameRules scratch;
    return game_rules_or_defaults(rules, &scratch)->allow_cheats;
}

bool game_rules_equal(const GameRules *a, const GameRules *b)
{
    GameRules da;
    GameRules db;
    game_rules_defaults(&da);
    game_rules_defaults(&db);
    if (a != NULL) {
        da = *a;
    }
    if (b != NULL) {
        db = *b;
    }
    return da.allow_cheats == db.allow_cheats && da.keep_inventory == db.keep_inventory &&
           da.mob_spawning == db.mob_spawning && da.daylight_cycle == db.daylight_cycle &&
           da.natural_regen == db.natural_regen && da.fall_damage == db.fall_damage && da.pvp == db.pvp;
}
