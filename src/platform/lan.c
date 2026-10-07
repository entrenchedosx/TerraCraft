#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
/* Strict ISO C hides getaddrinfo on Apple; request it explicitly. */
#define _DARWIN_C_SOURCE 1
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
/* Strict ISO C (C_EXTENSIONS OFF) hides POSIX declarations such as
 * getaddrinfo on glibc; request them explicitly before any header. */
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/lan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET LanSocket;
typedef int LanSockLen;
#define LAN_INVALID_SOCKET INVALID_SOCKET
#define lan_close_socket(s) closesocket(s)
#define lan_last_socket_error() WSAGetLastError()
#define lan_shutdown_socket(s) shutdown((s), SD_BOTH)
#define LAN_SOCKET_ERROR SOCKET_ERROR
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int LanSocket;
typedef socklen_t LanSockLen;
#define LAN_INVALID_SOCKET (-1)
#define lan_close_socket(s) close(s)
#define lan_last_socket_error() errno
#define lan_shutdown_socket(s) shutdown((s), SHUT_RDWR)
#define LAN_SOCKET_ERROR (-1)
#endif

#define LAN_EVENT_CAPACITY 64u
#define LAN_TX_QUEUE_BYTES (32u * 1024u)
#define LAN_ERR_PROTOCOL (-1)
#define LAN_ERR_QUEUE_OVERFLOW (-2)

typedef enum LanRole {
    LAN_ROLE_NONE = 0,
    LAN_ROLE_HOST,
    LAN_ROLE_CLIENT
} LanRole;

typedef struct LanPeer {
    LanSocket socket;
    bool occupied;
    bool connecting;
    bool connected;
    bool closing;
    bool connected_event_pending;
    bool disconnected_event_pending;
    bool connect_failed_event_pending;
    uint32_t peer_id;
    int pending_error;
    struct sockaddr_in address;
    uint8_t rx_header[4];
    size_t rx_header_used;
    uint8_t rx_payload[LAN_MAX_FRAME_SIZE];
    size_t rx_payload_used;
    uint32_t rx_expected;
    bool rx_frame_ready;
    uint8_t tx_queue[LAN_TX_QUEUE_BYTES];
    size_t tx_head;
    size_t tx_used;
} LanPeer;

struct LanSession {
    LanRole role;
    LanSocket listener;
    uint16_t local_port;
    uint32_t next_peer_id;
    LanPeer peers[LAN_MAX_PEERS];
    LanEvent events[LAN_EVENT_CAPACITY];
    size_t event_head;
    size_t event_count;
#if defined(_WIN32)
    bool winsock_started;
#endif
};

static bool lan_is_would_block(int error_code)
{
#if defined(_WIN32)
    return error_code == WSAEWOULDBLOCK || error_code == WSAEINPROGRESS || error_code == WSAEALREADY;
#else
    return error_code == EWOULDBLOCK || error_code == EAGAIN || error_code == EINPROGRESS ||
           error_code == EALREADY;
#endif
}

static bool lan_is_interrupted(int error_code)
{
#if defined(_WIN32)
    return error_code == WSAEINTR;
#else
    return error_code == EINTR;
#endif
}

static bool lan_set_nonblocking(LanSocket socket)
{
#if defined(_WIN32)
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void lan_set_no_sigpipe(LanSocket socket)
{
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    int enabled = 1;
    (void)setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#else
    (void)socket;
#endif
}

static void lan_set_tcp_nodelay(LanSocket socket)
{
    int enabled = 1;
    (void)setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&enabled, (LanSockLen)sizeof(enabled));
}

static void lan_socket_close(LanSocket *socket)
{
    if (socket != NULL && *socket != LAN_INVALID_SOCKET) {
        lan_close_socket(*socket);
        *socket = LAN_INVALID_SOCKET;
    }
}

static bool lan_event_push(LanSession *session, const LanEvent *event)
{
    if (session == NULL || event == NULL || session->event_count >= LAN_EVENT_CAPACITY) {
        return false;
    }
    size_t tail = (session->event_head + session->event_count) % LAN_EVENT_CAPACITY;
    session->events[tail] = *event;
    session->event_count++;
    return true;
}

static bool lan_event_room(const LanSession *session)
{
    return session != NULL && session->event_count < LAN_EVENT_CAPACITY;
}

