#include "render/shader.h"
#include "core/log.h"
#include "platform/gl_ctx.h"

#include <stdlib.h>
#include <string.h>

/* Concrete shader type. */
struct Shader {
    unsigned int program_id; /* GL program object (0 = invalid). */
};

/* Check that all required GL entry points are loaded.
 *
 * Returns: true when usable.
 */
static bool shader_gl_ready(void)
{
    return minec_glCreateShader != NULL && minec_glShaderSource != NULL && minec_glCompileShader != NULL &&
           minec_glGetShaderiv != NULL && minec_glGetShaderInfoLog != NULL && minec_glCreateProgram != NULL &&
           minec_glAttachShader != NULL && minec_glLinkProgram != NULL && minec_glGetProgramiv != NULL &&
           minec_glGetProgramInfoLog != NULL && minec_glUseProgram != NULL && minec_glDeleteShader != NULL &&
           minec_glDeleteProgram != NULL && minec_glGetUniformLocation != NULL && minec_glUniformMatrix4fv != NULL &&
           minec_glUniform1i != NULL && minec_glUniform1f != NULL && minec_glUniform3f != NULL;
}

/* Compile one shader stage.
 *
 * Args:
 *   type: MINEC_GL_VERTEX_SHADER / FRAGMENT_SHADER.
 *   src: GLSL source.
 *
 * Returns: shader object id, or 0 on failure.
 */
