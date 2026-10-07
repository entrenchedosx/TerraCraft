#include "core/mem.h"
#include "core/log.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <stdio.h>
#endif

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

/* Query resident process memory without installing hooks into allocation.
 * This keeps the diagnostic representative of both wrapped and direct
 * allocator use in the engine and its dependencies.
 */
size_t mem_process_working_set_bytes(void)
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters;
    memset(&counters, 0, sizeof(counters));
    counters.cb = (DWORD)sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, (DWORD)sizeof(counters)) != 0) {
        return (size_t)counters.WorkingSetSize;
    }
#elif defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return (size_t)info.resident_size;
    }
#elif defined(__linux__)
    FILE *status = fopen("/proc/self/status", "r");
    if (status != NULL) {
        char line[128];
        unsigned long long kib = 0;
        while (fgets(line, sizeof(line), status) != NULL) {
            if (sscanf(line, "VmRSS: %llu kB", &kib) == 1) {
                fclose(status);
                if (kib > (unsigned long long)(SIZE_MAX / 1024u)) {
                    return SIZE_MAX;
                }
                return (size_t)(kib * 1024ULL);
            }
        }
        fclose(status);
    }
#endif
    return 0;
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
