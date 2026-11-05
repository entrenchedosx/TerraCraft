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

/* Voxel vertex shader: fixed face shade, lightmap, AO and biome tint. */
const char *shader_voxel_vert_src(void)
{
    return "#version 330 core\n"
           "layout (location = 0) in vec3 aPos;\n"
           "layout (location = 1) in vec3 aNorm;\n"
           "layout (location = 2) in vec2 aUV;\n"
           "layout (location = 3) in float aAO;\n"
           "layout (location = 4) in vec2 aLight;\n"
           "layout (location = 5) in vec3 aTint;\n"
           "uniform mat4 uMVP;\n"
           "uniform sampler2D uLightmap;\n"
           "uniform float uAtlasMaterials;\n"
           "uniform float uEntityLighting;\n"
           "uniform float uColoredWater;\n"
           "out vec2 FragUV;\n"
           "out vec3 FragColorMultiplier;\n"
           "out vec3 FragWorldPos;\n"
           "void main() {\n"
           "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
           "    vec3 n = normalize(aNorm);\n"
           "    float shade = abs(n.y) > 0.99 ? (n.y > 0.0 ? 1.0 : 0.5)\n"
           "                : (abs(n.x) > 0.99 ? 0.6 : (abs(n.z) > 0.99 ? 0.8 : 1.0));\n"
           "    if (uEntityLighting > 0.5) {\n"
           "        float key = max(0.0, dot(n, normalize(vec3(0.2, 1.0, -0.7))));\n"
           "        float fill = max(0.0, dot(n, normalize(vec3(-0.2, 1.0, 0.7))));\n"
           "        shade = min(1.0, 0.4 + (key + fill) * 0.6);\n"
           "    }\n"
           "    vec2 lightUV = (clamp(aLight, 0.0, 1.0).yx * 15.0 + 0.5) / 16.0;\n"
           "    vec3 light = texture(uLightmap, lightUV).rgb;\n"
           "    ivec2 tile = ivec2(floor(aUV * 16.0));\n"
           "    if (uAtlasMaterials > 0.5 && tile == ivec2(8, 4)) { light = vec3(1.0); shade = 1.0; }\n"
           "    FragColorMultiplier = light * shade * clamp(aAO, 0.0, 1.0);\n"
           "    if (uAtlasMaterials > 0.5 && !(uColoredWater > 0.5 && tile == ivec2(8,0)))\n"
           "        FragColorMultiplier *= aTint;\n"
           "    FragUV = aUV;\n"
           "    FragWorldPos = aPos;\n"
           "}\n";
}

/* Voxel fragment shader: atlas cutout, distance-based linear fog and
 * chunk fade-in. Shading is interpolated from the vertex lightmap. */
const char *shader_voxel_frag_src(void)
{
    return "#version 330 core\n"
           "in vec2 FragUV;\n"
           "in vec3 FragColorMultiplier;\n"
           "in vec3 FragWorldPos;\n"
           "uniform sampler2D uAtlas;\n"
           "uniform vec3 uFogColor;\n"
           "uniform float uFogStart;\n"
           "uniform float uFogEnd;\n"
           "uniform float uUnderwater;\n"
           "uniform float uViewModel;\n"
           "uniform float uChunkVisibility;\n"
           "uniform vec3 uCamPos;\n"
           "uniform float uCutout;\n"
           "out vec4 FragColor;\n"
           "void main() {\n"
           "    vec4 texel = texture(uAtlas, FragUV);\n"
           "    /* Cutout pass: mip-averaged alpha must not blend leaves/plants with the sky. */\n"
           "    if (uCutout > 0.5) {\n"
           "        if (texel.a < 0.5) discard;\n"
           "        texel.a = 1.0;\n"
           "    } else if (texel.a < 0.1) {\n"
           "        discard;\n"
           "    }\n"
           "    vec3 delta = FragWorldPos - uCamPos;\n"
           "    float d = uUnderwater > 0.5 ? length(delta) : max(length(delta.xz), abs(delta.y));\n"
           "    float fog = uViewModel > 0.5 ? 0.0 : clamp((d - uFogStart) / max(uFogEnd-uFogStart, 0.001), 0.0, "
           "1.0);\n"
           "    FragColor = vec4(mix(mix(uFogColor,texel.rgb * FragColorMultiplier,clamp(uChunkVisibility,0.0,1.0)), "
           "uFogColor, fog), texel.a);\n"
           "}\n";
}

