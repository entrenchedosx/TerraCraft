#define _POSIX_C_SOURCE 199309L

#include "test_main.h"

#include "core/app.h"
#include "core/path.h"
#include "core/time.h"
#include "game/lan_protocol.h"
#include "game/player.h"
#include "platform/lan.h"
#include "platform/lan_discover.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/entity_save.h"
#include "world/world.h"
#include "world/world_meta.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static void lan_test_yield(void)
{
    Sleep(1);
}
#else
#include <time.h>
static void lan_test_yield(void)
{
    struct timespec delay = {0, 1000000L};
    (void)nanosleep(&delay, NULL);
}
#endif

typedef struct LanConnectCheck {
    bool host_connected;
    bool client_connected;
    uint32_t host_peer_id;
} LanConnectCheck;

static bool lan_test_wait_connected(LanSession *host, LanSession *client, LanConnectCheck *check)
{
    double deadline = time_now_seconds() + 3.0;
    while (time_now_seconds() < deadline) {
        LanEvent event;
        while (lan_poll(host, &event)) {
            if (event.type == LAN_EVENT_PEER_CONNECTED) {
                check->host_connected = true;
                check->host_peer_id = event.peer_id;
            }
        }
        while (lan_poll(client, &event)) {
            if (event.type == LAN_EVENT_PEER_CONNECTED && event.peer_id == LAN_SERVER_PEER_ID) {
                check->client_connected = true;
            }
        }
        if (check->host_connected && check->client_connected) {
            return true;
        }
        lan_test_yield();
    }
    return false;
}

typedef struct LanMessageCheck {
    uint32_t peer_id;
    const uint8_t *expected;
    size_t expected_size;
    bool received;
} LanMessageCheck;

static bool lan_test_check_message(const LanEvent *event, void *context)
{
    LanMessageCheck *check = (LanMessageCheck *)context;
    if (event->type == LAN_EVENT_MESSAGE && event->peer_id == check->peer_id &&
        event->size == check->expected_size && memcmp(event->payload, check->expected, check->expected_size) == 0) {
        check->received = true;
    }
    return check->received;
}

static bool lan_test_wait_messages(LanSession *host, LanSession *client, LanMessageCheck *to_host,
                                   LanMessageCheck *to_client)
{
    double deadline = time_now_seconds() + 3.0;
    while (time_now_seconds() < deadline) {
        LanEvent event;
        while (lan_poll(host, &event)) {
            (void)lan_test_check_message(&event, to_host);
            (void)lan_test_check_message(&event, to_client);
        }
        while (lan_poll(client, &event)) {
            (void)lan_test_check_message(&event, to_host);
            (void)lan_test_check_message(&event, to_client);
        }
        if (to_host->received && to_client->received) {
            return true;
        }
        lan_test_yield();
    }
    return false;
}

typedef struct LanDisconnectCheck {
    uint32_t peer_id;
    bool received;
} LanDisconnectCheck;

static bool lan_test_wait_disconnects(LanSession *host, LanSession *client, LanDisconnectCheck *host_closed,
                                     LanDisconnectCheck *client_closed)
{
    double deadline = time_now_seconds() + 3.0;
    while (time_now_seconds() < deadline) {
        LanEvent event;
        while (lan_poll(host, &event)) {
            if (event.type == LAN_EVENT_PEER_DISCONNECTED && event.peer_id == host_closed->peer_id) {
                host_closed->received = true;
            }
        }
        while (lan_poll(client, &event)) {
            if (event.type == LAN_EVENT_PEER_DISCONNECTED && event.peer_id == client_closed->peer_id) {
                client_closed->received = true;
            }
        }
        if (host_closed->received && client_closed->received) {
            return true;
        }
        lan_test_yield();
    }
    return false;
}

int test_lan_loopback_transport(void)
{
    int failures = 0;
    LanSession *host = lan_create();
    LanSession *client = lan_create();
    TEST_ASSERT(host != NULL);
    TEST_ASSERT(client != NULL);
    if (host == NULL || client == NULL) {
        lan_destroy(host);
        lan_destroy(client);
        return failures;
    }

    TEST_ASSERT(!lan_send(host, LAN_BROADCAST_PEER, "x", 1));
    TEST_ASSERT(lan_host_start(host, 0));
    uint16_t port = lan_local_port(host);
    TEST_ASSERT(port != 0);
    TEST_ASSERT(lan_is_host(host));
    TEST_ASSERT(lan_is_connected(host));
    TEST_ASSERT(!lan_host_start(host, port));
    TEST_ASSERT(lan_client_start(client, "127.0.0.1", port));

    LanConnectCheck connected = {false, false, 0};
    TEST_ASSERT(lan_test_wait_connected(host, client, &connected));
    TEST_ASSERT(connected.host_peer_id != 0);
    TEST_ASSERT(lan_is_connected(client));
    TEST_ASSERT(lan_peer_count(host) == 1);
    TEST_ASSERT(lan_peer_count(client) == 1);

    char host_address[48];
    char client_address[48];
    TEST_ASSERT(lan_peer_address(host, connected.host_peer_id, host_address, sizeof(host_address)));
    TEST_ASSERT(strstr(host_address, "127.0.0.1:") == host_address);
    char too_small[2];
    TEST_ASSERT(!lan_peer_address(host, connected.host_peer_id, too_small, sizeof(too_small)));
    TEST_ASSERT(too_small[0] == '\0');
    TEST_ASSERT(lan_peer_address(client, LAN_SERVER_PEER_ID, client_address, sizeof(client_address)));
    TEST_ASSERT(strstr(client_address, "127.0.0.1:") == client_address);
    TEST_ASSERT(!lan_peer_address(host, UINT32_MAX - 1u, host_address, sizeof(host_address)));

    uint8_t large_payload[LAN_MAX_FRAME_SIZE];
    for (size_t i = 0; i < sizeof(large_payload); ++i) {
        large_payload[i] = (uint8_t)((i * 37u) & 0xffu);
    }
    TEST_ASSERT(lan_send(client, LAN_SERVER_PEER_ID, large_payload, sizeof(large_payload)));
    TEST_ASSERT(lan_send(host, connected.host_peer_id, "reply", 5));
    TEST_ASSERT(!lan_send(client, LAN_SERVER_PEER_ID, large_payload, LAN_MAX_FRAME_SIZE + 1u));
    TEST_ASSERT(!lan_send(client, LAN_SERVER_PEER_ID, large_payload, 0));

    LanMessageCheck to_host = {connected.host_peer_id, large_payload, sizeof(large_payload), false};
    LanMessageCheck to_client = {LAN_SERVER_PEER_ID, (const uint8_t *)"reply", 5, false};
    TEST_ASSERT(lan_test_wait_messages(host, client, &to_host, &to_client));

    TEST_ASSERT(lan_disconnect_peer(client, LAN_SERVER_PEER_ID));
    LanDisconnectCheck client_closed = {LAN_SERVER_PEER_ID, false};
    LanDisconnectCheck host_closed = {connected.host_peer_id, false};
    TEST_ASSERT(lan_test_wait_disconnects(host, client, &host_closed, &client_closed));
    TEST_ASSERT(lan_peer_count(host) == 0);
    TEST_ASSERT(!lan_is_connected(client));

    lan_close(client);
    TEST_ASSERT(!lan_is_host(client));
    TEST_ASSERT(!lan_is_connected(client));
    lan_destroy(client);
    lan_destroy(host);
    return failures;
}

