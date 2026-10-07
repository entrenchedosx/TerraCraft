#pragma once

/* Shader program (M2): compile + link GLSL 330 Core via minec_gl_*.
 * Requires a current GL context. No global state; Shader owns program id.
 * The embedded voxel shader is textured (atlas sampler + Lambert lighting).
 */

#include <stdbool.h>

/* Error codes for shader operations. */
typedef enum ShaderError {
    MINEC_SHADER_OK = 0,
    MINEC_SHADER_E_BAD_ARG = -1,
    MINEC_SHADER_E_COMPILE = -2,
    MINEC_SHADER_E_LINK = -3,
    MINEC_SHADER_E_GL_MISSING = -5
} ShaderError;

/* Opaque shader program handle. */
typedef struct Shader Shader;

/* Create a shader program from GLSL source strings.
 * Compiles vertex + fragment, links, deletes intermediate shaders.
 *
 * Args:
 *   vert_src: vertex shader source (must not be NULL).
 *   frag_src: fragment shader source (must not be NULL).
 *
 * Returns: owned Shader on success, NULL on failure.
 */
Shader *shader_create(const char *vert_src, const char *frag_src);

/* Destroy a shader program. NULL-safe.
 *
 * Args:
 *   s: shader to destroy (may be NULL).
 */
void shader_destroy(Shader *s);

/* Bind a shader program for drawing.
 *
 * Args:
 *   s: shader (must not be NULL).
 *
 * Returns: MINEC_SHADER_OK on success, error code otherwise.
 */
int shader_bind(Shader *s);

/* Unbind any shader program (bind 0). No-op when GL entry missing. */
void shader_unbind(void);

/* Check whether a shader handle is ready for use.
 *
 * Args:
 *   s: shader (may be NULL).
 *
 * Returns: true if usable.
 */
bool shader_is_ready(const Shader *s);

/* Get a uniform location (may be -1 when optimized out).
 *
 * Args:
 *   s: shader (must not be NULL).
 *   name: uniform name (must not be NULL).
 *
 * Returns: location (>= -1), or -2 on bad args / missing GL.
 */
int shader_get_uniform_location(const Shader *s, const char *name);

/* Upload a column-major 4x4 matrix uniform.
 *
 * Args:
 *   location: uniform location (>= 0).
 *   m: 16 floats, column-major.
 */
void shader_set_uniform_mat4(int location, const float *m);

/* Upload an int uniform (e.g. sampler unit).
 *
 * Args:
 *   location: uniform location (>= 0).
 *   v: integer value.
 */
void shader_set_uniform_int(int location, int v);

/* Upload a float uniform.
 *
 * Args:
 *   location: uniform location (>= 0).
 *   v: float value.
 */
void shader_set_uniform_float(int location, float v);

/* Upload a vec3 uniform.
 *
 * Args:
 *   location: uniform location (>= 0).
 *   x, y, z: vector components.
 */
void shader_set_uniform_vec3(int location, float x, float y, float z);

/* Embedded M4 voxel shaders (GLSL 330 core, textured + lit + fogged).
 * Vertex: layout 0 pos, 1 normal, 2 uv, 3 ao; uniform uMVP;
 *   out normal/uv/ao/world-pos.
 * Fragment: samples uAtlas (cutout 0.1), sun diffuse (uSunDir,
 *   uLightIntensity) + skylight ambient, multiplies FragAO, applies
 *   exponential fog (uFogColor/uFogDensity/uCamPos).
 */
const char *shader_voxel_vert_src(void);
const char *shader_voxel_frag_src(void);

/* Camera-aware procedural sky shader with day/night lighting, sun/moon,
 * stars, atmospheric gradient, and animated cloud layer. */
const char *shader_sky_vert_src(void);
const char *shader_sky_frag_src(void);

/* Embedded M3 UI shaders (GLSL 330 core, flat unlit quads).
 * Vertex: layout 0 pos (vec2 pixels), 1 color (vec4); uniform uOrtho.
 * Fragment: outputs the interpolated color (no lighting, no texture).
 */
const char *shader_ui_vert_src(void);
const char *shader_ui_frag_src(void);
const char *shader_ui_tex_vert_src(void);
const char *shader_ui_tex_frag_src(void);

/* Embedded line shaders (GLSL 330 core, world-space segments).
 * Vertex: layout 0 pos (vec3 world); uniform uMVP.
 * Fragment: flat uColor (vec3) + uAlpha (float), no lighting.
 */
const char *shader_line_vert_src(void);
const char *shader_line_frag_src(void);
