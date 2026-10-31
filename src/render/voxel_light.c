#include "render/voxel_light.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <stdlib.h>
#include <string.h>

#define LIGHT_HALO 15
#define LIGHT_SIDE (CHUNK_X + 2 * LIGHT_HALO)
#define LIGHT_AREA (LIGHT_SIDE * LIGHT_SIDE)
#define LIGHT_VOLUME (LIGHT_AREA * CHUNK_Y)

struct VoxelLightBake {
    uint8_t values[LIGHT_VOLUME];
};

static size_t light_index(int x, int y, int z)
{
    return (size_t)y * LIGHT_AREA + (size_t)z * LIGHT_SIDE + (size_t)x;
}

/* FIFO entries are deduplicated by the scratch opacity high bit. The queue
 * cannot exceed the finite cell count, even with mixed source strengths. */
static void enqueue(uint32_t *queue, uint8_t *opacity, size_t *tail, size_t *count, size_t i)
{
    if (opacity[i] & 128) return;
    opacity[i] |= 128;
    queue[*tail] = (uint32_t)i;
    *tail = (*tail + 1) % LIGHT_VOLUME;
    ++*count;
}

static void propagate(VoxelLightBake *b, uint8_t *opacity, uint32_t *queue, int shift,
                       const int *tops, const int *bottoms, const uint32_t *sources, size_t source_count)
{
    size_t head = 0, tail = 0, count = 0;
    const int offsets[6] = {-1, 1, -LIGHT_SIDE, LIGHT_SIDE, -LIGHT_AREA, LIGHT_AREA};
    /* Only enqueue a seed if it can actually brighten a neighboring cell.
     * Most open-sky cells are uniformly 15 and need no flood work. */
    if (shift == 4) {
        for (size_t i = 0; i < source_count; ++i)
            enqueue(queue, opacity, &tail, &count, sources[i]);
    } else {
        for (int z = 0; z < LIGHT_SIDE; ++z) {
            for (int x = 0; x < LIGHT_SIDE; ++x) {
                int column = z * LIGHT_SIDE + x;
                int top = tops[column];
                if (x > 0 && tops[column - 1] > top) top = tops[column - 1];
                if (x + 1 < LIGHT_SIDE && tops[column + 1] > top) top = tops[column + 1];
                if (z > 0 && tops[column - LIGHT_SIDE] > top) top = tops[column - LIGHT_SIDE];
                if (z + 1 < LIGHT_SIDE && tops[column + LIGHT_SIDE] > top) top = tops[column + LIGHT_SIDE];
                /* Above all five column tops every cell already has sky15;
                 * below this column's direct bottom there is no source. */
                for (int y = bottoms[column]; y <= top; ++y) {
                    size_t i = light_index(x, y, z);
                    int level = (b->values[i] >> shift) & 15;
                    if (level < 2) continue;
                    for (int d = 0; d < 6; ++d) {
                        if ((d == 0 && x == 0) || (d == 1 && x == LIGHT_SIDE - 1) ||
                            (d == 2 && z == 0) || (d == 3 && z == LIGHT_SIDE - 1) ||
                            (d == 4 && y == 0) || (d == 5 && y == CHUNK_Y - 1)) continue;
                        size_t ni = (size_t)((int)i + offsets[d]);
                        int cost = opacity[ni] & 15;
                        if (cost < 1) cost = 1;
                        if (level - cost > ((b->values[ni] >> shift) & 15)) {
                            enqueue(queue, opacity, &tail, &count, i);
                            break;
                        }
                    }
                }
            }
        }
    }
    while (count) {
        size_t i = queue[head];
        head = (head + 1) % LIGHT_VOLUME;
        --count;
        opacity[i] &= 127;
        int x = (int)(i % LIGHT_SIDE);
        int z = (int)((i / LIGHT_SIDE) % LIGHT_SIDE);
        int y = (int)(i / LIGHT_AREA);
        int level = (b->values[i] >> shift) & 15;
        for (int d = 0; d < 6; ++d) {
            if ((d == 0 && x == 0) || (d == 1 && x == LIGHT_SIDE - 1) ||
                (d == 2 && z == 0) || (d == 3 && z == LIGHT_SIDE - 1) ||
                (d == 4 && y == 0) || (d == 5 && y == CHUNK_Y - 1)) continue;
            size_t ni = (size_t)((int)i + offsets[d]);
            int cost = opacity[ni] & 15;
            if (cost < 1) cost = 1;
            int next = level - cost;
            if (next > ((b->values[ni] >> shift) & 15)) {
                b->values[ni] = (uint8_t)((b->values[ni] & ~(15 << shift)) | (next << shift));
                enqueue(queue, opacity, &tail, &count, ni);
            }
        }
    }
}