int test_lan_local_address_filter(void)
{
    int failures = 0;
    TEST_ASSERT(lan_ipv4_is_local_address("127.0.0.1"));
    TEST_ASSERT(lan_ipv4_is_local_address("10.42.0.8"));
    TEST_ASSERT(lan_ipv4_is_local_address("172.16.0.1"));
    TEST_ASSERT(lan_ipv4_is_local_address("172.31.255.255"));
    TEST_ASSERT(lan_ipv4_is_local_address("192.168.1.20"));
    TEST_ASSERT(lan_ipv4_is_local_address("169.254.4.2"));
    TEST_ASSERT(!lan_ipv4_is_local_address("8.8.8.8"));
    TEST_ASSERT(!lan_ipv4_is_local_address("172.15.0.1"));
    TEST_ASSERT(!lan_ipv4_is_local_address("172.32.0.1"));
    TEST_ASSERT(!lan_ipv4_is_local_address("192.167.1.20"));
    TEST_ASSERT(!lan_ipv4_is_local_address("192.168.1.256"));
    TEST_ASSERT(!lan_ipv4_is_local_address("192.168.01.20"));
    TEST_ASSERT(!lan_ipv4_is_local_address("192.168.1.20:25566"));
    TEST_ASSERT(lan_ipv4_is_local_endpoint("192.168.1.20:25566"));
    TEST_ASSERT(lan_ipv4_is_local_endpoint("127.0.0.1:1"));
    TEST_ASSERT(!lan_ipv4_is_local_endpoint("8.8.8.8:25566"));
    TEST_ASSERT(!lan_ipv4_is_local_endpoint("192.168.1.20:0"));
    TEST_ASSERT(!lan_ipv4_is_local_endpoint("192.168.1.20:65536"));
    TEST_ASSERT(!lan_ipv4_is_local_endpoint("192.168.1.20:4294967297"));
    TEST_ASSERT(!lan_ipv4_is_local_endpoint("192.168.1.20"));
    TEST_ASSERT(!lan_ipv4_is_local_endpoint(NULL));
    return failures;
}

/* Test: beacon codec round-trips and rejects malformed datagrams. */
int test_lan_discover_codec(void)
{
    int failures = 0;
    char body[256];
    size_t n = lan_discover_format_beacon(body, sizeof(body), 25566, 3, 8, "sss World 2");
    TEST_ASSERT(n > 0);
    uint16_t port = 0u;
    unsigned players = 0u, capacity = 0u;
    char name[64];
    memset(name, 0xAA, sizeof(name));
    TEST_ASSERT(lan_discover_parse_beacon(body, n, &port, &players, &capacity, name, sizeof(name)));
    TEST_ASSERT(port == 25566u);
    TEST_ASSERT(players == 3u);
    TEST_ASSERT(capacity == 8u);
    TEST_ASSERT(strcmp(name, "sss World 2") == 0);
    /* Skipped fields stay untouched. */
    TEST_ASSERT(lan_discover_parse_beacon(body, n, NULL, NULL, NULL, NULL, 0));
    /* Bad inputs rejected, outputs never written. */
    TEST_ASSERT(lan_discover_format_beacon(NULL, 64, 25566, 1, 8, "x") == 0);
    TEST_ASSERT(lan_discover_format_beacon(body, 4, 25566, 1, 8, "x") == 0);
    TEST_ASSERT(lan_discover_format_beacon(body, sizeof(body), 0, 1, 8, "x") == 0);
    TEST_ASSERT(!lan_discover_parse_beacon(NULL, 10, &port, &players, &capacity, name, sizeof(name)));
    TEST_ASSERT(!lan_discover_parse_beacon(body, 0, &port, &players, &capacity, name, sizeof(name)));
    TEST_ASSERT(!lan_discover_parse_beacon("NOPE 1 2/3 x", 13, &port, &players, &capacity, name,
                                           sizeof(name)));
    TEST_ASSERT(!lan_discover_parse_beacon("TCRAFT1 0 1/8 x", 15, &port, &players, &capacity, name,
                                           sizeof(name)));
    TEST_ASSERT(!lan_discover_parse_beacon("TCRAFT1 70000 1/8 x", 19, &port, &players, &capacity,
                                           name, sizeof(name)));
    TEST_ASSERT(!lan_discover_parse_beacon("TCRAFT1 1 999/8 x", 17, &port, &players, &capacity, name,
                                           sizeof(name)));
    TEST_ASSERT(!lan_discover_parse_beacon("TCRAFT1 1 1/8 ", 14, &port, &players, &capacity, name,
                                           sizeof(name)));
    TEST_ASSERT(!lan_discover_parse_beacon("TCRAFT1 1 1/8 \x01\x02", 16, &port, &players, &capacity,
                                           name, sizeof(name)));
    /* Unsanitary names are cleaned, never stored raw. */
    n = lan_discover_format_beacon(body, sizeof(body), 1, 300, 9999, "a\x01"
                                                                        "b\nc");
    TEST_ASSERT(n > 0);
    TEST_ASSERT(lan_discover_parse_beacon(body, n, &port, &players, &capacity, name, sizeof(name)));
    TEST_ASSERT(strcmp(name, "abc") == 0);
    TEST_ASSERT(players == 255u);
    TEST_ASSERT(capacity == 255u);
    n = lan_discover_format_beacon(body, sizeof(body), 1, 1, 8, "");
    TEST_ASSERT(n > 0);
    TEST_ASSERT(lan_discover_parse_beacon(body, n, &port, &players, &capacity, name, sizeof(name)));
    TEST_ASSERT(strcmp(name, "TerraCraft World") == 0);
    return failures;
}

/* Pump polls until an entry appears (loopback query/answer). */
static bool lan_test_wait_server(LanDiscover *d)
{
    double deadline = time_now_seconds() + 4.0;
    while (time_now_seconds() < deadline) {
        lan_discover_poll(d, 0.05f);
        if (lan_discover_count(d) > 0) {
            return true;
        }
        lan_test_yield();
    }
    return false;
}

/* Test: live loopback discovery (own query answered, beacon stored,
 * entries expire) plus NULL-safety. */
int test_lan_discover_scan(void)
{
    int failures = 0;
    lan_discover_destroy(NULL);
    TEST_ASSERT(!lan_discover_scan(NULL));
    lan_discover_poll(NULL, 0.1f);
    TEST_ASSERT(lan_discover_count(NULL) == 0);
    TEST_ASSERT(lan_discover_at(NULL, 0) == NULL);
    lan_discover_host(NULL, "x", 1, 8, 25566, 0.1f);
    TEST_ASSERT(lan_discover_format_beacon(NULL, 0, 0, 0, 0, NULL) == 0);

    LanDiscover *d = lan_discover_create();
    TEST_ASSERT(d != NULL);
    if (d == NULL) {
        return failures;
    }
    TEST_ASSERT(lan_discover_count(d) == 0);
    TEST_ASSERT(lan_discover_at(d, 0) == NULL);
    lan_discover_host(d, "Loopback World", 2, 8, 25566, 2.0f);
    TEST_ASSERT(lan_discover_scan(d));
    TEST_ASSERT(lan_test_wait_server(d));
    /* Our query can arrive over several local paths (broadcast,
     * loopback); each sender address gets its own entry. */
    TEST_ASSERT(lan_discover_count(d) >= 1);
    const LanServerInfo *info = NULL;
    for (size_t i = 0; i < lan_discover_count(d); ++i) {
        const LanServerInfo *entry = lan_discover_at(d, i);
        if (entry != NULL && entry->port == 25566u) {
            info = entry;
            break;
        }
    }
    TEST_ASSERT(info != NULL);
    if (info != NULL) {
        /* Loopback self-delivery reports 127.0.0.1 or the LAN address. */
        TEST_ASSERT(lan_ipv4_is_local_address(info->address));
        TEST_ASSERT(info->port == 25566u);
        TEST_ASSERT(strcmp(info->name, "Loopback World") == 0);
        TEST_ASSERT(info->players == 2u);
        TEST_ASSERT(info->capacity == 8u);
    }
    /* Stale entries age out (poll clamps dt to 1 s per call). */
    for (int i = 0; i < 10; ++i) {
        lan_discover_poll(d, 10.0f);
    }
    TEST_ASSERT(lan_discover_count(d) == 0);
    lan_discover_destroy(d);
    return failures;
}

