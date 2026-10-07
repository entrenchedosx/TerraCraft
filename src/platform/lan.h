#pragma once

/* Small native TCP transport for direct LAN sessions.
 *
 * Calls are single-threaded: create, start, poll, send, and close must all be
 * called from the same application thread. No SDL or game types are exposed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAN_MAX_FRAME_SIZE 4096u
#define LAN_MAX_PEERS 8u
#define LAN_BROADCAST_PEER UINT32_MAX
#define LAN_SERVER_PEER_ID 1u

typedef struct LanSession LanSession;

typedef enum LanEventType {
    LAN_EVENT_NONE = 0,
    LAN_EVENT_PEER_CONNECTED,
    LAN_EVENT_PEER_DISCONNECTED,
    LAN_EVENT_MESSAGE,
    LAN_EVENT_CONNECT_FAILED
} LanEventType;

/* Received payload bytes are opaque to this transport. `address` is a
 * numeric IPv4 endpoint when available. `error_code` is zero for an orderly
 * disconnect and otherwise contains a platform socket error or a negative
 * LAN-local error code. */
typedef struct LanEvent {
    LanEventType type;
    uint32_t peer_id;
    uint32_t size;
    int error_code;
    char address[48];
    uint8_t payload[LAN_MAX_FRAME_SIZE];
} LanEvent;

/* Create/destroy a transport. NULL is returned on allocation or Winsock
 * startup failure. Destroy closes active sockets immediately. */
LanSession *lan_create(void);
void lan_destroy(LanSession *session);

/* Start a host listener on all IPv4 interfaces. Port 0 asks the OS to select
 * an available port; obtain it with lan_local_port(). */
bool lan_host_start(LanSession *session, uint16_t port);

/* Begin a nonblocking client connection. `host` may be a numeric IPv4
 * address or a resolvable hostname. Connection completion is reported by
 * lan_poll() as PEER_CONNECTED or CONNECT_FAILED. */
bool lan_client_start(LanSession *session, const char *host, uint16_t port);

/* Pump nonblocking accept/connect/read/write work and return at most one
 * queued event. Call repeatedly each frame until it returns false. TCP uses a
 * four-byte little-endian length prefix; empty and >4 KiB frames are rejected.
 */
bool lan_poll(LanSession *session, LanEvent *out_event);

/* Queue one opaque frame to a connected peer, or to every connected peer
 * with LAN_BROADCAST_PEER. Queue capacity is bounded; false means no frame
 * was queued. Broadcast is all-or-none. */
bool lan_send(LanSession *session, uint32_t peer_id, const void *payload, size_t size);

/* Request an orderly peer close. Already-queued frames are flushed before
 * the local TCP write side is shut down. The resulting disconnect event is
 * delivered through lan_poll(). */
bool lan_disconnect_peer(LanSession *session, uint32_t peer_id);

/* Close all sockets and reset the transport so it can be started again. */
void lan_close(LanSession *session);

/* State/query helpers. A host is connected while its listener is active; a
 * client is connected only after its host connection has completed. */
bool lan_is_host(const LanSession *session);
bool lan_is_connected(const LanSession *session);
size_t lan_peer_count(const LanSession *session);
uint16_t lan_local_port(const LanSession *session);

/* Copy a connected peer's numeric IPv4 endpoint into `out` (including NUL).
 * Returns false if the id is unknown or the buffer is too small. */
bool lan_peer_address(const LanSession *session, uint32_t peer_id, char *out, size_t out_cap);

/* Strict IPv4/local-network validators used by the LAN join screen and host.
 * Address accepts dotted IPv4; endpoint accepts the `address:port` form
 * returned by lan_peer_address(). Only loopback, private, and link-local
 * addresses count as local. */
bool lan_ipv4_is_local_address(const char *address);
bool lan_ipv4_is_local_endpoint(const char *endpoint);
