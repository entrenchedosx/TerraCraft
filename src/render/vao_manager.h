#pragma once

/* GPU chunk buffers (M1): one VAO+VBO+EBO per meshed chunk.
 * Uploads MeshData via minec_gl_*; requires a current GL context.
 * CPU MeshData is freed by the caller after upload.
 */

#include <stdbool.h>
#include <stddef.h>

/* Forward declaration (full type in render/mesher.h). */
typedef struct MeshData MeshData;

/* GPU resources for one chunk's mesh. */
typedef struct GpuChunkBuffer {
    unsigned int vao; /* Vertex array object (0 = none). */
    unsigned int vbo; /* Vertex buffer (0 = none). */
    unsigned int ebo; /* Index buffer (0 = none). */
    size_t index_count; /* Indices to draw (0 = nothing). */
    bool valid; /* True when buffers hold current mesh data. */
    int cx, cz; /* Chunk coords this buffer belongs to. */
} GpuChunkBuffer;

/* Upload a CPU mesh into GPU buffers (creates VAO/VBO/EBO on first use,
 * re-uploads on dirty). Empty meshes (0 indices) destroy GPU buffers and
 * mark the entry invalid (nothing to draw). Vertex layout: 9 floats
 * [pos xyz | normal xyz | uv uv | ao], locations 0/1/2/3.
 *
 * Args:
 *   g: buffer entry to fill (must not be NULL).
 *   mesh: CPU mesh (must not be NULL).
 *   cx, cz: chunk coords for bookkeeping.
 *
 * Returns: 0 on success, non-zero when GL symbols missing or on bad args.
 */
int gpu_chunk_upload(GpuChunkBuffer *g, const MeshData *mesh, int cx, int cz);

/* Draw a valid buffer (binds VAO, issues DrawElements).
 *
 * Args:
 *   g: buffer to draw (must not be NULL).
 */
void gpu_chunk_draw(const GpuChunkBuffer *g);

/* Delete GPU buffers and reset the entry. NULL-safe, GL-missing-safe.
 *
 * Args:
 *   g: buffer to destroy (may be NULL).
 */
void gpu_chunk_destroy(GpuChunkBuffer *g);
