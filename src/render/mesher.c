#include "render/mesher.h"
#include "core/log.h"
#include "core/time.h"
#include "render/texture_atlas.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <stdlib.h>

/* Face corner tables. Each face lists 4 corners (A,B,C,D) with the index
 * pattern (0,1,2, 2,1,3), wound CCW from outside so back-face culling keeps
 * outward faces. Verified: (B-A) x (C-A) == face normal for every face.
 * Corners are unit-cube offsets; world pos = chunk_origin + (x,y,z) + offset.
 * UV mapping is uniform: A->(u0,v0), B->(u1,v0), C->(u0,v1), D->(u1,v1),
 * which keeps the tile top (v1) on the block top (+Y offsets) for side faces.
 */
typedef struct FaceDef {
    float nx, ny, nz; /* Outward normal. */
    float corners[4][3]; /* A,B,C,D offsets. */
    int nx_o, ny_o, nz_o; /* Neighbor offset to test visibility. */
} FaceDef;

static const FaceDef FACES[6] = {
    /* -X */
    {.nx = -1.0f, .ny = 0.0f, .nz = 0.0f, .nx_o = -1, .ny_o = 0, .nz_o = 0,
     .corners = {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}, {0, 1, 1}}},
    /* +X */
    {.nx = 1.0f, .ny = 0.0f, .nz = 0.0f, .nx_o = 1, .ny_o = 0, .nz_o = 0,
     .corners = {{1, 0, 1}, {1, 0, 0}, {1, 1, 1}, {1, 1, 0}}},
    /* -Y (bottom) */
    {.nx = 0.0f, .ny = -1.0f, .nz = 0.0f, .nx_o = 0, .ny_o = -1, .nz_o = 0,
     .corners = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}}},
    /* +Y (top) */
    {.nx = 0.0f, .ny = 1.0f, .nz = 0.0f, .nx_o = 0, .ny_o = 1, .nz_o = 0,
     .corners = {{0, 1, 1}, {1, 1, 1}, {0, 1, 0}, {1, 1, 0}}},
    /* -Z */
    {.nx = 0.0f, .ny = 0.0f, .nz = -1.0f, .nx_o = 0, .ny_o = 0, .nz_o = -1,
     .corners = {{1, 0, 0}, {0, 0, 0}, {1, 1, 0}, {0, 1, 0}}},
    /* +Z */
    {.nx = 0.0f, .ny = 0.0f, .nz = 1.0f, .nx_o = 0, .ny_o = 0, .nz_o = 1,
     .corners = {{0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}}},
};

/* Per-corner UV selectors for the uniform A/B/C/D mapping. */
static const float FACE_U[4] = {0.0f, 1.0f, 0.0f, 1.0f};
static const float FACE_V[4] = {0.0f, 0.0f, 1.0f, 1.0f};

/* Forward declaration: neighbor resolver defined below the emit path. */
static uint16_t mesher_neighbor(const Chunk *c, const World *w, int lx, int ly, int lz);

/* AO brightness table (darkest -> brightest). */
static const float AO_TABLE[MESHER_AO_LEVELS] = {0.4f, 0.6f, 0.8f, 1.0f};

/* AO level from side/corner occupancy (M4 heuristic).
 *
 * Args:
 *   side1, side2, corner: occluder occupancy.
 *
 * Returns: level 0..3.
 */
int mesher_ao_level(bool side1, bool side2, bool corner)
{
    if (side1 || side2) {
        return 0;
    }
    if (corner) {
        return 2;
    }
    return 3;
}

/* Brightness factor for an AO level (clamped).
 *
 * Args:
 *   level: AO level.
 *
 * Returns: multiplier.
 */
float mesher_ao_factor(int level)
{
    if (level < 0) {
        level = 0;
    }
    if (level >= MESHER_AO_LEVELS) {
        level = MESHER_AO_LEVELS - 1;
    }
    return AO_TABLE[level];
}

/* Growable push helpers (returns 0 ok, -1 OOM). Buffers only ever
 * grow (doubling from 256/512 seeds): callers that can bound the
 * workload must pre-size instead — per-face reserve calls on a 20k-face
 * chunk cost milliseconds of repeated realloc/memcpy (measured: ~20 ms
 * of a ~23 ms terrain rebuild). */
