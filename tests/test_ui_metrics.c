#include "test_main.h"
#include "platform/window.h"
#include "ui/ui_style.h"

#include <limits.h>
#include <math.h>

int test_ui_pointer_drawable_scaling(void)
{
    int failures = 0;
    int x = 0, y = 0;
    window_mouse_to_drawable(640, 360, 1280, 720, 1280, 720, &x, &y);
    TEST_ASSERT(x == 640 && y == 360);
    window_mouse_to_drawable(640, 360, 1280, 720, 2560, 1440, &x, &y);
    TEST_ASSERT(x == 1280 && y == 720);
    window_mouse_to_drawable(200, 100, 1000, 800, 1500, 1600, &x, &y);
    TEST_ASSERT(x == 300 && y == 200);
    /* Last client pixel stays inside; the exclusive boundary stays outside. */
    window_mouse_to_drawable(1279, 719, 1280, 720, 2560, 1440, &x, &y);
    TEST_ASSERT(x == 2558 && y == 1438);
    window_mouse_to_drawable(1280, 720, 1280, 720, 2560, 1440, &x, &y);
    TEST_ASSERT(x == 2560 && y == 1440);
    /* Flooring preserves off-window negatives even at fractional scales. */
    window_mouse_to_drawable(-1, -1, 200, 200, 100, 300, &x, &y);
    TEST_ASSERT(x == -1 && y == -2);
    window_mouse_to_drawable(7, 9, 0, 720, 2560, 1440, &x, &y);
    TEST_ASSERT(x == 7 && y == 9);
    window_mouse_to_drawable(7, 9, 1280, 720, 2560, -1, &x, &y);
    TEST_ASSERT(x == 7 && y == 9);
    window_mouse_to_drawable(INT_MAX, INT_MIN, 1, 1, INT_MAX, INT_MAX, &x, &y);
    TEST_ASSERT(x == INT_MAX && y == INT_MIN);
    window_mouse_to_drawable(3, 4, 1, 1, 2, 2, NULL, &y);
    TEST_ASSERT(y == 8);
    window_mouse_to_drawable(3, 4, 1, 1, 2, 2, &x, NULL);
    TEST_ASSERT(x == 6);
    window_mouse_to_drawable(0, 0, 1, 1, 2, 2, NULL, NULL);
    return failures;
}

