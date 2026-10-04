#pragma once

/* SDL2 window wrapper: creation, event polling, buffer swap, DPI basics.
 * Ownership: window_create() owns the SDL_Window; window_destroy() frees it.
 * SDL video subsystem is initialised on first create and quit on last destroy
 * (reference counted internally).
 */

#include <stdbool.h>

/* Opaque window handle. */
typedef struct Window Window;

/* Simplified event kinds consumed by AppContext. */
typedef enum WindowEventKind {
    WINDOW_EVENT_NONE = 0,
    WINDOW_EVENT_QUIT,    /* Window X button / SDL_QUIT. */
    WINDOW_EVENT_KEY_ESC, /* ESC pressed -> request quit. */
    WINDOW_EVENT_RESIZE,  /* Drawable size changed; width/height valid. */
    WINDOW_EVENT_TEXT,    /* SDL text input; text[] carries UTF-8 bytes. */
    WINDOW_EVENT_FOCUS_LOST /* Window focus lost (auto-pause in game). */
} WindowEventKind;

/* Single polled event. For RESIZE, width/height carry the new drawable size.
 * For TEXT, text[] carries up to 15 UTF-8 bytes plus NUL.
 */
typedef struct WindowEvent {
    WindowEventKind kind;
    int width;
    int height;
    char text[16];
} WindowEvent;

/* Create a window with an OpenGL-capable, resizable, High-DPI surface.
 *
 * Args:
 *   title: window title (must not be NULL).
 *   width: initial width in screen coords (> 0).
 *   height: initial height in screen coords (> 0).
 *
 * Returns: owned Window on success, NULL on failure.
 */
Window *window_create(const char *title, int width, int height);

/* Destroy a window. NULL-safe. Releases SDL video when last window closes.
 *
 * Args:
 *   win: window to destroy (may be NULL).
 */
void window_destroy(Window *win);

/* Poll the next SDL event, translating it into a WindowEvent.
 *
 * Args:
 *   win: window (must not be NULL).
 *   out: receives the event (must not be NULL).
 *
 * Returns: true if an event was produced, false if the queue is empty.
 */
bool window_poll_event(Window *win, WindowEvent *out);

/* Swap front/back buffers (present the cleared frame).
 *
 * Args:
 *   win: window (must not be NULL).
 */
void window_swap(Window *win);

/* Get the current drawable size in pixels (accounts for DPI scaling;
 * may differ from requested size on HiDPI/Retina).
 *
 * Args:
 *   win: window (must not be NULL).
 *   out_w/out_h: receive size (each may be NULL if not needed).
 */
void window_get_size(Window *win, int *out_w, int *out_h);

/* Get the native SDL_Window handle for GL context creation.
 * The Window retains ownership; do not free the result.
 *
 * Args:
 *   win: window (must not be NULL).
 *
 * Returns: SDL_Window pointer, or NULL on bad args.
 */
void *window_native_handle(Window *win);

/* Enable/disable SDL relative mouse mode (FPS look). When enabled the cursor
 * is hidden and mouse motion is reported as relative deltas.
 *
 * Args:
 *   win: window (may be NULL; then applies globally).
 *   enabled: true to capture, false to release.
 */
void window_set_relative_mouse(Window *win, bool enabled);

/* Read relative mouse motion since the last call (requires relative mode).
 * Never fails; outputs are zeroed on bad args.
 *
 * Args:
 *   out_dx/out_dy: receive pixel deltas (each may be NULL).
 */
void window_get_relative_motion(int *out_dx, int *out_dy);

/* Query the current keyboard state for an SDL scancode.
 * The `scancode` argument is an SDL_Scancode value (e.g. SDL_SCANCODE_W);
 * pass it as int to keep SDL headers out of this header. Callers include
 * <SDL2/SDL.h> (or <SDL.h>) for the constants.
 *
 * Args:
 *   scancode: SDL scancode (e.g. 26 for W on most layouts; use SDL_SCANCODE_*).
 *
 * Returns: true if the key is currently held down.
 */
bool window_is_key_down(int scancode);

/* Consume one key-down edge latched while SDL events were drained. Key-up
 * before a simulation tick does not erase the press. Returns false for an
 * invalid scancode or when there was no pending edge.
 */
bool window_take_key_pressed(Window *win, int scancode);

/* Mouse buttons for window_is_mouse_down (match SDL_BUTTON_* values). */
#define MINEC_MOUSE_LEFT 1
#define MINEC_MOUSE_MIDDLE 2
#define MINEC_MOUSE_RIGHT 3

/* Query whether a mouse button is currently held down.
 *
 * Args:
 *   button: MINEC_MOUSE_LEFT/MIDDLE/RIGHT.
 *
 * Returns: true if held (false on bad args).
 */
bool window_is_mouse_down(int button);

/* Consume one mouse-button-down edge latched while SDL events were drained.
 */
bool window_take_mouse_pressed(Window *win, int button);

/* Clear queued key and mouse press edges on focus/state transitions so UI
 * or resumed gameplay cannot consume a stale action.
 */
void window_clear_input_edges(Window *win);

/* Take the accumulated mouse-wheel motion since the last call (consumes it).
 * Positive y = wheel up. Updated from SDL_MOUSEWHEEL events drained by
 * window_poll_event, so the queue must be polled regularly.
 *
 * Args:
 *   win: window (must not be NULL).
 *
 * Returns: accumulated vertical wheel ticks (0 when none / bad args).
 */
int window_take_wheel_delta(Window *win);

/* Get the cursor position in window pixels (y-down, undefined in
 * relative-mouse mode; menus run with capture off).
 *
 * Args:
 *   out_x/out_y: receivers (each may be NULL).
 */
void window_get_mouse_pos(int *out_x, int *out_y);

/* Start/stop SDL text input (UTF-8 TEXT events). Enable on text screens.
 *
 * Args:
 *   enable: true to start, false to stop.
 */
void window_text_input(bool enable);

/* Set vertical sync (swap interval 1/0). Failures are tolerated (logged).
 *
 * Args:
 *   win: window (may be NULL; applies to the current GL context).
 *   enable: true for vsync on.
 */
void window_set_vsync(Window *win, bool enable);
