#include "render/menu_panorama.h"
#include "core/bmp.h"
#include "core/log.h"
#include "platform/gl_ctx.h"
#include "render/shader.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Post-1.1 state tokens are queried through the system glGetIntegerv. */
#define PAN_GL_CURRENT_PROGRAM 0x8B8Du
#define PAN_GL_VERTEX_ARRAY_BINDING 0x85B5u
#define PAN_GL_ARRAY_BUFFER_BINDING 0x8894u
#define PAN_GL_ACTIVE_TEXTURE 0x84E0u
#define PAN_GL_TEXTURE_BINDING_2D 0x8069u

struct MenuPanorama {
    Shader *shader;
    unsigned int textures[6];
    unsigned int vao;
    unsigned int vbo;
    int sampler_loc;
    int face_loc;
    int lens_loc;
    int yaw_loc;
    int pitch_loc;
};

typedef struct PanoramaState {
    int program, vao, vbo, active_texture, texture0;
    GLboolean depth, blend, cull, scissor;
} PanoramaState;

static const char *panorama_vertex_source(void)
{
    return "#version 330 core\n"
           "layout(location=0) in vec2 aPosition;\n"
           "out vec2 vNdc;\n"
           "void main() {\n"
           "    vNdc=aPosition;\n"
           "    gl_Position=vec4(aPosition,0.0,1.0);\n"
           "}\n";
}

static const char *panorama_fragment_source(void)
{
    return "#version 330 core\n"
           "in vec2 vNdc;\n"
           "out vec4 FragColor;\n"
           "uniform sampler2D uFaceTexture;\n"
           "uniform int uFace;\n"
           "uniform vec3 uLens;\n"
           "uniform vec3 uYaw;\n"
           "uniform vec3 uPitch;\n"
           "void main() {\n"
           "    vec3 r=vec3(1.0,vNdc.y*uLens.y,vNdc.x*uLens.x);\n"
           "    r=vec3(r.x*uPitch.x-r.y*uPitch.y,\n"
           "           r.x*uPitch.y+r.y*uPitch.x,r.z);\n"
           "    r=vec3(r.x*uYaw.x-r.z*uYaw.y,r.y,r.x*uYaw.y+r.z*uYaw.x);\n"
           "    vec3 a=abs(r);\n"
           "    int face; vec2 st;\n"
           "    if(a.x>=a.y && a.x>=a.z) {\n"
           "        if(r.x>=0.0) { face=2; st=vec2(r.z,-r.y)/a.x; }\n"
           "        else { face=0; st=vec2(-r.z,-r.y)/a.x; }\n"
           "    } else if(a.y>a.x && a.y>=a.z) {\n"
           "        if(r.y>=0.0) { face=4; st=vec2(r.x,r.z)/a.y; }\n"
           "        else { face=5; st=vec2(r.x,-r.z)/a.y; }\n"
           "    } else {\n"
           "        if(r.z>=0.0) { face=3; st=vec2(-r.x,-r.y)/a.z; }\n"
           "        else { face=1; st=vec2(r.x,-r.y)/a.z; }\n"
           "    }\n"
           "    if(face!=uFace) discard;\n"
           "    FragColor=vec4(texture(uFaceTexture,st*0.5+0.5).rgb,1.0);\n"
           "}\n";
}

static bool panorama_entry_points_ready(void)
{
    return minec_glGenVertexArrays && minec_glBindVertexArray && minec_glDeleteVertexArrays && minec_glGenBuffers &&
           minec_glBindBuffer && minec_glBufferData && minec_glDeleteBuffers && minec_glEnableVertexAttribArray &&
           minec_glVertexAttribPointer && minec_glDrawArrays && minec_glGenTextures && minec_glBindTexture &&
           minec_glTexImage2D && minec_glTexParameteri && minec_glDeleteTextures && minec_glActiveTexture &&
           minec_glUseProgram && minec_glUniform1i && minec_glUniform3f;
}

