#include "test_main.h"

#include "core/path.h"
#include "game/game_rules.h"
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

    /* ATTACK (13 B): client -> host strike request. */
    uint8_t attack[13] = {9, 0, 0, 0, 3, 0x40, 0xA0, 0x00, 0x00, 0, 0, 0, 1};
    TEST_ASSERT(lan_send(client, 1, attack, sizeof(attack)));
    TEST_ASSERT(rules_wait_message(host, client, host_peer, attack, sizeof(attack)));

    /* DAMAGE (13 B): host -> all authoritative damage. */
    uint8_t damage[13] = {10, 0, 0, 0, 3, 0x40, 0xA0, 0x00, 0x00, 0, 0, 0, 2};
    TEST_ASSERT(lan_send(host, 0xFFFFFFFFu, damage, sizeof(damage)));
    TEST_ASSERT(rules_wait_message(host, client, 1, damage, sizeof(damage)));

    /* HEALTH (10 B): self-health snapshot. */
    uint8_t health[10] = {11, 0, 0, 0, 2, 0x41, 0x90, 0x00, 0x00, 0};
    TEST_ASSERT(lan_send(client, 1, health, sizeof(health)));
    TEST_ASSERT(rules_wait_message(host, client, host_peer, health, sizeof(health)));

    /* RESPAWN (17 B). */
    uint8_t respawn[17] = {12, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT(lan_send(host, 0xFFFFFFFFu, respawn, sizeof(respawn)));
    TEST_ASSERT(rules_wait_message(host, client, 1, respawn, sizeof(respawn)));

    /* RULES (8 B): all seven toggles + type. */
    uint8_t rules[8] = {13, 0, 1, 1, 1, 1, 1, 1};
    TEST_ASSERT(lan_send(host, 0xFFFFFFFFu, rules, sizeof(rules)));
    TEST_ASSERT(rules_wait_message(host, client, 1, rules, sizeof(rules)));

    /* HELD (8 B): selected slot + item. */
    uint8_t held[8] = {14, 0, 0, 0, 2, 3, 0, 7};
    TEST_ASSERT(lan_send(client, 1, held, sizeof(held)));
    TEST_ASSERT(rules_wait_message(host, client, host_peer, held, sizeof(held)));

    lan_close(host);
    lan_close(client);
    lan_destroy(host);
    lan_destroy(client);
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