/* --- PvP handler integration: two bare AppContexts, real loopback
 * sockets, the actual app_lan_handle_message / app_lan_update paths.
 * No window/renderer/audio needed (all handler-touchable subsystems are
 * NULL- or zero-safe by construction, verified in audit). */

static void pvp_sleep_ms(unsigned ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    (void)nanosleep(&ts, NULL);
#endif
}

typedef struct PvpPair {
    AppContext *host;
    AppContext *client;
    LanSession *hs;
    LanSession *cs;
    World *hworld;
    World *cworld;
    uint32_t cpid; /* Client transport/game ID as seen by the host. */
} PvpPair;

static AppContext *pvp_make_app(LanSession *s, World *w, bool is_host, uint32_t local_id,
                                const char *name, Vec3 pos)
{
    AppContext *a = (AppContext *)calloc(1, sizeof(AppContext));
    if (a == NULL) {
        return NULL;
    }
    a->world = w;
    a->world_open = true;
    player_init(&a->player);
    a->player.pos = pos;
    a->player.render_pos = pos;
    a->lan = s;
    a->lan_host = is_host;
    a->lan_client = !is_host;
    a->lan_local_player_id = local_id;
    snprintf(a->username, sizeof(a->username), "%s", name);
    a->state = GAME_STATE_PLAYING;
    return a;
}

/* Connect a host/client loopback pair and seed identity entries both ways.
 * Client stands at cpos; the host caches it. Returns false on setup error. */
static bool pvp_pair_connect(PvpPair *pair, Vec3 hpos, Vec3 cpos)
{
    memset(pair, 0, sizeof(*pair));
    pair->hs = lan_create();
    pair->cs = lan_create();
    if (pair->hs == NULL || pair->cs == NULL) {
        return false;
    }
    if (!lan_host_start(pair->hs, 0)) {
        return false;
    }
    if (!lan_client_start(pair->cs, "127.0.0.1", lan_local_port(pair->hs))) {
        return false;
    }
    bool hc = false;
    bool cc = false;
    uint32_t cpid = 0;
    double deadline = time_now_seconds() + 3.0;
    while (time_now_seconds() < deadline && !(hc && cc)) {
        LanEvent event;
        while (lan_poll(pair->hs, &event)) {
            if (event.type == LAN_EVENT_PEER_CONNECTED) {
                hc = true;
                cpid = event.peer_id;
            }
        }
        while (lan_poll(pair->cs, &event)) {
            if (event.type == LAN_EVENT_PEER_CONNECTED && event.peer_id == LAN_SERVER_PEER_ID) {
                cc = true;
            }
        }
        lan_test_yield();
    }
    if (!hc || !cc || cpid == 0) {
        return false;
    }
    pair->cpid = cpid;
    pair->hworld = world_create();
    pair->cworld = world_create();
    if (pair->hworld == NULL || pair->cworld == NULL) {
        return false;
    }
    pair->host = pvp_make_app(pair->hs, pair->hworld, true, 0, "HostUser", hpos);
    pair->client = pvp_make_app(pair->cs, pair->cworld, false, cpid, "ClientUser", cpos);
    if (pair->host == NULL || pair->client == NULL) {
        return false;
    }
    /* Host-side cache of the client (as HELLO + PLAYER snapshots would). */
    LanRemotePlayer *hcEntry = &pair->host->lan_players[0];
    hcEntry->active = true;
    hcEntry->peer_id = cpid;
    memcpy(hcEntry->username, "ClientUser", 11);
    hcEntry->pos = cpos;
    hcEntry->health = 20.0f;
    hcEntry->has_health = true;
    pair->host->lan_player_count = 1;
    /* Client-side cache of the host. */
    LanRemotePlayer *chEntry = &pair->client->lan_players[0];
    chEntry->active = true;
    chEntry->peer_id = 0;
    memcpy(chEntry->username, "HostUser", 9);
    chEntry->pos = hpos;
    chEntry->health = 20.0f;
    chEntry->has_health = true;
    pair->client->lan_player_count = 1;
    return true;
}

static void pvp_pair_destroy(PvpPair *pair)
{
    if (pair == NULL) {
        return;
    }
    lan_close(pair->hs);
    lan_close(pair->cs);
    lan_destroy(pair->hs);
    lan_destroy(pair->cs);
    world_destroy(pair->hworld);
    world_destroy(pair->cworld);
    free(pair->host);
    free(pair->client);
    memset(pair, 0, sizeof(*pair));
}

/* Settle the wire: poll both sessions through the real transport, routing
 * every received MESSAGE into its owner's actual handler, until several
 * consecutive rounds observe no activity (bounded by a deadline).
 * Fixed microsecond round counts are NOT enough: the sender flushes TX
 * bytes on its poll and the kernel delivers loopback frames
 * asynchronously, so settlement must be quiescence-driven. */
static void pvp_settle(PvpPair *pair)
{
    double deadline = time_now_seconds() + 3.0;
    int idle = 0;
    while (time_now_seconds() < deadline && idle < 5) {
        bool activity = false;
        LanEvent event;
        while (lan_poll(pair->cs, &event)) {
            activity = true;
            if (event.type == LAN_EVENT_MESSAGE) {
                app_lan_handle_message(pair->client, &event);
            }
        }
        while (lan_poll(pair->hs, &event)) {
            activity = true;
            if (event.type == LAN_EVENT_MESSAGE) {
                app_lan_handle_message(pair->host, &event);
            }
        }
        if (activity) {
            idle = 0;
        } else {
            idle++;
            lan_test_yield();
        }
    }
}

/* Client sends a melee ATTACK at the host through the real socket. */
static void pvp_client_attack(PvpPair *pair, uint32_t victim, float damage, uint32_t seq)
{
    uint8_t buf[LAN_MSG_ATTACK_SIZE];
    lan_attack_encode(buf, victim, damage, seq);
    (void)lan_send(pair->cs, LAN_SERVER_PEER_ID, buf, sizeof(buf));
    pvp_settle(pair);
}

/* Client sends a ranged ATTACK at the host through the real socket. */
static void pvp_client_attack_ranged(PvpPair *pair, uint32_t victim, float damage, uint32_t seq)
{
    uint8_t buf[LAN_MSG_ATTACK_RANGED_SIZE];
    lan_attack_ranged_encode(buf, victim, damage, seq);
    (void)lan_send(pair->cs, LAN_SERVER_PEER_ID, buf, sizeof(buf));
    pvp_settle(pair);
}

