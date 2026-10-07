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
           "uniform float uCloudTime;\n"
           "uniform float uCloudAltitude;\n"
           "out vec4 FragColor;\n"
           "uint cloudHash32(uint x) {\n"
           "    x ^= x >> 16; x *= 0x7feb352du;\n"
           "    x ^= x >> 15; x *= 0x846ca68bu;\n"
           "    x ^= x >> 16;\n"
           "    return x;\n"
           "}\n"
           "float cloudHash(vec2 p) {\n"
           "    uvec2 cell = uvec2(ivec2(floor(p)));\n"
           "    uint h = cloudHash32(cell.x ^ (cloudHash32(cell.y) + 0x9e3779b9u));\n"
           "    return float(h >> 8) * (1.0 / 16777216.0);\n"
           "}\n"
           "float cloudValueNoise(vec2 p) {\n"
           "    vec2 i = floor(p);\n"
           "    vec2 f = fract(p);\n"
           "    f = f * f * (3.0 - 2.0 * f);\n"
           "    float a = cloudHash(i);\n"
           "    float b = cloudHash(i + vec2(1.0, 0.0));\n"
           "    float c = cloudHash(i + vec2(0.0, 1.0));\n"
           "    float d = cloudHash(i + vec2(1.0, 1.0));\n"
           "    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);\n"
           "}\n"
           "float cloudNoise(vec2 p) {\n"
           "    float n = 0.0;\n"
           "    float amplitude = 0.66666667;\n"
           "    for (int i = 0; i < 2; ++i) {\n"
           "        n += cloudValueNoise(p) * amplitude;\n"
           "        p = p * 2.03 + vec2(17.1, 9.2);\n"
           "        amplitude *= 0.5;\n"
           "    }\n"
           "    return n;\n"
           "}\n"
           "void main() {\n"
           "    vec4 texColor = texture(uAtlas, FragUV);\n"
           "    if (texColor.a < 0.1) {\n"
           "        discard;\n"
           "    }\n"
           "    vec3 n = normalize(FragNorm);\n"
           "    vec3 sunDir = normalize(uSunDir);\n"
           "    float diff = max(dot(n, sunDir), 0.0);\n"
           "    float skyHemisphere = clamp(n.y * 0.62 + 0.38, 0.0, 1.0);\n"
           "    vec3 ambient = mix(vec3(0.055, 0.050, 0.045), uSkyColor * 0.40, skyHemisphere);\n"
           "    vec3 sunTint = mix(vec3(1.00, 0.55, 0.31), vec3(1.00, 0.96, 0.84),\n"
           "                      smoothstep(-0.02, 0.48, sunDir.y));\n"
           "    float cloudShadow = 0.0;\n"
           "    if (sunDir.y > 0.12 && n.y > 0.0) {\n"
           "        float lightPath = (uCloudAltitude - FragWorldPos.y) / max(sunDir.y, 0.16);\n"
           "        vec2 cloudPos = FragWorldPos.xz + sunDir.xz * lightPath\n"
           "                       + vec2(uCloudTime * 1.15, uCloudTime * 0.37);\n"
           "        vec2 cloudUv = cloudPos * 0.0045;\n"
           "        float cloudField = mix(cloudNoise(cloudUv),\n"
           "                               cloudValueNoise(cloudUv * 2.1 + vec2(7.4, -3.8)), 0.22);\n"
           "        cloudShadow = smoothstep(0.535, 0.635, cloudField)\n"
           "                    * smoothstep(0.12, 0.40, sunDir.y) * clamp(n.y * 1.5, 0.0, 1.0);\n"
           "    }\n"
           "    float sunFactor = smoothstep(-0.10, 0.12, sunDir.y);\n"
           "    vec3 lit = texColor.rgb * (ambient + sunTint * diff * uLightIntensity * sunFactor\n"
           "                                 * (1.0 - cloudShadow * 0.24)) * FragAO;\n"
           "    float dist = distance(FragWorldPos, uCamPos);\n"
           "    float f = 1.0 - exp(-uFogDensity * uFogDensity * dist * dist);\n"
           "    vec3 col = mix(lit, uFogColor, clamp(f, 0.0, 1.0));\n"
           "    FragColor = vec4(col, texColor.a);\n"
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

