#include "ui/ui_style.h"

#include <limits.h>
#include <math.h>

UiScale ui_scale_for_drawable(int width, int height)
{
    UiScale scale = {1, 0.5f, 0.0f, 0.0f};
    if (width <= 0 || height <= 0) {
        return scale;
    }
    int horizontal = width / 640;
    int vertical = height / 360;
    scale.native_scale = horizontal < vertical ? horizontal : vertical;
    if (scale.native_scale < 1) {
        scale.native_scale = 1;
    }
    scale.canvas_ratio = (float)scale.native_scale * 0.5f;
    scale.canvas_width = (float)width / scale.canvas_ratio;
    scale.canvas_height = (float)height / scale.canvas_ratio;
    return scale;
}

static void ui_texture_quad(float *out, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
{
    const float quad[24] = {
        x0, y0, u0, v0, x1, y0, u1, v0, x0, y1, u0, v1, x0, y1, u0, v1, x1, y0, u1, v0, x1, y1, u1, v1,
    };
    for (size_t i = 0; i < sizeof(quad) / sizeof(quad[0]); ++i) {
        out[i] = quad[i];
    }
}

size_t ui_nine_slice_build(float *out, size_t cap_vertices, float x, float y, float width, float height,
                           float source_width, float source_height, float border, float pixel_scale)
{
    if (out == NULL || cap_vertices < UI_NINE_SLICE_VERTICES || !isfinite(x) || !isfinite(y) || !isfinite(width) ||
        !isfinite(height) || !isfinite(source_width) || !isfinite(source_height) || !isfinite(border) ||
        !isfinite(pixel_scale) || width <= 0.0f || height <= 0.0f || source_width <= 0.0f || source_height <= 0.0f ||
        border < 0.0f || pixel_scale <= 0.0f || !isfinite(x + width) || !isfinite(y + height)) {
        return 0;
    }
    float source_border = fminf(border, fminf(source_width, source_height) * 0.5f);
    double desired_border = (double)source_border * (double)pixel_scale;
    float border_x = (float)fmin(desired_border, (double)width * 0.5);
    float border_y = (float)fmin(desired_border, (double)height * 0.5);
    const float xs[4] = {x, x + border_x, x + width - border_x, x + width};
    const float ys[4] = {y, y + border_y, y + height - border_y, y + height};
    const float us[4] = {0.0f, source_border / source_width, 1.0f - source_border / source_width, 1.0f};
    const float vs[4] = {0.0f, source_border / source_height, 1.0f - source_border / source_height, 1.0f};
    size_t offset = 0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            ui_texture_quad(out + offset, xs[column], ys[row], xs[column + 1], ys[row + 1], us[column], vs[row],
                            us[column + 1], vs[row + 1]);
            offset += 6u * UI_TEXTURE_FLOATS_PER_VERTEX;
        }
    }
    return UI_NINE_SLICE_VERTICES;
}

int ui_focus_next_enabled(int current, int direction, const bool *enabled, size_t count)
{
    if (count == 0 || count > (size_t)INT_MAX) {
        return -1;
    }
    bool valid = current >= 0 && (size_t)current < count;
    if (direction == 0) {
        return valid && (enabled == NULL || enabled[current]) ? current : -1;
    }
    int step = direction > 0 ? 1 : -1;
    int index = valid ? current : (step > 0 ? -1 : 0);
    for (size_t scanned = 0; scanned < count; ++scanned) {
        index += step;
        if (index < 0) {
            index = (int)count - 1;
        } else if ((size_t)index >= count) {
            index = 0;
        }
        if (enabled == NULL || enabled[index]) {
            return index;
        }
    }
    return -1;
}