/* Bare test contexts run no game ticks, so the victim hurt window never
 * decays on its own (production decays it in app_tick_playing). Clear it
 * explicitly before strikes that are meant to land. */
static void pvp_clear_hurt(AppContext *app)
{
    if (app != NULL) {
        app->player.hurt_t = 0.0f;
    }
}

/* Settle the wire, forwarding host broadcasts into the client's handler;
 * captures the first frame of a wanted type. Multi-round so queued
 * replies flush (a broadcast queued while handling needs a later poll
 * to leave its sender). */
static bool pvp_forward_to_client(PvpPair *pair, uint8_t want_type, uint8_t *out,
                                  size_t out_cap, size_t *out_size)
{
    bool found = false;
    double deadline = time_now_seconds() + 2.0;
    while (time_now_seconds() < deadline && !found) {
        LanEvent event;
        while (lan_poll(pair->hs, &event)) {
            if (event.type == LAN_EVENT_MESSAGE) {
                app_lan_handle_message(pair->host, &event);
            }
        }
        while (lan_poll(pair->cs, &event)) {
            if (event.type != LAN_EVENT_MESSAGE) {
                continue;
            }
            if (!found && event.size > 0 && event.payload[0] == want_type) {
                size_t n = event.size < out_cap ? event.size : out_cap;
                memcpy(out, event.payload, n);
                if (out_size != NULL) {
                    *out_size = event.size;
                }
                found = true;
            }
            app_lan_handle_message(pair->client, &event);
        }
        if (!found) {
            lan_test_yield();
        }
    }
    return found;
}

/* Test: melee ATTACK validation end-to-end through real handlers.
 *
 * Returns: failure count.
 */
int test_lan_pvp_melee_handlers(void)
{
    int failures = 0;
    PvpPair pair;
    TEST_ASSERT(pvp_pair_connect(&pair, mmath_vec3(0.0f, 70.0f, 0.0f),
                                 mmath_vec3(2.0f, 70.0f, 0.0f)));
    if (pair.host == NULL) {
        pvp_pair_destroy(&pair);
        return failures + 1;
    }
    LanRemotePlayer *attacker = &pair.host->lan_players[0];

    /* Valid first strike: host 20 -> 15, DAMAGE broadcast on the wire.
     * Send without pre-settling so the capture below observes the actual
     * broadcast (a settle first would consume it into the cache). */
    {
        uint8_t atk[LAN_MSG_ATTACK_SIZE];
        lan_attack_encode(atk, 0, 5.0f, 1);
        TEST_ASSERT(lan_send(pair.cs, LAN_SERVER_PEER_ID, atk, sizeof(atk)));
    }
    {
        uint8_t dmg[LAN_MSG_DAMAGE_SIZE];
        size_t n = 0;
        TEST_ASSERT(pvp_forward_to_client(&pair, LAN_MSG_DAMAGE, dmg, sizeof(dmg), &n));
        TEST_ASSERT(n == LAN_MSG_DAMAGE_SIZE);
        uint32_t victim = 0;
        float dd = 0.0f;
        uint32_t atk = 0;
        TEST_ASSERT(lan_damage_decode(dmg, n, &victim, &dd, &atk));
        TEST_ASSERT(victim == 0 && atk == pair.cpid);
        TEST_ASSERT_FLOAT_EQ(dd, 5.0f, 1e-4f);
    }
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 15.0f, 1e-4f);
    TEST_ASSERT(attacker->last_attack_seq == 1);
    /* Victim 0 is the host: client nudges its host cache 20 -> 15. */
    TEST_ASSERT_FLOAT_EQ(pair.client->lan_players[0].health, 15.0f, 1e-4f);
    /* Exact replay rejected (retransmission safe). */
    pvp_client_attack(&pair, 0, 5.0f, 1);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 15.0f, 1e-4f);
    /* Fresh strike after hurt/rate windows: 15 -> 10. */
    pvp_sleep_ms(650);
    pvp_clear_hurt(pair.host);
    pvp_client_attack(&pair, 0, 5.0f, 2);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 10.0f, 1e-4f);
    /* Out of range rejected. */
    attacker->pos = mmath_vec3(30.0f, 70.0f, 0.0f);
    pvp_sleep_ms(650);
    pvp_client_attack(&pair, 0, 5.0f, 3);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 10.0f, 1e-4f);
    attacker->pos = mmath_vec3(2.0f, 70.0f, 0.0f);
    /* Stale victim ID rejected without fabricating table entries. */
    {
        unsigned before = pair.host->lan_player_count;
        pvp_sleep_ms(650);
        pvp_client_attack(&pair, 99, 5.0f, 4);
        TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 10.0f, 1e-4f);
        TEST_ASSERT(pair.host->lan_player_count == before);
    }
    /* Self-hit rejected. */
    pvp_sleep_ms(650);
    pvp_client_attack(&pair, pair.cpid, 5.0f, 5);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 10.0f, 1e-4f);
    /* PvP-disabled world rejects. */
    pair.hworld->rules.pvp = false;
    pvp_sleep_ms(650);
    pvp_client_attack(&pair, 0, 5.0f, 6);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 10.0f, 1e-4f);
    pair.hworld->rules.pvp = true;
    /* Sequence epoch rollover stays live (positional checks still apply). */
    attacker->last_attack_seq = UINT32_MAX;
    pvp_sleep_ms(650);
    pvp_clear_hurt(pair.host);
    pvp_client_attack(&pair, 0, 5.0f, 1);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 5.0f, 1e-4f);
    TEST_ASSERT(attacker->last_attack_seq == 1);
    /* Immediate follow-up trips the authoritative rate floor. */
    pvp_client_attack(&pair, 0, 5.0f, 2);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 5.0f, 1e-4f);
    pvp_pair_destroy(&pair);
    return failures;
}

/* Test: death converges via HEALTH, respawn converges atomically with
 * wire-carried (not assumed) health.
 *
 * Returns: failure count.
 */