/* Full-screen triangle vertex shader: vNdc is the pixel's camera-plane
 * coordinate. The sky pass runs at depth 1, underneath all world geometry. */
const char *shader_sky_vert_src(void)
{
    return "#version 330 core\n"
           "layout (location = 0) in vec2 aPos;\n"
           "out vec2 vNdc;\n"
           "void main() {\n"
           "    vNdc = aPos;\n"
           "    gl_Position = vec4(aPos, 1.0, 1.0);\n"
           "}\n";
}

/* Pixel sky: native sun/moon sprites, fixed stars, and ray-cast 12-block
 * cloud cells with a four-block thickness. No cinematic lighting effects. */
const char *shader_sky_frag_src(void)
{
    return "#version 330 core\n"
           "in vec2 vNdc;\n"
           "uniform vec3 uCameraPosition;\n"
           "uniform vec3 uCameraForward;\n"
           "uniform vec3 uCameraRight;\n"
           "uniform vec3 uCameraUp;\n"
           "uniform vec3 uSunDir;\n"
           "uniform vec3 uSkyColor;\n"
           "uniform vec3 uFogColor;\n"
           "uniform vec3 uCloudColor;\n"
           "uniform float uStarBrightness;\n"
           "uniform float uAspect;\n"
           "uniform float uTanHalfFov;\n"
           "uniform float uTime;\n"
           "uniform float uLightIntensity;\n"
           "uniform float uCloudAltitude;\n"
           "uniform float uUnderwater;\n"
           "uniform sampler2D uSunTexture;\n"
           "uniform sampler2D uMoonTexture;\n"
           "uniform sampler2D uCloudTexture;\n"
           "uniform float uHasSkyTextures;\n"
           "out vec4 FragColor;\n"
           "uint hash32(uint x) {\n"
           "    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;\n"
           "    return x;\n"
           "}\n"
           "float hash21(vec2 p) {\n"
           "    uvec2 c = uvec2(ivec2(floor(p)));\n"
           "    return float(hash32(c.x ^ (hash32(c.y) + 0x9e3779b9u)) >> 8) / 16777216.0;\n"
           "}\n"
           "bool cloudCell(vec2 cell) {\n"
           "    if (uHasSkyTextures > 0.5) return texture(uCloudTexture, (cell + 0.5) / 256.0).a > 0.1;\n"
           "    return hash21(floor(cell / 4.0)) > 0.55;\n"
           "}\n"
           "vec4 cloudRay(vec3 ray) {\n"
           "    if (abs(ray.y) < 0.001) return vec4(0.0);\n"
           "    float a = (uCloudAltitude - uCameraPosition.y) / ray.y;\n"
           "    float b = (uCloudAltitude + 4.0 - uCameraPosition.y) / ray.y;\n"
           "    float enter = max(0.0, min(a,b)), leave = max(a,b);\n"
           "    if (leave < enter || enter > 768.0) return vec4(0.0);\n"
           "    float t = enter + 0.001;\n"
           "    float shade = ray.y > 0.0 ? 0.7 : 1.0;\n"
           "    vec2 drift = vec2(uTime * 0.6, 0.0);\n"
           "    for (int i = 0; i < 12; ++i) {\n"
           "        vec2 p = uCameraPosition.xz + ray.xz * t + drift;\n"
           "        vec2 cell = floor(p / 12.0);\n"
           "        if (cloudCell(cell)) {\n"
           "            vec3 color = uCloudColor * shade;\n"
           "            return vec4(color, 0.8 * (1.0-smoothstep(512.0,768.0,t)));\n"
           "        }\n"
           "        vec2 boundary = (cell + step(vec2(0.0), ray.xz)) * 12.0;\n"
           "        vec2 distanceTo = (boundary - p) / (ray.xz + vec2(0.000001));\n"
           "        float dx = abs(ray.x) < 0.00001 ? 1e8 : max(distanceTo.x,0.0);\n"
           "        float dz = abs(ray.z) < 0.00001 ? 1e8 : max(distanceTo.y,0.0);\n"
           "        t += min(dx,dz) + 0.002;\n"
           "        shade = dx < dz ? 0.6 : 0.8;\n"
           "        if (t > leave) break;\n"
           "    }\n"
           "    return vec4(0.0);\n"
           "}\n"
           "vec3 celestial(vec3 ray, vec3 axis, float extent, bool moon) {\n"
           "    float facing = dot(ray, axis);\n"
           "    if (facing <= 0.0) return vec3(0.0);\n"
           "    vec3 right = normalize(cross(vec3(0,0,1), axis));\n"
           "    vec3 up = cross(axis,right);\n"
           "    vec2 uv = vec2(dot(ray,right),dot(ray,up)) / facing * (100.0/extent) + 0.5;\n"
           "    if (any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) return vec3(0.0);\n"
           "    if (uHasSkyTextures > 0.5) {\n"
           "        vec4 pixel;\n"
           "        if (moon) pixel = texture(uMoonTexture, vec2(uv.x/4.0,uv.y/2.0+0.5));\n"
           "        else pixel = texture(uSunTexture, uv);\n"
           "        return pixel.rgb * pixel.a;\n"
           "    }\n"
           "    vec2 square = abs(uv-0.5);\n"
           "    return max(square.x,square.y) < 0.15 ? (moon ? vec3(0.75) : vec3(1.0)) : vec3(0.0);\n"
           "}\n"
           "void main() {\n"
           "    vec3 ray = normalize(uCameraForward + uCameraRight * vNdc.x * uAspect * uTanHalfFov\n"
           "                                        + uCameraUp * vNdc.y * uTanHalfFov);\n"
           "    vec3 sun = normalize(uSunDir);\n"
           "    float day = clamp((uLightIntensity-0.2)/0.8,0.0,1.0);\n"
           "    vec3 sky = mix(uSkyColor, uFogColor, exp(-max(ray.y,0.0)*10.0) * 0.45);\n"
           "    if (ray.y < -0.04) sky = mix(sky, uFogColor * 0.45, smoothstep(0.04,0.4,-ray.y));\n"
           "    float dusk = max(0.0, 1.0-abs(sun.y)/0.4);\n"
           "    vec3 horizonDirection = normalize(vec3(sun.x,0.001,sun.z));\n"
           "    float band = max(0.0,1.0-abs(ray.y)/0.35) * pow(max(dot(ray,horizonDirection),0.0),2.0);\n"
           "    sky = mix(sky, vec3(0.95,0.40,0.12), dusk*dusk*band*0.6);\n"
           "    vec2 starUv = vec2(atan(ray.z,ray.x)/6.2831853+0.5, asin(clamp(ray.y,-1.0,1.0))/3.14159265+0.5);\n"
           "    vec2 grid = starUv * vec2(420.0,210.0), cell = floor(grid);\n"
           "    vec2 point = cell + vec2(hash21(cell+13.7),hash21(cell+31.9));\n"
           "    vec2 delta = abs(grid-point);\n"
           "    float star = step(max(delta.x,delta.y),0.065) * step(0.992,hash21(cell));\n"
           "    sky += vec3(star * uStarBrightness) * step(0.0,ray.y);\n"
           "    sky += celestial(ray,sun,60.0,false);\n"
           "    sky += celestial(ray,-sun,40.0,true);\n"
           "    vec4 cloud = cloudRay(ray);\n"
           "    sky = mix(sky,cloud.rgb,cloud.a);\n"
           "    if (uUnderwater > 0.5) sky = uFogColor;\n"
           "    FragColor = vec4(clamp(sky,0.0,1.0),1.0);\n"
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
