#pragma once

/* OpenGL 3.3 Core context wrapper + minimal embedded GL loader.
 *
 * DESIGN (read before modifying):
 * - We deliberately avoid GLEW/GLAD to keep dependencies at SDL2-only.
 * - M0 basics (glClearColor, glClear, glViewport, glGetError, glEnable,
 *   glDisable) come from the OS system headers and system GL library:
 *     Windows: <GL/gl.h> + opengl32.lib (OpenGL 1.1 exports; sufficient for M0)
 *     Linux:   <GL/gl.h> + libGL
 *     macOS:   <OpenGL/gl3.h> + OpenGL framework
 *   These are linked via find_package(OpenGL) / OpenGL::GL.
 * - Modern entry points needed for M1+ (VAO/VBO, shader compile/link, etc.)
 *   are NOT exported by opengl32.dll, so they MUST be fetched at runtime via
 *   SDL_GL_GetProcAddress. Their typedefs + `minec_gl_*` pointers live below.
 *   `minec_gl_load_all()` loads them after context creation and logs failures.
 *   M0 treats missing extended symbols as warnings (future use); context
 *   creation itself only requires OpenGL 3.3 Core.
 *
 * Ownership: gl_ctx_init() owns the SDL_GLContext; gl_ctx_destroy() frees it.
 * The parent Window must outlive the GlContext.
 */

/* Forward declaration to avoid including SDL headers here. */
typedef struct Window Window;

/* Opaque GL context handle. */
typedef struct GlContext GlContext;

/* Create an OpenGL 3.3 Core context for `win` and make it current.
 * Sets SDL GL attributes (version 3.3, core profile, double buffer, depth 24,
 * stencil 8), creates the context, loads the embedded entry points, and sets
 * vsync (tolerates vsync failure).
 *
 * Args:
 *   win: window to attach the context to (must not be NULL, must be alive
 *        for the lifetime of the returned context).
 *
 * Returns: owned GlContext on success, NULL on failure.
 */
GlContext *gl_ctx_init(Window *win);

/* Destroy a GL context. NULL-safe. Does not destroy the parent window.
 *
 * Args:
 *   ctx: context to destroy (may be NULL).
 */
void gl_ctx_destroy(GlContext *ctx);

/* Make the context current for its window.
 *
 * Args:
 *   ctx: context (must not be NULL).
 *
 * Returns: 0 on success, non-zero on failure.
 */
int gl_ctx_make_current(GlContext *ctx);

/* Generic proc-address lookup (thin wrapper over SDL_GL_GetProcAddress).
 *
 * Args:
 *   name: GL function name (must not be NULL).
 *
 * Returns: function pointer, or NULL if unavailable.
 */
void *gl_ctx_get_proc(const char *name);

/* Human-readable GL strings. Never NULL (returns "unknown" when unavailable).
 * Must be called with a current context (safe after gl_ctx_init).
 */
const char *gl_ctx_get_vendor(void);
const char *gl_ctx_get_renderer(void);
const char *gl_ctx_get_version(void);

/* --- Minimal embedded loader (M1-relevant entry points) ------------------
 * Each `minec_gl_*` is filled by minec_gl_load_all() via SDL_GL_GetProcAddress.
 * Signatures match the Khronos OpenGL 3.3 Core specification.
 * M0 code does not call these yet; they are pre-loaded and verified so M1
 * meshing/shaders can rely on them.
 */

typedef unsigned int MinecGLenum;
typedef unsigned int MinecGLuint;
typedef int MinecGLint;
typedef int MinecGLsizei;
typedef unsigned char MinecGLboolean;
typedef float MinecGLfloat;
typedef double MinecGLdouble;
typedef char MinecGLchar;
typedef long long MinecGLsizeiptr;
typedef long long MinecGLintptr;
typedef unsigned char MinecGLubyte;

/* --- TerraCraft GL enum constants (Khronos values) --------------------------------
 * System <GL/gl.h> on Windows only exposes OpenGL 1.1 enums. Shader/VAO/VBO
 * tokens (2.0+) are defined here so shader.c / vao_manager.c compile
 * everywhere without GLEW/GLAD. Values match the GL 3.3 Core specification.
 */