static void lan_format_address(const struct sockaddr_in *address, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (address == NULL) {
        return;
    }
    char ip[INET_ADDRSTRLEN];
#if defined(_WIN32)
    if (InetNtopA(AF_INET, (void *)&address->sin_addr, ip, sizeof(ip)) == NULL) {
#else
    if (inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)) == NULL) {
#endif
        return;
    }
    (void)snprintf(out, out_cap, "%s:%u", ip, (unsigned)ntohs(address->sin_port));
}

static bool lan_ipv4_parse(const char *text, uint8_t out[4])
{
    if (text == NULL || out == NULL) {
        return false;
    }
    const char *at = text;
    for (size_t part = 0; part < 4u; ++part) {
        unsigned value = 0;
        size_t digits = 0;
        while (*at >= '0' && *at <= '9') {
            if (++digits > 3u) {
                return false;
            }
            value = value * 10u + (unsigned)(*at - '0');
            if (value > 255u) {
                return false;
            }
            ++at;
        }
        if (digits == 0u || (digits > 1u && at[-(ptrdiff_t)digits] == '0')) {
            return false;
        }
        out[part] = (uint8_t)value;
        if (part < 3u) {
            if (*at != '.') {
                return false;
            }
            ++at;
        } else if (*at != '\0') {
            return false;
        }
    }
    return true;
}

static bool lan_ipv4_octets_are_local(const uint8_t octet[4])
{
    return octet[0] == 10u || octet[0] == 127u ||
           (octet[0] == 169u && octet[1] == 254u) ||
           (octet[0] == 192u && octet[1] == 168u) ||
           (octet[0] == 172u && octet[1] >= 16u && octet[1] <= 31u);
}

bool lan_ipv4_is_local_address(const char *address)
{
    uint8_t octet[4];
    return lan_ipv4_parse(address, octet) && lan_ipv4_octets_are_local(octet);
}

bool lan_ipv4_is_local_endpoint(const char *endpoint)
{
    if (endpoint == NULL) {
        return false;
    }
    const char *separator = strchr(endpoint, ':');
    if (separator == NULL || separator == endpoint || separator[1] == '\0') {
        return false;
    }
    size_t address_len = (size_t)(separator - endpoint);
    if (address_len >= 16u) {
        return false;
    }
    char address[16];
    memcpy(address, endpoint, address_len);
    address[address_len] = '\0';
    unsigned port = 0;
    for (const char *at = separator + 1; *at != '\0'; ++at) {
        if (*at < '0' || *at > '9') {
            return false;
        }
        unsigned digit = (unsigned)(*at - '0');
        if (port > (65535u - digit) / 10u) {
            return false;
        }
        port = port * 10u + digit;
    }
    return port != 0u && lan_ipv4_is_local_address(address);
}

/* LAN hosting accepts only loopback, RFC1918, and IPv4 link-local peers.
 * The caller is responsible for closing rejected sockets before they enter
 * the peer table or generate application events. */
static bool lan_address_is_local(const struct sockaddr_in *address)
{
    if (address == NULL || address->sin_family != AF_INET) {
        return false;
    }
    uint32_t ip = ntohl(address->sin_addr.s_addr);
    uint8_t octet[4] = {(uint8_t)(ip >> 24), (uint8_t)(ip >> 16),
                        (uint8_t)(ip >> 8), (uint8_t)ip};
    return lan_ipv4_octets_are_local(octet);
}

static void lan_peer_reset(LanPeer *peer)
{
    if (peer == NULL) {
        return;
    }
    memset(peer, 0, sizeof(*peer));
    peer->socket = LAN_INVALID_SOCKET;
}

static LanPeer *lan_find_peer(LanSession *session, uint32_t peer_id)
{
    if (session == NULL || peer_id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < LAN_MAX_PEERS; ++i) {
        LanPeer *peer = &session->peers[i];
        if (peer->occupied && peer->peer_id == peer_id) {
            return peer;
        }
    }
    return NULL;
}

static const LanPeer *lan_find_peer_const(const LanSession *session, uint32_t peer_id)
{
    if (session == NULL || peer_id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < LAN_MAX_PEERS; ++i) {
        const LanPeer *peer = &session->peers[i];
        if (peer->occupied && peer->connected && peer->peer_id == peer_id) {
            return peer;
        }
    }
    return NULL;
}