static PanoramaState panorama_capture_state(void)
{
    PanoramaState s;
    glGetIntegerv((GLenum)PAN_GL_CURRENT_PROGRAM, &s.program);
    glGetIntegerv((GLenum)PAN_GL_VERTEX_ARRAY_BINDING, &s.vao);
    glGetIntegerv((GLenum)PAN_GL_ARRAY_BUFFER_BINDING, &s.vbo);
    glGetIntegerv((GLenum)PAN_GL_ACTIVE_TEXTURE, &s.active_texture);
    minec_glActiveTexture(MINEC_GL_TEXTURE0);
    glGetIntegerv((GLenum)PAN_GL_TEXTURE_BINDING_2D, &s.texture0);
    s.depth = glIsEnabled(GL_DEPTH_TEST);
    s.blend = glIsEnabled(GL_BLEND);
    s.cull = glIsEnabled(GL_CULL_FACE);
    s.scissor = glIsEnabled(GL_SCISSOR_TEST);
    return s;
}

static void panorama_restore_state(const PanoramaState *s)
{
    minec_glBindTexture(MINEC_GL_TEXTURE_2D, (unsigned int)s->texture0);
    minec_glActiveTexture((MinecGLenum)s->active_texture);
    minec_glBindVertexArray((unsigned int)s->vao);
    minec_glBindBuffer(MINEC_GL_ARRAY_BUFFER, (unsigned int)s->vbo);
    minec_glUseProgram((unsigned int)s->program);
    if (s->depth)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    if (s->blend)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
    if (s->cull)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    if (s->scissor)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);
}

/* Shared asset lookup falls back per file, so a partial executable copy
 * does not mask the working-directory owner assets. */
static bool panorama_load_face(int face, int expected_size, BmpImage *out)
{
    char relative[128];
    snprintf(relative, sizeof(relative), "generated/menu/panorama_%d.bmp", face);
    if (bmp_load_mcasset(relative, expected_size, expected_size, out) == 0) {
        if (out->width == out->height && out->width >= 1 && out->width <= 2048) {
            LOG_INFO("menu panorama: loaded native face %d (%dx%d)", face, out->width, out->height);
            return true;
        }
        LOG_WARN("menu panorama: invalid face %d (%dx%d; expected square, max 2048)", face, out->width, out->height);
        bmp_free(out);
    }
    LOG_INFO("menu panorama: native face %d unavailable; retaining original menu fallback", face);
    return false;
}

void menu_panorama_destroy(MenuPanorama *p)
{
    if (!p)
        return;
    if (minec_glDeleteTextures)
        minec_glDeleteTextures(6, p->textures);
    if (p->vbo && minec_glDeleteBuffers)
        minec_glDeleteBuffers(1, &p->vbo);
    if (p->vao && minec_glDeleteVertexArrays)
        minec_glDeleteVertexArrays(1, &p->vao);
    shader_destroy(p->shader);
    free(p);
}