#define MINEC_GL_FALSE 0
#define MINEC_GL_TRUE 1
#define MINEC_GL_TRIANGLES 0x0004u
#define MINEC_GL_UNSIGNED_INT 0x1405u
#define MINEC_GL_FLOAT 0x1406u
#define MINEC_GL_ARRAY_BUFFER 0x8892u
#define MINEC_GL_ELEMENT_ARRAY_BUFFER 0x8893u
#define MINEC_GL_STATIC_DRAW 0x88E4u
#define MINEC_GL_VERTEX_SHADER 0x8B31u
#define MINEC_GL_FRAGMENT_SHADER 0x8B30u
#define MINEC_GL_COMPILE_STATUS 0x8B81u
#define MINEC_GL_LINK_STATUS 0x8B82u

/* --- TerraCraft GL texture/sampler constants (Khronos values) ----------------------
 * gl.h on Windows also lacks post-1.1 texture tokens (TEXTURE0 is 1.3).
 * Defined here so texture_atlas.c compiles without GLEW/GLAD.
 */
#define MINEC_GL_TEXTURE_2D 0x0DE1u
#define MINEC_GL_RGBA 0x1908u
#define MINEC_GL_UNSIGNED_BYTE 0x1401u
#define MINEC_GL_TEXTURE0 0x84C0u
#define MINEC_GL_TEXTURE_MIN_FILTER 0x2801u
#define MINEC_GL_TEXTURE_MAG_FILTER 0x2800u
#define MINEC_GL_NEAREST 0x2600u
#define MINEC_GL_CLAMP_TO_EDGE 0x812Fu
#define MINEC_GL_TEXTURE_WRAP_S 0x2802u
#define MINEC_GL_TEXTURE_WRAP_T 0x2803u

/* Function-pointer types (Khronos-compatible). */
typedef void (*MinecPFN_glGenVertexArrays)(MinecGLsizei n, MinecGLuint *arrays);
typedef void (*MinecPFN_glBindVertexArray)(MinecGLuint array);
typedef void (*MinecPFN_glDeleteVertexArrays)(MinecGLsizei n, const MinecGLuint *arrays);
typedef void (*MinecPFN_glGenBuffers)(MinecGLsizei n, MinecGLuint *buffers);
typedef void (*MinecPFN_glBindBuffer)(MinecGLenum target, MinecGLuint buffer);
typedef void (*MinecPFN_glBufferData)(MinecGLenum target, MinecGLsizeiptr size, const void *data, MinecGLenum usage);
typedef void (*MinecPFN_glDeleteBuffers)(MinecGLsizei n, const MinecGLuint *buffers);
typedef void (*MinecPFN_glEnableVertexAttribArray)(MinecGLuint index);
typedef void (*MinecPFN_glVertexAttribPointer)(
    MinecGLuint index, MinecGLint size, MinecGLenum type, MinecGLboolean normalized, MinecGLsizei stride, const void *pointer);
typedef MinecGLuint (*MinecPFN_glCreateShader)(MinecGLenum type);
typedef void (*MinecPFN_glShaderSource)(
    MinecGLuint shader, MinecGLsizei count, const MinecGLchar *const *string, const MinecGLint *length);
typedef void (*MinecPFN_glCompileShader)(MinecGLuint shader);
typedef void (*MinecPFN_glGetShaderiv)(MinecGLuint shader, MinecGLenum pname, MinecGLint *params);
typedef void (*MinecPFN_glGetShaderInfoLog)(MinecGLuint shader, MinecGLsizei bufSize, MinecGLsizei *length, MinecGLchar *infoLog);
typedef MinecGLuint (*MinecPFN_glCreateProgram)(void);
typedef void (*MinecPFN_glAttachShader)(MinecGLuint program, MinecGLuint shader);
typedef void (*MinecPFN_glLinkProgram)(MinecGLuint program);
typedef void (*MinecPFN_glGetProgramiv)(MinecGLuint program, MinecGLenum pname, MinecGLint *params);
typedef void (*MinecPFN_glGetProgramInfoLog)(
    MinecGLuint program, MinecGLsizei bufSize, MinecGLsizei *length, MinecGLchar *infoLog);
typedef void (*MinecPFN_glUseProgram)(MinecGLuint program);
typedef void (*MinecPFN_glDeleteShader)(MinecGLuint shader);
typedef void (*MinecPFN_glDeleteProgram)(MinecGLuint program);
typedef MinecGLint (*MinecPFN_glGetUniformLocation)(MinecGLuint program, const MinecGLchar *name);
typedef void (*MinecPFN_glUniformMatrix4fv)(
    MinecGLint location, MinecGLsizei count, MinecGLboolean transpose, const MinecGLfloat *value);
