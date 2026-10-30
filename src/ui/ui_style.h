#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Native GUI pixels are an integer number of drawable pixels. The existing
 * screens use a 2x baseline, so canvas_ratio is native_scale / 2. At common
 * 16:9 resolutions the resulting canvas remains 1280x720. */
typedef struct UiScale {
    int native_scale;
    float canvas_ratio;
    float canvas_width;
    float canvas_height;
} UiScale;

UiScale ui_scale_for_drawable(int width, int height);

#define UI_NINE_SLICE_VERTICES 54u
#define UI_TEXTURE_FLOATS_PER_VERTEX 4u

/* Build nine quads as x,y,u,v triangles. Coordinates and UVs are top-down,
 * matching the existing UI texture pass. A source-pixel border is scaled
 * into drawable/canvas pixels by pixel_scale and clamped independently to
 * half the destination width/height. Source border is also clamped to half
 * its smallest dimension. Valid positive rectangles always emit 54 vertices
 * (collapsed center regions are degenerate quads).
 *
 * cap_vertices describes vertices, not floats: out must contain at least
 * cap_vertices * 4 floats. Invalid input/insufficient capacity returns zero
 * without touching out. A 200x20 button uses border=3, pixel_scale=2 at the
 * existing baseline. Text-field assets may use border=1 instead.
 */
size_t ui_nine_slice_build(float *out, size_t cap_vertices, float x, float y, float width, float height,
                           float source_width, float source_height, float border, float pixel_scale);

/* Move one focus step in the sign of direction, skipping disabled entries
 * and wrapping. enabled=NULL means every entry is enabled. Invalid current
 * indices enter at the first/last enabled entry for forward/backward motion.
 * Zero direction preserves a valid enabled focus. Returns -1 for no focus.
 */
int ui_focus_next_enabled(int current, int direction, const bool *enabled, size_t count);
