#include "core/mem.h"
#include "core/log.h"

#include <stdlib.h>

/* Allocate `size` bytes.
 *
 * Args:
 *   size: bytes to allocate.
 *
 * Returns: pointer or NULL (logs on failure / zero size).
 */
void *mem_alloc(size_t size)
{
    if (size == 0) {
        LOG_WARN("mem_alloc: requested 0 bytes");
        return NULL;
    }
    void *ptr = malloc(size);
    if (ptr == NULL) {
        LOG_ERROR("mem_alloc: out of memory (%zu bytes)", size);
    }
    return ptr;
}

/* Allocate zeroed `count * size` bytes.
 *
 * Args:
 *   count: element count.
 *   size: element size.
 *
 * Returns: pointer or NULL.
 */
void *mem_calloc(size_t count, size_t size)
{
    if (count == 0 || size == 0) {
        LOG_WARN("mem_calloc: requested 0 elements/bytes");
        return NULL;
    }
    void *ptr = calloc(count, size);
    if (ptr == NULL) {
        LOG_ERROR("mem_calloc: out of memory (%zu x %zu bytes)", count, size);
    }
    return ptr;
}

/* Free a pointer. NULL-safe.
 *
 * Args:
 *   ptr: pointer to free.
 */
void mem_free(void *ptr)
{
    free(ptr);
}

/* Align `n` up to the next multiple of `alignment` (power of two). */
static size_t align_up(size_t n, size_t alignment)
{
    return (n + (alignment - 1)) & ~(alignment - 1);
}

/* Create an arena with `capacity` bytes of backing storage.
 *
 * Args:
 *   out: arena to initialise.
 *   capacity: backing size.
 *
 * Returns: 0 on success, -1 on bad args, -2 on OOM.
 */
int arena_init(Arena *out, size_t capacity)
{
    if (out == NULL || capacity == 0) {
        return -1;
    }
    out->data = (unsigned char *)malloc(capacity);
    if (out->data == NULL) {
        LOG_ERROR("arena_init: out of memory (%zu bytes)", capacity);
        out->capacity = 0;
        out->offset = 0;
        return -2;
    }
    out->capacity = capacity;
    out->offset = 0;
    return 0;
}

/* Release an arena's backing storage.
 *
 * Args:
 *   arena: arena to destroy.
 */
void arena_destroy(Arena *arena)
{
    if (arena == NULL) {
        return;
    }
    free(arena->data);
    arena->data = NULL;
    arena->capacity = 0;
    arena->offset = 0;
}

/* Bump-allocate from the arena (8-byte aligned).
 *
 * Args:
 *   arena: arena to allocate from.
 *   size: bytes requested.
 *
 * Returns: pointer or NULL when out of space.
 */
void *arena_push(Arena *arena, size_t size)
{
    if (arena == NULL || arena->data == NULL || size == 0) {
        return NULL;
    }
    size_t aligned_offset = align_up(arena->offset, 8);
    if (aligned_offset + size > arena->capacity) {
        return NULL;
    }
    void *ptr = arena->data + aligned_offset;
    arena->offset = aligned_offset + size;
    return ptr;
}

/* Reset an arena for reuse.
 *
 * Args:
 *   arena: arena to reset.
 */
void arena_reset(Arena *arena)
{
    if (arena == NULL) {
        return;
    }
    arena->offset = 0;
}