int test_lan_pvp_death_respawn(void)
{
    int failures = 0;
    PvpPair pair;
    TEST_ASSERT(pvp_pair_connect(&pair, mmath_vec3(0.0f, 70.0f, 0.0f),
                                 mmath_vec3(2.0f, 70.0f, 0.0f)));
    if (pair.host == NULL) {
        pvp_pair_destroy(&pair);
        return failures + 1;
    }
    /* Lethal strike: host 3 HP -> dead. */
    pair.host->player.health = 3.0f;
    pvp_client_attack(&pair, 0, 5.0f, 1);
    TEST_ASSERT(pair.host->player.dead == true);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 0.0f, 1e-4f);
    /* The replication pump emits the death HEALTH; the client converges.
     * dt 0.2 clears the pump's 0.15 s change batching (production calls
     * this every frame, so the timer is always hot there). */
    app_lan_update(pair.host, 0.2);
    {
        uint8_t frame[LAN_MSG_HEALTH_SIZE];
        size_t n = 0;
        TEST_ASSERT(pvp_forward_to_client(&pair, LAN_MSG_HEALTH, frame, sizeof(frame), &n));
        uint32_t pid = 0;
        float hp = -1.0f;
        bool dead = false;
        bool creative = true;
        TEST_ASSERT(lan_health_decode(frame, n, &pid, &hp, &dead, &creative));
        TEST_ASSERT(pid == 0 && dead == true && creative == false);
        TEST_ASSERT_FLOAT_EQ(hp, 0.0f, 1e-4f);
    }
    TEST_ASSERT(pair.client->lan_players[0].dead == true);
    TEST_ASSERT_FLOAT_EQ(pair.client->lan_players[0].health, 0.0f, 1e-4f);
    /* Respawn converges atomically (position + exact health, one pump). */
    pair.host->spawn_point = mmath_vec3(5.0f, 70.0f, 5.0f);
    pair.host->has_spawn_point = true;
    app_respawn_player(pair.host);
    TEST_ASSERT(pair.host->player.dead == false);
    {
        uint8_t frame[LAN_MSG_RESPAWN_SIZE];
        size_t n = 0;
        TEST_ASSERT(pvp_forward_to_client(&pair, LAN_MSG_RESPAWN, frame, sizeof(frame), &n));
        uint32_t pid = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f, hp = 0.0f;
        TEST_ASSERT(lan_respawn_decode(frame, n, &pid, &x, &y, &z, &hp));
        TEST_ASSERT(pid == 0);
        TEST_ASSERT_FLOAT_EQ(hp, 20.0f, 1e-4f);
    }
    TEST_ASSERT(pair.client->lan_players[0].dead == false);
    TEST_ASSERT_FLOAT_EQ(pair.client->lan_players[0].pos.x, 5.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(pair.client->lan_players[0].health, 20.0f, 1e-4f);
    /* Wire health is authoritative: a non-full value applies verbatim
     * (the old 20.0 assumption would fail this assertion). */
    {
        uint8_t frame[LAN_MSG_RESPAWN_SIZE];
        lan_respawn_encode(frame, 0, 9.0f, 70.0f, 9.0f, 13.5f);
        LanEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = LAN_EVENT_MESSAGE;
        ev.peer_id = LAN_SERVER_PEER_ID;
        ev.size = (uint32_t)sizeof(frame);
        memcpy(ev.payload, frame, sizeof(frame));
        app_lan_handle_message(pair.client, &ev);
        TEST_ASSERT_FLOAT_EQ(pair.client->lan_players[0].health, 13.5f, 1e-4f);
    }
    pvp_pair_destroy(&pair);
    return failures;
}

/* Test: version handshake rejects mismatched peers instead of playing a
 * silently inconsistent session.
 *
 * Returns: failure count.
 */
int test_lan_version_reject(void)
{
    int failures = 0;
    PvpPair pair;
    TEST_ASSERT(pvp_pair_connect(&pair, mmath_vec3(0.0f, 70.0f, 0.0f),
                                 mmath_vec3(2.0f, 70.0f, 0.0f)));
    if (pair.host == NULL) {
        pvp_pair_destroy(&pair);
        return failures + 1;
    }
    {
        /* Pinned (not a manifest-constant comparison): combat replication
         * requires v4; v3 peers must be rejected, never silently accepted. */
        int wire_version = LAN_PROTOCOL_VERSION;
        TEST_ASSERT(wire_version == 4);
    }
    TEST_ASSERT(lan_version_supported(4) && !lan_version_supported(3));
    /* v3 HELLO: host drops the peer; the client observes the disconnect. */
    {
        const char *name = "OldClient";
        size_t nl = strlen(name);
        uint8_t hello[3 + 16];
        hello[0] = (uint8_t)LAN_MSG_HELLO;
        hello[1] = 3;
        hello[2] = (uint8_t)nl;
        memcpy(hello + 3, name, nl);
        TEST_ASSERT(lan_send(pair.cs, LAN_SERVER_PEER_ID, hello, 3 + nl));
        /* One loop drives the whole rejection: cs flushes HELLO, hs
         * receives/handles/drops, cs observes the disconnect. Routing
         * cs events anywhere else would eat the evidence. */
        bool dropped = false;
        double deadline = time_now_seconds() + 3.0;
        while (time_now_seconds() < deadline && !dropped) {
            LanEvent event;
            while (lan_poll(pair.cs, &event)) {
                if (event.type == LAN_EVENT_PEER_DISCONNECTED &&
                    event.peer_id == LAN_SERVER_PEER_ID) {
                    dropped = true;
                }
            }
            while (lan_poll(pair.hs, &event)) {
                if (event.type == LAN_EVENT_MESSAGE) {
                    app_lan_handle_message(pair.host, &event);
                }
            }
            lan_test_yield();
        }
        TEST_ASSERT(dropped);
    }
    pvp_pair_destroy(&pair);
    return failures;
}

/* Test: arrow-range strikes validate at arrow budgets while melee stays
 * short-range; a solid wall vetoes arrows.
 *
 * Returns: failure count.
 */
int test_lan_pvp_ranged_handlers(void)
{
    int failures = 0;
    PvpPair pair;
    TEST_ASSERT(pvp_pair_connect(&pair, mmath_vec3(0.0f, 70.0f, 0.0f),
                                 mmath_vec3(10.0f, 70.0f, 0.0f)));
    if (pair.host == NULL) {
        pvp_pair_destroy(&pair);
        return failures + 1;
    }
    LanRemotePlayer *attacker = &pair.host->lan_players[0];
    /* Melee at 10 blocks: rejected (range budget unchanged by arrows). */
    pvp_client_attack(&pair, 0, 4.0f, 1);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 20.0f, 1e-4f);
    TEST_ASSERT(attacker->last_attack_seq == 0);
    /* Same geometry as an arrow: accepted. */
    pvp_sleep_ms(300);
    pvp_client_attack_ranged(&pair, 0, 4.0f, 2);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 16.0f, 1e-4f);
    /* Stone wall between shooter and victim vetoes the next arrow. */
    {
        Chunk *c = chunk_create(0, 0);
        TEST_ASSERT(c != NULL);
        if (c == NULL) {
            pvp_pair_destroy(&pair);
            return failures + 1;
        }
        for (int y = 68; y <= 73; ++y) {
            chunk_set_block(c, 5, y, 0, BLOCK_STONE);
        }
        TEST_ASSERT(world_add_chunk(pair.hworld, c) == 0);
    }
    pair.host->player.hurt_t = 0.0f;
    pvp_sleep_ms(650);
    pvp_client_attack_ranged(&pair, 0, 4.0f, 3);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 16.0f, 1e-4f);
    pvp_pair_destroy(&pair);
    return failures;
}

/* Inject one crafted frame into an app's real handler (peer id selectable;
 * used for spoof and multi-remote cases the single socket cannot produce). */
static void pvp_inject(AppContext *app, uint32_t peer_id, const uint8_t *payload, size_t size)
{
    LanEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = LAN_EVENT_MESSAGE;
    ev.peer_id = peer_id;
    ev.size = (uint32_t)size;
    memcpy(ev.payload, payload, size);
    app_lan_handle_message(app, &ev);
}

/* Test: creative immunity is authoritative (host rejects), mode reports
 * are clamped to the world mode (spoof-proof), and arrow damage is
 * capped at the strongest bow shot.
 *
 * Returns: failure count.
 */
