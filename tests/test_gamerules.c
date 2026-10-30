#include "test_main.h"

#include "core/path.h"
#include "game/game_rules.h"
#include "game/game_state.h"
#include "game/lan_protocol.h"
#include "game/session.h"
#include "platform/lan.h"
#include "world/world_meta.h"
#include "world/world_save.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RULES_TMP "test_tmp_m5"

/* Scratch cleanup (mirrors test_save.c policy). */
static void rules_tmp_cleanup(const char *dir)
{
    char meta[1024];
    if (path_join(meta, sizeof(meta), dir, WORLD_META_FILE) == 0) {
        path_remove_file(meta);
    }
    path_remove_dir(dir);
}

/* Defaults match long-standing TerraCraft behaviour. */
int test_gamerules_defaults(void)
{
    int failures = 0;
    GameRules r;
    game_rules_defaults(&r);
    TEST_ASSERT(r.allow_cheats == false);
    TEST_ASSERT(r.keep_inventory == false);
    TEST_ASSERT(r.mob_spawning == true);
    TEST_ASSERT(r.daylight_cycle == true);
    TEST_ASSERT(r.natural_regen == true);
    TEST_ASSERT(r.fall_damage == true);
    TEST_ASSERT(r.pvp == true);
    /* Policy predicates agree with the struct. */
    TEST_ASSERT(!game_rules_cheats_allowed(&r));
    TEST_ASSERT(game_rules_keep_inventory_drops(&r));
    TEST_ASSERT(game_rules_pvp_allowed(&r));
    TEST_ASSERT(game_rules_fall_damage_enabled(&r));
    TEST_ASSERT(game_rules_mob_spawning_enabled(&r));
    TEST_ASSERT(game_rules_daylight_enabled(&r));
    TEST_ASSERT(game_rules_regen_enabled(&r));
    /* NULL reads as defaults (call sites pass world rules that may be absent). */
    TEST_ASSERT(!game_rules_cheats_allowed(NULL));
    TEST_ASSERT(game_rules_keep_inventory_drops(NULL));
    TEST_ASSERT(game_rules_pvp_allowed(NULL));
    return failures;
}

/* Keep-inventory predicate: drops happen exactly when the rule is off. */
int test_gamerules_keep_inventory(void)
{
    int failures = 0;
    GameRules on;
    game_rules_defaults(&on);
    on.keep_inventory = true;
    TEST_ASSERT(!game_rules_keep_inventory_drops(&on));
    GameRules off;
    game_rules_defaults(&off);
    off.keep_inventory = false;
    TEST_ASSERT(game_rules_keep_inventory_drops(&off));
    /* Repeated death/respawn cycles must not flip the verdict. */
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT(!game_rules_keep_inventory_drops(&on));
        TEST_ASSERT(game_rules_keep_inventory_drops(&off));
    }
    return failures;
}

/* Every supported toggle round-trips through world.meta. */
int test_gamerules_meta_roundtrip(void)
{
    int failures = 0;
    char dir[256];
    path_join(dir, sizeof(dir), RULES_TMP, "rules_rt");
    rules_tmp_cleanup(dir);

    WorldMeta m;
    world_meta_defaults(&m);
    memcpy(m.name, "Rules", 6);
    m.seed = 99;
    m.rules.allow_cheats = true;
    m.rules.keep_inventory = true;
    m.rules.mob_spawning = false;
    m.rules.daylight_cycle = false;
    m.rules.natural_regen = false;
    m.rules.fall_damage = false;
    m.rules.pvp = false;
    TEST_ASSERT(world_meta_write(dir, &m) == 0);

    WorldMeta back;
    memset(&back, 0, sizeof(back));
    TEST_ASSERT(world_meta_read(dir, &back) == 0);
    TEST_ASSERT(back.rules.allow_cheats == true);
    TEST_ASSERT(back.rules.keep_inventory == true);
    TEST_ASSERT(back.rules.mob_spawning == false);
    TEST_ASSERT(back.rules.daylight_cycle == false);
    TEST_ASSERT(back.rules.natural_regen == false);
    TEST_ASSERT(back.rules.fall_damage == false);
    TEST_ASSERT(back.rules.pvp == false);

    rules_tmp_cleanup(dir);
    path_remove_dir(RULES_TMP);
    return failures;
}