static LanPeer *lan_free_peer(LanSession *session)
{
    if (session == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < LAN_MAX_PEERS; ++i) {
        LanPeer *peer = &session->peers[i];
        if (!peer->occupied && !peer->disconnected_event_pending && !peer->connect_failed_event_pending) {
            return peer;
        }
    }
    return NULL;
}

static uint32_t lan_next_id(LanSession *session)
{
    uint32_t id = session->next_peer_id++;
    if (id == 0) {
        id = session->next_peer_id++;
    }
    if (session->next_peer_id == 0) {
        session->next_peer_id = 1;
    }
    return id;
}

static void lan_close_peer(LanPeer *peer, int error_code, bool connect_failed)
{
    if (peer == NULL) {
        return;
    }
    bool had_connection = peer->connected;
    lan_socket_close(&peer->socket);
    peer->occupied = false;
    peer->connecting = false;
    peer->connected = false;
    peer->closing = false;
    peer->rx_header_used = 0;
    peer->rx_payload_used = 0;
    peer->rx_expected = 0;
    peer->rx_frame_ready = false;
    peer->tx_head = 0;
    peer->tx_used = 0;
    peer->pending_error = error_code;
    if (had_connection) {
        peer->disconnected_event_pending = true;
    } else if (connect_failed) {
        peer->connect_failed_event_pending = true;
    }
}

static void lan_emit_pending(LanSession *session)
{
    if (session == NULL) {
        return;
    }
    for (size_t i = 0; i < LAN_MAX_PEERS && lan_event_room(session); ++i) {
        LanPeer *peer = &session->peers[i];
        LanEvent event;
        if (peer->connected_event_pending) {
            memset(&event, 0, sizeof(event));
            event.type = LAN_EVENT_PEER_CONNECTED;
            event.peer_id = peer->peer_id;
            lan_format_address(&peer->address, event.address, sizeof(event.address));
            if (lan_event_push(session, &event)) {
                peer->connected_event_pending = false;
            }
        }
        if (lan_event_room(session) && peer->disconnected_event_pending) {
            memset(&event, 0, sizeof(event));
            event.type = LAN_EVENT_PEER_DISCONNECTED;
            event.peer_id = peer->peer_id;
            event.error_code = peer->pending_error;
            lan_format_address(&peer->address, event.address, sizeof(event.address));
            if (lan_event_push(session, &event)) {
                lan_peer_reset(peer);
            }
        } else if (lan_event_room(session) && peer->connect_failed_event_pending) {
            memset(&event, 0, sizeof(event));
            event.type = LAN_EVENT_CONNECT_FAILED;
            event.peer_id = peer->peer_id;
            event.error_code = peer->pending_error;
            lan_format_address(&peer->address, event.address, sizeof(event.address));
            if (lan_event_push(session, &event)) {
                lan_peer_reset(peer);
            }
        }
    }
}

