#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/lan_discover.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET DiscoverSocket;
#define DISCOVER_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int DiscoverSocket;
#define DISCOVER_INVALID (-1)
#endif

#define DISCOVER_MAGIC "TCRAFT1"
#define DISCOVER_QUERY "TCRAFT1?"
#define DISCOVER_DATAGRAM_MAX 256u

struct LanDiscover {
    DiscoverSocket socket;
    bool socket_ok;
#if defined(_WIN32)
    bool winsock_started;
#endif
    LanServerInfo servers[LAN_DISCOVERY_MAX];
    size_t server_count;
    float beacon_t;
    bool host_armed;
    char host_name[LAN_DISCOVERY_NAME_MAX + 1];
    unsigned host_players;
    unsigned host_capacity;
    uint16_t host_port;
};

/* True for loopback, RFC1918, and link-local IPv4 (dotted quad). */
static bool discover_is_local_ipv4(const char *address)
{
    unsigned a = 256u, b = 256u, c = 256u, d = 256u;
    char tail[8] = {0};
    if (address == NULL || address[0] == '\0') {
        return false;
    }
    if (sscanf(address, "%u.%u.%u.%u%7s", &a, &b, &c, &d, tail) < 4 || tail[0] != '\0') {
        return false;
    }
    if (a > 255u || b > 255u || c > 255u || d > 255u) {
        return false;
    }
    if (a == 127u) {
        return true;
    }
    if (a == 10u) {
        return true;
    }
    if (a == 172u && b >= 16u && b <= 31u) {
        return true;
    }
    if (a == 192u && b == 168u) {
        return true;
    }
    if (a == 169u && b == 254u) {
        return true;
    }
    return false;
}

size_t lan_discover_format_beacon(char *out, size_t cap, uint16_t port, unsigned players,
                                  unsigned capacity, const char *name)
{
    if (out == NULL || cap < 16u || port == 0u) {
        return 0;
    }
    if (players > 255u) {
        players = 255u;
    }
    if (capacity > 255u) {
        capacity = 255u;
    }
    char clean[LAN_DISCOVERY_NAME_MAX + 1];
    size_t n = 0;
    if (name != NULL) {
        for (size_t i = 0; name[i] != '\0' && n < LAN_DISCOVERY_NAME_MAX; ++i) {
            char ch = name[i];
            if (ch >= 32 && ch <= 126) {
                clean[n++] = ch;
            }
        }
    }
    clean[n] = '\0';
    if (n == 0) {
        memcpy(clean, "TerraCraft World", 17);
    }
    int written =
        snprintf(out, cap, "%s %u %u/%u %s", DISCOVER_MAGIC, (unsigned)port, players, capacity, clean);
    if (written <= 0 || (size_t)written >= cap) {
        return 0;
    }
    return (size_t)written;
}

bool lan_discover_parse_beacon(const char *data, size_t len, uint16_t *out_port,
                               unsigned *out_players, unsigned *out_capacity, char *out_name,
                               size_t name_cap)
{
    unsigned port = 0u, players = 0u, capacity = 0u;
    size_t head = 0;
    if (data == NULL || len == 0 || len > 512u) {
        return false;
    }
    /* Magic + space. */
    const char magic[] = DISCOVER_MAGIC " ";
    if (len < sizeof(magic) || memcmp(data, magic, sizeof(magic) - 1u) != 0) {
        return false;
    }
    head = sizeof(magic) - 1u;
    /* Port digits. */
    if (head >= len || data[head] < '0' || data[head] > '9') {
        return false;
    }
    while (head < len && data[head] >= '0' && data[head] <= '9') {
        port = port * 10u + (unsigned)(data[head] - '0');
        if (port > 65535u) {
            return false;
        }
        ++head;
    }
    if (port == 0u || head >= len || data[head] != ' ') {
        return false;
    }
    ++head;
    /* players/capacity digits. */
    if (head >= len || data[head] < '0' || data[head] > '9') {
        return false;
    }
    while (head < len && data[head] >= '0' && data[head] <= '9') {
        players = players * 10u + (unsigned)(data[head] - '0');
        if (players > 255u) {
            return false;
        }
        ++head;
    }
    if (head >= len || data[head] != '/') {
        return false;
    }
    ++head;
    if (head >= len || data[head] < '0' || data[head] > '9') {
        return false;
    }
    while (head < len && data[head] >= '0' && data[head] <= '9') {
        capacity = capacity * 10u + (unsigned)(data[head] - '0');
        if (capacity > 255u) {
            return false;
        }
        ++head;
    }
    if (head >= len || data[head] != ' ') {
        return false;
    }
    ++head;
    /* Name: 1..48 printable chars, then end (trailing newline tolerated). */
    size_t name_len = 0;
    while (head < len && data[head] != '\0' && data[head] != '\n' && data[head] != '\r') {
        if (data[head] < 32 || data[head] > 126 || name_len >= LAN_DISCOVERY_NAME_MAX) {
            return false;
        }
        ++name_len;
        ++head;
    }
    if (name_len == 0) {
        return false;
    }
    if (out_port != NULL) {
        *out_port = (uint16_t)port;
    }
    if (out_players != NULL) {
        *out_players = players;
    }
    if (out_capacity != NULL) {
        *out_capacity = capacity;
    }
    if (out_name != NULL && name_cap > 0) {
        size_t copy = name_len < name_cap - 1u ? name_len : name_cap - 1u;
        memcpy(out_name, data + head - name_len, copy);
        out_name[copy] = '\0';
    }
    return true;
}

