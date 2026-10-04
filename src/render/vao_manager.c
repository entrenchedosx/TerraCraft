#include "render/vao_manager.h"
#include "core/log.h"
#include "platform/gl_ctx.h"
#include "render/mesher.h"

#include <stddef.h>

/* Upload a mesh to GPU.
 *
 * Args:
 *   g: buffer entry.
 *   mesh: CPU mesh.
 *   cx, cz: chunk coords.
 *
 * Returns: 0 on success.
 */
int gpu_chunk_upload(GpuChunkBuffer *g, const MeshData *mesh, int cx, int cz)
{
    if (g == NULL || mesh == NULL) {
        return -1;
    }
    if (minec_glGenVertexArrays == NULL || minec_glBindVertexArray == NULL || minec_glGenBuffers == NULL ||
        minec_glBindBuffer == NULL || minec_glBufferData == NULL || minec_glVertexAttribPointer == NULL ||
        minec_glEnableVertexAttribArray == NULL) {
        LOG_ERROR("gpu_chunk_upload: required minec_gl_* symbols missing");
        return -2;
    }

    /* Empty mesh: free any previous buffers, mark invalid (nothing to draw). */
    if (mesh->index_count == 0 || mesh->vertex_count == 0) {
        gpu_chunk_destroy(g);
        g->cx = cx;
        g->cz = cz;
        return 0;
    }

    /* Lazily create buffers on first upload. */
    if (g->vao == 0) {
        minec_glGenVertexArrays(1, &g->vao);
    }
    if (g->vbo == 0) {
        minec_glGenBuffers(1, &g->vbo);
    }
    if (g->ebo == 0) {
        minec_glGenBuffers(1, &g->ebo);
    }
    if (g->vao == 0 || g->vbo == 0 || g->ebo == 0) {
        LOG_ERROR("gpu_chunk_upload: GL object creation failed");
        return -3;
    }

    minec_glBindVertexArray(g->vao);

    minec_glBindBuffer((MinecGLenum)MINEC_GL_ARRAY_BUFFER, g->vbo);
    minec_glBufferData((MinecGLenum)MINEC_GL_ARRAY_BUFFER,
                       (MinecGLsizeiptr)(mesh->vertex_count * MESHER_FLOATS_PER_VERTEX * sizeof(float)), mesh->vertices,
                       (MinecGLenum)MINEC_GL_STATIC_DRAW);

    minec_glBindBuffer((MinecGLenum)MINEC_GL_ELEMENT_ARRAY_BUFFER, g->ebo);
    minec_glBufferData((MinecGLenum)MINEC_GL_ELEMENT_ARRAY_BUFFER,
                       (MinecGLsizeiptr)(mesh->index_count * sizeof(unsigned int)), mesh->indices,
                       (MinecGLenum)MINEC_GL_STATIC_DRAW);

    /* Layout: stride 9 floats; 0:pos(0), 1:normal(3f), 2:uv(6f), 3:ao(8f). */
    const MinecGLsizei stride = (MinecGLsizei)(MESHER_FLOATS_PER_VERTEX * sizeof(float));
    minec_glVertexAttribPointer(0, 3, (MinecGLenum)MINEC_GL_FLOAT, 0, stride, (const void *)0);
    minec_glEnableVertexAttribArray(0);
    minec_glVertexAttribPointer(1, 3, (MinecGLenum)MINEC_GL_FLOAT, 0, stride, (const void *)(3 * sizeof(float)));
    minec_glEnableVertexAttribArray(1);
    minec_glVertexAttribPointer(2, 2, (MinecGLenum)MINEC_GL_FLOAT, 0, stride, (const void *)(6 * sizeof(float)));
    minec_glEnableVertexAttribArray(2);
    minec_glVertexAttribPointer(3, 1, (MinecGLenum)MINEC_GL_FLOAT, 0, stride, (const void *)(8 * sizeof(float)));
    minec_glEnableVertexAttribArray(3);

    minec_glBindVertexArray(0);

    g->index_count = mesh->index_count;
    g->valid = true;
    g->cx = cx;
    g->cz = cz;
    return 0;
}

/* Draw a chunk buffer.
 *
 * Args:
 *   g: buffer to draw.
 */
void gpu_chunk_draw(const GpuChunkBuffer *g)
{
    if (g == NULL || !g->valid || g->index_count == 0 || g->vao == 0) {
        return;
    }
    if (minec_glBindVertexArray == NULL || minec_glDrawElements == NULL) {
        return;
    }
    minec_glBindVertexArray(g->vao);
    minec_glDrawElements((MinecGLenum)MINEC_GL_TRIANGLES, (MinecGLsizei)g->index_count,
                         (MinecGLenum)MINEC_GL_UNSIGNED_INT, (const void *)0);
    minec_glBindVertexArray(0);
}

/* Destroy GPU buffers.
 *
 * Args:
 *   g: buffer to destroy.
 */
void gpu_chunk_destroy(GpuChunkBuffer *g)
{
    if (g == NULL) {
        return;
    }
    if (g->ebo != 0 && minec_glDeleteBuffers != NULL) {
        minec_glDeleteBuffers(1, &g->ebo);
    }
    if (g->vbo != 0 && minec_glDeleteBuffers != NULL) {
        minec_glDeleteBuffers(1, &g->vbo);
    }
    if (g->vao != 0 && minec_glDeleteVertexArrays != NULL) {
        minec_glDeleteVertexArrays(1, &g->vao);
    }
    g->vao = 0;
    g->vbo = 0;
    g->ebo = 0;
    g->index_count = 0;
    g->valid = false;
}