int test_lan_pvp_creative_handlers(void)
{
    int failures = 0;
    PvpPair pair;
    TEST_ASSERT(pvp_pair_connect(&pair, mmath_vec3(0.0f, 70.0f, 0.0f),
                                 mmath_vec3(2.0f, 70.0f, 0.0f)));
    if (pair.host == NULL) {
        pvp_pair_destroy(&pair);
        return failures + 1;
    }
    /* Second remote acts as the attacker; the client entry is the victim. */
    LanRemotePlayer *attacker = &pair.host->lan_players[1];
    attacker->active = true;
    attacker->peer_id = 7;
    memcpy(attacker->username, "Attacker", 9);
    attacker->pos = mmath_vec3(2.0f, 70.0f, 2.0f);
    attacker->health = 20.0f;
    attacker->has_health = true;
    pair.host->lan_player_count = 2;
    LanRemotePlayer *victim = &pair.host->lan_players[0];

    /* Creative victim: melee strike rejected before sequence accounting. */
    victim->creative = true;
    {
        uint8_t atk[LAN_MSG_ATTACK_SIZE];
        lan_attack_encode(atk, pair.cpid, 5.0f, 1);
        pvp_inject(pair.host, 7, atk, sizeof(atk));
        TEST_ASSERT_FLOAT_EQ(victim->health, 20.0f, 1e-4f);
        TEST_ASSERT(attacker->last_attack_seq == 0);
    }
    /* Same geometry, survival victim: accepted. */
    victim->creative = false;
    pvp_sleep_ms(250);
    {
        uint8_t atk[LAN_MSG_ATTACK_SIZE];
        lan_attack_encode(atk, pair.cpid, 5.0f, 1);
        pvp_inject(pair.host, 7, atk, sizeof(atk));
        TEST_ASSERT(attacker->last_attack_seq == 1);
        TEST_ASSERT_FLOAT_EQ(victim->health, 15.0f, 1e-4f);
    }
    /* Creative victim: arrow rejected the same way. */
    victim->creative = true;
    victim->health = 20.0f;
    pvp_sleep_ms(250);
    {
        uint8_t atk[LAN_MSG_ATTACK_RANGED_SIZE];
        lan_attack_ranged_encode(atk, pair.cpid, 4.0f, 2);
        pvp_inject(pair.host, 7, atk, sizeof(atk));
        TEST_ASSERT_FLOAT_EQ(victim->health, 20.0f, 1e-4f);
        TEST_ASSERT(attacker->last_attack_seq == 1);
    }
    victim->creative = false;
    /* Spoofed mode report is clamped to the survival world mode. */
    {
        uint8_t frame[LAN_MSG_HEALTH_SIZE];
        lan_health_encode(frame, pair.cpid, 20.0f, false, true);
        pvp_inject(pair.host, pair.cpid, frame, sizeof(frame));
        TEST_ASSERT(victim->creative == false);
        TEST_ASSERT_FLOAT_EQ(victim->health, 20.0f, 1e-4f);
    }
    /* ... and a survival claim is clamped up in a creative world. */
    pair.hworld->mode = 1;
    {
        uint8_t frame[LAN_MSG_HEALTH_SIZE];
        lan_health_encode(frame, pair.cpid, 20.0f, false, false);
        pvp_inject(pair.host, pair.cpid, frame, sizeof(frame));
        TEST_ASSERT(victim->creative == true);
    }
    pair.hworld->mode = 0;
    /* Inflated arrow damage is capped at 6 (host is the victim here). */
    pvp_clear_hurt(pair.host);
    pvp_sleep_ms(250);
    {
        uint8_t atk[LAN_MSG_ATTACK_RANGED_SIZE];
        lan_attack_ranged_encode(atk, 0, 20.0f, 3);
        pvp_inject(pair.host, pair.cpid, atk, sizeof(atk));
        TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 14.0f, 1e-4f);
    }
    pvp_pair_destroy(&pair);
    return failures;
}

/* Scratch cleanup for the rules-editing persistence check. */
static void pvp_rules_tmp_cleanup(const char *dir)
{
    char meta[1024];
    char ents[1024];
    if (path_join(meta, sizeof(meta), dir, WORLD_META_FILE) == 0) {
        path_remove_file(meta);
    }
    if (path_join(ents, sizeof(ents), dir, ENTITY_SAVE_FILE) == 0) {
        path_remove_file(ents);
    }
    path_remove_dir(dir);
}

/* Test: only host-local controls edit world rules. Guest packets never
 * mutate/persist rules, even with cheats enabled. Host changes still
 * broadcast, persist, and take effect immediately.
 *
 * Returns: failure count.
 */
int test_lan_rules_editing(void)
{
    int failures = 0;
    PvpPair pair;
    TEST_ASSERT(pvp_pair_connect(&pair, mmath_vec3(0.0f, 70.0f, 0.0f),
                                 mmath_vec3(2.0f, 70.0f, 0.0f)));
    if (pair.host == NULL) {
        pvp_pair_destroy(&pair);
        return failures + 1;
    }
    const char *dir = "test_tmp_rules";
    pvp_rules_tmp_cleanup(dir);
    TEST_ASSERT(path_mkdir_p(dir) == 0);
    world_set_save_dir(pair.hworld, dir);
    snprintf(pair.hworld->name, sizeof(pair.hworld->name), "RulesEdit");
    snprintf(pair.host->world_dir, sizeof(pair.host->world_dir), "%s", dir);

    /* Unknown peer: ignored, rules untouched. */
    {
        uint8_t frame[LAN_MSG_RULES_SET_SIZE];
        frame[0] = (uint8_t)LAN_MSG_RULES_SET;
        GameRules r;
        game_rules_defaults(&r);
        r.pvp = false;
        lan_rules_encode(frame + 1, &r);
        pvp_inject(pair.host, 99, frame, sizeof(frame));
        TEST_ASSERT(pair.hworld->rules.pvp == true);
    }
    /* Clients never act on RULES_SET (only the host decides). */
    {
        uint8_t frame[LAN_MSG_RULES_SET_SIZE];
        frame[0] = (uint8_t)LAN_MSG_RULES_SET;
        GameRules r;
        game_rules_defaults(&r);
        r.pvp = false;
        lan_rules_encode(frame + 1, &r);
        pvp_inject(pair.client, LAN_SERVER_PEER_ID, frame, sizeof(frame));
        TEST_ASSERT(pair.cworld->rules.pvp == true);
    }
    /* Registered guests cannot modify rules with cheats off or on. */
    for(int cheats=0;cheats<2;++cheats) {
        pair.hworld->rules.allow_cheats=cheats!=0;
        GameRules before=pair.hworld->rules,attack=before;
        attack.allow_cheats=!before.allow_cheats;
        attack.keep_inventory=true;attack.pvp=false;
        uint8_t request[LAN_MSG_RULES_SET_SIZE];
        request[0]=LAN_MSG_RULES_SET;lan_rules_encode(request+1,&attack);
        pvp_inject(pair.host,pair.cpid,request,sizeof(request));
        TEST_ASSERT(game_rules_equal(&before,&pair.hworld->rules));
    }
    { char meta[1024];path_join(meta,sizeof(meta),dir,WORLD_META_FILE);TEST_ASSERT(!path_is_file(meta)); }
    /* Host-local edits still broadcast RULES and persist to world.meta. */
    {
        uint8_t frame[LAN_MSG_RULES_SET_SIZE];
        frame[0] = (uint8_t)LAN_MSG_RULES_SET;
        GameRules r;
        game_rules_defaults(&r);
        r.pvp = false;
        r.keep_inventory = true;
        lan_rules_encode(frame + 1, &r);
        pair.hworld->rules=r;
        app_apply_world_rules(pair.host);
        uint8_t rules[LAN_MSG_RULES_SIZE];
        size_t n = 0;
        TEST_ASSERT(pvp_forward_to_client(&pair, LAN_MSG_RULES, rules, sizeof(rules), &n));
        TEST_ASSERT(pair.hworld->rules.pvp == false);
        TEST_ASSERT(pair.hworld->rules.keep_inventory == true);
        GameRules back;
        lan_rules_decode(&back, rules + 1);
        TEST_ASSERT(back.pvp == false && back.keep_inventory == true);
        /* Client applies the broadcast like any RULES frame. */
        TEST_ASSERT(pair.cworld->rules.pvp == false);
        WorldMeta m;
        memset(&m, 0, sizeof(m));
        TEST_ASSERT(world_meta_read(dir, &m) == 0);
        TEST_ASSERT(m.rules.pvp == false);
        TEST_ASSERT(m.rules.keep_inventory == true);
    }
    /* Enforcement is immediate: PvP strikes now rejected pre-sequence. */
    pvp_client_attack(&pair, 0, 5.0f, 1);
    TEST_ASSERT_FLOAT_EQ(pair.host->player.health, 20.0f, 1e-4f);
    TEST_ASSERT(pair.host->lan_players[0].last_attack_seq == 0);
    pvp_rules_tmp_cleanup(dir);
    pvp_pair_destroy(&pair);
    return failures;
}

