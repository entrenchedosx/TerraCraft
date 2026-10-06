#if defined(__APPLE__)
/* macOS 10.14+ deprecates OpenGL; TerraCraft targets GL 3.3 core
 * deliberately, so silence the deprecation warning at the source. */
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION 1
#endif
#endif

#include "platform/gl_ctx.h"
#include "core/log.h"
#include "platform/window.h"

/* Portable SDL include: vcpkg uses <SDL2/SDL.h>, some distros expose <SDL.h>. */
#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#elif __has_include(<SDL.h>)
#include <SDL.h>
#else
#error "SDL headers not found"
#endif
#else
#include <SDL2/SDL.h>
#endif

/* Portable GL system header for M0 basics + version/vendor strings.
 * NOTE (Windows/MSVC): <GL/gl.h> requires WINGDIAPI/APIENTRY from <windows.h>.
 * It must be included first, otherwise MSVC fails with C2054/C2085. */
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

#include <stddef.h>

/* Concrete context type. */
struct GlContext {
    SDL_GLContext handle; /* Owned GL context. */
    Window *window;       /* Non-owning parent window. */
};

/* Loaded entry points (defined here, declared extern in header). */
MinecPFN_glGenVertexArrays minec_glGenVertexArrays = NULL;
MinecPFN_glBindVertexArray minec_glBindVertexArray = NULL;
MinecPFN_glDeleteVertexArrays minec_glDeleteVertexArrays = NULL;
MinecPFN_glGenBuffers minec_glGenBuffers = NULL;
MinecPFN_glBindBuffer minec_glBindBuffer = NULL;
MinecPFN_glBufferData minec_glBufferData = NULL;
MinecPFN_glDeleteBuffers minec_glDeleteBuffers = NULL;
MinecPFN_glEnableVertexAttribArray minec_glEnableVertexAttribArray = NULL;
MinecPFN_glVertexAttribPointer minec_glVertexAttribPointer = NULL;
MinecPFN_glCreateShader minec_glCreateShader = NULL;
MinecPFN_glShaderSource minec_glShaderSource = NULL;
MinecPFN_glCompileShader minec_glCompileShader = NULL;
MinecPFN_glGetShaderiv minec_glGetShaderiv = NULL;
MinecPFN_glGetShaderInfoLog minec_glGetShaderInfoLog = NULL;
MinecPFN_glCreateProgram minec_glCreateProgram = NULL;
MinecPFN_glAttachShader minec_glAttachShader = NULL;
MinecPFN_glLinkProgram minec_glLinkProgram = NULL;
MinecPFN_glGetProgramiv minec_glGetProgramiv = NULL;
MinecPFN_glGetProgramInfoLog minec_glGetProgramInfoLog = NULL;
MinecPFN_glUseProgram minec_glUseProgram = NULL;
MinecPFN_glDeleteShader minec_glDeleteShader = NULL;
MinecPFN_glDeleteProgram minec_glDeleteProgram = NULL;
MinecPFN_glGetUniformLocation minec_glGetUniformLocation = NULL;
MinecPFN_glUniformMatrix4fv minec_glUniformMatrix4fv = NULL;
MinecPFN_glDrawElements minec_glDrawElements = NULL;
MinecPFN_glDrawArrays minec_glDrawArrays = NULL;
MinecPFN_glDisableVertexAttribArray minec_glDisableVertexAttribArray = NULL;
MinecPFN_glGenTextures minec_glGenTextures = NULL;
MinecPFN_glBindTexture minec_glBindTexture = NULL;
MinecPFN_glTexImage2D minec_glTexImage2D = NULL;
MinecPFN_glTexParameteri minec_glTexParameteri = NULL;
MinecPFN_glDeleteTextures minec_glDeleteTextures = NULL;
MinecPFN_glActiveTexture minec_glActiveTexture = NULL;
MinecPFN_glUniform1i minec_glUniform1i = NULL;
MinecPFN_glUniform1f minec_glUniform1f = NULL;
MinecPFN_glUniform3f minec_glUniform3f = NULL;