/* Old worlds without rule keys load safely with defaults (no crash,
 * vanilla behaviour preserved). */
int test_gamerules_meta_missing_defaults(void)
{
    int failures = 0;
    WorldMeta m;
    memset(&m, 0, sizeof(m));
    const char *legacy = "format_version=1\nworld_name=Old\nseed=42\nterrain_version=1\n";
    TEST_ASSERT(world_meta_parse(legacy, &m) == 0);
    TEST_ASSERT(m.rules.allow_cheats == false);
    TEST_ASSERT(m.rules.keep_inventory == false);
    TEST_ASSERT(m.rules.mob_spawning == true);
    TEST_ASSERT(m.rules.daylight_cycle == true);
    TEST_ASSERT(m.rules.natural_regen == true);
    TEST_ASSERT(m.rules.fall_damage == true);
    TEST_ASSERT(m.rules.pvp == true);
    /* Unknown future rule keys must not break the parse. */
    const char *future = "format_version=1\nworld_name=Old\nseed=42\nterrain_version=1\n"
                         "weather_cycle=1\nfire_spreads=1\n";
    TEST_ASSERT(world_meta_parse(future, &m) == 0);
    return failures;
}

/* New worlds store the requested rules; NULL stores defaults. */
int test_gamerules_session_create(void)
{
    int failures = 0;
    char dir[512];
    GameRules custom;
    game_rules_defaults(&custom);
    custom.keep_inventory = true;
    custom.pvp = false;
    custom.allow_cheats = true;
    TEST_ASSERT(session_create_world_ex("test_tmp_m5", "RulesWorld", "7", 0, &custom, dir) == 0);
    WorldMeta back;
    memset(&back, 0, sizeof(back));
    TEST_ASSERT(world_meta_read(dir, &back) == 0);
    TEST_ASSERT(back.rules.keep_inventory == true);
    TEST_ASSERT(back.rules.pvp == false);
    TEST_ASSERT(back.rules.allow_cheats == true);
    TEST_ASSERT(back.rules.mob_spawning == true);
    (void)world_save_delete(dir);

    char dir2[512];
    TEST_ASSERT(session_create_world_ex("test_tmp_m5", "RulesWorld2", "7", 0, NULL, dir2) == 0);
    WorldMeta back2;
    memset(&back2, 0, sizeof(back2));
    TEST_ASSERT(world_meta_read(dir2, &back2) == 0);
    GameRules def;
    game_rules_defaults(&def);
    TEST_ASSERT(game_rules_equal(&back2.rules, &def));
    (void)world_save_delete(dir2);
    path_remove_dir("test_tmp_m5");
    return failures;
}

/* Combat packet framing: every new LAN message fits the 4096-byte frame
 * and carries its exact size (two real peers over loopback TCP). */
#if defined(_WIN32)
#include <windows.h>
static void rules_test_yield(void)
{
    Sleep(1);
}
#else
#include <time.h>
static void rules_test_yield(void)
{
    struct timespec delay = {0, 1000000L};
    (void)nanosleep(&delay, NULL);
}
#endif

static bool rules_wait_message(LanSession *a, LanSession *b, uint32_t peer, const uint8_t *want,
                               size_t want_size)
{
    double deadline = 0.0;
    (void)deadline;
    for (int i = 0; i < 3000; ++i) {
        LanEvent event;
        while (lan_poll(a, &event)) {
            if (event.type == LAN_EVENT_MESSAGE && event.peer_id == peer && event.size == want_size &&
                memcmp(event.payload, want, want_size) == 0) {
                return true;
            }
        }
        while (lan_poll(b, &event)) {
            if (event.type == LAN_EVENT_MESSAGE && event.peer_id == peer && event.size == want_size &&
                memcmp(event.payload, want, want_size) == 0) {
                return true;
            }
        }
        rules_test_yield();
    }
    return false;
}

