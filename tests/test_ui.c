#include "test_main.h"

#include "game/game_state.h"
#include "render/font.h"
#include "render/texture_atlas.h"
#include "ui/hud.h"
#include "ui/ui.h"

#include <stdint.h>
#include <string.h>

#include <string.h>

/* Test: state names, transition table, live/world predicates.
 *
 * Returns: failure count.
 */
int test_game_states(void)
{
    int failures = 0;
    TEST_ASSERT(strcmp(game_state_name(GAME_STATE_MAIN_MENU), "MAIN_MENU") == 0);
    TEST_ASSERT(strcmp(game_state_name(GAME_STATE_QUIT), "QUIT") == 0);
    TEST_ASSERT(game_state_name((GameState)99) != NULL);

    /* Menu flow edges. */
    TEST_ASSERT(game_state_can_transition(GAME_STATE_MAIN_MENU, GAME_STATE_WORLD_SELECT) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_MAIN_MENU, GAME_STATE_SETTINGS) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_MAIN_MENU, GAME_STATE_QUIT) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_MAIN_MENU, GAME_STATE_PLAYING) == false);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_WORLD_SELECT, GAME_STATE_CREATE_WORLD) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_WORLD_SELECT, GAME_STATE_LOADING) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_WORLD_SELECT, GAME_STATE_MAIN_MENU) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_CREATE_WORLD, GAME_STATE_LOADING) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_CREATE_WORLD, GAME_STATE_PLAYING) == false);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_LOADING, GAME_STATE_PLAYING) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_PLAYING, GAME_STATE_PAUSED) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_PLAYING, GAME_STATE_MAIN_MENU) == false);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_PAUSED, GAME_STATE_PLAYING) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_PAUSED, GAME_STATE_SETTINGS) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_PAUSED, GAME_STATE_MAIN_MENU) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_SETTINGS, GAME_STATE_MAIN_MENU) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_SETTINGS, GAME_STATE_PAUSED) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_SETTINGS, GAME_STATE_PLAYING) == false);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_PLAYING, GAME_STATE_CRAFTING) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_CRAFTING, GAME_STATE_PLAYING) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_CRAFTING, GAME_STATE_PAUSED) == false);
    TEST_ASSERT(game_state_needs_world(GAME_STATE_CRAFTING) == true);
    TEST_ASSERT(game_state_can_transition(GAME_STATE_QUIT, GAME_STATE_MAIN_MENU) == false);
    TEST_ASSERT(game_state_can_transition((GameState)99, GAME_STATE_QUIT) == false);

    /* Predicates. */
    TEST_ASSERT(game_state_is_live(GAME_STATE_PLAYING) == true);
    TEST_ASSERT(game_state_is_live(GAME_STATE_LOADING) == true);
    TEST_ASSERT(game_state_is_live(GAME_STATE_PAUSED) == false);
    TEST_ASSERT(game_state_is_live(GAME_STATE_MAIN_MENU) == false);
    TEST_ASSERT(game_state_needs_world(GAME_STATE_LOADING) == true);
    TEST_ASSERT(game_state_needs_world(GAME_STATE_PLAYING) == true);
    TEST_ASSERT(game_state_needs_world(GAME_STATE_PAUSED) == true);
    TEST_ASSERT(game_state_needs_world(GAME_STATE_MAIN_MENU) == false);
    TEST_ASSERT(game_state_needs_world(GAME_STATE_SETTINGS) == false);
    return failures;
}

/* Test: font glyphs, measure, and quad emission.
 *
 * Returns: failure count.
 */