static void discover_close_socket(LanDiscover *d)
{
    if (d == NULL || !d->socket_ok) {
        return;
    }
#if defined(_WIN32)
    closesocket(d->socket);
#else
    close(d->socket);
#endif
    d->socket_ok = false;
}

static bool discover_set_nonblocking(DiscoverSocket socket)
{
#if defined(_WIN32)
    u_long mode = 1u;
    return ioctlsocket(socket, (long)FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

LanDiscover *lan_discover_create(void)
{
    LanDiscover *d = (LanDiscover *)calloc(1, sizeof(LanDiscover));
    if (d == NULL) {
        return NULL;
    }
#if defined(_WIN32)
    WSADATA winsock_data;
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        free(d);
        return NULL;
    }
    d->winsock_started = true;
#endif
    DiscoverSocket sock = (DiscoverSocket)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == DISCOVER_INVALID) {
        lan_discover_destroy(d);
        return NULL;
    }
    int reuse = 1;
    (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, (socklen_t)sizeof(reuse));
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast,
                   (socklen_t)sizeof(broadcast)) != 0 ||
        !discover_set_nonblocking(sock)) {
        lan_discover_destroy(d);
        return NULL;
    }
    struct sockaddr_in bind_address;
    memset(&bind_address, 0, sizeof(bind_address));
    bind_address.sin_family = AF_INET;
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_address.sin_port = htons((uint16_t)LAN_DISCOVERY_PORT);
    if (bind(sock, (struct sockaddr *)&bind_address, (socklen_t)sizeof(bind_address)) != 0) {
        lan_discover_destroy(d);
        return NULL;
    }
    d->socket = sock;
    d->socket_ok = true;
    d->server_count = 0;
    d->beacon_t = LAN_DISCOVERY_BEACON_T; /* First host() call beacons at once. */
    d->host_armed = false;
    (void)lan_discover_scan(d);
    return d;
}

void lan_discover_destroy(LanDiscover *d)
{
    if (d == NULL) {
        return;
    }
    discover_close_socket(d);
#if defined(_WIN32)
    if (d->winsock_started) {
        WSACleanup();
    }
#endif
    free(d);
}

static bool discover_send_to(LanDiscover *d, const char *data, size_t len, uint32_t ip,
                             uint16_t port)
{
    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = htonl(ip);
    target.sin_port = htons(port);
#if defined(_WIN32)
    int sent = sendto(d->socket, data, (int)len, 0, (struct sockaddr *)&target, (int)sizeof(target));
    return sent == (int)len;
#else
    ssize_t sent =
        sendto(d->socket, data, len, 0, (struct sockaddr *)&target, (socklen_t)sizeof(target));
    return sent == (ssize_t)len;
#endif
}

bool lan_discover_scan(LanDiscover *d)
{
    if (d == NULL || !d->socket_ok) {
        return false;
    }
    /* Limited broadcast reaches every host on the local network. The
     * loopback broadcast is a fallback for sandboxes that refuse the
     * limited address, and loopback unicast always reaches this machine
     * (still discovers other local instances). */
    bool lan = discover_send_to(d, DISCOVER_QUERY, sizeof(DISCOVER_QUERY) - 1u, 0xFFFFFFFFu,
                                (uint16_t)LAN_DISCOVERY_PORT);
    bool loopback = discover_send_to(d, DISCOVER_QUERY, sizeof(DISCOVER_QUERY) - 1u, 0x7FFFFFFFu,
                                     (uint16_t)LAN_DISCOVERY_PORT);
    bool self = discover_send_to(d, DISCOVER_QUERY, sizeof(DISCOVER_QUERY) - 1u, 0x7F000001u,
                                 (uint16_t)LAN_DISCOVERY_PORT);
    return lan || loopback || self;
}