MenuPanorama *menu_panorama_create(void)
{
    if (!panorama_entry_points_ready())
        return NULL;
    MenuPanorama *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    PanoramaState previous = panorama_capture_state();
    int size = 0;
    for (int face = 0; face < 6; ++face) {
        BmpImage image = {0};
        if (!panorama_load_face(face, size, &image))
            goto failure;
        size = image.width;
        minec_glGenTextures(1, &p->textures[face]);
        if (!p->textures[face]) {
            bmp_free(&image);
            goto failure;
        }
        minec_glBindTexture(MINEC_GL_TEXTURE_2D, p->textures[face]);
        minec_glTexImage2D(MINEC_GL_TEXTURE_2D, 0, (MinecGLint)MINEC_GL_RGBA, image.width, image.height, 0,
                           MINEC_GL_RGBA, MINEC_GL_UNSIGNED_BYTE, image.px);
        bmp_free(&image);
        minec_glTexParameteri(MINEC_GL_TEXTURE_2D, MINEC_GL_TEXTURE_MIN_FILTER, MINEC_GL_LINEAR);
        minec_glTexParameteri(MINEC_GL_TEXTURE_2D, MINEC_GL_TEXTURE_MAG_FILTER, MINEC_GL_LINEAR);
        minec_glTexParameteri(MINEC_GL_TEXTURE_2D, MINEC_GL_TEXTURE_WRAP_S, MINEC_GL_CLAMP_TO_EDGE);
        minec_glTexParameteri(MINEC_GL_TEXTURE_2D, MINEC_GL_TEXTURE_WRAP_T, MINEC_GL_CLAMP_TO_EDGE);
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            LOG_WARN("menu panorama: texture upload failed (GL 0x%x)", (unsigned)error);
            goto failure;
        }
    }
    p->shader = shader_create(panorama_vertex_source(), panorama_fragment_source());
    if (!p->shader)
        goto failure;
    p->sampler_loc = shader_get_uniform_location(p->shader, "uFaceTexture");
    p->face_loc = shader_get_uniform_location(p->shader, "uFace");
    p->lens_loc = shader_get_uniform_location(p->shader, "uLens");
    p->yaw_loc = shader_get_uniform_location(p->shader, "uYaw");
    p->pitch_loc = shader_get_uniform_location(p->shader, "uPitch");
    if (p->sampler_loc < 0 || p->face_loc < 0 || p->lens_loc < 0 || p->yaw_loc < 0 || p->pitch_loc < 0)
        goto failure;
    static const float triangle[] = {-1, -1, 3, -1, -1, 3};
    minec_glGenVertexArrays(1, &p->vao);
    minec_glGenBuffers(1, &p->vbo);
    if (!p->vao || !p->vbo)
        goto failure;
    minec_glBindVertexArray(p->vao);
    minec_glBindBuffer(MINEC_GL_ARRAY_BUFFER, p->vbo);
    minec_glBufferData(MINEC_GL_ARRAY_BUFFER, (MinecGLsizeiptr)sizeof(triangle), triangle, MINEC_GL_STATIC_DRAW);
    minec_glEnableVertexAttribArray(0);
    minec_glVertexAttribPointer(0, 2, MINEC_GL_FLOAT, MINEC_GL_FALSE, 2 * (MinecGLsizei)sizeof(float), NULL);
    if (glGetError() != GL_NO_ERROR)
        goto failure;
    panorama_restore_state(&previous);
    LOG_INFO("menu panorama: native six-face perspective renderer ready (%dx%d per face)", size, size);
    return p;
failure:
    panorama_restore_state(&previous);
    menu_panorama_destroy(p);
    return NULL;
}

bool menu_panorama_draw(MenuPanorama *p, int width, int height, float seconds)
{
    if (!p || !p->vao || width <= 0 || height <= 0 || !shader_is_ready(p->shader) || !panorama_entry_points_ready())
        return false;
    if (!isfinite(seconds) || seconds < 0)
        seconds = 0;
    PanoramaState previous = panorama_capture_state();
    if (shader_bind(p->shader) != MINEC_SHADER_OK) {
        panorama_restore_state(&previous);
        return false;
    }
    /* Compute uniform trigonometry once on the CPU, not per output pixel. */
    float yaw = fmodf(seconds * .010f, 6.28318530718f);
    float pitch = .025f + sinf(fmodf(seconds * .025f, 6.28318530718f)) * .012f;
    const float tan_vertical = .70020753821f; /* tan(70 degrees / 2). */
    shader_set_uniform_vec3(p->lens_loc, (float)width / (float)height * tan_vertical, tan_vertical, 0);
    shader_set_uniform_vec3(p->yaw_loc, cosf(yaw), sinf(yaw), 0);
    shader_set_uniform_vec3(p->pitch_loc, cosf(pitch), sinf(pitch), 0);
    shader_set_uniform_int(p->sampler_loc, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    minec_glBindVertexArray(p->vao);
    for (int face = 0; face < 6; ++face) {
        shader_set_uniform_int(p->face_loc, face);
        minec_glBindTexture(MINEC_GL_TEXTURE_2D, p->textures[face]);
        minec_glDrawArrays(MINEC_GL_TRIANGLES, 0, 3);
    }
    panorama_restore_state(&previous);
    return true;
}