/* Procedural day/night sky. Clouds are evaluated where the view ray meets a
 * high world-space layer, so they drift smoothly and have natural parallax. */
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
           "uniform float uAspect;\n"
           "uniform float uTanHalfFov;\n"
           "uniform float uTime;\n"
           "uniform float uLightIntensity;\n"
           "uniform float uCloudAltitude;\n"
           "uniform float uUnderwater;\n"
           "out vec4 FragColor;\n"
           "uint hash32(uint x) {\n"
           "    x ^= x >> 16; x *= 0x7feb352du;\n"
           "    x ^= x >> 15; x *= 0x846ca68bu;\n"
           "    x ^= x >> 16;\n"
           "    return x;\n"
           "}\n"
           "float hash21(vec2 p) {\n"
           "    uvec2 cell = uvec2(ivec2(floor(p)));\n"
           "    uint h = hash32(cell.x ^ (hash32(cell.y) + 0x9e3779b9u));\n"
           "    return float(h >> 8) * (1.0 / 16777216.0);\n"
           "}\n"
           "float valueNoise(vec2 p) {\n"
           "    vec2 i = floor(p);\n"
           "    vec2 f = fract(p);\n"
           "    f = f * f * (3.0 - 2.0 * f);\n"
           "    float a = hash21(i);\n"
           "    float b = hash21(i + vec2(1.0, 0.0));\n"
           "    float c = hash21(i + vec2(0.0, 1.0));\n"
           "    float d = hash21(i + vec2(1.0, 1.0));\n"
           "    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);\n"
           "}\n"
           "float cloudNoise(vec2 p) {\n"
           "    float n = 0.0;\n"
           "    float amplitude = 0.66666667;\n"
           "    for (int i = 0; i < 2; ++i) {\n"
           "        n += valueNoise(p) * amplitude;\n"
           "        p = p * 2.03 + vec2(17.1, 9.2);\n"
           "        amplitude *= 0.5;\n"
           "    }\n"
           "    return n;\n"
           "}\n"
           "void main() {\n"
           "    vec3 ray = normalize(uCameraForward + uCameraRight * (vNdc.x * uAspect * uTanHalfFov)\n"
           "                         + uCameraUp * (vNdc.y * uTanHalfFov));\n"
           "    vec3 sunDir = normalize(uSunDir);\n"
           "    float daylight = smoothstep(-0.18, 0.24, sunDir.y);\n"
           "    float night = 1.0 - smoothstep(-0.25, 0.05, sunDir.y);\n"
           "    vec3 zenith = mix(vec3(0.009, 0.015, 0.045), vec3(0.17, 0.43, 0.78), daylight);\n"
           "    vec3 sky = mix(uSkyColor, zenith, smoothstep(0.015, 0.82, ray.y));\n"
           "    float horizonBand = exp(-abs(ray.y) * 7.0);\n"
           "    vec3 horizon = mix(vec3(0.035, 0.045, 0.085), uSkyColor * 1.08, daylight);\n"
           "    float twilight = (1.0 - smoothstep(0.04, 0.48, abs(sunDir.y))) * (1.0 - night);\n"
           "    sky = mix(sky, horizon, horizonBand * 0.42);\n"
           "    sky += vec3(1.0, 0.34, 0.13) * horizonBand * twilight * 0.38;\n"
           "    vec3 belowHorizon = mix(vec3(0.018, 0.026, 0.030), uSkyColor * 0.32, daylight);\n"
           "    sky = mix(belowHorizon, sky, smoothstep(-0.045, 0.018, ray.y));\n"
           "\n"
           "    float sunDot = dot(ray, sunDir);\n"
           "    float sunVisible = smoothstep(-0.12, 0.10, sunDir.y);\n"
           "    float sunDisk = smoothstep(cos(radians(1.05)), cos(radians(0.70)), sunDot);\n"
           "    float sunGlow = pow(max(sunDot, 0.0), 18.0) * 0.12\n"
           "                  + pow(max(sunDot, 0.0), 90.0) * 0.22;\n"
           "    vec3 sunColor = mix(vec3(1.0, 0.39, 0.16), vec3(1.0, 0.97, 0.83),\n"
           "                        smoothstep(0.0, 0.52, sunDir.y));\n"
           "    sky += sunColor * sunVisible * (sunDisk * 1.8 + sunGlow * (0.35 + daylight * 0.65));\n"
           "\n"
           "    vec3 moonDir = -sunDir;\n"
           "    float moonDot = dot(ray, moonDir);\n"
           "    float moonDisk = smoothstep(cos(radians(1.10)), cos(radians(0.72)), moonDot);\n"
           "    float moonVisible = smoothstep(-0.08, 0.16, moonDir.y) * night;\n"
           "    float moonNoise = valueNoise(ray.xz * 240.0 + vec2(3.0, 7.0));\n"
           "    vec3 moonColor = mix(vec3(0.46, 0.57, 0.79), vec3(0.79, 0.86, 1.0), moonNoise);\n"
           "    sky += moonColor * moonDisk * moonVisible * 1.25;\n"
           "\n"
           "    vec2 starUv = vec2(atan(ray.z, ray.x) * 0.15915494 + 0.5,\n"
           "                        asin(clamp(ray.y, -1.0, 1.0)) * 0.31830989 + 0.5);\n"
           "    vec2 starGrid = starUv * vec2(420.0, 210.0);\n"
           "    vec2 starCell = floor(starGrid);\n"
           "    float starSeed = hash21(starCell);\n"
           "    vec2 starPoint = starCell + vec2(hash21(starCell + vec2(13.7)),\n"
           "                                     hash21(starCell + vec2(31.9)));\n"
           "    float starDistance = length(starGrid - starPoint);\n"
           "    float star = (1.0 - smoothstep(0.025, 0.10, starDistance)) * step(0.992, starSeed) * night;\n"
           "    float twinkle = 0.88 + 0.12 * sin(uTime * (0.7 + starSeed * 2.2) + starSeed * 31.0);\n"
           "    sky += mix(vec3(0.62, 0.76, 1.0), vec3(1.0, 0.91, 0.76), starSeed) * star * twinkle;\n"
           "\n"
           "    if (uUnderwater > 0.5) {\n"
           "        sky = mix(vec3(0.012, 0.055, 0.095), vec3(0.04, 0.18, 0.27),\n"
           "                   smoothstep(-0.65, 0.25, ray.y));\n"
           "    } else if (ray.y > 0.01 && uCameraPosition.y < uCloudAltitude) {\n"
           "        float cloudDistance = (uCloudAltitude - uCameraPosition.y) / ray.y;\n"
           "        if (cloudDistance < 5000.0) {\n"
           "            vec2 worldXZ = uCameraPosition.xz + ray.xz * cloudDistance;\n"
           "            vec2 drift = vec2(uTime * 1.15, uTime * 0.37);\n"
           "            vec2 cloudUv = (worldXZ + drift) * 0.0045;\n"
           "            float broad = cloudNoise(cloudUv);\n"
           "            float detail = valueNoise(cloudUv * 2.1 + vec2(7.4, -3.8));\n"
           "            float field = mix(broad, detail, 0.22);\n"
           "            float edgeWidth = max(fwidth(field) * 2.2, 0.018);\n"
           "            float cloud = smoothstep(0.535 - edgeWidth, 0.635 + edgeWidth, field);\n"
           "            float farFade = 1.0 - smoothstep(2600.0, 4800.0, cloudDistance);\n"
           "            cloud *= farFade;\n"
           "            float sunlit = smoothstep(-0.08, 0.65, sunDir.y) * uLightIntensity;\n"
           "            vec3 cloudShadow = mix(vec3(0.035, 0.048, 0.080), vec3(0.35, 0.40, 0.50), daylight);\n"
           "            vec3 cloudLit = mix(vec3(0.18, 0.22, 0.34), vec3(0.98, 0.98, 0.94), sunlit);\n"
           "            float silverEdge = pow(max(dot(ray, sunDir), 0.0), 36.0) * (1.0 - cloud) * 0.28;\n"
           "            vec3 cloudColor = mix(cloudShadow, cloudLit, smoothstep(0.0, 0.72, ray.y));\n"
           "            sky = mix(sky, cloudColor, cloud);\n"
           "            sky += vec3(1.0, 0.82, 0.61) * silverEdge * sunlit;\n"
           "        }\n"
           "    }\n"
           "    FragColor = vec4(max(sky, vec3(0.0)), 1.0);\n"
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
