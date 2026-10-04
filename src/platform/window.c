#include "platform/window.h"
#include "core/log.h"
#include "game/input_buffer.h"

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

/* Concrete window type (opaque to callers). */
struct Window {
    SDL_Window *handle; /* Owned SDL window. */
    int wheel_accum;    /* Accumulated SDL_MOUSEWHEEL y ticks (consumed on take). */
    InputBuffer input;
};

/* Reference count of live windows; SDL video init/quit is tied to it. */
static int s_window_count = 0;

/* Create a window with OpenGL support.
 *
 * Args:
 *   title: window title.
 *   width, height: initial size.
 *
 * Returns: owned Window or NULL.
 */
Window *window_create(const char *title, int width, int height)
{
    if (title == NULL || width <= 0 || height <= 0) {
        LOG_ERROR("window_create: invalid args");
        return NULL;
    }

    if (s_window_count == 0) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
            return NULL;
        }
    }

    /* Ask SDL for a High-DPI aware, resizable, OpenGL-capable window.
     * Actual GL attributes (version/profile) are set in gl_ctx_init before
     * context creation; SDL applies them at SDL_GL_CreateContext time. */
    SDL_Window *handle = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
                                          height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (handle == NULL) {
        LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        if (s_window_count == 0) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        return NULL;
    }

    Window *win = (Window *)SDL_malloc(sizeof(Window));
    if (win == NULL) {
        LOG_ERROR("window_create: out of memory");
        SDL_DestroyWindow(handle);
        if (s_window_count == 0) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        return NULL;
    }
    win->handle = handle;
    win->wheel_accum = 0;
    input_buffer_init(&win->input);
    s_window_count++;

    int dw = width;
    int dh = height;
    SDL_GL_GetDrawableSize(handle, &dw, &dh);
    LOG_INFO("Window created: requested %dx%d, drawable %dx%d", width, height, dw, dh);
    return win;
}

/* Destroy a window.
 *
 * Args:
 *   win: window to destroy.
 */
void window_destroy(Window *win)
{
    if (win == NULL) {
        return;
    }
    if (win->handle != NULL) {
        SDL_DestroyWindow(win->handle);
    }
    SDL_free(win);
    s_window_count--;
    if (s_window_count <= 0) {
        s_window_count = 0;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

/* Poll the next event.
 *
 * Args:
 *   win: window.
 *   out: receives translated event.
 *
 * Returns: true if an event was produced.
 */
bool window_poll_event(Window *win, WindowEvent *out)
{
    if (win == NULL || out == NULL) {
        return false;
    }
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            if (ev.key.windowID != SDL_GetWindowID(win->handle)) {
                continue;
            }
            int scancode = (int)ev.key.keysym.scancode;
            bool pressed = ev.type == SDL_KEYDOWN;
            input_buffer_key_event(&win->input, scancode, pressed, ev.key.repeat != 0);
            if (pressed && ev.key.keysym.sym == SDLK_ESCAPE && !ev.key.repeat) {
                out->kind = WINDOW_EVENT_KEY_ESC;
                out->width = 0;
                out->height = 0;
                return true;
            }
            continue;
        }
        if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
            if (ev.button.windowID == SDL_GetWindowID(win->handle)) {
                unsigned button = (unsigned)ev.button.button;
                input_buffer_mouse_event(&win->input, (int)button, ev.type == SDL_MOUSEBUTTONDOWN);
            }
            continue;
        }
        if (ev.type == SDL_QUIT) {
            out->kind = WINDOW_EVENT_QUIT;
            out->width = 0;
            out->height = 0;
            return true;
        }
        if (ev.type == SDL_WINDOWEVENT) {
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE && ev.window.windowID == SDL_GetWindowID(win->handle)) {
                out->kind = WINDOW_EVENT_QUIT;
                out->width = 0;
                out->height = 0;
                return true;
            }
            if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                int dw = 0;
                int dh = 0;
                SDL_GL_GetDrawableSize(win->handle, &dw, &dh);
                out->kind = WINDOW_EVENT_RESIZE;
                out->width = dw;
                out->height = dh;
                return true;
            }
        }
        if (ev.type == SDL_MOUSEWHEEL) {
            /* Accumulate for window_take_wheel_delta; keep draining the queue. */
            win->wheel_accum += ev.wheel.y;
            continue;
        }
        if (ev.type == SDL_TEXTINPUT) {
            out->kind = WINDOW_EVENT_TEXT;
            out->width = 0;
            out->height = 0;
            size_t n = 0;
            while (n + 1 < sizeof(out->text) && ev.text.text[n] != '\0') {
                out->text[n] = ev.text.text[n];
                ++n;
            }
            out->text[n] = '\0';
            return true;
        }
        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
            ev.window.windowID == SDL_GetWindowID(win->handle)) {
            input_buffer_focus_lost(&win->input);
            out->kind = WINDOW_EVENT_FOCUS_LOST;
            out->width = 0;
            out->height = 0;
            out->text[0] = '\0';
            return true;
        }
        /* Ignore all other events. */
    }
    out->kind = WINDOW_EVENT_NONE;
    out->width = 0;
    out->height = 0;
    out->text[0] = '\0';
    return false;
}

