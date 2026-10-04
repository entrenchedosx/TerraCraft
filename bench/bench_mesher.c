/* Mesher benchmark: times mesher_build_chunk_mesh() over five chunk
 * workloads. Each case builds once untimed (warmup), then times
 * BENCH_REPEATS rebuilds with clock() and prints the average ms plus
 * the vertex/index counts of the last build.
 *
 * Build (snippet appended to CMakeLists.txt, not applied here):
  *   add_executable(terracraft_bench bench/bench_mesher.c)
  *   target_link_libraries(terracraft_bench PRIVATE terracraft_core)
 */

#include "render/mesher.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"
#include "world/world_gen.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* Timed rebuilds per case (plus one untimed warmup build). */
#define BENCH_REPEATS 20

/* Seed for all generated-terrain cases (matches save-test fixtures). */
#define BENCH_SEED 123456L

/* Fill every cell of c with id. */
static void bench_fill_solid(Chunk *c, uint16_t id)
{
    for (int x = 0; x < CHUNK_X; ++x) {
        for (int z = 0; z < CHUNK_Z; ++z) {
            for (int y = 0; y < CHUNK_Y; ++y) {
                chunk_set_block(c, x, y, z, id);
            }
        }
    }
}

/* Fill c with a worst-case checkerboard (stone/air alternating on every
 * axis). Each stone cube exposes all six faces, stressing vertex/index
 * throughput. */
static void bench_fill_checkerboard(Chunk *c)
{
    for (int x = 0; x < CHUNK_X; ++x) {
        for (int z = 0; z < CHUNK_Z; ++z) {
            for (int y = 0; y < CHUNK_Y; ++y) {
                uint16_t id = (uint16_t)(((x + y + z) % 2 == 0) ? BLOCK_STONE : BLOCK_AIR);
                chunk_set_block(c, x, y, z, id);
            }
        }
    }
}

/* Warm up once (untimed), then time BENCH_REPEATS rebuilds.
 * On success stores the last build's counts and returns the average
 * milliseconds per build; returns -1.0 when the mesher fails. */
static double bench_mesh(const Chunk *c, const World *w, size_t *out_vertices, size_t *out_indices)
{
    if (c == NULL || out_vertices == NULL || out_indices == NULL) {
        return -1.0;
    }
    MeshData *warm = mesher_build_chunk_mesh(c, w);
    if (warm == NULL) {
        return -1.0;
    }
    *out_vertices = warm->vertex_count;
    *out_indices = warm->index_count;
    mesher_free(warm);

    clock_t start = clock();
    for (int i = 0; i < BENCH_REPEATS; ++i) {
        MeshData *m = mesher_build_chunk_mesh(c, w);
        if (m == NULL) {
            return -1.0;
        }
        *out_vertices = m->vertex_count;
        *out_indices = m->index_count;
        mesher_free(m);
    }
    clock_t end = clock();
    double total_ms = (double)(end - start) * 1000.0 / (double)CLOCKS_PER_SEC;
    return total_ms / (double)BENCH_REPEATS;
}

/* Report one case: label, average ms, vertex/index counts. */
static void bench_report(const char *label, double ms, size_t verts, size_t idx)
{
    printf("%s: %.3f ms avg (%d rebuilds), %zu verts, %zu indices\n", label, ms, BENCH_REPEATS, verts, idx);
}

int main(void)
{
    int failures = 0;

    printf("mesher bench (N=%d rebuilds per case, seed 123456):\n", BENCH_REPEATS);

    /* (a) Solid stone chunk (interior fully occluded; only the shell meshes). */
    {
        Chunk *c = chunk_create(0, 0);
        if (c == NULL) {
            fprintf(stderr, "bench: solid-stone chunk_create failed\n");
            return 1;
        }
        bench_fill_solid(c, (uint16_t)BLOCK_STONE);
        size_t verts = 0;
        size_t idx = 0;
        double ms = bench_mesh(c, NULL, &verts, &idx);
        if (ms < 0.0) {
            fprintf(stderr, "bench: solid-stone meshing failed\n");
            failures += 1;
        } else {
            bench_report("solid stone", ms, verts, idx);
        }
        chunk_destroy(c);
    }

    /* (b)(c)(d) Generated terrain (seed 123456). Real terrain already
     * contains surface relief, caves, and vegetation, so three distinct
     * chunk coords sample those workloads. One shared world keeps
     * cross-chunk borders exact. world_generate_chunk() adopts each chunk
     * into w (see world_gen.h), so w->chunks[i] is the generated chunk. */
    {
        World *w = world_create();
        if (w == NULL) {
            fprintf(stderr, "bench: world_create failed\n");
            return 1;
        }
        w->seed = BENCH_SEED;
        if (world_generate_chunk(w, 0, 0) != 0 || world_generate_chunk(w, 1, 0) != 0 ||
            world_generate_chunk(w, 0, 1) != 0 || w->count < (size_t)3) {
            fprintf(stderr, "bench: world_generate_chunk failed\n");
            world_destroy(w);
            return 1;
        }
        const char *labels[3] = {
            "generated surface-terrain (0,0)",
            "generated cave-heavy (1,0)",
            "generated vegetation-heavy (0,1)",
        };
        for (int i = 0; i < 3; ++i) {
            Chunk *c = w->chunks[i];
            size_t verts = 0;
            size_t idx = 0;
            double ms = bench_mesh(c, w, &verts, &idx);
            if (ms < 0.0) {
                fprintf(stderr, "bench: %s meshing failed\n", labels[i]);
                failures += 1;
            } else {
                bench_report(labels[i], ms, verts, idx);
            }
        }
        world_destroy(w);
    }

    /* (e) Worst-case checkerboard stone/air. */
    {
        Chunk *c = chunk_create(0, 0);
        if (c == NULL) {
            fprintf(stderr, "bench: checkerboard chunk_create failed\n");
            return 1;
        }
        bench_fill_checkerboard(c);
        size_t verts = 0;
        size_t idx = 0;
        double ms = bench_mesh(c, NULL, &verts, &idx);
        if (ms < 0.0) {
            fprintf(stderr, "bench: checkerboard meshing failed\n");
            failures += 1;
        } else {
            bench_report("checkerboard stone/air", ms, verts, idx);
        }
        chunk_destroy(c);
    }

    if (failures == 0) {
        printf("bench: all cases passed\n");
    } else {
        fprintf(stderr, "bench: %d case(s) failed\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