/* Build a PLAYER snapshot frame by hand (same layout the app sends). */
static size_t pvp_player_frame(uint8_t *out, uint32_t pid, Vec3 pos, float yaw, const char *name)
{
    size_t nl = strlen(name);
    size_t at = 0;
    out[at++] = (uint8_t)LAN_MSG_PLAYER;
    lan_write_u32(out + at, pid);
    at += 4;
    lan_write_f32(out + at, pos.x);
    at += 4;
    lan_write_f32(out + at, pos.y);
    at += 4;
    lan_write_f32(out + at, pos.z);
    at += 4;
    lan_write_f32(out + at, yaw);
    at += 4;
    lan_write_f32(out + at, 0.0f);
    at += 4;
    lan_write_f32(out + at, 0.0f);
    at += 4;
    out[at++] = 0;
    out[at++] = (uint8_t)nl;
    memcpy(out + at, name, nl);
    at += nl;
    return at; /* 31 + N. */
}

/* Test: a late joiner receives a coherent snapshot (WELCOME + every
 * peer's movement, rules, health, and held item) through production code.
 *
 * Returns: failure count.
 */
int test_lan_late_join_snapshot(void)
{
    int failures = 0;
    PvpPair pair;
    TEST_ASSERT(pvp_pair_connect(&pair, mmath_vec3(0.0f, 70.0f, 0.0f),
                                 mmath_vec3(5.0f, 70.0f, 5.0f)));
    if (pair.host == NULL) {
        pvp_pair_destroy(&pair);
        return failures + 1;
    }
    /* Distinctive host state: custom rules, off-full health, held item. */
    game_rules_defaults(&pair.hworld->rules);
    pair.hworld->rules.pvp = false;
    pair.hworld->rules.daylight_cycle = false;
    pair.host->player.health = 13.5f;
    pair.host->player.hotbar_sel = 4;
    pair.host->player.inv.slots[4].item = (ItemId)BLOCK_STONE;
    pair.host->player.inv.slots[4].count = 1;
    /* Distinctive existing-peer state on the host. */
    LanRemotePlayer *existing = &pair.host->lan_players[0];
    existing->pos = mmath_vec3(5.0f, 70.0f, 5.0f);
    existing->yaw = 1.0f;
    existing->health = 11.0f;
    existing->has_health = true;
    existing->held_slot = 1;
    existing->held_item = (uint16_t)BLOCK_DIRT;
    existing->has_held = true;
    /* Second transport-only client joins late. */
    LanSession *late = lan_create();
    TEST_ASSERT(late != NULL);
    if (late == NULL) {
        pvp_pair_destroy(&pair);
        return failures + 1;
    }
    TEST_ASSERT(lan_client_start(late, "127.0.0.1", lan_local_port(pair.hs)));
    bool cc = false;
    double deadline = time_now_seconds() + 3.0;
    while (time_now_seconds() < deadline && !cc) {
        LanEvent event;
        while (lan_poll(late, &event)) {
            if (event.type == LAN_EVENT_PEER_CONNECTED && event.peer_id == LAN_SERVER_PEER_ID) {
                cc = true;
            }
        }
        while (lan_poll(pair.hs, &event)) {
            if (event.type == LAN_EVENT_MESSAGE) {
                app_lan_handle_message(pair.host, &event);
            }
        }
        lan_test_yield();
    }
    TEST_ASSERT(cc);
    /* Late client says HELLO; the host answers with the full snapshot. */
    {
        const char *name = "LateJoin";
        size_t nl = strlen(name);
        uint8_t hello[3 + 16];
        hello[0] = (uint8_t)LAN_MSG_HELLO;
        hello[1] = (uint8_t)LAN_PROTOCOL_VERSION;
        hello[2] = (uint8_t)nl;
        memcpy(hello + 3, name, nl);
        TEST_ASSERT(lan_send(late, LAN_SERVER_PEER_ID, hello, 3 + nl));
    }
    bool saw_welcome = false;
    bool saw_host_player = false;
    bool saw_rules = false;
    bool saw_host_health = false;
    bool saw_host_held = false;
    bool saw_peer_player = false;
    bool saw_peer_health = false;
    bool saw_peer_held = false;
    deadline = time_now_seconds() + 3.0;
    while (time_now_seconds() < deadline &&
           !(saw_welcome && saw_host_player && saw_rules && saw_host_health && saw_host_held &&
             saw_peer_player && saw_peer_health && saw_peer_held)) {
        LanEvent event;
        while (lan_poll(late, &event)) {
        }
        while (lan_poll(pair.hs, &event)) {
            if (event.type == LAN_EVENT_MESSAGE) {
                app_lan_handle_message(pair.host, &event);
            }
        }
        while (lan_poll(late, &event)) {
            if (event.type != LAN_EVENT_MESSAGE || event.size == 0) {
                continue;
            }
            switch (event.payload[0]) {
            case LAN_MSG_WELCOME:
                saw_welcome = true;
                break;
            case LAN_MSG_PLAYER: {
                if (event.size < 32) {
                    break;
                }
                uint32_t pid = lan_read_u32(event.payload + 1);
                float x = lan_read_f32(event.payload + 5);
                if (pid == 0 && x > -0.5f && x < 0.5f) {
                    saw_host_player = true;
                }
                if (pid == pair.cpid && x > 4.5f && x < 5.5f) {
                    saw_peer_player = true;
                }
                break;
            }
            case LAN_MSG_RULES: {
                if (event.size != LAN_MSG_RULES_SIZE) {
                    break;
                }
                GameRules r;
                lan_rules_decode(&r, event.payload + 1);
                if (r.pvp == false && r.daylight_cycle == false && r.mob_spawning == true) {
                    saw_rules = true;
                }
                break;
            }
            case LAN_MSG_HEALTH: {
                uint32_t pid = 0;
                float hp = 0.0f;
                bool dead = true;
                bool creative = true;
                if (!lan_health_decode(event.payload, event.size, &pid, &hp, &dead,
                                       &creative)) {
                    break;
                }
                if (pid == 0 && hp > 13.4f && hp < 13.6f && !dead && !creative) {
                    saw_host_health = true;
                }
                if (pid == pair.cpid && hp > 10.9f && hp < 11.1f && !dead) {
                    saw_peer_health = true;
                }
                break;
            }
            case LAN_MSG_HELD: {
                uint32_t pid = 0;
                uint8_t slot = 0;
                uint16_t item = 0;
                if (!lan_held_decode(event.payload, event.size, &pid, &slot, &item)) {
                    break;
                }
                if (pid == 0 && slot == 4 && item == (uint16_t)BLOCK_STONE) {
                    saw_host_held = true;
                }
                if (pid == pair.cpid && slot == 1 && item == (uint16_t)BLOCK_DIRT) {
                    saw_peer_held = true;
                }
                break;
            }
            default:
                break;
            }
        }
        lan_test_yield();
    }
    TEST_ASSERT(saw_welcome);
    TEST_ASSERT(saw_host_player);
    TEST_ASSERT(saw_rules);
    TEST_ASSERT(saw_host_health);
    TEST_ASSERT(saw_host_held);
    TEST_ASSERT(saw_peer_player);
    TEST_ASSERT(saw_peer_health);
    TEST_ASSERT(saw_peer_held);
    lan_close(late);
    lan_destroy(late);
    pvp_pair_destroy(&pair);
    return failures;
}