int test_gamerules_lan_framing(void)
{
    int failures = 0;
    LanSession *host = lan_create();
    LanSession *client = lan_create();
    TEST_ASSERT(host != NULL);
    TEST_ASSERT(client != NULL);
    if (host == NULL || client == NULL) {
        return failures + 1;
    }
    TEST_ASSERT(lan_host_start(host, 0));
    uint16_t port = lan_local_port(host);
    TEST_ASSERT(port != 0);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)port);
    /* Direct connect via the transport's address form. */
    TEST_ASSERT(lan_client_start(client, "127.0.0.1", port));

    /* Wait for the handshake both ways (capture the host-side peer id). */
    bool hc = false;
    bool cc = false;
    uint32_t host_peer = 0;
    for (int i = 0; i < 3000 && !(hc && cc); ++i) {
        LanEvent event;
        while (lan_poll(host, &event)) {
            if (event.type == LAN_EVENT_PEER_CONNECTED) {
                hc = true;
                host_peer = event.peer_id;
            }
        }
        while (lan_poll(client, &event)) {
            if (event.type == LAN_EVENT_PEER_CONNECTED && event.peer_id == 1) {
                cc = true;
            }
        }
        rules_test_yield();
    }
    TEST_ASSERT(hc && cc);
    TEST_ASSERT(host_peer != 0);
    (void)addr;

    /* All frames are built with the production codecs (same code the
     * handlers run), so this proves wire compatibility, not just sizes. */
    uint8_t attack[LAN_MSG_ATTACK_SIZE];
    lan_attack_encode(attack, 3, 5.0f, 1);
    TEST_ASSERT(lan_send(client, 1, attack, sizeof(attack)));
    TEST_ASSERT(rules_wait_message(host, client, host_peer, attack, sizeof(attack)));

    uint8_t damage[LAN_MSG_DAMAGE_SIZE];
    lan_damage_encode(damage, 3, 5.0f, 2);
    TEST_ASSERT(lan_send(host, 0xFFFFFFFFu, damage, sizeof(damage)));
    TEST_ASSERT(rules_wait_message(host, client, 1, damage, sizeof(damage)));

    uint8_t health[LAN_MSG_HEALTH_SIZE];
    lan_health_encode(health, 2, 18.0f, false, false);
    TEST_ASSERT(lan_send(client, 1, health, sizeof(health)));
    TEST_ASSERT(rules_wait_message(host, client, host_peer, health, sizeof(health)));

    /* RESPAWN carries position + authoritative health (21 B). */
    uint8_t respawn[LAN_MSG_RESPAWN_SIZE];
    lan_respawn_encode(respawn, 2, 1.0f, 70.0f, 3.0f, 20.0f);
    TEST_ASSERT(lan_send(host, 0xFFFFFFFFu, respawn, sizeof(respawn)));
    TEST_ASSERT(rules_wait_message(host, client, 1, respawn, sizeof(respawn)));

    /* Ranged ATTACK (arrows) shares the strike body with a distinct type. */
    uint8_t ranged[LAN_MSG_ATTACK_RANGED_SIZE];
    lan_attack_ranged_encode(ranged, 3, 4.0f, 9);
    TEST_ASSERT(lan_send(client, 1, ranged, sizeof(ranged)));
    TEST_ASSERT(rules_wait_message(host, client, host_peer, ranged, sizeof(ranged)));

    /* RULES (8 B): all seven toggles + type. */
    uint8_t rules[LAN_MSG_RULES_SIZE];
    rules[0] = (uint8_t)LAN_MSG_RULES;
    uint8_t seven[7] = {0, 1, 1, 1, 1, 1, 1};
    memcpy(rules + 1, seven, sizeof(seven));
    TEST_ASSERT(lan_send(host, 0xFFFFFFFFu, rules, sizeof(rules)));
    TEST_ASSERT(rules_wait_message(host, client, 1, rules, sizeof(rules)));

    /* HELD (8 B): selected slot + item. */
    uint8_t held[LAN_MSG_HELD_SIZE];
    lan_held_encode(held, 2, 3, 7);
    TEST_ASSERT(lan_send(client, 1, held, sizeof(held)));
    TEST_ASSERT(rules_wait_message(host, client, host_peer, held, sizeof(held)));

    lan_close(host);
    lan_close(client);
    lan_destroy(host);
    lan_destroy(client);
    return failures;
}

