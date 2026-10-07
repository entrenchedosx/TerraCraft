#pragma once

/* LAN world discovery (UDP beacons): hosts announce, joiners listen.
 *
 * Hosts broadcast a short text beacon (world name, game port, player
 * count) every LAN_DISCOVERY_BEACON_T seconds and answer direct
 * queries; the join screen collects beacons into a bounded server list
 * so players pick a world instead of typing an IP. All entries are
 * validated (local-network senders, sane ports, printable names);
 * malformed datagrams are dropped, never stored.
 *
 * Single-threaded like the TCP transport. Payloads are plain ASCII.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAN_DISCOVERY_PORT 25567u
#define LAN_DISCOVERY_MAX 16u
#define LAN_DISCOVERY_BEACON_T 1.5f
#define LAN_DISCOVERY_EXPIRE_T 8.0f
#define LAN_DISCOVERY_NAME_MAX 48u

/* One visible world (owned by the discover table, do not free). */
typedef struct LanServerInfo {
    char address[48]; /* Beacon sender's numeric IPv4. */
    uint16_t port;    /* Claimed game port. */
    char name[LAN_DISCOVERY_NAME_MAX + 1]; /* World name (printable). */
    uint8_t players;  /* Claimed player count. */
    uint8_t capacity; /* Claimed max players. */
    float age;        /* Seconds since the last beacon. */
} LanServerInfo;

typedef struct LanDiscover LanDiscover;

/* Open the discovery socket (UDP broadcast + receive on
 * LAN_DISCOVERY_PORT). Returns NULL when unavailable (a second local
 * game already holds the port, no network, Winsock failure); the join
 * screen degrades to direct-IP entry.
 */
LanDiscover *lan_discover_create(void);

/* Close the socket and free everything. NULL-safe. */
void lan_discover_destroy(LanDiscover *d);

/* Broadcast a presence query now (also sent once at create).
 * Returns false when the socket is down.
 */
bool lan_discover_scan(LanDiscover *d);

/* Receive pending datagrams, age entries, and expire stale ones
 * (dt seconds, clamped 0..1 internally; NaN reads as 0). NULL-safe.
 */
void lan_discover_poll(LanDiscover *d, float dt);

/* Live entries, oldest-beacon order not guaranteed. */
size_t lan_discover_count(const LanDiscover *d);

/* Entry by index (NULL on bad args/range; pointer valid until the
 * next poll/destroy call).
 */
const LanServerInfo *lan_discover_at(const LanDiscover *d, size_t index);

/* Arm host announcements: answers queries immediately and beacons
 * every LAN_DISCOVERY_BEACON_T seconds (dt-driven, same clamp).
 * world_name may be NULL (falls back to "TerraCraft World"); player
 * counts are clamped to 255. NULL-safe.
 */
void lan_discover_host(LanDiscover *d, const char *world_name, unsigned players, unsigned capacity,
                       uint16_t port, float dt);

/* Format one beacon datagram ("TCRAFT1 <port> <players>/<cap> <name>").
 * Pure (no sockets): name is sanitized to printable ASCII, truncated
 * to LAN_DISCOVERY_NAME_MAX. Returns bytes written (excluding NUL),
 * 0 when out is NULL/tiny or port is 0.
 */
size_t lan_discover_format_beacon(char *out, size_t cap, uint16_t port, unsigned players,
                                  unsigned capacity, const char *name);

/* Parse one beacon datagram into fields (sender address validated
 * separately by the receiver). Returns true on a well-formed beacon.
 * Any out pointer may be NULL (field skipped).
 */
bool lan_discover_parse_beacon(const char *data, size_t len, uint16_t *out_port,
                               unsigned *out_players, unsigned *out_capacity, char *out_name,
                               size_t name_cap);