static void lan_put_u32le(uint8_t out[4], uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
    out[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint32_t lan_get_u32le(const uint8_t in[4])
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static size_t lan_tx_free(const LanPeer *peer)
{
    return peer != NULL && peer->tx_used <= LAN_TX_QUEUE_BYTES ? LAN_TX_QUEUE_BYTES - peer->tx_used : 0;
}

static bool lan_tx_can_queue(const LanPeer *peer, size_t size)
{
    return peer != NULL && peer->occupied && peer->connected && !peer->closing &&
           size <= LAN_MAX_FRAME_SIZE && size + 4u <= lan_tx_free(peer);
}

static void lan_tx_append(LanPeer *peer, const uint8_t *payload, size_t size)
{
    uint8_t header[4];
    lan_put_u32le(header, (uint32_t)size);
    size_t tail = (peer->tx_head + peer->tx_used) % LAN_TX_QUEUE_BYTES;
    for (size_t i = 0; i < sizeof(header); ++i) {
        peer->tx_queue[tail] = header[i];
        tail = (tail + 1u) % LAN_TX_QUEUE_BYTES;
    }
    for (size_t i = 0; i < size; ++i) {
        peer->tx_queue[tail] = payload[i];
        tail = (tail + 1u) % LAN_TX_QUEUE_BYTES;
    }
    peer->tx_used += size + sizeof(header);
}

static int lan_send_flags(void)
{
#if defined(_WIN32)
    return 0;
#elif defined(MSG_NOSIGNAL)
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

static void lan_flush_peer(LanPeer *peer)
{
    if (peer == NULL || !peer->occupied || !peer->connected) {
        return;
    }
    while (peer->tx_used > 0) {
        size_t contiguous = LAN_TX_QUEUE_BYTES - peer->tx_head;
        if (contiguous > peer->tx_used) {
            contiguous = peer->tx_used;
        }
#if defined(_WIN32)
        int sent = send(peer->socket, (const char *)(peer->tx_queue + peer->tx_head), (int)contiguous,
                        lan_send_flags());
#else
        ssize_t sent = send(peer->socket, peer->tx_queue + peer->tx_head, contiguous, lan_send_flags());
#endif
        if (sent > 0) {
            size_t n = (size_t)sent;
            peer->tx_head = (peer->tx_head + n) % LAN_TX_QUEUE_BYTES;
            peer->tx_used -= n;
            continue;
        }
        if (sent == 0) {
            lan_close_peer(peer, 0, false);
            return;
        }
        int error_code = lan_last_socket_error();
        if (lan_is_would_block(error_code) || lan_is_interrupted(error_code)) {
            return;
        }
        lan_close_peer(peer, error_code, false);
        return;
    }
    if (peer->closing && peer->tx_used == 0 && peer->occupied) {
        (void)lan_shutdown_socket(peer->socket);
        lan_close_peer(peer, 0, false);
    }
}

static void lan_finish_client_connect(LanPeer *peer)
{
    if (peer == NULL || !peer->occupied || !peer->connecting) {
        return;
    }
    fd_set writable;
    fd_set exceptional;
    FD_ZERO(&writable);
    FD_ZERO(&exceptional);
    FD_SET(peer->socket, &writable);
    FD_SET(peer->socket, &exceptional);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
#if defined(_WIN32)
    int ready = select(0, NULL, &writable, &exceptional, &timeout);
#else
    int ready = select(peer->socket + 1, NULL, &writable, &exceptional, &timeout);
#endif
    if (ready <= 0) {
        return;
    }
    int socket_error = 0;
    LanSockLen length = (LanSockLen)sizeof(socket_error);
    if (getsockopt(peer->socket, SOL_SOCKET, SO_ERROR, (char *)&socket_error, &length) != 0) {
        socket_error = lan_last_socket_error();
    }
    if (socket_error != 0 || FD_ISSET(peer->socket, &exceptional)) {
        lan_close_peer(peer, socket_error != 0 ? socket_error : LAN_ERR_PROTOCOL, true);
        return;
    }
    peer->connecting = false;
    peer->connected = true;
    peer->connected_event_pending = true;
}

static bool lan_receive_more(LanPeer *peer)
{
    if (peer == NULL || !peer->occupied || !peer->connected) {
        return false;
    }
    while (peer->occupied && peer->connected) {
        if (peer->rx_frame_ready) {
            return false;
        }
        if (peer->rx_header_used < sizeof(peer->rx_header)) {
            size_t remaining = sizeof(peer->rx_header) - peer->rx_header_used;
#if defined(_WIN32)
            int received = recv(peer->socket, (char *)(peer->rx_header + peer->rx_header_used), (int)remaining, 0);
#else
            ssize_t received = recv(peer->socket, peer->rx_header + peer->rx_header_used, remaining, 0);
#endif
            if (received == 0) {
                lan_close_peer(peer, 0, false);
                return false;
            }
            if (received < 0) {
                int error_code = lan_last_socket_error();
                if (lan_is_would_block(error_code) || lan_is_interrupted(error_code)) {
                    return false;
                }
                lan_close_peer(peer, error_code, false);
                return false;
            }
            peer->rx_header_used += (size_t)received;
            if (peer->rx_header_used < sizeof(peer->rx_header)) {
                continue;
            }
            peer->rx_expected = lan_get_u32le(peer->rx_header);
            if (peer->rx_expected == 0 || peer->rx_expected > LAN_MAX_FRAME_SIZE) {
                lan_close_peer(peer, LAN_ERR_PROTOCOL, false);
                return false;
            }
        }
        if (peer->rx_payload_used < peer->rx_expected) {
            size_t remaining = peer->rx_expected - peer->rx_payload_used;
#if defined(_WIN32)
            int received = recv(peer->socket, (char *)(peer->rx_payload + peer->rx_payload_used), (int)remaining, 0);
#else
            ssize_t received = recv(peer->socket, peer->rx_payload + peer->rx_payload_used, remaining, 0);
#endif
            if (received == 0) {
                lan_close_peer(peer, 0, false);
                return false;
            }
            if (received < 0) {
                int error_code = lan_last_socket_error();
                if (lan_is_would_block(error_code) || lan_is_interrupted(error_code)) {
                    return false;
                }
                lan_close_peer(peer, error_code, false);
                return false;
            }
            peer->rx_payload_used += (size_t)received;
            if (peer->rx_payload_used < peer->rx_expected) {
                continue;
            }
        }
        peer->rx_frame_ready = true;
        return true;
    }
    return false;
}

static void lan_queue_complete_frame(LanSession *session, LanPeer *peer)
{
    if (session == NULL || peer == NULL || !peer->occupied || !peer->rx_frame_ready || !lan_event_room(session)) {
        return;
    }
    LanEvent event;
    memset(&event, 0, sizeof(event));
    event.type = LAN_EVENT_MESSAGE;
    event.peer_id = peer->peer_id;
    event.size = peer->rx_expected;
    lan_format_address(&peer->address, event.address, sizeof(event.address));
    memcpy(event.payload, peer->rx_payload, peer->rx_expected);
    if (lan_event_push(session, &event)) {
        peer->rx_header_used = 0;
        peer->rx_payload_used = 0;
        peer->rx_expected = 0;
        peer->rx_frame_ready = false;
    }
}

static void lan_service_listener(LanSession *session)
{
    if (session == NULL || session->role != LAN_ROLE_HOST || session->listener == LAN_INVALID_SOCKET) {
        return;
    }
    while (lan_event_room(session)) {
        LanPeer *peer = lan_free_peer(session);
        if (peer == NULL) {
            return;
        }
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        LanSockLen length = (LanSockLen)sizeof(address);
        LanSocket accepted = accept(session->listener, (struct sockaddr *)&address, &length);
        if (accepted == LAN_INVALID_SOCKET) {
            int error_code = lan_last_socket_error();
            if (!lan_is_would_block(error_code) && !lan_is_interrupted(error_code)) {
                /* The next frame will retry accept; a transient listener error
                 * must not stall the render loop or poison the session. */
            }
            return;
        }
        if (!lan_address_is_local(&address)) {
            lan_close_socket(accepted);
            continue;
        }
        if (!lan_set_nonblocking(accepted)) {
            lan_close_socket(accepted);
            continue;
        }
        lan_set_no_sigpipe(accepted);
        lan_set_tcp_nodelay(accepted);
        lan_peer_reset(peer);
        peer->socket = accepted;
        peer->occupied = true;
        peer->connected = true;
        peer->peer_id = lan_next_id(session);
        peer->address = address;
        peer->connected_event_pending = true;
        lan_emit_pending(session);
    }
}

static void lan_service(LanSession *session)
{
    if (session == NULL || session->role == LAN_ROLE_NONE) {
        return;
    }
    lan_emit_pending(session);
    lan_service_listener(session);
    for (size_t i = 0; i < LAN_MAX_PEERS; ++i) {
        LanPeer *peer = &session->peers[i];
        if (!peer->occupied) {
            continue;
        }
        if (peer->connecting) {
            lan_finish_client_connect(peer);
        }
        if (!peer->occupied) {
            continue;
        }
        lan_flush_peer(peer);
        if (!peer->occupied || !peer->connected) {
            continue;
        }
        /* Do not expose messages before the app has observed connection. */
        if (peer->connected_event_pending) {
            continue;
        }
        if (peer->rx_frame_ready) {
            lan_queue_complete_frame(session, peer);
        }
        if (peer->occupied && !peer->rx_frame_ready && lan_event_room(session)) {
            (void)lan_receive_more(peer);
            if (peer->occupied && peer->rx_frame_ready) {
                lan_queue_complete_frame(session, peer);
            }
        }
    }
    lan_emit_pending(session);
}

LanSession *lan_create(void)
{
    LanSession *session = (LanSession *)calloc(1, sizeof(*session));
    if (session == NULL) {
        return NULL;
    }
    session->listener = LAN_INVALID_SOCKET;
    session->next_peer_id = 1;
    for (size_t i = 0; i < LAN_MAX_PEERS; ++i) {
        session->peers[i].socket = LAN_INVALID_SOCKET;
    }
#if defined(_WIN32)
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        free(session);
        return NULL;
    }
    session->winsock_started = true;
#endif
    return session;
}

void lan_close(LanSession *session)
{
    if (session == NULL) {
        return;
    }
    for (size_t i = 0; i < LAN_MAX_PEERS; ++i) {
        LanPeer *peer = &session->peers[i];
        if (peer->socket != LAN_INVALID_SOCKET) {
            (void)lan_shutdown_socket(peer->socket);
            lan_socket_close(&peer->socket);
        }
        lan_peer_reset(peer);
    }
    if (session->listener != LAN_INVALID_SOCKET) {
        lan_socket_close(&session->listener);
    }
    session->role = LAN_ROLE_NONE;
    session->local_port = 0;
    session->next_peer_id = 1;
    session->event_head = 0;
    session->event_count = 0;
}

void lan_destroy(LanSession *session)
{
    if (session == NULL) {
        return;
    }
    lan_close(session);
#if defined(_WIN32)
    if (session->winsock_started) {
        WSACleanup();
    }
#endif
    free(session);
}

bool lan_host_start(LanSession *session, uint16_t port)
{
    if (session == NULL || session->role != LAN_ROLE_NONE) {
        return false;
    }
    LanSocket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == LAN_INVALID_SOCKET) {
        return false;
    }
    int reuse = 1;
#if defined(_WIN32)
    (void)setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&reuse, (LanSockLen)sizeof(reuse));
#else
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, (LanSockLen)sizeof(reuse));
#endif
    lan_set_no_sigpipe(listener);
    if (!lan_set_nonblocking(listener)) {
        lan_socket_close(&listener);
        return false;
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(listener, (struct sockaddr *)&address, (LanSockLen)sizeof(address)) == LAN_SOCKET_ERROR ||
        listen(listener, (int)LAN_MAX_PEERS) == LAN_SOCKET_ERROR) {
        lan_socket_close(&listener);
        return false;
    }
    LanSockLen length = (LanSockLen)sizeof(address);
    if (getsockname(listener, (struct sockaddr *)&address, &length) == LAN_SOCKET_ERROR) {
        lan_socket_close(&listener);
        return false;
    }
    session->listener = listener;
    session->role = LAN_ROLE_HOST;
    session->local_port = ntohs(address.sin_port);
    session->next_peer_id = 1;
    return true;
}

