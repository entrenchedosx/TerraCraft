#pragma once

/* Memory utilities: checked allocation wrappers + linear arena allocator.
 *
 * Ownership rule: whoever allocates frees. Arenas own their backing block;
 * resetting an arena does not free individual allocations.
 */

#include <stddef.h>

/* Allocate `size` bytes. Logs on failure.
 *
 * Args:
 *   size: bytes to allocate (must be > 0).
 *
 * Returns: pointer on success, NULL on failure.
 */
void *mem_alloc(size_t size);

/* Allocate zeroed array (`count` x `size`). Logs on failure.
 *
 * Args:
 *   count: element count.
 *   size: element size.
 *
 * Returns: pointer on success, NULL on failure.
 */
void *mem_calloc(size_t count, size_t size);

/* Free a pointer allocated with mem_alloc/mem_calloc. NULL-safe.
 *
 * Args:
 *   ptr: pointer to free (may be NULL).
 */
void mem_free(void *ptr);

/* Linear (bump) arena for transient per-frame data.
 * Allocate once, bump `offset` per allocation, reset per frame.
 */
typedef struct Arena {
    unsigned char *data; /* Backing block (owned). */
    size_t capacity;     /* Total bytes in backing block. */
    size_t offset;       /* Next free byte. */
} Arena;

/* Create an arena with `capacity` bytes of backing storage.
 *
 * Args:
 *   out: arena to initialise (must not be NULL).
 *   capacity: backing size in bytes (must be > 0).
 *
 * Returns: 0 on success, non-zero on failure.
 */
int arena_init(Arena *out, size_t capacity);

/* Release an arena's backing storage. NULL-safe fields.
 *
 * Args:
 *   arena: arena to destroy (may be NULL).
 */
void arena_destroy(Arena *arena);

/* Bump-allocate `size` bytes with 8-byte alignment from the arena.
 *
 * Args:
 *   arena: arena to allocate from (must not be NULL).
 *   size: bytes requested.
 *
 * Returns: pointer on success, NULL when out of space or on bad args.
 */
void *arena_push(Arena *arena, size_t size);

/* Reset an arena so its storage can be reused (does not free backing).
 *
 * Args:
 *   arena: arena to reset (must not be NULL).
 */
void arena_reset(Arena *arena);