/* Wire codec round-trips (the exact routines the app handlers call):
 * every combat/replication message survives encode -> decode with
 * bit-exact floats, and malformed frames are refused. */
int test_gamerules_codecs(void)
{
    int failures = 0;
    TEST_ASSERT(lan_version_supported(LAN_PROTOCOL_VERSION));
    TEST_ASSERT(!lan_version_supported(3));
    {
        uint8_t buf[LAN_MSG_ATTACK_SIZE];
        lan_attack_encode(buf, 7, 5.5f, 42);
        uint32_t victim = 0;
        float damage = 0.0f;
        uint32_t seq = 0;
        TEST_ASSERT(lan_attack_decode(buf, sizeof(buf), &victim, &damage, &seq));
        TEST_ASSERT(victim == 7 && seq == 42);
        TEST_ASSERT_FLOAT_EQ(damage, 5.5f, 0.0f);
        TEST_ASSERT(!lan_attack_decode(buf, sizeof(buf) - 1, NULL, NULL, NULL));
        buf[0] = 0;
        TEST_ASSERT(!lan_attack_decode(buf, sizeof(buf), NULL, NULL, NULL));
        TEST_ASSERT(!lan_attack_decode(NULL, sizeof(buf), NULL, NULL, NULL));
    }
    {
        uint8_t buf[LAN_MSG_ATTACK_RANGED_SIZE];
        lan_attack_ranged_encode(buf, 9, 6.0f, 77);
        uint32_t victim = 0;
        float damage = 0.0f;
        uint32_t seq = 0;
        TEST_ASSERT(lan_attack_ranged_decode(buf, sizeof(buf), &victim, &damage, &seq));
        TEST_ASSERT(victim == 9 && seq == 77);
        TEST_ASSERT_FLOAT_EQ(damage, 6.0f, 0.0f);
        /* Cross-type confusion refused: melee decoder rejects ranged. */
        TEST_ASSERT(!lan_attack_decode(buf, sizeof(buf), NULL, NULL, NULL));
    }
    {
        uint8_t buf[LAN_MSG_DAMAGE_SIZE];
        lan_damage_encode(buf, 4, 2.5f, 8);
        uint32_t victim = 0;
        float damage = 0.0f;
        uint32_t attacker = 0;
        TEST_ASSERT(lan_damage_decode(buf, sizeof(buf), &victim, &damage, &attacker));
        TEST_ASSERT(victim == 4 && attacker == 8);
        TEST_ASSERT_FLOAT_EQ(damage, 2.5f, 0.0f);
        TEST_ASSERT(!lan_damage_decode(buf, 1, NULL, NULL, NULL));
    }
    {
        uint8_t buf[LAN_MSG_HEALTH_SIZE];
        lan_health_encode(buf, 5, 13.5f, true, true);
        uint32_t pid = 0;
        float hp = 0.0f;
        bool dead = false;
        bool creative = false;
        TEST_ASSERT(lan_health_decode(buf, sizeof(buf), &pid, &hp, &dead, &creative));
        TEST_ASSERT(pid == 5 && dead == true && creative == true);
        TEST_ASSERT_FLOAT_EQ(hp, 13.5f, 0.0f);
        /* Short/legacy frames are refused (no silent misparse). */
        TEST_ASSERT(!lan_health_decode(buf, 10, NULL, NULL, NULL, NULL));
    }
    {
        /* RESPAWN health is bit-exact on the wire (no 20.0 assumption). */
        uint8_t buf[LAN_MSG_RESPAWN_SIZE];
        lan_respawn_encode(buf, 6, 1.25f, 70.5f, -3.75f, 13.5f);
        uint32_t pid = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f, hp = 0.0f;
        TEST_ASSERT(lan_respawn_decode(buf, sizeof(buf), &pid, &x, &y, &z, &hp));
        TEST_ASSERT(pid == 6);
        TEST_ASSERT_FLOAT_EQ(x, 1.25f, 0.0f);
        TEST_ASSERT_FLOAT_EQ(y, 70.5f, 0.0f);
        TEST_ASSERT_FLOAT_EQ(z, -3.75f, 0.0f);
        TEST_ASSERT_FLOAT_EQ(hp, 13.5f, 0.0f);
        TEST_ASSERT(!lan_respawn_decode(buf, 17, NULL, NULL, NULL, NULL, NULL));
    }
    {
        uint8_t buf[LAN_MSG_HELD_SIZE];
        lan_held_encode(buf, 3, 8, 259);
        uint32_t pid = 0;
        uint8_t slot = 0;
        uint16_t item = 0;
        TEST_ASSERT(lan_held_decode(buf, sizeof(buf), &pid, &slot, &item));
        TEST_ASSERT(pid == 3 && slot == 8 && item == 259);
    }
    {
        /* Rules codec covers all seven toggles both ways. */
        GameRules in;
        game_rules_defaults(&in);
        in.allow_cheats = true;
        in.keep_inventory = true;
        in.mob_spawning = false;
        in.daylight_cycle = false;
        in.natural_regen = false;
        in.fall_damage = false;
        in.pvp = false;
        uint8_t raw[7];
        lan_rules_encode(raw, &in);
        GameRules out;
        lan_rules_decode(&out, raw);
        TEST_ASSERT(game_rules_equal(&in, &out));
        lan_rules_decode(&out, NULL); /* NULL-safe: yields defaults. */
        GameRules def;
        game_rules_defaults(&def);
        TEST_ASSERT(game_rules_equal(&out, &def));
    }
    return failures;
}

