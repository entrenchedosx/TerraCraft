#include "test_main.h"

#include "render/mesher.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <stddef.h>

/* AO channel index in the 9-float vertex. */
#define AO_OFF 8

/* Test: pure AO level mapping and brightness table.
 *
 * Returns: failure count.
 */
int test_ao_levels(void)
{
    int failures = 0;
    /* NOTE: compared via variables to avoid MSVC C4127 under /W4. */
    int l_none = mesher_ao_level(false, false, false);
    int l_corner = mesher_ao_level(false, false, true);
    int l_side1 = mesher_ao_level(true, false, false);
    int l_side2 = mesher_ao_level(false, true, true);
    int l_both = mesher_ao_level(true, true, true);
    TEST_ASSERT(l_none == 3);
    TEST_ASSERT(l_corner == 2);
    TEST_ASSERT(l_side1 == 0);
    TEST_ASSERT(l_side2 == 0);
    TEST_ASSERT(l_both == 0);

    TEST_ASSERT_FLOAT_EQ(mesher_ao_factor(0), 0.4f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(mesher_ao_factor(1), 0.6f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(mesher_ao_factor(2), 0.8f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(mesher_ao_factor(3), 1.0f, 1e-6f);
    /* Clamping. */
    TEST_ASSERT_FLOAT_EQ(mesher_ao_factor(-5), 0.4f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(mesher_ao_factor(99), 1.0f, 1e-6f);
    return failures;
}

/* Collect AO of +Y verts at a given height whose (x,z) fall in a 2x2 set.
 * Returns the number of matching verts (0 on bad args). */
static int collect_top_ao(const MeshData *m, float y, float x0, float x1, float z0, float z1, float *out,
                          int cap)
{
    if (m == NULL || out == NULL || cap <= 0) {
        return 0;
    }
    int n = 0;
    for (size_t i = 0; i < m->vertex_count && n < cap; ++i) {
        const float *v = m->vertices + i * MESHER_FLOATS_PER_VERTEX;
        if (v[1] == y && v[4] == 1.0f && v[0] >= x0 && v[0] <= x1 && v[2] >= z0 && v[2] <= z1) {
            out[n++] = v[AO_OFF];
        }
    }
    return n;
}

/* Find one top-face vert AO at an exact (x, z) corner. Returns -1 if absent. */
static float corner_ao(const MeshData *m, float y, float x, float z)
{
    for (size_t i = 0; i < m->vertex_count; ++i) {
        const float *v = m->vertices + i * MESHER_FLOATS_PER_VERTEX;
        if (v[1] == y && v[4] == 1.0f && v[0] == x && v[2] == z) {
            return v[AO_OFF];
        }
    }
    return -1.0f;
}

/* Test: flat open floor is fully bright (level 3, full sun).
 *
 * Returns: failure count.
 */
int test_ao_flat(void)
{
    int failures = 0;
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 60; y <= 64; ++y) {
                chunk_set_block(c, x, y, z, BLOCK_STONE);
            }
        }
    }
    MeshData *m = mesher_build_chunk_mesh(c, NULL);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        /* 256 top faces -> 1024 verts, every one fully lit. */
        float aos[1100];
        int n = collect_top_ao(m, 65.0f, 0.0f, 16.0f, 0.0f, 16.0f, aos, 1100);
        TEST_ASSERT(n == 1024);
        int bright = 1;
        for (int i = 0; i < n; ++i) {
            if (aos[i] != 1.0f) {
                bright = 0;
                break;
            }
        }
        TEST_ASSERT(bright == 1);
        mesher_free(m);
    }
    chunk_destroy(c);
    return failures;
}

/* Test: L-shaped occluders darken exactly the enclosed corner.
 *
 * Returns: failure count.
 */
int test_ao_corner(void)
{
    int failures = 0;
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    chunk_set_block(c, 8, 64, 8, BLOCK_STONE);
    chunk_set_block(c, 7, 65, 8, BLOCK_STONE);
    chunk_set_block(c, 8, 65, 7, BLOCK_STONE);
    MeshData *m = mesher_build_chunk_mesh(c, NULL);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        /* Top face of (8,64,8): three enclosed corners dark, open one bright. */
        TEST_ASSERT_FLOAT_EQ(corner_ao(m, 65.0f, 8.0f, 8.0f), 0.4f, 1e-6f);
        TEST_ASSERT_FLOAT_EQ(corner_ao(m, 65.0f, 8.0f, 9.0f), 0.4f, 1e-6f);
        TEST_ASSERT_FLOAT_EQ(corner_ao(m, 65.0f, 9.0f, 8.0f), 0.4f, 1e-6f);
        TEST_ASSERT_FLOAT_EQ(corner_ao(m, 65.0f, 9.0f, 9.0f), 1.0f, 1e-6f);
        mesher_free(m);
    }
    chunk_destroy(c);
    return failures;
}

/* Test: occluders across a chunk border shade identically (no seams).
 *
 * Returns: failure count.
 */
int test_ao_seam(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Chunk *a = chunk_create(0, 0);
    Chunk *b = chunk_create(1, 0);
    TEST_ASSERT(a != NULL && b != NULL);
    if (a == NULL || b == NULL) {
        chunk_destroy(a);
        chunk_destroy(b);
        world_destroy(w);
        return failures + 1;
    }
    chunk_set_block(a, 15, 64, 8, BLOCK_STONE);
    chunk_set_block(b, 0, 65, 8, BLOCK_STONE); /* World (16,65,8). */
    TEST_ASSERT(world_add_chunk(w, a) == 0);
    TEST_ASSERT(world_add_chunk(w, b) == 0);

    MeshData *m = mesher_build_chunk_mesh(a, w);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        /* Corner (16,65,8) sees the cross-chunk occluder -> dark. */
        TEST_ASSERT_FLOAT_EQ(corner_ao(m, 65.0f, 16.0f, 8.0f), 0.4f, 1e-6f);
        /* Far corner (15,65,9) is fully open -> bright. */
        TEST_ASSERT_FLOAT_EQ(corner_ao(m, 65.0f, 15.0f, 9.0f), 1.0f, 1e-6f);
        mesher_free(m);
    }
    world_destroy(w);
    return failures;
}

/* Test: roofed faces get the shade factor; top-solid helper works.
 *
 * Returns: failure count.
 */
int test_ao_sun_shade(void)
{
    int failures = 0;
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    TEST_ASSERT(chunk_top_solid(c, 8, 8) == -1);
    TEST_ASSERT(chunk_top_solid(NULL, 0, 0) == -1);
    TEST_ASSERT(chunk_top_solid(c, -1, 0) == -1);

    chunk_set_block(c, 8, 64, 8, BLOCK_STONE);
    chunk_set_block(c, 8, 66, 8, BLOCK_STONE); /* Roof, one air gap above. */
    TEST_ASSERT(chunk_top_solid(c, 8, 8) == 66);

    MeshData *m = mesher_build_chunk_mesh(c, NULL);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        /* Top face of (8,64,8): open AO (level 3) but shaded sun -> 0.45. */
        float aos[8];
        int n = collect_top_ao(m, 65.0f, 8.0f, 9.0f, 8.0f, 9.0f, aos, 8);
        TEST_ASSERT(n == 4);
        int shaded = 1;
        for (int i = 0; i < n; ++i) {
            float d = aos[i] - 0.45f;
            if (d < 0.0f) {
                d = -d;
            }
            if (!(d <= 1e-5f)) {
                shaded = 0;
            }
        }
        TEST_ASSERT(shaded == 1);
        mesher_free(m);
    }
    chunk_destroy(c);
    return failures;
}