static int mesh_reserve_vertices(MeshData *m, size_t extra_verts)
{
    size_t need = m->vertex_count + extra_verts;
    if (need <= m->vertex_cap) {
        return 0;
    }
    size_t cap = m->vertex_cap ? m->vertex_cap : 256;
    while (cap < need) {
        cap *= 2;
    }
    float *nv = (float *)realloc(m->vertices, cap * MESHER_FLOATS_PER_VERTEX * sizeof(float));
    if (nv == NULL) {
        return -1;
    }
    m->vertices = nv;
    m->vertex_cap = cap;
    return 0;
}

static int mesh_reserve_indices(MeshData *m, size_t extra_idx)
{
    size_t need = m->index_count + extra_idx;
    if (need <= m->index_cap) {
        return 0;
    }
    size_t cap = m->index_cap ? m->index_cap : 512;
    while (cap < need) {
        cap *= 2;
    }
    unsigned int *ni = (unsigned int *)realloc(m->indices, cap * sizeof(unsigned int));
    if (ni == NULL) {
        return -1;
    }
    m->indices = ni;
    m->index_cap = cap;
    return 0;
}

/* Light occlusion test for sun/AO: solid blocks occlude, except glass
 * (transparent cover must not darken what is below/beside it, matching
 * water which is already non-solid). Leaves keep occluding (canopy shade).
 */
static bool mesher_occludes(uint16_t id)
{
    return id != (uint16_t)BLOCK_GLASS && block_is_solid(id);
}

/* Emit one textured face quad with AO (4 verts, 6 indices).
 *
 * Args:
 *   m: mesh being built.
 *   f: face definition.
 *   face_idx: face index 0..5 (for tile selection).
 *   c: chunk being meshed (for neighbor resolution).
 *   w: world for cross-chunk lookups (may be NULL).
 *   x, y, z: block local coords.
 *   wx, wy, wz: block world-space minimum corner.
 *   block: block ID (for tile selection).
 *
 * Returns: 0 on success, -1 on OOM.
 */
static int mesh_emit_face(MeshData *m, const FaceDef *f, int face_idx, const Chunk *c, const World *w, int x,
                          int y, int z, float wx, float wy, float wz, uint16_t block)
{
    if (mesh_reserve_vertices(m, 4) != 0 || mesh_reserve_indices(m, 6) != 0) {
        return -1;
    }
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    texture_atlas_tile_uv(block_tile_for_face(block, face_idx), &u0, &v0, &u1, &v1);

    /* Empty-cell coords of this face (local, may be out of chunk bounds). */
    int ex = x + f->nx_o;
    int ey = y + f->ny_o;
    int ez = z + f->nz_o;

    /* Per-face sun visibility: full sun unless an occluding block sits
     * anywhere in the column above the face (M4 skylight heuristic).
     * The scan exits early on the first occluder (canopy shade) — but
     * open sky must NOT scan all 256 cells per face (~1M wasted lookups
     * per chunk); a short probe covers trees, and anything past it is
     * effectively skylit for gameplay shading. */
    float sun = MESHER_SUN_FULL;
    if (ey < CHUNK_Y) {
        int top = ey + MESHER_SKY_PROBE;
        if (top > CHUNK_Y) {
            top = CHUNK_Y;
        }
        for (int sy = ey; sy < top; ++sy) {
            if (mesher_occludes(mesher_neighbor(c, w, ex, sy, ez))) {
                sun = MESHER_SUN_SHADE;
                break;
            }
        }
    }

    /* Tangent axes (the two non-normal axes) for AO corner offsets. */
    int naxis = (f->nx_o != 0) ? 0 : ((f->ny_o != 0) ? 1 : 2);
    int t1 = (naxis == 0) ? 1 : 0;
    int t2 = (naxis == 2) ? 1 : 2;

    unsigned int base = (unsigned int)m->vertex_count;
    float fluid_height = block_is_water(block) ? block_water_height(block) : 1.0f;
    float fluid_bottom = 0.0f;
    if (block_is_water(block) && f->ny_o == 0) {
        uint16_t side = mesher_neighbor(c, w, ex, ey, ez);
        if (block_is_water(side)) {
            fluid_bottom = block_water_height(side);
        }
    }
    for (int i = 0; i < 4; ++i) {
        /* Corner offset sign per tangent axis: 0-offset -> -1, 1-offset -> +1. */
        int c1 = (int)f->corners[i][t1];
        int c2 = (int)f->corners[i][t2];
        int s1 = (c1 == 0) ? -1 : 1;
        int s2 = (c2 == 0) ? -1 : 1;
        int o1[3] = {0, 0, 0};
        int o2[3] = {0, 0, 0};
        o1[t1] = s1;
        o2[t2] = s2;
        bool side1 = mesher_occludes(mesher_neighbor(c, w, ex + o1[0], ey + o1[1], ez + o1[2]));
        bool side2 = mesher_occludes(mesher_neighbor(c, w, ex + o2[0], ey + o2[1], ez + o2[2]));
        bool corner =
            mesher_occludes(mesher_neighbor(c, w, ex + o1[0] + o2[0], ey + o1[1] + o2[1], ez + o1[2] + o2[2]));
        float ao = mesher_ao_factor(mesher_ao_level(side1, side2, corner)) * sun;

        float *v = m->vertices + (m->vertex_count * MESHER_FLOATS_PER_VERTEX);
        v[0] = wx + f->corners[i][0];
        float local_y = f->corners[i][1] * fluid_height;
        if (f->ny_o == 0 && f->corners[i][1] == 0.0f && fluid_bottom > 0.0f) {
            local_y = fluid_bottom;
        }
        v[1] = wy + local_y;
        v[2] = wz + f->corners[i][2];
        v[3] = f->nx;
        v[4] = f->ny;
        v[5] = f->nz;
        v[6] = (FACE_U[i] == 0.0f) ? u0 : u1;
        v[7] = (FACE_V[i] == 0.0f) ? v0 : v1;
        v[8] = ao;
        m->vertex_count++;
    }
    unsigned int *ix = m->indices + m->index_count;
    ix[0] = base + 0;
    ix[1] = base + 1;
    ix[2] = base + 2;
    ix[3] = base + 2;
    ix[4] = base + 1;
    ix[5] = base + 3;
    m->index_count += 6;
    return 0;
}