/* Helper macro: load one symbol, count failures. */
#define MINEC_LOAD_GL(sym)                                                                                        \
    do {                                                                                                          \
        minec_##sym = (MinecPFN_##sym)SDL_GL_GetProcAddress(#sym);                                                \
        if (minec_##sym == NULL) {                                                                                \
            LOG_WARN("SDL_GL_GetProcAddress(\"%s\") returned NULL", #sym);                                        \
            missing++;                                                                                            \
        }                                                                                                         \
    } while (0)

/* Load all extended entry points.
 *
 * Returns: number of missing symbols.
 */
int minec_gl_load_all(void)
{
    int missing = 0;
    MINEC_LOAD_GL(glGenVertexArrays);
    MINEC_LOAD_GL(glBindVertexArray);
    MINEC_LOAD_GL(glDeleteVertexArrays);
    MINEC_LOAD_GL(glGenBuffers);
    MINEC_LOAD_GL(glBindBuffer);
    MINEC_LOAD_GL(glBufferData);
    MINEC_LOAD_GL(glDeleteBuffers);
    MINEC_LOAD_GL(glEnableVertexAttribArray);
    MINEC_LOAD_GL(glVertexAttribPointer);
    MINEC_LOAD_GL(glCreateShader);
    MINEC_LOAD_GL(glShaderSource);
    MINEC_LOAD_GL(glCompileShader);
    MINEC_LOAD_GL(glGetShaderiv);
    MINEC_LOAD_GL(glGetShaderInfoLog);
    MINEC_LOAD_GL(glCreateProgram);
    MINEC_LOAD_GL(glAttachShader);
    MINEC_LOAD_GL(glLinkProgram);
    MINEC_LOAD_GL(glGetProgramiv);
    MINEC_LOAD_GL(glGetProgramInfoLog);
    MINEC_LOAD_GL(glUseProgram);
    MINEC_LOAD_GL(glDeleteShader);
    MINEC_LOAD_GL(glDeleteProgram);
    MINEC_LOAD_GL(glGetUniformLocation);
    MINEC_LOAD_GL(glUniformMatrix4fv);
    MINEC_LOAD_GL(glDrawElements);
    MINEC_LOAD_GL(glDrawArrays);
    MINEC_LOAD_GL(glDisableVertexAttribArray);
    MINEC_LOAD_GL(glGenTextures);
    MINEC_LOAD_GL(glBindTexture);
    MINEC_LOAD_GL(glTexImage2D);
    MINEC_LOAD_GL(glTexParameteri);
    MINEC_LOAD_GL(glDeleteTextures);
    MINEC_LOAD_GL(glActiveTexture);
    MINEC_LOAD_GL(glUniform1i);
    MINEC_LOAD_GL(glUniform1f);
    MINEC_LOAD_GL(glUniform3f);
    if (missing == 0) {
        LOG_INFO("Embedded GL loader: all 39 extended symbols loaded");
    } else {
        LOG_WARN("Embedded GL loader: %d/39 extended symbols missing (rendering requires all)", missing);
    }
    return missing;
}

/* Create an OpenGL 3.3 Core context.
 *
 * Args:
 *   win: parent window.
 *
 * Returns: owned GlContext or NULL.
 */
GlContext *gl_ctx_init(Window *win)
{
    if (win == NULL) {
        LOG_ERROR("gl_ctx_init: win is NULL");
        return NULL;
    }
    void *native = window_native_handle(win);
    if (native == NULL) {
        LOG_ERROR("gl_ctx_init: native window handle is NULL");
        return NULL;
    }
    SDL_Window *sdl_win = (SDL_Window *)native;

    /* Request OpenGL 3.3 Core. Must be set before SDL_GL_CreateContext. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_GLContext handle = SDL_GL_CreateContext(sdl_win);
    if (handle == NULL) {
        LOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return NULL;
    }

    if (SDL_GL_MakeCurrent(sdl_win, handle) != 0) {
        LOG_ERROR("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        SDL_GL_DeleteContext(handle);
        return NULL;
    }

    /* Vsync on; tolerate failure (headless/VM drivers may refuse). */
    if (SDL_GL_SetSwapInterval(1) != 0) {
        LOG_WARN("SDL_GL_SetSwapInterval(1) failed: %s (continuing without vsync)", SDL_GetError());
    }

    GlContext *ctx = (GlContext *)SDL_malloc(sizeof(GlContext));
    if (ctx == NULL) {
        LOG_ERROR("gl_ctx_init: out of memory");
        SDL_GL_DeleteContext(handle);
        return NULL;
    }
    ctx->handle = handle;
    ctx->window = win;

    /* Pre-load M1 entry points now so missing-driver issues surface early. */
    minec_gl_load_all();

    LOG_INFO("OpenGL context created: %s / %s / %s", gl_ctx_get_vendor(), gl_ctx_get_renderer(), gl_ctx_get_version());
    return ctx;
}

/* Destroy a GL context.
 *
 * Args:
 *   ctx: context to destroy.
 */
void gl_ctx_destroy(GlContext *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->handle != NULL) {
        SDL_GL_DeleteContext(ctx->handle);
    }
    SDL_free(ctx);
}

/* Make the context current.
 *
 * Args:
 *   ctx: context.
 *
 * Returns: 0 on success.
 */
int gl_ctx_make_current(GlContext *ctx)
{
    if (ctx == NULL || ctx->window == NULL) {
        return -1;
    }
    SDL_Window *sdl_win = (SDL_Window *)window_native_handle(ctx->window);
    if (sdl_win == NULL) {
        return -2;
    }
    if (SDL_GL_MakeCurrent(sdl_win, ctx->handle) != 0) {
        LOG_ERROR("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        return -3;
    }
    return 0;
}

/* Generic proc lookup.
 *
 * Args:
 *   name: function name.
 *
 * Returns: address or NULL.
 */
void *gl_ctx_get_proc(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    return SDL_GL_GetProcAddress(name);
}

/* Safe wrapper around glGetString that never returns NULL. */
static const char *gl_string_safe(GLenum name)
{
    const GLubyte *s = glGetString(name);
    return s ? (const char *)s : "unknown";
}

/* Get GL vendor string. Returns: vendor or "unknown". */
const char *gl_ctx_get_vendor(void)
{
    return gl_string_safe(GL_VENDOR);
}

/* Get GL renderer string. Returns: renderer or "unknown". */
const char *gl_ctx_get_renderer(void)
{
    return gl_string_safe(GL_RENDERER);
}

/* Get GL version string. Returns: version or "unknown". */
const char *gl_ctx_get_version(void)
{
    return gl_string_safe(GL_VERSION);
}