int test_ui_scale_layout(void)
{
    int failures = 0;
    const int widths[4] = {1280, 1920, 2560, 3840};
    const int heights[4] = {720, 1080, 1440, 2160};
    const int native[4] = {2, 3, 4, 6};
    const float ratios[4] = {1.0f, 1.5f, 2.0f, 3.0f};
    for (int i = 0; i < 4; ++i) {
        UiScale scale = ui_scale_for_drawable(widths[i], heights[i]);
        TEST_ASSERT(scale.native_scale == native[i]);
        TEST_ASSERT_FLOAT_EQ(scale.canvas_ratio, ratios[i], 0.0f);
        TEST_ASSERT_FLOAT_EQ(scale.canvas_width, 1280.0f, 0.0f);
        TEST_ASSERT_FLOAT_EQ(scale.canvas_height, 720.0f, 0.0f);
        /* A 400x40 baseline button and a two-pixel glyph preserve native pixels. */
        TEST_ASSERT_FLOAT_EQ(400.0f * scale.canvas_ratio, 200.0f * native[i], 0.0f);
        TEST_ASSERT_FLOAT_EQ(2.0f * scale.canvas_ratio, (float)native[i], 0.0f);
    }
    UiScale wide = ui_scale_for_drawable(2560, 1080);
    TEST_ASSERT(wide.native_scale == 3);
    TEST_ASSERT_FLOAT_EQ(wide.canvas_height, 720.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(wide.canvas_width / wide.canvas_height, 2560.0f / 1080.0f, 0.000001f);
    UiScale tiny = ui_scale_for_drawable(320, 240);
    TEST_ASSERT(tiny.native_scale == 1);
    TEST_ASSERT_FLOAT_EQ(tiny.canvas_ratio, 0.5f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(tiny.canvas_width, 640.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(tiny.canvas_height, 480.0f, 0.0f);
    UiScale invalid = ui_scale_for_drawable(0, 720);
    TEST_ASSERT_FLOAT_EQ(invalid.canvas_width, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(invalid.canvas_height, 0.0f, 0.0f);
    TEST_ASSERT(invalid.canvas_ratio > 0.0f);
    return failures;
}

int test_ui_nine_slice_geometry(void)
{
    int failures = 0;
    float verts[54 * 4 + 4];
    for (size_t i = 0; i < sizeof(verts) / sizeof(verts[0]); ++i) {
        verts[i] = -99.0f;
    }
    size_t count = ui_nine_slice_build(verts, 54, 10, 20, 400, 40, 200, 20, 3, 2);
    TEST_ASSERT(count == 54);
    TEST_ASSERT_FLOAT_EQ(verts[0], 10.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[1], 20.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[2], 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[3], 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[4], 16.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[6], 3.0f / 200.0f, 0.000001f);
    TEST_ASSERT_FLOAT_EQ(verts[9], 26.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[11], 3.0f / 20.0f, 0.000001f);
    TEST_ASSERT_FLOAT_EQ(verts[212], 410.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[213], 60.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[214], 1.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[215], 1.0f, 0.0f);
    for (size_t i = 0; i < 54; ++i) {
        TEST_ASSERT(verts[i * 4] >= 10.0f && verts[i * 4] <= 410.0f);
        TEST_ASSERT(verts[i * 4 + 1] >= 20.0f && verts[i * 4 + 1] <= 60.0f);
        TEST_ASSERT(verts[i * 4 + 2] >= 0.0f && verts[i * 4 + 2] <= 1.0f);
        TEST_ASSERT(verts[i * 4 + 3] >= 0.0f && verts[i * 4 + 3] <= 1.0f);
    }
    for (size_t i = 216; i < 220; ++i) {
        TEST_ASSERT_FLOAT_EQ(verts[i], -99.0f, 0.0f);
    }
    TEST_ASSERT(ui_nine_slice_build(verts, 54, 0, 0, 2, 1, 200, 20, 3, 2) == 54);
    for (size_t i = 0; i < 54; ++i) {
        TEST_ASSERT(verts[i * 4] >= 0.0f && verts[i * 4] <= 2.0f);
        TEST_ASSERT(verts[i * 4 + 1] >= 0.0f && verts[i * 4 + 1] <= 1.0f);
    }
    TEST_ASSERT_FLOAT_EQ(verts[4], 1.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[9], 0.5f, 0.0f);
    TEST_ASSERT(ui_nine_slice_build(verts, 54, 0, 0, 400, 40, 200, 20, 1, 2) == 54);
    TEST_ASSERT_FLOAT_EQ(verts[4], 2.0f, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[6], 1.0f / 200.0f, 0.000001f);
    float first = verts[0], last = verts[215];
    TEST_ASSERT(ui_nine_slice_build(verts, 53, 0, 0, 400, 40, 200, 20, 3, 2) == 0);
    TEST_ASSERT_FLOAT_EQ(verts[0], first, 0.0f);
    TEST_ASSERT_FLOAT_EQ(verts[215], last, 0.0f);
    TEST_ASSERT(ui_nine_slice_build(NULL, 54, 0, 0, 400, 40, 200, 20, 3, 2) == 0);
    TEST_ASSERT(ui_nine_slice_build(verts, 54, 0, 0, 0, 40, 200, 20, 3, 2) == 0);
    TEST_ASSERT(ui_nine_slice_build(verts, 54, 0, 0, 400, 40, 0, 20, 3, 2) == 0);
    TEST_ASSERT(ui_nine_slice_build(verts, 54, 0, 0, 400, 40, 200, 20, 3, 0) == 0);
    TEST_ASSERT(ui_nine_slice_build(verts, 54, NAN, 0, 400, 40, 200, 20, 3, 2) == 0);
    return failures;
}

int test_ui_focus_navigation(void)
{
    int failures = 0;
    const bool enabled[5] = {true, false, true, false, true};
    const bool none[3] = {false, false, false};
    TEST_ASSERT(ui_focus_next_enabled(-1, 1, enabled, 5) == 0);
    TEST_ASSERT(ui_focus_next_enabled(-1, -1, enabled, 5) == 4);
    TEST_ASSERT(ui_focus_next_enabled(0, 1, enabled, 5) == 2);
    TEST_ASSERT(ui_focus_next_enabled(2, 1, enabled, 5) == 4);
    TEST_ASSERT(ui_focus_next_enabled(4, 1, enabled, 5) == 0);
    TEST_ASSERT(ui_focus_next_enabled(0, -1, enabled, 5) == 4);
    TEST_ASSERT(ui_focus_next_enabled(4, -1, enabled, 5) == 2);
    TEST_ASSERT(ui_focus_next_enabled(1, 0, enabled, 5) == -1);
    TEST_ASSERT(ui_focus_next_enabled(2, 0, enabled, 5) == 2);
    TEST_ASSERT(ui_focus_next_enabled(INT_MAX, 1, enabled, 5) == 0);
    TEST_ASSERT(ui_focus_next_enabled(INT_MIN, -1, enabled, 5) == 4);
    TEST_ASSERT(ui_focus_next_enabled(1, 1, none, 3) == -1);
    TEST_ASSERT(ui_focus_next_enabled(0, 1, NULL, 1) == 0);
    TEST_ASSERT(ui_focus_next_enabled(-1, 1, NULL, 0) == -1);
    TEST_ASSERT(ui_focus_next_enabled(0, INT_MIN, enabled, 5) == 4);
    return failures;
}