/* Swap buffers.
 *
 * Args:
 *   win: window.
 */
void window_swap(Window *win)
{
    if (win == NULL || win->handle == NULL) {
        return;
    }
    SDL_GL_SwapWindow(win->handle);
}

/* Get drawable size in pixels.
 *
 * Args:
 *   win: window.
 *   out_w/out_h: receive size.
 */
void window_get_size(Window *win, int *out_w, int *out_h)
{
    if (win == NULL || win->handle == NULL) {
        return;
    }
    int w = 0;
    int h = 0;
    SDL_GL_GetDrawableSize(win->handle, &w, &h);
    if (out_w != NULL) {
        *out_w = w;
    }
    if (out_h != NULL) {
        *out_h = h;
    }
}

/* Get native handle.
 *
 * Args:
 *   win: window.
 *
 * Returns: SDL_Window pointer.
 */
void *window_native_handle(Window *win)
{
    if (win == NULL) {
        return NULL;
    }
    return win->handle;
}

/* Enable/disable relative mouse mode.
 *
 * Args:
 *   win: window (unused beyond logging; mode is global in SDL2).
 *   enabled: capture flag.
 */
void window_set_relative_mouse(Window *win, bool enabled)
{
    (void)win;
    if (SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) != 0) {
        LOG_WARN("SDL_SetRelativeMouseMode(%d) failed: %s", (int)enabled, SDL_GetError());
    }
}

/* Read relative mouse motion.
 *
 * Args:
 *   out_dx/out_dy: receivers.
 */
void window_get_relative_motion(int *out_dx, int *out_dy)
{
    int dx = 0;
    int dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy);
    if (out_dx != NULL) {
        *out_dx = dx;
    }
    if (out_dy != NULL) {
        *out_dy = dy;
    }
}

/* Query one scancode's down state.
 *
 * Args:
 *   scancode: SDL_Scancode as int.
 *
 * Returns: true if held.
 */
bool window_is_key_down(int scancode)
{
    if (scancode < 0) {
        return false;
    }
    int numkeys = 0;
    const Uint8 *state = SDL_GetKeyboardState(&numkeys);
    if (state == NULL || scancode >= numkeys) {
        return false;
    }
    return state[scancode] != 0;
}

bool window_take_key_pressed(Window *win, int scancode)
{
    return win != NULL && input_buffer_take_key_pressed(&win->input, scancode);
}

/* Query a mouse button's down state.
 *
 * Args:
 *   button: MINEC_MOUSE_LEFT/MIDDLE/RIGHT.
 *
 * Returns: true if held.
 */
bool window_is_mouse_down(int button)
{
    int mask = 0;
    if (button == MINEC_MOUSE_LEFT) {
        mask = SDL_BUTTON_LMASK;
    } else if (button == MINEC_MOUSE_MIDDLE) {
        mask = SDL_BUTTON_MMASK;
    } else if (button == MINEC_MOUSE_RIGHT) {
        mask = SDL_BUTTON_RMASK;
    } else {
        return false;
    }
    return (SDL_GetMouseState(NULL, NULL) & mask) != 0;
}

bool window_take_mouse_pressed(Window *win, int button)
{
    return win != NULL && input_buffer_take_mouse_pressed(&win->input, button);
}

void window_clear_input_edges(Window *win)
{
    if (win == NULL) {
        return;
    }
    input_buffer_clear_edges(&win->input);
}

/* Take accumulated wheel motion (consumes it).
 *
 * Args:
 *   win: window.
 *
 * Returns: wheel ticks since last take.
 */
int window_take_wheel_delta(Window *win)
{
    if (win == NULL) {
        return 0;
    }
    int d = win->wheel_accum;
    win->wheel_accum = 0;
    return d;
}

/* Get the cursor position in window pixels (meaningful with capture off).
 *
 * Args:
 *   out_x/out_y: receivers.
 */
void window_get_mouse_pos(int *out_x, int *out_y)
{
    int x = 0;
    int y = 0;
    SDL_GetMouseState(&x, &y);
    if (out_x != NULL) {
        *out_x = x;
    }
    if (out_y != NULL) {
        *out_y = y;
    }
}

/* Start/stop SDL text input.
 *
 * Args:
 *   enable: true to start.
 */
void window_text_input(bool enable)
{
    if (enable) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

/* Set vsync (tolerates failure with a warning).
 *
 * Args:
 *   win: window (unused; applies to the current GL context).
 *   enable: true for interval 1.
 */
void window_set_vsync(Window *win, bool enable)
{
    (void)win;
    if (SDL_GL_SetSwapInterval(enable ? 1 : 0) != 0) {
        LOG_WARN("window_set_vsync(%d) failed: %s", (int)enable, SDL_GetError());
    }
}
