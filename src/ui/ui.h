#pragma once

/* Immediate-mode UI widgets (M5): buttons, labels (via renderer text),
 * text fields, sliders, toggles, and cycle buttons. Logic only — no GL;
 * screens compute rects, query these helpers, and issue renderer draws.
 * All interaction state lives in UiFrame, rebuilt by the app each frame
 * from polled SDL events. Headless-testable.
 */

#include <stdbool.h>
#include <stddef.h>

/* Per-frame input snapshot assembled by the app from SDL events. */
typedef struct UiFrame {
    int mouse_x;      /* Cursor position in pixels (y-down). */
    int mouse_y;      /* Cursor position in pixels (y-down). */
    bool mouse_down;  /* Left button currently held. */
    bool mouse_clicked; /* Left button pressed this frame (edge). */
    bool mouse_rdown;   /* Right button currently held. */
    bool mouse_rclicked; /* Right button pressed this frame (edge). */
    int wheel;        /* Accumulated wheel ticks this frame (signed). */
    char text[64];    /* UTF-8 bytes typed this frame (may be empty). */
    bool key_backspace; /* Backspace pressed this frame. */
    bool key_return;    /* Return/Enter pressed this frame. */
    bool key_escape;    /* Escape pressed this frame. */
    bool key_up;        /* Up arrow pressed this frame. */
    bool key_down;      /* Down arrow pressed this frame. */
    bool key_delete;    /* Delete key pressed this frame. */
} UiFrame;

/* Zero an input frame (call once per frame before filling events).
 *
 * Args:
 *   f: frame to clear (must not be NULL).
 */
void ui_frame_clear(UiFrame *f);

/* Point-in-rect hit test (inclusive left/top, exclusive right/bottom).
 *
 * Args:
 *   mx, my: point in pixels.
 *   x, y, w, h: rectangle.
 *
 * Returns: true when inside.
 */
bool ui_hit(int mx, int my, float x, float y, float w, float h);

/* Button outcome for one frame.
 *
 * Args:
 *   f: input frame (must not be NULL).
 *   x, y, w, h: button rect in pixels.
 *   hovered: receives hover state (may be NULL).
 *
 * Returns: true on click (press began and ended... here: press edge
 * inside the rect; simple and predictable for menus).
 */
bool ui_button(const UiFrame *f, float x, float y, float w, float h, bool *hovered);

/* Single-line text field editor. Click focuses (captured via focused_id
 * handshake below); typing appends UTF-8 bytes; backspace deletes one
 * trailing byte (ASCII-safe; multibyte deletes one byte — documented).
 * Printable ASCII only (32..126); other bytes ignored. Return confirms.
 *
 * Args:
 *   f: input frame (must not be NULL).
 *   focused: in/out focus flag for this field (must not be NULL).
 *   buf: NUL-terminated text buffer (must not be NULL).
 *   cap: buffer capacity in bytes (> 1).
 *   x, y, w, h: field rect (click focuses).
 *
 * Returns: true when Return was pressed while focused (confirm edge).
 */
bool ui_text_field(const UiFrame *f, bool *focused, char *buf, size_t cap, float x, float y, float w, float h);

/* Horizontal slider drag. Grabs on press-inside, follows mouse-x while
 * held (tracked via held_id handshake), clamps to [lo,hi].
 *
 * Args:
 *   f: input frame (must not be NULL).
 *   value: in/out current value (must not be NULL).
 *   lo, hi: range (hi > lo required).
 *   x, y, w, h: slider rect in pixels.
 *   held: in/out drag flag for this slider (must not be NULL).
 *
 * Returns: true while dragging or on click (value may have changed).
 */
bool ui_slider(const UiFrame *f, float *value, float lo, float hi, float x, float y, float w, float h,
               bool *held);

/* Clamp helper for ints.
 *
 * Args:
 *   v, lo, hi: value and bounds.
 *
 * Returns: clamped value.
 */
int ui_clampi(int v, int lo, int hi);