int test_font_basic(void)
{
    int failures = 0;
    uint8_t rows[8];
    uint8_t blank[8];
    memset(blank, 0, sizeof(blank));
    font_glyph('A', rows);
    int set = 0;
    for (int i = 0; i < 8; ++i) {
        if (rows[i] != 0) {
            set = 1;
        }
    }
    TEST_ASSERT(set == 1);
    font_glyph(' ', rows);
    TEST_ASSERT(memcmp(rows, blank, 8) == 0);
    font_glyph((char)1, rows);
    TEST_ASSERT(memcmp(rows, blank, 8) == 0);
    { /* Out-of-range high byte via runtime value (avoids C4310 on constants). */
        int big = 200;
        char cbig = (char)big;
        font_glyph(cbig, rows);
        TEST_ASSERT(memcmp(rows, blank, 8) == 0);
    }
    font_glyph('A', NULL); /* NULL-safe. */

    float w = 0.0f, h = 0.0f;
    font_measure("AB", 1.0f, &w, &h);
    TEST_ASSERT_FLOAT_EQ(w, 16.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(h, 8.0f, 1e-5f);
    font_measure("AB", 2.0f, &w, &h);
    TEST_ASSERT_FLOAT_EQ(w, 32.0f, 1e-5f);
    font_measure("A\nBC", 1.0f, &w, &h);
    TEST_ASSERT_FLOAT_EQ(w, 16.0f, 1e-5f);
    TEST_ASSERT_FLOAT_EQ(h, 16.0f, 1e-5f);
    font_measure(NULL, 1.0f, &w, &h);
    TEST_ASSERT_FLOAT_EQ(w, 0.0f, 1e-5f);
    font_measure("AB", 0.0f, &w, &h);
    TEST_ASSERT_FLOAT_EQ(w, 0.0f, 1e-5f);

    /* Quad emission: 'A' yields verts, multiple of 6, bounded by cap. */
    float verts[4096];
    size_t n = font_build_quads("A", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, verts, 4096);
    TEST_ASSERT(n > 0 && n % 6 == 0);
    TEST_ASSERT(font_build_quads("", 0.0f, 0.0f, 1.0f, 1, 1, 1, 1, verts, 4096) == 0);
    TEST_ASSERT(font_build_quads(NULL, 0.0f, 0.0f, 1.0f, 1, 1, 1, 1, verts, 4096) == 0);
    TEST_ASSERT(font_build_quads("A", 0, 0, 1, 1, 1, 1, 1, NULL, 4096) == 0);
    /* Tiny cap truncates without overflow. */
    size_t small = font_build_quads("Hello World", 0.0f, 0.0f, 1.0f, 1, 1, 1, 1, verts, 12);
    TEST_ASSERT(small <= 12);
    return failures;
}

/* Test: button hit/hover/click logic.
 *
 * Returns: failure count.
 */
int test_ui_button(void)
{
    int failures = 0;
    UiFrame f;
    ui_frame_clear(&f);
    TEST_ASSERT(ui_hit(5, 5, 0.0f, 0.0f, 10.0f, 10.0f) == true);
    TEST_ASSERT(ui_hit(10, 5, 0.0f, 0.0f, 10.0f, 10.0f) == false); /* Exclusive edge. */
    TEST_ASSERT(ui_hit(5, 5, 0.0f, 0.0f, 0.0f, 10.0f) == false);

    bool hov = false;
    f.mouse_x = 50;
    f.mouse_y = 20;
    f.mouse_clicked = true;
    TEST_ASSERT(ui_button(&f, 0.0f, 0.0f, 100.0f, 40.0f, &hov) == true);
    TEST_ASSERT(hov == true);
    f.mouse_x = 500;
    f.mouse_y = 500;
    TEST_ASSERT(ui_button(&f, 0.0f, 0.0f, 100.0f, 40.0f, &hov) == false);
    TEST_ASSERT(hov == false);
    f.mouse_x = 50;
    f.mouse_y = 20;
    f.mouse_clicked = false;
    TEST_ASSERT(ui_button(&f, 0.0f, 0.0f, 100.0f, 40.0f, NULL) == false);
    TEST_ASSERT(ui_button(NULL, 0.0f, 0.0f, 10.0f, 10.0f, NULL) == false);
    ui_frame_clear(NULL); /* NULL-safe. */
    return failures;
}

/* Test: text field focus, typing, backspace, cap, confirm.
 *
 * Returns: failure count.
 */
int test_ui_text_field(void)
{
    int failures = 0;
    UiFrame f;
    ui_frame_clear(&f);
    char buf[8];
    buf[0] = '\0';
    bool focused = false;

    /* Click inside focuses; typing appends. */
    f.mouse_x = 50;
    f.mouse_y = 10;
    f.mouse_clicked = true;
    TEST_ASSERT(ui_text_field(&f, &focused, buf, sizeof(buf), 0.0f, 0.0f, 200.0f, 30.0f) == false);
    TEST_ASSERT(focused == true);
    f.mouse_clicked = false;
    memcpy(f.text, "ab", 3);
    TEST_ASSERT(ui_text_field(&f, &focused, buf, sizeof(buf), 0.0f, 0.0f, 200.0f, 30.0f) == false);
    TEST_ASSERT(strcmp(buf, "ab") == 0);

    /* Backspace deletes; Return confirms. */
    f.text[0] = '\0';
    f.key_backspace = true;
    ui_text_field(&f, &focused, buf, sizeof(buf), 0.0f, 0.0f, 200.0f, 30.0f);
    TEST_ASSERT(strcmp(buf, "a") == 0);
    f.key_backspace = false;
    f.key_return = true;
    TEST_ASSERT(ui_text_field(&f, &focused, buf, sizeof(buf), 0.0f, 0.0f, 200.0f, 30.0f) == true);
    f.key_return = false;

    /* Click outside unfocuses (typing then ignored). */
    f.mouse_x = 500;
    f.mouse_y = 500;
    f.mouse_clicked = true;
    ui_text_field(&f, &focused, buf, sizeof(buf), 0.0f, 0.0f, 200.0f, 30.0f);
    TEST_ASSERT(focused == false);
    memcpy(f.text, "ZZZ", 4);
    f.mouse_clicked = false;
    ui_text_field(&f, &focused, buf, sizeof(buf), 0.0f, 0.0f, 200.0f, 30.0f);
    TEST_ASSERT(strcmp(buf, "a") == 0);

    /* Capacity respected (buf 8 -> max 7 chars). */
    focused = true;
    f.mouse_clicked = false;
    memset(f.text, 'x', sizeof(f.text) - 1);
    f.text[sizeof(f.text) - 1] = '\0';
    ui_text_field(&f, &focused, buf, sizeof(buf), 0.0f, 0.0f, 200.0f, 30.0f);
    TEST_ASSERT(strlen(buf) <= 7);
    TEST_ASSERT(ui_text_field(NULL, &focused, buf, sizeof(buf), 0, 0, 1, 1) == false);
    return failures;
}

/* Test: slider drag, clamp, release.
 *
 * Returns: failure count.
 */
int test_ui_slider(void)
{
    int failures = 0;
    UiFrame f;
    ui_frame_clear(&f);
    float v = 5.0f;
    bool held = false;

    /* Click grabs; held drag follows mouse-x. */
    f.mouse_x = 50;
    f.mouse_y = 5;
    f.mouse_clicked = true;
    f.mouse_down = true;
    TEST_ASSERT(ui_slider(&f, &v, 0.0f, 10.0f, 0.0f, 0.0f, 100.0f, 20.0f, &held) == true);
    TEST_ASSERT(held == true);
    TEST_ASSERT_FLOAT_EQ(v, 5.0f, 1e-4f);
    f.mouse_clicked = false;
    f.mouse_x = 100;
    TEST_ASSERT(ui_slider(&f, &v, 0.0f, 10.0f, 0.0f, 0.0f, 100.0f, 20.0f, &held) == true);
    TEST_ASSERT_FLOAT_EQ(v, 10.0f, 1e-4f);
    f.mouse_x = -50;
    ui_slider(&f, &v, 0.0f, 10.0f, 0.0f, 0.0f, 100.0f, 20.0f, &held);
    TEST_ASSERT_FLOAT_EQ(v, 0.0f, 1e-4f);

    /* Release ends the drag. */
    f.mouse_down = false;
    TEST_ASSERT(ui_slider(&f, &v, 0.0f, 10.0f, 0.0f, 0.0f, 100.0f, 20.0f, &held) == false);
    TEST_ASSERT(held == false);

    /* Click outside never grabs. */
    f.mouse_x = 500;
    f.mouse_clicked = true;
    f.mouse_down = true;
    v = 3.0f;
    TEST_ASSERT(ui_slider(&f, &v, 0.0f, 10.0f, 0.0f, 0.0f, 100.0f, 20.0f, &held) == false);
    TEST_ASSERT_FLOAT_EQ(v, 3.0f, 1e-4f);
    TEST_ASSERT(ui_clampi(99, 0, 10) == 10);
    TEST_ASSERT(ui_clampi(-5, 0, 10) == 0);
    TEST_ASSERT(ui_slider(NULL, &v, 0, 1, 0, 0, 1, 1, &held) == false);
    return failures;
}

/* Test: icon batch geometry — quad layout, UV orientation (tile top on
 * the screen-top edge), overflow cap, bad-arg safety.
 *
 * Returns: failure count.
 */
int test_ui_icons(void)
{
    int failures = 0;
    IconBatch b;
    icons_clear(NULL); /* Safe. */
    icons_clear(&b);
    TEST_ASSERT(b.quads == 0);
    icons_push(NULL, 0.0f, 0.0f, 16.0f, 3);
    icons_push(&b, 0.0f, 0.0f, 0.0f, 3); /* Zero size dropped. */
    icons_push(&b, 0.0f, 0.0f, -4.0f, 3); /* Negative size dropped. */
    TEST_ASSERT(b.quads == 0);
    icons_push(&b, 10.0f, 20.0f, 16.0f, TILE_STONE);
    TEST_ASSERT(b.quads == 1);
    /* First vert = top-left corner at (x, y). */
    TEST_ASSERT_FLOAT_EQ(b.verts[0], 10.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(b.verts[1], 20.0f, 1e-6f);
    /* Top edge samples v1 (tile top), bottom edge samples v0. */
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    texture_atlas_tile_uv(TILE_STONE, &u0, &v0, &u1, &v1);
    TEST_ASSERT_FLOAT_EQ(b.verts[2], u0, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(b.verts[3], v1, 1e-6f);
    /* Second vert = top-right (u1, v1); third = bottom-left (u0, v0). */
    TEST_ASSERT_FLOAT_EQ(b.verts[4], 26.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(b.verts[5], 20.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(b.verts[6], u1, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(b.verts[7], v1, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(b.verts[8], 10.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(b.verts[9], 36.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(b.verts[10], u0, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(b.verts[11], v0, 1e-6f);
    /* Fill to the cap: extras dropped, count pinned. */
    for (int i = 0; i < ICON_MAX_QUADS + 8; ++i) {
        icons_push(&b, 0.0f, 0.0f, 8.0f, 1);
    }
    TEST_ASSERT(b.quads == ICON_MAX_QUADS);
    icons_clear(&b);
    TEST_ASSERT(b.quads == 0);
    return failures;
}