bool lan_client_start(LanSession *session, const char *host, uint16_t port)
{
    if (session == NULL || session->role != LAN_ROLE_NONE || host == NULL || host[0] == '\0' || port == 0 ||
        strlen(host) > 255u) {
        return false;
    }
    char port_text[6];
    (void)snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo *results = NULL;
    if (getaddrinfo(host, port_text, &hints, &results) != 0 || results == NULL) {
        if (results != NULL) {
            freeaddrinfo(results);
        }
        return false;
    }

    LanSocket connected_socket = LAN_INVALID_SOCKET;
    struct sockaddr_in chosen;
    memset(&chosen, 0, sizeof(chosen));
    bool connecting = false;
    for (struct addrinfo *it = results; it != NULL; it = it->ai_next) {
        if (it->ai_addrlen < (LanSockLen)sizeof(struct sockaddr_in)) {
            continue;
        }
        LanSocket candidate = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (candidate == LAN_INVALID_SOCKET) {
            continue;
        }
        lan_set_no_sigpipe(candidate);
        lan_set_tcp_nodelay(candidate);
        if (!lan_set_nonblocking(candidate)) {
            lan_socket_close(&candidate);
            continue;
        }
        int rc = connect(candidate, it->ai_addr, (LanSockLen)it->ai_addrlen);
        if (rc == 0) {
            connected_socket = candidate;
            connecting = false;
            memcpy(&chosen, it->ai_addr, sizeof(chosen));
            break;
        }
        int error_code = lan_last_socket_error();
        if (lan_is_would_block(error_code)) {
            connected_socket = candidate;
            connecting = true;
            memcpy(&chosen, it->ai_addr, sizeof(chosen));
            break;
        }
        lan_socket_close(&candidate);
    }
    freeaddrinfo(results);
    if (connected_socket == LAN_INVALID_SOCKET) {
        return false;
    }

    LanPeer *peer = lan_free_peer(session);
    if (peer == NULL) {
        lan_socket_close(&connected_socket);
        return false;
    }
    lan_peer_reset(peer);
    peer->socket = connected_socket;
    peer->occupied = true;
    peer->connecting = connecting;
    peer->connected = !connecting;
    peer->peer_id = LAN_SERVER_PEER_ID;
    peer->address = chosen;
    peer->connected_event_pending = !connecting;
    session->role = LAN_ROLE_CLIENT;
    return true;
}

