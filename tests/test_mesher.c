#include "test_main.h"

#include "render/mesher.h"
#include "render/texture_atlas.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <stdlib.h>

/* Helper: heap chunk with a single block, no world (neighbors = AIR). */
static Chunk *make_single_block(int x, int y, int z, uint16_t id)
{
    Chunk *c = chunk_create(0, 0);
    if (c == NULL) {
        return NULL;
    }
    chunk_set_block(c, x, y, z, id);
    return c;
}

/* Test: a cross-sprite block (torch) meshes as 4 double-sided quads
 * (16 verts, 24 indices) with unit diagonal normals and full-tile UVs —
 * never as a 6-face cube.
 *
 * Returns: failure count.
 */
int test_mesher_cross_sprite(void)
{
    int failures = 0;
    TEST_ASSERT(block_is_cross(BLOCK_GRASS_PLANT) == true);
    TEST_ASSERT(block_is_cross(BLOCK_FLOWER) == true);
    TEST_ASSERT(block_is_cross(BLOCK_TORCH) == true);
    TEST_ASSERT(block_is_cross(BLOCK_STONE) == false);
    TEST_ASSERT(block_is_cross(BLOCK_LEAVES) == false);
    Chunk *c = make_single_block(8, 64, 8, BLOCK_TORCH);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    MeshData *m = mesher_build_chunk_mesh(c, NULL);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        TEST_ASSERT(m->vertex_count == 16);
        TEST_ASSERT(m->index_count == 24);
        /* All verts inside the cell, normals unit length. */
        int sane = 1;
        for (size_t i = 0; i < m->vertex_count && sane; ++i) {
            const float *v = m->vertices + i * MESHER_FLOATS_PER_VERTEX;
            if (!(v[0] >= 8.0f && v[0] <= 9.0f && v[1] >= 64.0f && v[1] <= 65.0f && v[2] >= 8.0f &&
                  v[2] <= 9.0f)) {
                sane = 0;
            }
            float len2 = v[3] * v[3] + v[4] * v[4] + v[5] * v[5];
            if (len2 < 0.999f || len2 > 1.001f) {
                sane = 0;
            }
            /* V spans the full tile (v1 top corners, v0 bottom corners). */
            float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
            texture_atlas_tile_uv(TILE_TORCH, &u0, &v0, &u1, &v1);
            int is_top = (v[1] == 65.0f);
            int is_bottom = (v[1] == 64.0f);
            if (!((is_top && v[7] == v1) || (is_bottom && v[7] == v0))) {
                sane = 0;
            }
            (void)u0;
            (void)u1;
        }
        TEST_ASSERT(sane == 1);
        /* Both diagonal directions present (normals span two planes). */
        int diag_a = 0;
        int diag_b = 0;
        for (size_t i = 0; i < m->vertex_count; ++i) {
            const float *v = m->vertices + i * MESHER_FLOATS_PER_VERTEX;
            if (v[3] < -0.5f && v[5] > 0.5f) {
                diag_a = 1;
            }
            if (v[3] < -0.5f && v[5] < -0.5f) {
                diag_b = 1;
            }
        }
        TEST_ASSERT(diag_a == 1 && diag_b == 1);
        mesher_free(m);
    }
    chunk_destroy(c);
    return failures;
}

/* Test: empty chunk yields an empty mesh.
 *
 * Returns: failure count.
 */
int test_mesher_empty(void)
{
    int failures = 0;
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    MeshData *m = mesher_build_chunk_mesh(c, NULL);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        TEST_ASSERT(m->vertex_count == 0);
        TEST_ASSERT(m->index_count == 0);
        mesher_free(m);
    }
    TEST_ASSERT(mesher_build_chunk_mesh(NULL, NULL) == NULL);
    chunk_destroy(c);
    return failures;
}

/* Test: one isolated stone cube exposes 6 faces (24 verts, 36 indices).
 *
 * Returns: failure count.
 */
int test_mesher_single_block(void)
{
    int failures = 0;
    Chunk *c = make_single_block(8, 64, 8, BLOCK_STONE);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    MeshData *m = mesher_build_chunk_mesh(c, NULL);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        TEST_ASSERT(m->vertex_count == 24);
        TEST_ASSERT(m->index_count == 36);
        /* Vertex stride sanity: first vertex position inside [8..9]. */
        TEST_ASSERT(m->vertices[0] >= 8.0f && m->vertices[0] <= 9.0f);
        /* Normal is unit axis. */
        float nx = m->vertices[3];
        float ny = m->vertices[4];
        float nz = m->vertices[5];
        float len2 = nx * nx + ny * ny + nz * nz;
        TEST_ASSERT_FLOAT_EQ(len2, 1.0f, 1e-5f);
        mesher_free(m);
    }
    chunk_destroy(c);
    return failures;
}

/* Test: two adjacent cubes share a face (10 exposed faces).
 *
 * Returns: failure count.
 */
int test_mesher_two_adjacent(void)
{
    int failures = 0;
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    chunk_set_block(c, 4, 64, 4, BLOCK_STONE);
    chunk_set_block(c, 5, 64, 4, BLOCK_STONE);
    MeshData *m = mesher_build_chunk_mesh(c, NULL);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        TEST_ASSERT(m->vertex_count == 40);
        TEST_ASSERT(m->index_count == 60);
        mesher_free(m);
    }
    chunk_destroy(c);
    return failures;
}

/* Test: buried block contributes no faces; world-aware borders work.
 *
 * Returns: failure count.
 */
int test_mesher_hidden_faces(void)
{
    int failures = 0;
    /* 3x3x3 solid cube in the middle of a chunk: center hidden, 6 outer
     * cells per face... simpler assertion: fill 3x3x3 and check the center
     * block's 6 faces are all culled by comparing against naive upper bound.
     * Full 27-block cube has 54 exposed faces (6 sides x 9). */
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    for (int x = 4; x < 7; ++x) {
        for (int y = 64; y < 67; ++y) {
            for (int z = 4; z < 7; ++z) {
                chunk_set_block(c, x, y, z, BLOCK_STONE);
            }
        }
    }
    MeshData *m = mesher_build_chunk_mesh(c, NULL);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        TEST_ASSERT(m->vertex_count == (size_t)(54 * 4));
        TEST_ASSERT(m->index_count == (size_t)(54 * 6));
        mesher_free(m);
    }
    chunk_destroy(c);

    /* Cross-chunk: block at local x=15 with neighbor chunk present vs absent.
     * Without a world, +X face is exposed (6 faces). With a world whose
     * neighbor chunk has stone at x=16, +X is hidden (5 faces). */
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
    chunk_set_block(b, 0, 64, 8, BLOCK_STONE); /* world (16,64,8) */
    TEST_ASSERT(world_add_chunk(w, a) == 0);
    TEST_ASSERT(world_add_chunk(w, b) == 0);

    MeshData *ma = mesher_build_chunk_mesh(a, w);
    TEST_ASSERT(ma != NULL);
    if (ma != NULL) {
        /* a's block: -X,-Y,+Y,-Z,+Z exposed (5 faces); +X hidden by b. */
        TEST_ASSERT(ma->vertex_count == 20);
        TEST_ASSERT(ma->index_count == 30);
        mesher_free(ma);
    }
    world_destroy(w);
    return failures;
}