/* Test: render interpolation eases toward snapshots (no origin streaks,
 * no snapping) while authoritative state stays exact.
 *
 * Returns: failure count.
 */
int test_lan_remote_interpolation(void)
{
    int failures = 0;
    PvpPair pair;
    TEST_ASSERT(pvp_pair_connect(&pair, mmath_vec3(0.0f, 70.0f, 0.0f),
                                 mmath_vec3(2.0f, 70.0f, 0.0f)));
    if (pair.host == NULL) {
        pvp_pair_destroy(&pair);
        return failures + 1;
    }
    /* First snapshot seeds the render pose exactly (no streak). */
    {
        uint8_t frame[64];
        size_t n = pvp_player_frame(frame, 0, mmath_vec3(0.0f, 70.0f, 0.0f), 0.0f, "HostUser");
        pvp_inject(pair.client, LAN_SERVER_PEER_ID, frame, n);
        LanRemotePlayer *r = &pair.client->lan_players[0];
        TEST_ASSERT(r->has_render == true);
        TEST_ASSERT_FLOAT_EQ(r->rpos.x, 0.0f, 1e-4f);
    }
    /* Teleport snapshot: authoritative jumps, render lags, then converges. */
    {
        uint8_t frame[64];
        size_t n = pvp_player_frame(frame, 0, mmath_vec3(4.0f, 70.0f, 0.0f), 1.0f, "HostUser");
        pvp_inject(pair.client, LAN_SERVER_PEER_ID, frame, n);
        LanRemotePlayer *r = &pair.client->lan_players[0];
        TEST_ASSERT_FLOAT_EQ(r->pos.x, 4.0f, 1e-4f);
        TEST_ASSERT_FLOAT_EQ(r->rpos.x, 0.0f, 1e-4f);
        app_lan_update(pair.client, 0.016);
        TEST_ASSERT(r->rpos.x > 0.0f && r->rpos.x < 4.0f);
        TEST_ASSERT(r->ryaw > 0.0f && r->ryaw < 1.0f);
        for (int i = 0; i < 60; ++i) {
            app_lan_update(pair.client, 0.016);
        }
        TEST_ASSERT_FLOAT_EQ(r->rpos.x, 4.0f, 0.05f);
        TEST_ASSERT_FLOAT_EQ(r->ryaw, 1.0f, 0.05f);
    }
    pvp_pair_destroy(&pair);
    return failures;
}

/* Test: HEALTH relay, RULES apply, HELD cache, PLAYER_LEFT cleanup.
 *
 * Returns: failure count.
 */
int test_lan_replication_handlers(void)
{
    int failures = 0;
    PvpPair pair;
    TEST_ASSERT(pvp_pair_connect(&pair, mmath_vec3(0.0f, 70.0f, 0.0f),
                                 mmath_vec3(2.0f, 70.0f, 0.0f)));
    if (pair.host == NULL) {
        pvp_pair_destroy(&pair);
        return failures + 1;
    }
    /* Client HEALTH: host caches, relay echoes (sender ignores its own). */
    {
        uint8_t frame[LAN_MSG_HEALTH_SIZE];
        lan_health_encode(frame, pair.cpid, 17.5f, false, false);
        TEST_ASSERT(lan_send(pair.cs, LAN_SERVER_PEER_ID, frame, sizeof(frame)));
        pvp_settle(&pair);
        LanRemotePlayer *cached = &pair.host->lan_players[0];
        TEST_ASSERT(cached->has_health == true);
        TEST_ASSERT_FLOAT_EQ(cached->health, 17.5f, 1e-4f);
        TEST_ASSERT_FLOAT_EQ(pair.client->player.health, 20.0f, 1e-4f);
    }
    /* Host RULES: client applies to its authoritative copy. */
    {
        GameRules r;
        game_rules_defaults(&r);
        r.pvp = false;
        r.keep_inventory = true;
        uint8_t frame[LAN_MSG_RULES_SIZE];
        frame[0] = (uint8_t)LAN_MSG_RULES;
        lan_rules_encode(frame + 1, &r);
        TEST_ASSERT(lan_send(pair.hs, LAN_BROADCAST_PEER, frame, sizeof(frame)));
        pvp_settle(&pair);
        TEST_ASSERT(pair.client->world->rules.pvp == false);
        TEST_ASSERT(pair.client->world->rules.keep_inventory == true);
    }
    /* Client HELD: host caches the snapshot. */
    {
        uint8_t frame[LAN_MSG_HELD_SIZE];
        lan_held_encode(frame, pair.cpid, 3, 7);
        TEST_ASSERT(lan_send(pair.cs, LAN_SERVER_PEER_ID, frame, sizeof(frame)));
        pvp_settle(&pair);
        LanRemotePlayer *cached = &pair.host->lan_players[0];
        TEST_ASSERT(cached->has_held == true);
        TEST_ASSERT(cached->held_slot == 3 && cached->held_item == 7);
    }
    /* PLAYER_LEFT from the server clears the entry without leftovers. */
    {
        uint8_t frame[5] = {(uint8_t)LAN_MSG_PLAYER_LEFT, 0, 0, 0, 0};
        frame[1] = (uint8_t)(pair.cpid >> 24);
        frame[2] = (uint8_t)(pair.cpid >> 16);
        frame[3] = (uint8_t)(pair.cpid >> 8);
        frame[4] = (uint8_t)pair.cpid;
        /* Route it as a server message: inject directly (peer id is what
         * the client checks, and only the host sends PLAYER_LEFT). */
        LanEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = LAN_EVENT_MESSAGE;
        ev.peer_id = LAN_SERVER_PEER_ID;
        ev.size = (uint32_t)sizeof(frame);
        memcpy(ev.payload, frame, sizeof(frame));
        /* Address it at the host entry on the client (peer 0 is the host;
         * use the host entry to prove removal targets the right slot). */
        ev.payload[1] = 0;
        ev.payload[2] = 0;
        ev.payload[3] = 0;
        ev.payload[4] = 0;
        TEST_ASSERT(pair.client->lan_players[0].active == true);
        app_lan_handle_message(pair.client, &ev);
        TEST_ASSERT(pair.client->lan_players[0].active == false);
    }
    pvp_pair_destroy(&pair);
    return failures;
}