bool lan_poll(LanSession *session, LanEvent *out_event)
{
    if (session == NULL || out_event == NULL) {
        return false;
    }
    lan_service(session);
    if (session->event_count == 0) {
        memset(out_event, 0, sizeof(*out_event));
        return false;
    }
    *out_event = session->events[session->event_head];
    memset(&session->events[session->event_head], 0, sizeof(session->events[session->event_head]));
    session->event_head = (session->event_head + 1u) % LAN_EVENT_CAPACITY;
    session->event_count--;
    return true;
}

bool lan_send(LanSession *session, uint32_t peer_id, const void *payload, size_t size)
{
    if (session == NULL || payload == NULL || size == 0 || size > LAN_MAX_FRAME_SIZE ||
        session->role == LAN_ROLE_NONE) {
        return false;
    }
    if (peer_id != LAN_BROADCAST_PEER) {
        LanPeer *peer = lan_find_peer(session, peer_id);
        if (!lan_tx_can_queue(peer, size)) {
            return false;
        }
        lan_tx_append(peer, (const uint8_t *)payload, size);
        return true;
    }

    size_t recipients = 0;
    for (size_t i = 0; i < LAN_MAX_PEERS; ++i) {
        LanPeer *peer = &session->peers[i];
        if (peer->occupied && peer->connected && !peer->closing) {
            if (!lan_tx_can_queue(peer, size)) {
                return false;
            }
            recipients++;
        }
    }
    if (recipients == 0) {
        return false;
    }
    for (size_t i = 0; i < LAN_MAX_PEERS; ++i) {
        LanPeer *peer = &session->peers[i];
        if (peer->occupied && peer->connected && !peer->closing) {
            lan_tx_append(peer, (const uint8_t *)payload, size);
        }
    }
    return true;
}