/* Resolve a neighbor block, crossing chunk borders via the world when
 * available. Falls back to in-chunk lookup (OOB = AIR) when w is NULL.
 */
static uint16_t mesher_neighbor(const Chunk *c, const World *w, int lx, int ly, int lz)
{
    if (chunk_in_bounds(lx, ly, lz)) {
        return chunk_get_block(c, lx, ly, lz);
    }
    if (w == NULL) {
        return BLOCK_AIR;
    }
    int wx = c->cx * CHUNK_X + lx;
    int wz = c->cz * CHUNK_Z + lz;
    return world_get_block(w, wx, ly, wz);
}

/* Emit one sprite vertex (9-float VoxelVertex layout). */
static void mesh_emit_sprite_vert(MeshData *m, float x, float y, float z, float nx, float ny, float nz,
                                  float u, float v, float ao)
{
    float *o = m->vertices + m->vertex_count * MESHER_FLOATS_PER_VERTEX;
    o[0] = x;
    o[1] = y;
    o[2] = z;
    o[3] = nx;
    o[4] = ny;
    o[5] = nz;
    o[6] = u;
    o[7] = v;
    o[8] = ao;
    m->vertex_count++;
}

/* Emit a cross-sprite block (plants, flowers, torches): two diagonal
 * quads corner-to-corner, each wound both ways (double-sided, 4 quads =
 * 16 verts + 24 indices). Full tile UVs with v1 at the top. Sunlight
 * applies (no AO occlusion); depth-tested like everything else, so
 * buried sprites stay hidden.
 *
 * Args:
 *   m: mesh being built.
 *   c: chunk being meshed (for neighbor resolution).
 *   w: world for cross-chunk lookups (may be NULL).
 *   x, y, z: block local coords.
 *   wx, wy, wz: block world-space minimum corner.
 *   block: block ID (for tile selection).
 *
 * Returns: 0 on success, -1 on OOM.
 */