VoxelLightBake *voxel_light_bake(const Chunk *c, const World *w)
{
    if (!c) return NULL;
    VoxelLightBake *b = (VoxelLightBake *)calloc(1, sizeof(*b));
    uint8_t *opacity = (uint8_t *)malloc(LIGHT_VOLUME);
    uint32_t *queue = (uint32_t *)malloc((size_t)LIGHT_VOLUME * sizeof(*queue));
    uint32_t *sources = (uint32_t *)malloc((size_t)LIGHT_VOLUME * sizeof(*sources));
    int tops[LIGHT_AREA], bottoms[LIGHT_AREA];
    size_t source_count = 0;
    if (!b || !opacity || !queue || !sources) {
        free(b); free(opacity); free(queue); free(sources);
        return NULL;
    }
    /* Resolve columns once rather than hashing the world for every voxel. */
    for (int z = 0; z < LIGHT_SIDE; ++z) {
        for (int x = 0; x < LIGHT_SIDE; ++x) {
            int lx = x - LIGHT_HALO, lz = z - LIGHT_HALO;
            int dx = lx < 0 ? -1 : (lx >= CHUNK_X ? 1 : 0);
            int dz = lz < 0 ? -1 : (lz >= CHUNK_Z ? 1 : 0);
            const Chunk *source = dx == 0 && dz == 0 ? c :
                                  (w ? world_get_chunk(w, c->cx + dx, c->cz + dz) : NULL);
            int sx = lx - dx * CHUNK_X, sz = lz - dz * CHUNK_Z;
            int sky = 15;
            int column = z * LIGHT_SIDE + x;
            tops[column] = -1;
            bottoms[column] = CHUNK_Y;
            for (int y = CHUNK_Y - 1; y >= 0; --y) {
                uint16_t id = source ? source->blocks[chunk_index(sx, y, sz)] : BLOCK_AIR;
                size_t i = light_index(x, y, z);
                int a = block_light_opacity(id);
                if (a && tops[column] < 0) tops[column] = y;
                opacity[i] = (uint8_t)a;
                sky = a >= sky ? 0 : sky - a;
                if (sky) bottoms[column] = y;
                int emitted = block_light_emission(id);
                if (emitted) sources[source_count++] = (uint32_t)i;
                b->values[i] = (uint8_t)(sky | (emitted << 4));
            }
        }
    }
    propagate(b, opacity, queue, 0, tops, bottoms, sources, source_count);
    propagate(b, opacity, queue, 4, tops, bottoms, sources, source_count);
    free(opacity);
    free(queue);
    free(sources);
    return b;
}

void voxel_light_free(VoxelLightBake *b) { free(b); }

void voxel_light_sample(const VoxelLightBake *b, int x, int y, int z, float *sky, float *block)
{
    uint8_t value = y >= CHUNK_Y ? 15 : 0;
    x += LIGHT_HALO; z += LIGHT_HALO;
    if (b && x >= 0 && x < LIGHT_SIDE && z >= 0 && z < LIGHT_SIDE && y >= 0 && y < CHUNK_Y)
        value = b->values[light_index(x, y, z)];
    if (sky) *sky = (float)(value & 15) / 15.0f;
    if (block) *block = (float)(value >> 4) / 15.0f;
}

void voxel_light_copy_chunk(const VoxelLightBake *b, uint8_t *dst)
{
    if (!b || !dst) return;
    for (int y = 0; y < CHUNK_Y; ++y)
        for (int z = 0; z < CHUNK_Z; ++z)
            memcpy(dst + chunk_index(0, y, z),
                   b->values + light_index(LIGHT_HALO, y, z + LIGHT_HALO), CHUNK_X);
}

float voxel_light_direct_sky(const World *w, int x, int y, int z)
{
    int sky = 15;
    if (y < 0) return 0.0f;
    for (int sy = CHUNK_Y - 1; sy >= y && sky; --sy) {
        int a = block_light_opacity(world_get_block(w, x, sy, z));
        sky = a >= sky ? 0 : sky - a;
    }
    return (float)sky / 15.0f;
}