bool lan_disconnect_peer(LanSession *session, uint32_t peer_id)
{
    LanPeer *peer = lan_find_peer(session, peer_id);
    if (peer == NULL || !peer->connected || peer->closing) {
        return false;
    }
    peer->closing = true;
    if (peer->tx_used == 0) {
        (void)lan_shutdown_socket(peer->socket);
        lan_close_peer(peer, 0, false);
        lan_emit_pending(session);
    }
    return true;
}

bool lan_is_host(const LanSession *session)
{
    return session != NULL && session->role == LAN_ROLE_HOST;
}

bool lan_is_connected(const LanSession *session)
{
    if (session == NULL) {
        return false;
    }
    if (session->role == LAN_ROLE_HOST) {
        return session->listener != LAN_INVALID_SOCKET;
    }
    if (session->role == LAN_ROLE_CLIENT) {
        return lan_find_peer_const(session, LAN_SERVER_PEER_ID) != NULL;
    }
    return false;
}

size_t lan_peer_count(const LanSession *session)
{
    if (session == NULL || session->role == LAN_ROLE_NONE) {
        return 0;
    }
    if (session->role == LAN_ROLE_CLIENT) {
        return lan_is_connected(session) ? 1u : 0u;
    }
    size_t count = 0;
    for (size_t i = 0; i < LAN_MAX_PEERS; ++i) {
        if (session->peers[i].occupied && session->peers[i].connected) {
            count++;
        }
    }
    return count;
}

uint16_t lan_local_port(const LanSession *session)
{
    return session != NULL ? session->local_port : 0;
}

bool lan_peer_address(const LanSession *session, uint32_t peer_id, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return false;
    }
    out[0] = '\0';
    const LanPeer *peer = lan_find_peer_const(session, peer_id);
    if (peer == NULL) {
        return false;
    }
    char address[48];
    lan_format_address(&peer->address, address, sizeof(address));
    size_t size = strlen(address) + 1u;
    if (address[0] == '\0' || size > out_cap) {
        return false;
    }
    memcpy(out, address, size);
    return true;
}