typedef void (*MinecPFN_glDrawElements)(MinecGLenum mode, MinecGLsizei count, MinecGLenum type, const void *indices);
typedef void (*MinecPFN_glDrawArrays)(MinecGLenum mode, MinecGLint first, MinecGLsizei count);
typedef void (*MinecPFN_glDisableVertexAttribArray)(MinecGLuint index);
/* Texture/sampler entry points (M2). System gl.h on Windows lacks 1.2+
 * declarations, so these are always resolved via SDL_GL_GetProcAddress. */
typedef void (*MinecPFN_glGenTextures)(MinecGLsizei n, MinecGLuint *textures);
typedef void (*MinecPFN_glBindTexture)(MinecGLenum target, MinecGLuint texture);
typedef void (*MinecPFN_glTexImage2D)(MinecGLenum target, MinecGLint level, MinecGLint internalformat,
                                      MinecGLsizei width, MinecGLsizei height, MinecGLint border, MinecGLenum format,
                                      MinecGLenum type, const void *pixels);
typedef void (*MinecPFN_glTexParameteri)(MinecGLenum target, MinecGLenum pname, MinecGLint param);
typedef void (*MinecPFN_glDeleteTextures)(MinecGLsizei n, const MinecGLuint *textures);
typedef void (*MinecPFN_glActiveTexture)(MinecGLenum texture);
typedef void (*MinecPFN_glUniform1i)(MinecGLint location, MinecGLint v0);
typedef void (*MinecPFN_glUniform1f)(MinecGLint location, MinecGLfloat v0);
typedef void (*MinecPFN_glUniform3f)(MinecGLint location, MinecGLfloat v0, MinecGLfloat v1, MinecGLfloat v2);

/* Loaded entry points. NULL until minec_gl_load_all() succeeds for that symbol. */
extern MinecPFN_glGenVertexArrays minec_glGenVertexArrays;
extern MinecPFN_glBindVertexArray minec_glBindVertexArray;
extern MinecPFN_glDeleteVertexArrays minec_glDeleteVertexArrays;
extern MinecPFN_glGenBuffers minec_glGenBuffers;
extern MinecPFN_glBindBuffer minec_glBindBuffer;
extern MinecPFN_glBufferData minec_glBufferData;
extern MinecPFN_glDeleteBuffers minec_glDeleteBuffers;
extern MinecPFN_glEnableVertexAttribArray minec_glEnableVertexAttribArray;
extern MinecPFN_glVertexAttribPointer minec_glVertexAttribPointer;
extern MinecPFN_glCreateShader minec_glCreateShader;
extern MinecPFN_glShaderSource minec_glShaderSource;
extern MinecPFN_glCompileShader minec_glCompileShader;
extern MinecPFN_glGetShaderiv minec_glGetShaderiv;
extern MinecPFN_glGetShaderInfoLog minec_glGetShaderInfoLog;
extern MinecPFN_glCreateProgram minec_glCreateProgram;
extern MinecPFN_glAttachShader minec_glAttachShader;
extern MinecPFN_glLinkProgram minec_glLinkProgram;
extern MinecPFN_glGetProgramiv minec_glGetProgramiv;
extern MinecPFN_glGetProgramInfoLog minec_glGetProgramInfoLog;
extern MinecPFN_glUseProgram minec_glUseProgram;
extern MinecPFN_glDeleteShader minec_glDeleteShader;
extern MinecPFN_glDeleteProgram minec_glDeleteProgram;
extern MinecPFN_glGetUniformLocation minec_glGetUniformLocation;
extern MinecPFN_glUniformMatrix4fv minec_glUniformMatrix4fv;
extern MinecPFN_glDrawElements minec_glDrawElements;
extern MinecPFN_glDrawArrays minec_glDrawArrays;
extern MinecPFN_glDisableVertexAttribArray minec_glDisableVertexAttribArray;
extern MinecPFN_glGenTextures minec_glGenTextures;
extern MinecPFN_glBindTexture minec_glBindTexture;
extern MinecPFN_glTexImage2D minec_glTexImage2D;
extern MinecPFN_glTexParameteri minec_glTexParameteri;
extern MinecPFN_glDeleteTextures minec_glDeleteTextures;
extern MinecPFN_glActiveTexture minec_glActiveTexture;
extern MinecPFN_glUniform1i minec_glUniform1i;
extern MinecPFN_glUniform1f minec_glUniform1f;
extern MinecPFN_glUniform3f minec_glUniform3f;

/* Load all `minec_gl_*` entry points via SDL_GL_GetProcAddress.
 * Missing symbols are logged as warnings (M0 does not yet need them).
 *
 * Returns: number of symbols that failed to load (0 = all loaded).
 */
int minec_gl_load_all(void);