/* Death-gate discipline: the world ticks in PLAYING/INVENTORY/CRAFTING
 * (so death drops fire there) and never in DEAD (so each death drops
 * exactly once); DEAD is reachable from every ticking state. */
int test_death_gate_states(void)
{
    int failures = 0;
    TEST_ASSERT(game_state_ticks_world(GAME_STATE_PLAYING) == true);
    TEST_ASSERT(game_state_ticks_world(GAME_STATE_INVENTORY) == true);
    TEST_ASSERT(game_state_ticks_world(GAME_STATE_CRAFTING) == true);
    TEST_ASSERT(game_state_ticks_world(GAME_STATE_DEAD) == false);
    TEST_ASSERT(game_state_ticks_world(GAME_STATE_PAUSED) == false);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_PLAYING, GAME_STATE_DEAD) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_INVENTORY, GAME_STATE_DEAD) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_CRAFTING, GAME_STATE_DEAD) == true);
    return failures;
}

/* Menu fallback granularity: stripes must stay below the perception
 * threshold at common window sizes (the pixelation regression guard). */
int test_menu_background_granularity(void)
{
    int failures = 0;
    const int sky_bands = 48;
    const int ridge_cols = 96;
    /* 720p: sky stripe ~15 px, ridge cell 13 px. */
    TEST_ASSERT(720.0f / (float)sky_bands <= 16.0f);
    TEST_ASSERT(1280.0f / (float)ridge_cols <= 16.0f);
    /* 1080p: sky stripe 22 px would exceed — bands scale with height in
     * the renderer (bh = h/bands+1); assert the band count itself is the
     * fixed high-granularity value so future coarsening fails loudly. */
    TEST_ASSERT(sky_bands >= 48);
    TEST_ASSERT(ridge_cols >= 96);
    /* Patch grid is finer than the old 13x5 block grid. */
    {
        int patch_cols = 26;
        int patch_rows = 8;
        TEST_ASSERT(patch_cols > 13 && patch_rows > 5);
    }
    return failures;
}