static int mesh_emit_cross(MeshData *m, const Chunk *c, const World *w, int x, int y, int z, float wx,
                           float wy, float wz, uint16_t block)
{
    if (mesh_reserve_vertices(m, 16) != 0 || mesh_reserve_indices(m, 24) != 0) {
        return -1;
    }
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    texture_atlas_tile_uv(block_tile_for_face(block, 0), &u0, &v0, &u1, &v1);
    /* Sun visibility like a top face (skips AO occlusion entirely;
     * same bounded probe as faces — open sky never scans 256 cells). */
    float sun = MESHER_SUN_FULL;
    if (y + 1 < CHUNK_Y) {
        int top = y + 1 + MESHER_SKY_PROBE;
        if (top > CHUNK_Y) {
            top = CHUNK_Y;
        }
        for (int sy = y + 1; sy < top; ++sy) {
            if (mesher_occludes(mesher_neighbor(c, w, x, sy, z))) {
                sun = MESHER_SUN_SHADE;
                break;
            }
        }
    }
    unsigned int base = (unsigned int)m->vertex_count;
    /* Diagonal A: (0,0,0)-(1,0,1), normal (-1,0,1)/sqrt(2). */
    const float na = -0.70710678f;
    mesh_emit_sprite_vert(m, wx, wy, wz, na, 0.0f, -na, u0, v0, sun);
    mesh_emit_sprite_vert(m, wx + 1.0f, wy, wz + 1.0f, na, 0.0f, -na, u1, v0, sun);
    mesh_emit_sprite_vert(m, wx, wy + 1.0f, wz, na, 0.0f, -na, u0, v1, sun);
    mesh_emit_sprite_vert(m, wx + 1.0f, wy + 1.0f, wz + 1.0f, na, 0.0f, -na, u1, v1, sun);
    /* Diagonal B: (1,0,0)-(0,0,1), normal (-1,0,-1)/sqrt(2). */
    const float nb = -0.70710678f;
    mesh_emit_sprite_vert(m, wx + 1.0f, wy, wz, nb, 0.0f, nb, u0, v0, sun);
    mesh_emit_sprite_vert(m, wx, wy, wz + 1.0f, nb, 0.0f, nb, u1, v0, sun);
    mesh_emit_sprite_vert(m, wx + 1.0f, wy + 1.0f, wz, nb, 0.0f, nb, u0, v1, sun);
    mesh_emit_sprite_vert(m, wx, wy + 1.0f, wz + 1.0f, nb, 0.0f, nb, u1, v1, sun);
    /* Each diagonal wound both ways (offset 8 = reversed copy). */
    mesh_emit_sprite_vert(m, wx, wy, wz, -na, 0.0f, na, u0, v0, sun);
    mesh_emit_sprite_vert(m, wx, wy + 1.0f, wz, -na, 0.0f, na, u0, v1, sun);
    mesh_emit_sprite_vert(m, wx + 1.0f, wy, wz + 1.0f, -na, 0.0f, na, u1, v0, sun);
    mesh_emit_sprite_vert(m, wx + 1.0f, wy + 1.0f, wz + 1.0f, -na, 0.0f, na, u1, v1, sun);
    mesh_emit_sprite_vert(m, wx + 1.0f, wy, wz, -nb, 0.0f, -nb, u0, v0, sun);
    mesh_emit_sprite_vert(m, wx + 1.0f, wy + 1.0f, wz, -nb, 0.0f, -nb, u0, v1, sun);
    mesh_emit_sprite_vert(m, wx, wy, wz + 1.0f, -nb, 0.0f, -nb, u1, v0, sun);
    mesh_emit_sprite_vert(m, wx, wy + 1.0f, wz + 1.0f, -nb, 0.0f, -nb, u1, v1, sun);
    unsigned int *ix = m->indices + m->index_count;
    for (int q = 0; q < 4; ++q) {
        unsigned int bq = base + (unsigned int)(q * 4);
        ix[0] = bq + 0;
        ix[1] = bq + 1;
        ix[2] = bq + 2;
        ix[3] = bq + 2;
        ix[4] = bq + 1;
        ix[5] = bq + 3;
        ix += 6;
    }
    m->index_count += 24;
    return 0;
}

/* Build a mesh for one pass (transparent selects blended blocks only).
 *
 * Args:
 *   c: chunk to mesh.
 *   w: world for neighbor lookups (may be NULL).
 *   transparent: true for the blended pass, false for opaque.
 *
 * Returns: owned MeshData or NULL.
 */