static unsigned int shader_compile_stage(unsigned int type, const char *src)
{
    unsigned int sh = minec_glCreateShader((MinecGLenum)type);
    if (sh == 0) {
        LOG_ERROR("shader: glCreateShader(%u) returned 0", type);
        return 0;
    }
    const MinecGLchar *csrc = (const MinecGLchar *)src;
    minec_glShaderSource(sh, 1, &csrc, NULL);
    minec_glCompileShader(sh);
    MinecGLint ok = 0;
    minec_glGetShaderiv(sh, (MinecGLenum)0x8B81 /* COMPILE_STATUS */, &ok);
    if (ok == 0) {
        char log[512];
        MinecGLsizei len = 0;
        minec_glGetShaderInfoLog(sh, (MinecGLsizei)sizeof(log), &len, (MinecGLchar *)log);
        log[sizeof(log) - 1] = '\0';
        LOG_ERROR("shader compile failed (type %u): %s", type, log);
        minec_glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* Create a shader program.
 *
 * Args:
 *   vert_src, frag_src: GLSL sources.
 *
 * Returns: owned Shader or NULL.
 */
Shader *shader_create(const char *vert_src, const char *frag_src)
{
    if (vert_src == NULL || frag_src == NULL) {
        LOG_ERROR("shader_create: NULL source");
        return NULL;
    }
    if (!shader_gl_ready()) {
        LOG_ERROR("shader_create: required minec_gl_* symbols missing (no GL context?)");
        return NULL;
    }
    unsigned int vs = shader_compile_stage(MINEC_GL_VERTEX_SHADER, vert_src);
    if (vs == 0) {
        return NULL;
    }
    unsigned int fs = shader_compile_stage(MINEC_GL_FRAGMENT_SHADER, frag_src);
    if (fs == 0) {
        minec_glDeleteShader(vs);
        return NULL;
    }
    unsigned int prog = minec_glCreateProgram();
    if (prog == 0) {
        LOG_ERROR("shader_create: glCreateProgram returned 0");
        minec_glDeleteShader(vs);
        minec_glDeleteShader(fs);
        return NULL;
    }
    minec_glAttachShader(prog, vs);
    minec_glAttachShader(prog, fs);
    minec_glLinkProgram(prog);
    minec_glDeleteShader(vs);
    minec_glDeleteShader(fs);

    MinecGLint ok = 0;
    minec_glGetProgramiv(prog, (MinecGLenum)MINEC_GL_LINK_STATUS, &ok);
    if (ok == 0) {
        char log[512];
        MinecGLsizei len = 0;
        minec_glGetProgramInfoLog(prog, (MinecGLsizei)sizeof(log), &len, (MinecGLchar *)log);
        log[sizeof(log) - 1] = '\0';
        LOG_ERROR("shader link failed: %s", log);
        minec_glDeleteProgram(prog);
        return NULL;
    }
    Shader *s = (Shader *)calloc(1, sizeof(Shader));
    if (s == NULL) {
        LOG_ERROR("shader_create: out of memory");
        minec_glDeleteProgram(prog);
        return NULL;
    }
    s->program_id = prog;
    LOG_INFO("shader created: program %u", prog);
    return s;
}

/* Destroy a shader.
 *
 * Args:
 *   s: shader to destroy.
 */
void shader_destroy(Shader *s)
{
    if (s == NULL) {
        return;
    }
    if (s->program_id != 0 && minec_glDeleteProgram != NULL) {
        minec_glDeleteProgram(s->program_id);
    }
    free(s);
}

/* Bind a shader.
 *
 * Args:
 *   s: shader.
 *
 * Returns: status code.
 */
int shader_bind(Shader *s)
{
    if (s == NULL || s->program_id == 0) {
        return MINEC_SHADER_E_BAD_ARG;
    }
    if (minec_glUseProgram == NULL) {
        return MINEC_SHADER_E_GL_MISSING;
    }
    minec_glUseProgram(s->program_id);
    return MINEC_SHADER_OK;
}

/* Unbind any shader. */
void shader_unbind(void)
{
    if (minec_glUseProgram != NULL) {
        minec_glUseProgram(0);
    }
}

/* Check readiness.
 *
 * Args:
 *   s: shader.
 *
 * Returns: true if usable.
 */
bool shader_is_ready(const Shader *s)
{
    return s != NULL && s->program_id != 0;
}

/* Get uniform location.
 *
 * Args:
 *   s: shader.
 *   name: uniform name.
 *
 * Returns: location or negative error.
 */
int shader_get_uniform_location(const Shader *s, const char *name)
{
    if (s == NULL || s->program_id == 0 || name == NULL) {
        return -2;
    }
    if (minec_glGetUniformLocation == NULL) {
        return -2;
    }
    return (int)minec_glGetUniformLocation(s->program_id, (const MinecGLchar *)name);
}

/* Upload mat4 uniform.
 *
 * Args:
 *   location: uniform location.
 *   m: 16 floats column-major.
 */
void shader_set_uniform_mat4(int location, const float *m)
{
    if (location < 0 || m == NULL || minec_glUniformMatrix4fv == NULL) {
        return;
    }
    minec_glUniformMatrix4fv(location, 1, 0, (const MinecGLfloat *)m);
}

/* Upload int uniform.
 *
 * Args:
 *   location: uniform location.
 *   v: value.
 */
void shader_set_uniform_int(int location, int v)
{
    if (location < 0 || minec_glUniform1i == NULL) {
        return;
    }
    minec_glUniform1i(location, (MinecGLint)v);
}

/* Upload float uniform.
 *
 * Args:
 *   location: uniform location.
 *   v: value.
 */
void shader_set_uniform_float(int location, float v)
{
    if (location < 0 || minec_glUniform1f == NULL) {
        return;
    }
    minec_glUniform1f(location, (MinecGLfloat)v);
}

/* Upload vec3 uniform.
 *
 * Args:
 *   location: uniform location.
 *   x, y, z: components.
 */
void shader_set_uniform_vec3(int location, float x, float y, float z)
{
    if (location < 0 || minec_glUniform3f == NULL) {
        return;
    }
    minec_glUniform3f(location, (MinecGLfloat)x, (MinecGLfloat)y, (MinecGLfloat)z);
}

/* Embedded textured voxel vertex shader (M4: +AO +world pos). */
const char *shader_voxel_vert_src(void)
{
    return "#version 330 core\n"
           "layout (location = 0) in vec3 aPos;\n"
           "layout (location = 1) in vec3 aNorm;\n"
           "layout (location = 2) in vec2 aUV;\n"
           "layout (location = 3) in float aAO;\n"
           "uniform mat4 uMVP;\n"
           "out vec3 FragNorm;\n"
           "out vec2 FragUV;\n"
           "out float FragAO;\n"
           "out vec3 FragWorldPos;\n"
           "void main() {\n"
           "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
           "    FragNorm = aNorm;\n"
           "    FragUV = aUV;\n"
           "    FragAO = aAO;\n"
           "    FragWorldPos = aPos;\n"
           "}\n";
}

/* Embedded voxel fragment shader (M4): atlas sample, cutout, dynamic sun +
 * skylight ambient, per-vertex AO, exponential fog to the sky color. */
const char *shader_voxel_frag_src(void)
{
    return "#version 330 core\n"
           "in vec3 FragNorm;\n"
           "in vec2 FragUV;\n"
           "in float FragAO;\n"
           "in vec3 FragWorldPos;\n"
           "uniform sampler2D uAtlas;\n"
           "uniform vec3 uSunDir;\n"
           "uniform float uLightIntensity;\n"
           "uniform vec3 uSkyColor;\n"
           "uniform vec3 uFogColor;\n"
           "uniform float uFogDensity;\n"
           "uniform vec3 uCamPos;\n"
           "out vec4 FragColor;\n"
           "void main() {\n"
           "    vec4 texColor = texture(uAtlas, FragUV);\n"
           "    if (texColor.a < 0.1) {\n"
           "        discard;\n"
           "    }\n"
           "    vec3 n = normalize(FragNorm);\n"
           "    float diff = max(dot(n, normalize(uSunDir)), 0.0);\n"
           "    vec3 amb = uSkyColor * 0.35 + vec3(0.12);\n"
           "    vec3 lit = texColor.rgb * (diff * uLightIntensity + amb) * FragAO;\n"
           "    float dist = distance(FragWorldPos, uCamPos);\n"
           "    float f = 1.0 - exp(-uFogDensity * uFogDensity * dist * dist);\n"
           "    vec3 col = mix(lit, uFogColor, clamp(f, 0.0, 1.0));\n"
           "    FragColor = vec4(col, texColor.a);\n"
           "}\n";
}

/* Embedded flat UI vertex shader (pixel-space quads). */
const char *shader_ui_vert_src(void)
{
    return "#version 330 core\n"
           "layout (location = 0) in vec2 aPos;\n"
           "layout (location = 1) in vec4 aColor;\n"
           "uniform mat4 uOrtho;\n"
           "out vec4 vColor;\n"
           "void main() {\n"
           "    gl_Position = uOrtho * vec4(aPos, 0.0, 1.0);\n"
           "    vColor = aColor;\n"
           "}\n";
}

/* Embedded flat UI fragment shader (unlit, alpha respected). */
const char *shader_ui_frag_src(void)
{
    return "#version 330 core\n"
           "in vec4 vColor;\n"
           "out vec4 FragColor;\n"
           "void main() {\n"
           "    FragColor = vColor;\n"
           "}\n";
}

/* Embedded textured UI vertex shader (pixel-space quads + atlas UVs). */
const char *shader_ui_tex_vert_src(void)
{
    return "#version 330 core\n"
           "layout (location = 0) in vec2 aPos;\n"
           "layout (location = 1) in vec2 aUV;\n"
           "uniform mat4 uOrtho;\n"
           "out vec2 vUV;\n"
           "void main() {\n"
           "    gl_Position = uOrtho * vec4(aPos, 0.0, 1.0);\n"
           "    vUV = aUV;\n"
           "}\n";
}

/* Embedded textured UI fragment shader (atlas sample, cutout discard). */
const char *shader_ui_tex_frag_src(void)
{
    return "#version 330 core\n"
           "in vec2 vUV;\n"
           "uniform sampler2D uAtlas;\n"
           "uniform vec3 uTint;\n"
           "uniform float uAlpha;\n"
           "out vec4 FragColor;\n"
           "void main() {\n"
           "    vec4 tex = texture(uAtlas, vUV);\n"
           "    if (tex.a < 0.05) {\n"
           "        discard;\n"
           "    }\n"
           "    FragColor = vec4(tex.rgb * uTint, tex.a * uAlpha);\n"
           "}\n";
}

/* Embedded line vertex shader (world-space 3D segments for the block
 * selection outline). */
const char *shader_line_vert_src(void)
{
    return "#version 330 core\n"
           "layout (location = 0) in vec3 aPos;\n"
           "uniform mat4 uMVP;\n"
           "void main() {\n"
           "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
           "}\n";
}

/* Embedded line fragment shader (flat color + alpha, no lighting). */
const char *shader_line_frag_src(void)
{
    return "#version 330 core\n"
           "uniform vec3 uColor;\n"
           "uniform float uAlpha;\n"
           "out vec4 FragColor;\n"
           "void main() {\n"
           "    FragColor = vec4(uColor, uAlpha);\n"
           "}\n";
}
