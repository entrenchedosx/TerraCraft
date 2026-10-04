#include "core/hashmap.h"
#include "core/log.h"

#include <stdlib.h>

/* Slot states. */
#define HASHMAP_EMPTY 0
#define HASHMAP_FILLED 1
#define HASHMAP_TOMBSTONE 2

/* Maximum average load (count*10/capacity) before resize. 7/10 = 70%. */
#define HASHMAP_MAX_LOAD_NUM 7
#define HASHMAP_MAX_LOAD_DEN 10

/* Minimum capacity (power of two). */
#define HASHMAP_MIN_CAP 16

/* 64-bit mix (splitmix64 finalizer) for probe distribution. */
static uint64_t hashmap_mix(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/* Round up to the next power of two (>= 1). */
static size_t next_pow2(size_t n)
{
    size_t p = 1;
    while (p < n) {
        p *= 2;
    }
    return p;
}

/* Find a slot for key: returns index, sets *found. Scans the probe chain
 * until an empty slot (miss) or the key (hit). First tombstone is recorded
 * for reuse on insert. NULL-safe on empty maps (returns 0, found=false).
 */
static size_t hashmap_probe(const HashMap *m, int64_t key, bool *found, size_t *first_tomb)
{
    if (found != NULL) {
        *found = false;
    }
    if (first_tomb != NULL) {
        *first_tomb = (size_t)-1;
    }
    if (m == NULL || m->capacity == 0) {
        return 0;
    }
    size_t mask = m->capacity - 1;
    size_t idx = (size_t)hashmap_mix((uint64_t)key) & mask;
    for (size_t i = 0; i < m->capacity; ++i) {
        size_t s = (idx + i) & mask;
        unsigned char st = m->states[s];
        if (st == HASHMAP_EMPTY) {
            return s;
        }
        if (st == HASHMAP_TOMBSTONE) {
            if (first_tomb != NULL && *first_tomb == (size_t)-1) {
                *first_tomb = s;
            }
            continue;
        }
        if (m->keys[s] == key) {
            if (found != NULL) {
                *found = true;
            }
            return s;
        }
    }
    return idx;
}

/* Resize to new_cap (power of two), rehashing filled entries. Tombstones are
 * dropped. Returns 0 on success, non-zero on OOM (map left untouched).
 */
static int hashmap_resize(HashMap *m, size_t new_cap)
{
    int64_t *old_keys = m->keys;
    void **old_vals = m->values;
    unsigned char *old_states = m->states;
    size_t old_cap = m->capacity;

    m->keys = (int64_t *)calloc(new_cap, sizeof(int64_t));
    m->values = (void **)calloc(new_cap, sizeof(void *));
    m->states = (unsigned char *)calloc(new_cap, sizeof(unsigned char));
    if (m->keys == NULL || m->values == NULL || m->states == NULL) {
        free(m->keys);
        free(m->values);
        free(m->states);
        m->keys = old_keys;
        m->values = old_vals;
        m->states = old_states;
        return -1;
    }
    m->capacity = new_cap;
    m->count = 0;
    for (size_t i = 0; i < old_cap; ++i) {
        if (old_states[i] == HASHMAP_FILLED) {
            bool found = false;
            size_t s = hashmap_probe(m, old_keys[i], &found, NULL);
            m->keys[s] = old_keys[i];
            m->values[s] = old_vals[i];
            m->states[s] = HASHMAP_FILLED;
            m->count++;
        }
    }
    free(old_keys);
    free(old_vals);
    free(old_states);
    return 0;
}

/* Create a map.
 *
 * Args:
 *   m: map to initialise.
 *   initial_capacity: size hint.
 *
 * Returns: 0 on success.
 */
int hashmap_init(HashMap *m, size_t initial_capacity)
{
    if (m == NULL) {
        return -1;
    }
    m->keys = NULL;
    m->values = NULL;
    m->states = NULL;
    m->capacity = 0;
    m->count = 0;
    if (initial_capacity == 0) {
        initial_capacity = HASHMAP_MIN_CAP;
    }
    size_t cap = next_pow2(initial_capacity);
    if (cap < HASHMAP_MIN_CAP) {
        cap = HASHMAP_MIN_CAP;
    }
    m->keys = (int64_t *)calloc(cap, sizeof(int64_t));
    m->values = (void **)calloc(cap, sizeof(void *));
    m->states = (unsigned char *)calloc(cap, sizeof(unsigned char));
    if (m->keys == NULL || m->values == NULL || m->states == NULL) {
        LOG_ERROR("hashmap_init: out of memory (%zu slots)", cap);
        free(m->keys);
        free(m->values);
        free(m->states);
        m->keys = NULL;
        m->values = NULL;
        m->states = NULL;
        m->capacity = 0;
        return -2;
    }
    m->capacity = cap;
    return 0;
}

/* Release internal arrays.
 *
 * Args:
 *   m: map to release.
 */
void hashmap_free(HashMap *m)
{
    if (m == NULL) {
        return;
    }
    free(m->keys);
    free(m->values);
    free(m->states);
    m->keys = NULL;
    m->values = NULL;
    m->states = NULL;
    m->capacity = 0;
    m->count = 0;
}

/* Insert or replace.
 *
 * Args:
 *   m: map.
 *   key: key.
 *   value: value.
 *
 * Returns: 0 on success.
 */
int hashmap_put(HashMap *m, int64_t key, void *value)
{
    if (m == NULL || m->capacity == 0) {
        return -1;
    }
    /* Grow before insert when the next entry would exceed max load. */
    if ((m->count + 1) * HASHMAP_MAX_LOAD_DEN > m->capacity * HASHMAP_MAX_LOAD_NUM) {
        if (hashmap_resize(m, m->capacity * 2) != 0) {
            LOG_ERROR("hashmap_put: resize out of memory");
            return -2;
        }
    }
    bool found = false;
    size_t tomb = (size_t)-1;
    size_t s = hashmap_probe(m, key, &found, &tomb);
    if (found) {
        m->values[s] = value;
        return 0;
    }
    if (tomb != (size_t)-1) {
        s = tomb;
    }
    m->keys[s] = key;
    m->values[s] = value;
    m->states[s] = HASHMAP_FILLED;
    m->count++;
    return 0;
}

/* Look up a key.
 *
 * Args:
 *   m: map.
 *   key: key.
 *
 * Returns: value or NULL.
 */
void *hashmap_get(const HashMap *m, int64_t key)
{
    if (m == NULL || m->capacity == 0) {
        return NULL;
    }
    bool found = false;
    size_t s = hashmap_probe(m, key, &found, NULL);
    if (!found) {
        return NULL;
    }
    return m->values[s];
}

/* Remove a key.
 *
 * Args:
 *   m: map.
 *   key: key.
 *
 * Returns: true when removed.
 */
bool hashmap_remove(HashMap *m, int64_t key)
{
    if (m == NULL || m->capacity == 0) {
        return false;
    }
    bool found = false;
    size_t s = hashmap_probe(m, key, &found, NULL);
    if (!found) {
        return false;
    }
    m->states[s] = HASHMAP_TOMBSTONE;
    m->values[s] = NULL;
    m->count--;
    return true;
}

/* Entry count.
 *
 * Args:
 *   m: map.
 *
 * Returns: count.
 */
size_t hashmap_count(const HashMap *m)
{
    if (m == NULL) {
        return 0;
    }
    return m->count;
}

/* Build a chunk key (lossless for 32-bit coords).
 *
 * Args:
 *   cx, cz: chunk coords.
 *
 * Returns: combined key.
 */
int64_t hashmap_chunk_key(int cx, int cz)
{
    return (((int64_t)cx) << 32) | (uint32_t)cz;
}