static MeshData *mesher_build_filtered(const Chunk *c, const World *w, bool transparent)
{
    if (c == NULL) {
        return NULL;
    }
    double t0 = time_now_seconds();
    MeshData *m = (MeshData *)calloc(1, sizeof(MeshData));
    if (m == NULL) {
        LOG_ERROR("mesher: out of memory");
        return NULL;
    }
    /* Pre-size once: a full chunk holds 16x256x16 cells; even dense
     * terrain exposes a small fraction of its faces, and one upfront
     * allocation beats thousands of doubles. 24k verts / 36k indices
     * covers measured terrain (~23k worst case); bigger workloads grow
     * from there instead of from 256. */
    m->vertices = (float *)malloc((size_t)24576 * MESHER_FLOATS_PER_VERTEX * sizeof(float));
    m->indices =
        (unsigned int *)malloc((size_t)36864 * sizeof(unsigned int));
    if (m->vertices == NULL || m->indices == NULL) {
        LOG_ERROR("mesher: out of memory (presize)");
        mesher_free(m);
        return NULL;
    }
    m->vertex_cap = 24576;
    m->index_cap = 36864;
    for (int y = 0; y < CHUNK_Y; ++y) {
        for (int z = 0; z < CHUNK_Z; ++z) {
            for (int x = 0; x < CHUNK_X; ++x) {
                uint16_t id = chunk_get_block(c, x, y, z);
                if (id == BLOCK_AIR) {
                    continue;
                }
                if (block_is_blended(id) != transparent) {
                    continue;
                }
                float wx = (float)(c->cx * CHUNK_X + x);
                float wy = (float)y;
                float wz = (float)(c->cz * CHUNK_Z + z);
                /* Cross-sprite decor (plants/flowers/torches) meshes as
                 * crossed quads, never as a cube (a cube turns a flower
                 * into a red box). */
                if (block_is_cross(id)) {
                    if (mesh_emit_cross(m, c, w, x, y, z, wx, wy, wz, id) != 0) {
                        LOG_ERROR("mesher: out of memory emitting sprite");
                        mesher_free(m);
                        return NULL;
                    }
                    continue;
                }
                for (int f = 0; f < 6; ++f) {
                    const FaceDef *fd = &FACES[f];
                    uint16_t nb = mesher_neighbor(c, w, x + fd->nx_o, y + fd->ny_o, z + fd->nz_o);
                    bool visible = block_is_face_visible(id, nb);
                    if (!visible && block_is_water(id) && block_is_water(nb) && fd->ny_o == 0 &&
                        block_water_height(id) > block_water_height(nb)) {
                        /* Expose only the step above a lower neighboring
                         * fluid surface; equal-height water stays culled. */
                        visible = true;
                    }
                    if (!visible) {
                        continue;
                    }
                    if (mesh_emit_face(m, fd, f, c, w, x, y, z, wx, wy, wz, id) != 0) {
                        LOG_ERROR("mesher: out of memory emitting face");
                        mesher_free(m);
                        return NULL;
                    }
                }
            }
        }
    }
    double t1 = time_now_seconds();
    LOG_DEBUG("mesher: chunk (%d,%d) %s -> %zu verts, %zu indices in %.3f ms", c->cx, c->cz,
              transparent ? "transparent" : "opaque", m->vertex_count, m->index_count, (t1 - t0) * 1000.0);
    return m;
}

/* Build the opaque mesh (all non-blended faces).
 *
 * Args:
 *   c: chunk.
 *   w: world (may be NULL).
 *
 * Returns: owned MeshData or NULL.
 */
MeshData *mesher_build_chunk_mesh(const Chunk *c, const World *w)
{
    return mesher_build_filtered(c, w, false);
}

/* Build the transparent mesh (blended faces only).
 *
 * Args:
 *   c: chunk.
 *   w: world (may be NULL).
 *
 * Returns: owned MeshData or NULL.
 */
MeshData *mesher_build_transparent_mesh(const Chunk *c, const World *w)
{
    return mesher_build_filtered(c, w, true);
}

/* Free a mesh.
 *
 * Args:
 *   m: mesh to free.
 */
void mesher_free(MeshData *m)
{
    if (m == NULL) {
        return;
    }
    free(m->vertices);
    free(m->indices);
    free(m);
}
