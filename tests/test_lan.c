#define _POSIX_C_SOURCE 199309L

#include "test_main.h"

#include "core/time.h"
#include "platform/lan.h"

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