static void discover_note_beacon(LanDiscover *d, const char *sender, uint16_t port,
                                 const char *name, unsigned players, unsigned capacity)
{
    size_t slot = d->server_count;
    for (size_t i = 0; i < d->server_count; ++i) {
        if (d->servers[i].port == port && strcmp(d->servers[i].address, sender) == 0) {
            slot = i;
            break;
        }
    }
    if (slot == d->server_count) {
        if (d->server_count >= LAN_DISCOVERY_MAX) {
            return;
        }
        ++d->server_count;
    }
    LanServerInfo *entry = &d->servers[slot];
    snprintf(entry->address, sizeof(entry->address), "%s", sender);
    entry->port = port;
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->players = (uint8_t)(players > 255u ? 255u : players);
    entry->capacity = (uint8_t)(capacity > 255u ? 255u : capacity);
    entry->age = 0.0f;
}

static void discover_send_beacon(LanDiscover *d, uint32_t ip)
{
    char body[DISCOVER_DATAGRAM_MAX];
    size_t n = lan_discover_format_beacon(body, sizeof(body), d->host_port, d->host_players,
                                          d->host_capacity, d->host_name);
    if (n == 0) {
        return;
    }
    (void)discover_send_to(d, body, n, ip, (uint16_t)LAN_DISCOVERY_PORT);
}

void lan_discover_poll(LanDiscover *d, float dt)
{
    if (d == NULL) {
        return;
    }
    float step = dt;
    if (!(step >= 0.0f)) {
        step = 0.0f;
    } else if (step > 1.0f) {
        step = 1.0f;
    }
    if (d->socket_ok) {
        for (;;) {
            char data[DISCOVER_DATAGRAM_MAX];
            struct sockaddr_in sender;
            memset(&sender, 0, sizeof(sender));
#if defined(_WIN32)
            int sender_len = (int)sizeof(sender);
            int got = recvfrom(d->socket, data, (int)sizeof(data) - 1, 0,
                               (struct sockaddr *)&sender, &sender_len);
            if (got <= 0) {
                break;
            }
#else
            socklen_t sender_len = (socklen_t)sizeof(sender);
            ssize_t got = recvfrom(d->socket, data, sizeof(data) - 1, 0,
                                   (struct sockaddr *)&sender, &sender_len);
            if (got <= 0) {
                break;
            }
#endif
            if (sender.sin_family != AF_INET) {
                continue;
            }
            data[got] = '\0';
            uint32_t ip = ntohl(sender.sin_addr.s_addr);
            char ip_text[48];
            snprintf(ip_text, sizeof(ip_text), "%u.%u.%u.%u", (ip >> 24) & 255u, (ip >> 16) & 255u,
                     (ip >> 8) & 255u, ip & 255u);
            if (!discover_is_local_ipv4(ip_text)) {
                continue;
            }
            if ((size_t)got == sizeof(DISCOVER_QUERY) - 1u &&
                memcmp(data, DISCOVER_QUERY, sizeof(DISCOVER_QUERY) - 1u) == 0) {
                /* A joiner is asking: answer directly when hosting. */
                if (d->host_armed) {
                    discover_send_beacon(d, ip);
                }
                continue;
            }
            uint16_t port = 0u;
            unsigned players = 0u, capacity = 0u;
            char name[LAN_DISCOVERY_NAME_MAX + 1];
            if (lan_discover_parse_beacon(data, (size_t)got, &port, &players, &capacity, name,
                                          sizeof(name))) {
                discover_note_beacon(d, ip_text, port, name, players, capacity);
            }
        }
    }
    /* Age out entries older than the window (swap-remove, order-free). */
    for (size_t i = 0; i < d->server_count;) {
        d->servers[i].age += step;
        if (d->servers[i].age > LAN_DISCOVERY_EXPIRE_T) {
            d->servers[i] = d->servers[d->server_count - 1u];
            --d->server_count;
        } else {
            ++i;
        }
    }
}

size_t lan_discover_count(const LanDiscover *d)
{
    return d == NULL ? 0 : d->server_count;
}

const LanServerInfo *lan_discover_at(const LanDiscover *d, size_t index)
{
    if (d == NULL || index >= d->server_count) {
        return NULL;
    }
    return &d->servers[index];
}

void lan_discover_host(LanDiscover *d, const char *world_name, unsigned players, unsigned capacity,
                       uint16_t port, float dt)
{
    if (d == NULL) {
        return;
    }
    float step = dt;
    if (!(step >= 0.0f)) {
        step = 0.0f;
    } else if (step > 1.0f) {
        step = 1.0f;
    }
    if (world_name != NULL && world_name[0] != '\0') {
        snprintf(d->host_name, sizeof(d->host_name), "%s", world_name);
    } else {
        memcpy(d->host_name, "TerraCraft World", 17);
    }
    d->host_players = players > 255u ? 255u : players;
    d->host_capacity = capacity > 255u ? 255u : capacity;
    d->host_port = port;
    d->host_armed = port != 0u && d->socket_ok;
    if (!d->host_armed) {
        return;
    }
    d->beacon_t += step;
    if (d->beacon_t >= LAN_DISCOVERY_BEACON_T) {
        d->beacon_t = 0.0f;
        discover_send_beacon(d, 0xFFFFFFFFu);
    }
}
