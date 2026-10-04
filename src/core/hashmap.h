#pragma once

/* Minimal open-addressing hash map (M2): int64 key -> void* value.
 * Linear probing with tombstones, power-of-two capacity, auto-resize at
 * ~70% load. Used by World for O(1) chunk lookup. No dependencies, C17.
 *
 * Ownership: the map owns its internal arrays only, never keys/values.
 * Key 0 is fully usable (empty slots are tracked via state bytes).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Hash map with parallel key/value/state arrays. */
typedef struct HashMap {
    int64_t *keys; /* Key slots (valid only where state == 1). */
    void **values; /* Value slots. */
    unsigned char *states; /* Per-slot state: 0 empty, 1 filled, 2 tombstone. */
    size_t capacity; /* Slot count (always a power of two, 0 when empty). */
    size_t count; /* Number of filled slots. */
} HashMap;

/* Create a map with room for roughly `initial_capacity` entries.
 * The map starts empty either way; capacity is rounded up to power of two.
 *
 * Args:
 *   m: map to initialise (must not be NULL).
 *   initial_capacity: hint (0 selects a small default).
 *
 * Returns: 0 on success, non-zero on bad args/OOM.
 */
int hashmap_init(HashMap *m, size_t initial_capacity);

/* Release internal arrays. Values are NOT freed. NULL-state-safe.
 *
 * Args:
 *   m: map to release (may be NULL; uninitialised zero-map is safe).
 */
void hashmap_free(HashMap *m);

/* Insert or replace a key/value pair (takes no ownership of value).
 *
 * Args:
 *   m: map (must not be NULL).
 *   key: int64 key (any value including 0 and negatives).
 *   value: value pointer (may be NULL to store an explicit NULL).
 *
 * Returns: 0 on success, non-zero on bad args/OOM.
 */
int hashmap_put(HashMap *m, int64_t key, void *value);

/* Look up a key.
 *
 * Args:
 *   m: map (must not be NULL).
 *   key: key to find.
 *
 * Returns: stored value, or NULL when absent (or when NULL was stored).
 */
void *hashmap_get(const HashMap *m, int64_t key);

/* Remove a key (leaves a tombstone; does not free the value).
 *
 * Args:
 *   m: map (must not be NULL).
 *   key: key to remove.
 *
 * Returns: true when the key was present and removed.
 */
bool hashmap_remove(HashMap *m, int64_t key);

/* Number of filled slots.
 *
 * Args:
 *   m: map (may be NULL).
 *
 * Returns: entry count (0 on NULL).
 */
size_t hashmap_count(const HashMap *m);

/* Build a chunk-map key from chunk coords (lossless, sign-preserving).
 *
 * Args:
 *   cx, cz: chunk coordinates.
 *
 * Returns: combined int64 key.
 */
int64_t hashmap_chunk_key(int cx, int cz);
