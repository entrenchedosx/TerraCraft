#include "ui/ui.h"

#include <string.h>

/* Zero an input frame. */
void ui_frame_clear(UiFrame *f)
{
    if (f == NULL) {
        return;
    }
    f->mouse_x = 0;
    f->mouse_y = 0;
    f->mouse_down = false;
    f->mouse_clicked = false;
    f->mouse_rdown = false;
    f->mouse_rclicked = false;
    f->wheel = 0;
    f->text[0] = '\0';
    f->key_backspace = false;
    f->key_return = false;
    f->key_escape = false;
    f->key_up = false;
    f->key_down = false;
    f->key_delete = false;
}

/* Point-in-rect test. */
bool ui_hit(int mx, int my, float x, float y, float w, float h)
{
    if (w <= 0.0f || h <= 0.0f) {
        return false;
    }
    float fx = (float)mx;
    float fy = (float)my;
    return fx >= x && fx < x + w && fy >= y && fy < y + h;
}

/* Button click = press edge inside the rect. */
bool ui_button(const UiFrame *f, float x, float y, float w, float h, bool *hovered)
{
    bool hov = false;
    bool clicked = false;
    if (f != NULL) {
        hov = ui_hit(f->mouse_x, f->mouse_y, x, y, w, h);
        clicked = hov && f->mouse_clicked;
    }
    if (hovered != NULL) {
        *hovered = hov;
    }
    return clicked;
}

/* Append one byte when printable ASCII and room remains (for NUL). */
static void field_put(char *buf, size_t cap, size_t *len, char c)
{
    if (c < 32 || c > 126) {
        return;
    }
    if (*len + 1 >= cap) {
        return;
    }
    buf[*len] = c;
    ++(*len);
    buf[*len] = '\0';
}

/* Text field editor. */
bool ui_text_field(const UiFrame *f, bool *focused, char *buf, size_t cap, float x, float y, float w, float h)
{
    if (f == NULL || focused == NULL || buf == NULL || cap < 2) {
        return false;
    }
    buf[cap - 1] = '\0';
    if (f->mouse_clicked) {
        *focused = ui_hit(f->mouse_x, f->mouse_y, x, y, w, h);
    }
    if (!(*focused)) {
        return false;
    }
    size_t len = strlen(buf);
    if (len >= cap) {
        len = cap - 1;
        buf[len] = '\0';
    }
    for (const char *p = f->text; *p != '\0'; ++p) {
        field_put(buf, cap, &len, *p);
    }
    if (f->key_backspace && len > 0) {
        buf[--len] = '\0'; /* ASCII-safe single-byte delete. */
    }
    return f->key_return;
}

/* Slider drag. */
bool ui_slider(const UiFrame *f, float *value, float lo, float hi, float x, float y, float w, float h,
               bool *held)
{
    if (f == NULL || value == NULL || held == NULL || !(hi > lo) || w <= 0.0f) {
        return false;
    }
    if (f->mouse_clicked && ui_hit(f->mouse_x, f->mouse_y, x, y, w, h)) {
        *held = true;
    }
    if (!f->mouse_down) {
        *held = false;
    }
    if (*held) {
        float t = ((float)f->mouse_x - x) / w;
        if (!(t >= 0.0f)) {
            t = 0.0f;
        }
        if (!(t <= 1.0f)) {
            t = 1.0f;
        }
        *value = lo + t * (hi - lo);
        return true;
    }
    return false;
}

/* Integer clamp. */
int ui_clampi(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}
