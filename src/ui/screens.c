#include "ui/screens.h"
#include "core/app.h"
#include "core/log.h"
#include "core/path.h"
#include "core/seed.h"
#include "core/settings.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/audio.h"
#include "game/mob.h"
#include "game/recipe.h"
#include "game/session.h"
#include "game/survival.h"
#include "game/time_system.h"
#include "platform/lan_discover.h"
#include "platform/window.h"
#include "render/renderer.h"
#include "render/texture_atlas.h"
#include "ui/hud.h"
#include "ui/ui.h"
#include "ui/ui_style.h"
#include "world/block.h"
#include "world/world.h"
#include "world/world_meta.h"
#include "world/world_save.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <stdio.h>
#include <string.h>

/* UI palette + metrics (TerraCraft original theme, dark stone + moss accents). */
#define SCR_BG_R 0.055f
#define SCR_BG_G 0.058f
#define SCR_BG_B 0.075f
#define SCR_BTN_R 0.23f
#define SCR_BTN_G 0.23f
#define SCR_BTN_B 0.25f
#define SCR_BTN_HOV_R 0.33f
#define SCR_BTN_HOV_G 0.33f
#define SCR_BTN_HOV_B 0.36f
#define SCR_ACC_R 0.35f
#define SCR_ACC_G 0.75f
#define SCR_ACC_B 0.35f
#define SCR_TXT_R 1.0f
#define SCR_TXT_G 1.0f
#define SCR_TXT_B 1.0f
#define SCR_DIM_R 0.62f
#define SCR_DIM_G 0.62f
#define SCR_DIM_B 0.65f
#define SCR_WARN_R 1.0f
#define SCR_WARN_G 0.45f
#define SCR_WARN_B 0.40f

#define SCR_BTN_W 320.0f
#define SCR_BTN_H 40.0f
#define SCR_GAP 12.0f
#define SCR_TITLE_SCALE 4.0f
#define SCR_TEXT_SCALE 2.0f
#define SCR_SMALL_SCALE 1.5f

static bool screen_scaled(const AppContext *app)
{
    return app->state != GAME_STATE_PLAYING;
}
static int screen_width(const AppContext *app)
{
    return screen_scaled(app) ? (int)ui_scale_for_drawable(app->width, app->height).canvas_width : app->width;
}
static int screen_height(const AppContext *app)
{
    return screen_scaled(app) ? (int)ui_scale_for_drawable(app->width, app->height).canvas_height : app->height;
}

/* Scratch rect buffer for one screen (stack-friendly, bounded). */
#define SCR_MAX_RECT_VERTS 2048

/* Append one filled quad to a rect buffer (HUD verts, 6 floats each). */
static void srect(float *dst, size_t *n, size_t cap, float x, float y, float w, float h, float r, float g, float b,
                  float a)
{
    if (dst == NULL || n == NULL || *n + 6 > cap || w <= 0.0f || h <= 0.0f) {
        return;
    }
    float *v = dst + (*n) * 6;
    v[0] = x;
    v[1] = y;
    v[2] = r;
    v[3] = g;
    v[4] = b;
    v[5] = a;
    v[6] = x + w;
    v[7] = y;
    v[8] = r;
    v[9] = g;
    v[10] = b;
    v[11] = a;
    v[12] = x;
    v[13] = y + h;
    v[14] = r;
    v[15] = g;
    v[16] = b;
    v[17] = a;
    v[18] = x;
    v[19] = y + h;
    v[20] = r;
    v[21] = g;
    v[22] = b;
    v[23] = a;
    v[24] = x + w;
    v[25] = y;
    v[26] = r;
    v[27] = g;
    v[28] = b;
    v[29] = a;
    v[30] = x + w;
    v[31] = y + h;
    v[32] = r;
    v[33] = g;
    v[34] = b;
    v[35] = a;
    *n += 6;
}

/* Flush accumulated rects in one draw call. */
static void sflush(AppContext *app, float *verts, size_t *n)
{
    if (*n == 0) {
        return;
    }
    renderer_draw_rects(app->renderer, screen_width(app), screen_height(app), verts, *n);
    *n = 0;
}

/* Centered text helper (scale-aware). */
static void stext_c(AppContext *app, const char *s, float cx, float y, float scale, float r, float g, float b)
{
    float w = 0.0f;
    float h = 0.0f;
    renderer_measure_text(app->renderer, s, scale, &w, &h);
    renderer_draw_text(app->renderer, cx - w * 0.5f, y, scale, r, g, b, 1.0f, s);
}

/* Button: background + hover tint + top-edge highlight + centered label.
 * Returns true on click.
 */
static bool sbutton_skin(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, float x, float y, float w,
                         float h, float scale, const char *label, bool enabled)
{
    UiControl c = ui_control(&app->menu.controls, ui, x, y, w, h, enabled);
    if (app->state == GAME_STATE_SETTINGS && c.focused) {
        if (y < 20)
            app->menu.settings_scroll = fmaxf(0, app->menu.settings_scroll + y - 20);
        if (y + h > screen_height(app) - 20)
            app->menu.settings_scroll += y + h - screen_height(app) + 20;
    }
    sflush(app, rects, rn);
    renderer_draw_button(app->renderer, screen_width(app), screen_height(app), x, y, w, h,
                         !enabled                   ? 2
                         : (c.hovered || c.focused) ? 1
                                                    : 0,
                         c.pressed);
    float tw = 0, th = 0;
    renderer_measure_text(app->renderer, label, scale, &tw, &th);
    float tx = x + (w - tw) * .5f, ty = y + (h - th) * .5f + (c.pressed ? 1 : 0);
    renderer_draw_text(app->renderer, tx + scale, ty + scale, scale, .10f, .10f, .10f, 1, label);
    float value = enabled ? 1.0f : .56f;
    renderer_draw_text(app->renderer, tx, ty, scale, value, value, value, 1, label);
    if (c.activated)
        audio_play(&app->audio, AUDIO_UI_CLICK);
    return c.activated;
}
static bool sbutton(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, float x, float y, float w, float h,
                    const char *label, bool enabled)
{
    return sbutton_skin(app, ui, rects, rn, x, y, w, h, SCR_TEXT_SCALE, label, enabled);
}

/* Title block: big title + dim subtitle, returns y below the block. */
static float stitle(AppContext *app, const char *title, const char *sub, float cx, float y)
{
    stext_c(app, title, cx, y, SCR_TITLE_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B);
    y += 8.0f * SCR_TITLE_SCALE + 10.0f;
    if (sub != NULL) {
        stext_c(app, sub, cx, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
        y += 8.0f * SCR_TEXT_SCALE + 18.0f;
    }
    return y;
}

static void smenu_background(AppContext *app, float *rects, size_t *rn, float anim_t);

/* Full-screen moving panorama shared by the title and menu screens. It uses
 * the owner's local Minecraft cube faces when converted; clean checkouts
 * keep the original TerraCraft animated landscape as their fallback.
 */
static void sbackground_frame(AppContext *app, float *rects, size_t *rn, float shade)
{
    float anim_t = (float)(app->last_frame_time - app->start_time);
    renderer_set_clear_color(app->renderer, 0.04f, 0.05f, 0.07f, 1.0f);
    renderer_clear(app->renderer);
    if (!renderer_draw_menu_panorama(app->renderer, app->width, app->height, anim_t)) {
        smenu_background(app, rects, rn, anim_t);
        return;
    }
    if (shade > 0.0f) {
        srect(rects, rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)screen_width(app), (float)screen_height(app), 0.015f,
              0.02f, 0.03f, shade);
        sflush(app, rects, rn);
    }
}

static void sbackground(AppContext *app, float *rects, size_t *rn)
{
    sbackground_frame(app, rects, rn, 0.44f);
}

/* Refresh the cached world list (resets cursor/scroll/confirm). */
void screens_refresh_worlds(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    app->menu.world_count = 0;
    app->menu.select_idx = 0;
    app->menu.select_scroll = 0;
    app->menu.delete_armed = false;
    WorldEntry entries[MINEC_MENU_WORLDS];
    int n = session_list_worlds(SESSION_SAVES_DIR, entries, MINEC_MENU_WORLDS);
    if (n < 0) {
        n = 0;
    }
    for (int i = 0; i < n; ++i) {
        app->menu.worlds[i] = entries[i];
    }
    app->menu.world_count = (size_t)n;
}

/* Refresh discovered packs and resolve the active index. */
void screens_refresh_packs(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    app->menu.pack_count = 0;
    app->menu.pack_idx = 0;
    size_t total = texture_atlas_list_packs(app->menu.packs, MINEC_MENU_PACKS);
    app->menu.pack_count = total < MINEC_MENU_PACKS ? total : MINEC_MENU_PACKS;
    for (size_t i = 0; i < app->menu.pack_count; ++i) {
        if (strcmp(app->menu.packs[i], app->settings.pack) == 0) {
            app->menu.pack_idx = (int)i;
            return;
        }
    }
    /* Current pack vanished: fall back to Default entry. */
    app->menu.pack_idx = 0;
}

/* Copy live settings into slider scratch (call on entering SETTINGS). */
static void settings_to_sliders(AppContext *app)
{
    app->menu.sl_rd = (float)app->settings.render_distance;
    app->menu.sl_sens = app->settings.sensitivity;
    app->menu.sl_fov = app->settings.fov;
    app->menu.sl_vol = (float)app->settings.volume;
    app->menu.sl_sfx = (float)app->settings.sfx_volume;
    app->menu.held_rd = false;
    app->menu.held_sens = false;
    app->menu.held_fov = false;
    app->menu.held_vol = false;
    app->menu.held_sfx = false;
}

/* Commit slider scratch into settings (clamped, unsaved). */
static void sliders_to_settings(AppContext *app)
{
    app->settings.render_distance = ui_clampi((int)(app->menu.sl_rd + 0.5f), 2, 8);
    app->settings.sensitivity = app->menu.sl_sens;
    app->settings.fov = app->menu.sl_fov;
    app->settings.volume = ui_clampi((int)(app->menu.sl_vol + 0.5f), 0, 100);
    app->settings.sfx_volume = ui_clampi((int)(app->menu.sl_sfx + 0.5f), 0, 100);
    settings_clamp(&app->settings);
}

static float smenu_mix(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* The title screen uses a small, deterministic voxel landscape drawn from
 * UI quads. It is intentionally self-contained: no texture allocation,
 * asset dependency, or lifetime to leak. */
static void smenu_tree(float *rects, size_t *rn, float x, float base, float unit)
{
    const float trunk_w = unit * 0.62f;
    const float trunk_h = unit * 3.0f;
    srect(rects, rn, SCR_MAX_RECT_VERTS, x - trunk_w * 0.5f, base - trunk_h, trunk_w, trunk_h, 0.24f, 0.16f, 0.10f,
          1.0f);
    srect(rects, rn, SCR_MAX_RECT_VERTS, x - trunk_w * 0.5f, base - trunk_h, trunk_w * 0.22f, trunk_h, 0.35f, 0.23f,
          0.13f, 1.0f);
    for (int row = 0; row < 4; ++row) {
        int half = row == 0 ? 1 : (row == 3 ? 2 : 3);
        float y = base - trunk_h - unit * (4.0f - (float)row);
        for (int col = -half; col <= half; ++col) {
            float shade = ((row + col + 8) % 3 == 0) ? 0.18f : 0.0f;
            srect(rects, rn, SCR_MAX_RECT_VERTS, x + (float)col * unit - unit * 0.5f, y, unit + 1.0f, unit + 1.0f,
                  0.15f + shade, 0.31f + shade, 0.15f + shade * 0.5f, 1.0f);
        }
    }
}

static void smenu_background(AppContext *app, float *rects, size_t *rn, float anim_t)
{
    const float w = (float)screen_width(app);
    const float h = (float)screen_height(app);
    /* Fine-grained bands/columns: the old 20-band sky + 30-column ridges
     * produced visible ~36-42 px stripes at 720p (the reported menu
     * pixelation). 48 bands + 96 columns keep the same flat-quad style
     * but below the ~15 px perception threshold at 1080p. */
    const int sky_bands = 48;
    renderer_set_clear_color(app->renderer, 0.36f, 0.57f, 0.69f, 1.0f);
    renderer_clear(app->renderer);

    for (int i = 0; i < sky_bands; ++i) {
        float t = (float)i / (float)(sky_bands - 1);
        float y = h * (float)i / (float)sky_bands;
        float bh = h / (float)sky_bands + 1.0f;
        srect(rects, rn, SCR_MAX_RECT_VERTS, 0.0f, y, w, bh, smenu_mix(0.28f, 0.83f, t), smenu_mix(0.49f, 0.55f, t),
              smenu_mix(0.68f, 0.39f, t), 1.0f);
    }

    /* Block clouds and a square sun give the scene depth without borrowing
     * Minecraft's title art or requiring bundled image assets. The whole
     * panorama sways almost imperceptibly (ping-pong, never a seam). */
    float sway = sinf(anim_t * 0.05f) * 20.0f;
    float sun = h * 0.105f;
    float sun_x = w * 0.76f + sway;
    float sun_y = h * 0.19f;
    srect(rects, rn, SCR_MAX_RECT_VERTS, sun_x - sun * 0.5f, sun_y - sun * 0.5f, sun, sun, 1.0f, 0.82f, 0.57f, 1.0f);
    srect(rects, rn, SCR_MAX_RECT_VERTS, sun_x - sun * 0.36f, sun_y - sun * 0.36f, sun * 0.72f, sun * 0.72f, 1.0f,
          0.91f, 0.70f, 1.0f);

    const float cloud_x[3] = {0.13f, 0.47f, 0.84f};
    const float cloud_y[3] = {0.22f, 0.12f, 0.34f};
    const float cloud_s[3] = {0.72f, 0.55f, 0.62f};
    const float cloud_v[3] = {0.004f, 0.007f, 0.003f};
    for (int i = 0; i < 3; ++i) {
        /* Slow drift with wraparound: the sky never sits still. */
        float fx = cloud_x[i] + anim_t * cloud_v[i];
        fx = fmodf(fx, 1.3f) - 0.15f;
        float x = w * fx + sway;
        float y = h * cloud_y[i];
        float unit = fmaxf(5.0f, h * 0.018f) * cloud_s[i];
        srect(rects, rn, SCR_MAX_RECT_VERTS, x, y + unit, unit * 5.0f, unit * 1.2f, 0.73f, 0.78f, 0.76f, 0.9f);
        srect(rects, rn, SCR_MAX_RECT_VERTS, x + unit, y, unit * 2.6f, unit * 1.8f, 0.82f, 0.85f, 0.81f, 0.94f);
        srect(rects, rn, SCR_MAX_RECT_VERTS, x + unit * 3.0f, y + unit * 0.35f, unit * 1.8f, unit * 1.4f, 0.78f, 0.82f,
              0.79f, 0.92f);
    }

    /* Two rolling ridges form the distant terrain silhouette, panning
     * almost imperceptibly so the panorama feels alive. */
    const int ridge_cols = 96;
    float cell = w / (float)ridge_cols;
    float horizon = h * 0.57f;
    for (int i = -2; i <= ridge_cols + 2; ++i) {
        float f = (float)i / (float)ridge_cols + anim_t * 0.002f;
        float rise =
            0.035f + 0.045f * (0.5f + 0.5f * sinf(f * 18.0f + 0.4f)) + 0.025f * (0.5f + 0.5f * sinf(f * 37.0f));
        float y = horizon - h * rise;
        srect(rects, rn, SCR_MAX_RECT_VERTS, (float)i * cell + sway, y, cell + 1.0f, h - y, 0.26f, 0.39f, 0.25f, 1.0f);
    }
    for (int i = -2; i <= ridge_cols + 2; ++i) {
        float f = (float)i / (float)ridge_cols + anim_t * 0.003f;
        float rise =
            0.018f + 0.030f * (0.5f + 0.5f * sinf(f * 13.0f + 1.7f)) + 0.020f * (0.5f + 0.5f * sinf(f * 29.0f + 0.9f));
        float y = h * 0.63f - h * rise;
        srect(rects, rn, SCR_MAX_RECT_VERTS, (float)i * cell + sway, y, cell + 1.0f, h - y, 0.21f, 0.34f, 0.19f, 1.0f);
    }

    /* Foreground terraces and sparse pixel patches suggest block-scale
     * terrain while leaving the menu controls easy to read. Finer grid
     * (26x8 vs 13x5) so patches read as texture, not blocks. */
    float ground = h * 0.68f;
    srect(rects, rn, SCR_MAX_RECT_VERTS, 0.0f, ground, w, h - ground, 0.20f, 0.31f, 0.15f, 1.0f);
    float patch_w = fmaxf(10.0f, w / 30.0f);
    float patch_h = fmaxf(6.0f, h / 32.0f);
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 26; ++col) {
            if (((row * 7 + col * 3) % 4) == 0) {
                float x = (float)col * w / 26.0f + (row % 2 ? patch_w * 0.3f : 0.0f);
                float y = ground + (float)row * patch_h * 1.4f;
                if (x < w && y < h) {
                    float tint = (float)((row + col) % 3) * 0.018f;
                    srect(rects, rn, SCR_MAX_RECT_VERTS, x, y, patch_w, patch_h, 0.21f + tint, 0.325f + tint,
                          0.155f + tint, 0.55f);
                }
            }
        }
    }

    float tree_unit = fmaxf(6.0f, h * 0.022f);
    smenu_tree(rects, rn, w * 0.18f + sway, h * 0.67f, tree_unit);
    smenu_tree(rects, rn, w * 0.89f + sway, h * 0.68f, tree_unit * 1.15f);
    smenu_tree(rects, rn, w * 0.96f + sway, h * 0.70f, tree_unit * 0.78f);

    /* Rising motes catch the light (deterministic per index + time:
     * no per-frame allocation, no RNG state). */
    for (int m = 0; m < 24; ++m) {
        float mx = fmodf((float)m * 0.377f + anim_t * 0.004f * (1.0f + (float)(m % 3) * 0.4f), 1.0f);
        float my = 1.0f - fmodf((float)m * 0.613f + anim_t * 0.016f * (1.0f + (float)(m % 5) * 0.2f), 1.0f);
        float tw = 0.5f + 0.5f * sinf(anim_t * 3.0f + (float)m * 1.7f);
        float ms = 2.0f + (float)(m % 3);
        srect(rects, rn, SCR_MAX_RECT_VERTS, mx * w, my * h, ms, ms, 1.0f, 0.95f, 0.75f, 0.10f + 0.25f * tw);
    }

    /* A restrained veil keeps white menu text legible over both sky and land. */
    srect(rects, rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, w, h, 0.025f, 0.035f, 0.045f, 0.34f);
    sflush(app, rects, rn);
}

static bool smenu_button(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, float x, float y, float w,
                         float h, float text_scale, const char *label, bool focused)
{
    (void)focused;
    return sbutton_skin(app, ui, rects, rn, x, y, w, h, text_scale, label, true);
}

/* Java Edition-style title screen. Every visible button has a live action;
 * the player's profile fills the third primary row used by online services
 * in the reference layout. */
static void screen_main_menu(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)screen_width(app) * 0.5f;
    float width = (float)screen_width(app);
    float height = (float)screen_height(app);
    float gui = fminf(width / 1280.0f, height / 720.0f);
    gui = fmaxf(0.63f, fminf(gui, 1.50f));
    float button_w = fminf(400.0f * gui, width - 36.0f * gui);
    float button_h = 40.0f * gui;
    float gap = 8.0f * gui;
    float button_x = cx - button_w * 0.5f;
    float menu_y = height * 0.438f;
    float bottom_gap = 28.0f * gui;
    float label_scale = 2.0f * gui;
    float title_scale = 9.0f * gui;
    float title_y = fmaxf(18.0f * gui, height * 0.078f);

    sbackground_frame(app, rects, &rn, 0.0f);

    const char *logo = "TERRACRAFT";
    float title_w = 0.0f, title_h = 0.0f;
    renderer_measure_text(app->renderer, logo, title_scale, &title_w, &title_h);
    float max_logo_w = width * 0.68f;
    if (title_w > max_logo_w) {
        title_scale *= max_logo_w / title_w;
        renderer_measure_text(app->renderer, logo, title_scale, &title_w, &title_h);
    }
    float wall = (float)(app->last_frame_time - app->start_time);
    float title_x = cx - title_w * 0.5f;
    float edge = fmaxf(2.0f, 3.0f * gui);
    /* Chunky shadow and bright top pass approximate the beveled Java wordmark
     * while retaining TerraCraft's original name and assets. */
    renderer_draw_text(app->renderer, title_x + edge, title_y + edge * 2.0f, title_scale, 0.04f, 0.04f, 0.04f, 1.0f,
                       logo);
    renderer_draw_text(app->renderer, title_x - edge, title_y + edge, title_scale, 0.18f, 0.18f, 0.18f, 1.0f, logo);
    renderer_draw_text(app->renderer, title_x + edge, title_y + edge, title_scale, 0.22f, 0.22f, 0.22f, 1.0f, logo);
    renderer_draw_text(app->renderer, title_x, title_y, title_scale, 0.78f, 0.78f, 0.78f, 1.0f, logo);
    renderer_draw_text(app->renderer, title_x, title_y - gui, title_scale, 0.96f, 0.96f, 0.96f, 0.72f, logo);
    stext_c(app, "C EDITION", cx + gui, title_y + title_h - 3.0f * gui, 2.0f * gui, 0.16f, 0.16f, 0.16f);
    stext_c(app, "C EDITION", cx, title_y + title_h - 5.0f * gui, 2.0f * gui, 0.92f, 0.92f, 0.92f);

    static const char *const splashes[] = {
        "100% Blocks!",       "Now With LAN!",      "Punch Trees!",      "Also Try F5!",
        "Runs At 60 TPS!",    "Hand-Baked Pixels!", "As Seen On Wi-Fi!", "No Placeholders!",
        "Dial Up Your Dirt!", "Creeper-Free Zone!", "Steve Approved!",
    };
    size_t splash_n = sizeof(splashes) / sizeof(splashes[0]);
    const char *splash = splashes[(size_t)(app->start_time * 1000.0) % splash_n];
    float pulse = (1.85f + 0.12f * sinf(wall * 3.2f)) * gui;
    float splash_w = 0.0f;
    renderer_measure_text(app->renderer, splash, pulse, &splash_w, NULL);
    float splash_x = title_x + title_w * 0.70f;
    float splash_y = title_y + title_h * 0.86f;
    float char_x = 0.0f;
    for (size_t ci = 0; splash[ci] != '\0'; ++ci) {
        char glyph[2] = {(char)splash[ci], '\0'};
        float char_w = 0.0f;
        renderer_measure_text(app->renderer, glyph, pulse, &char_w, NULL);
        float gx = splash_x + char_x - splash_w * 0.10f;
        float gy = splash_y - char_x * 0.27f;
        renderer_draw_text(app->renderer, gx + gui, gy + gui, pulse, 0.12f, 0.12f, 0.02f, 1.0f, glyph);
        renderer_draw_text(app->renderer, gx, gy, pulse, 1.0f, 1.0f, 0.16f, 1.0f, glyph);
        char_x += char_w;
    }

    char profile_label[64];
    snprintf(profile_label, sizeof(profile_label), "Player: %.24s", app->username);
    float y = menu_y;
    bool single = smenu_button(app, ui, rects, &rn, button_x, y, button_w, button_h, label_scale, "Singleplayer",
                               app->menu.main_focus == 0);
    y += button_h + gap;
    bool multiplayer = smenu_button(app, ui, rects, &rn, button_x, y, button_w, button_h, label_scale,
                                    "Multiplayer (LAN)", app->menu.main_focus == 1);
    y += button_h + gap;
    bool profile = smenu_button(app, ui, rects, &rn, button_x, y, button_w, button_h, label_scale, profile_label,
                                app->menu.main_focus == 2);
    y += button_h + bottom_gap;
    float small_gap = 8.0f * gui;
    float small_w = (button_w - small_gap) * 0.5f;
    bool options = smenu_button(app, ui, rects, &rn, button_x, y, small_w, button_h, label_scale, "Options...",
                                app->menu.main_focus == 3);
    bool quit = smenu_button(app, ui, rects, &rn, button_x + small_w + small_gap, y, small_w, button_h, label_scale,
                             "Quit Game", app->menu.main_focus == 4);

    if (single) {
        app_enter_state(app, GAME_STATE_WORLD_SELECT);
        return;
    }
    if (multiplayer) {
        app->menu.lan_address_buf[0] = '\0';
        app->menu.error[0] = '\0';
        app_enter_state(app, GAME_STATE_LAN_MENU);
        return;
    }
    if (profile) {
        snprintf(app->menu.profile_buf, sizeof(app->menu.profile_buf), "%s", app->username);
        app->menu.name_focused = true;
        app->menu.error[0] = '\0';
        app_enter_state(app, GAME_STATE_PROFILE);
        return;
    }
    if (options) {
        app->menu.settings_return = GAME_STATE_MAIN_MENU;
        settings_to_sliders(app);
        screens_refresh_packs(app);
        app_enter_state(app, GAME_STATE_SETTINGS);
        return;
    }
    if (quit) {
        app_enter_state(app, GAME_STATE_QUIT);
        return;
    }
    if (ui->key_escape) {
        app_enter_state(app, GAME_STATE_QUIT);
    }

    float footer_scale = fmaxf(0.85f, 1.5f * gui);
    float footer_h = 0.0f;
    renderer_measure_text(app->renderer, "TerraCraft M9 (v0.9.0)", footer_scale, NULL, &footer_h);
    renderer_draw_text(app->renderer, 4.0f * gui, height - footer_h - 2.0f * gui, footer_scale, 0.10f, 0.10f, 0.10f,
                       1.0f, "TerraCraft M9 (v0.9.0)");
    renderer_draw_text(app->renderer, 3.0f * gui, height - footer_h - 3.0f * gui, footer_scale, 1.0f, 1.0f, 1.0f, 1.0f,
                       "TerraCraft M9 (v0.9.0)");
    {
        const char *note = "Copyright TerraCraft contributors. Not affiliated with Mojang.";
        float note_w = 0.0f, note_h = 0.0f;
        renderer_measure_text(app->renderer, note, footer_scale, &note_w, &note_h);
        renderer_draw_text(app->renderer, width - note_w - 2.0f * gui, height - note_h - 2.0f * gui, footer_scale,
                           0.10f, 0.10f, 0.10f, 1.0f, note);
        renderer_draw_text(app->renderer, width - note_w - 3.0f * gui, height - note_h - 3.0f * gui, footer_scale, 1.0f,
                           1.0f, 1.0f, 1.0f, note);
    }
}

/* Format a world row subtitle: "Seed 123456 - Survival - <played>". */
static void world_subtitle(const WorldEntry *e, char *out, size_t cap)
{
    const char *mode = e->mode == 1 ? "Creative" : "Survival";
    if (e->played > 0) {
        time_t played = (time_t)e->played;
        struct tm *date = gmtime(&played);
        char formatted[20] = {0};
        if (date)
            (void)strftime(formatted, sizeof(formatted), "%Y-%m-%d", date);
        snprintf(out, cap, "%s - Seed %lld%s%s", mode, (long long)e->seed, formatted[0] ? " - " : "", formatted);
    } else {
        snprintf(out, cap, "Seed %lld - %s", (long long)e->seed, mode);
    }
}

/* World select: cached list + Play/Create/Delete/Back. */
static void screen_world_select(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)screen_width(app) * 0.5f;
    sbackground(app, rects, &rn);
    float list_w = fminf(660.0f, (float)screen_width(app) - 48.0f);
    float list_x = cx - list_w * 0.5f;
    float y = 64.0f;
    y = stitle(app, "Select World", NULL, cx, y);

    /* Keyboard navigation edges. */
    if (ui->key_down) {
        app->menu.select_idx++;
        app->menu.delete_armed = false;
    }
    if (ui->key_up) {
        app->menu.select_idx--;
        app->menu.delete_armed = false;
    }
    int count = (int)app->menu.world_count;
    if (ui->wheel) {
        app->menu.select_idx -= ui->wheel * 3;
        app->menu.delete_armed = false;
    }
    if (count > 0) {
        app->menu.select_idx = ui_clampi(app->menu.select_idx, 0, count - 1);
    } else {
        app->menu.select_idx = 0;
    }

    /* List box geometry: rows of 56px, visible count from space. */
    float list_y = y;
    float btn_y = (float)screen_height(app) - 4.0f * (SCR_BTN_H * 0.72f + 10.0f) - 20.0f;
    float row_h = 56.0f;
    int visible = (int)((btn_y - list_y - 8.0f) / row_h);
    if (visible < 1) {
        visible = 1;
    }
    if (visible > 12) {
        visible = 12;
    }
    if (app->menu.select_idx < app->menu.select_scroll) {
        app->menu.select_scroll = app->menu.select_idx;
    }
    if (app->menu.select_idx >= app->menu.select_scroll + visible) {
        app->menu.select_scroll = app->menu.select_idx - visible + 1;
    }
    if (count == 0) {
        stext_c(app, "No worlds yet - create one!", cx, list_y + 20.0f, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G,
                SCR_DIM_B);
    }
    for (int r = 0; r < visible; ++r) {
        int idx = app->menu.select_scroll + r;
        if (idx >= count) {
            break;
        }
        float ry = list_y + (float)r * row_h;
        bool sel = idx == app->menu.select_idx;
        bool hov = false;
        bool clicked = ui_button(ui, list_x, ry, list_w, row_h - 6.0f, &hov);
        /* Repaint the row manually: selection accent + two text lines. */
        if (sel) {
            srect(rects, &rn, SCR_MAX_RECT_VERTS, list_x - 2, ry - 2, list_w + 4, row_h, .68f, .68f, .68f, 1);
            srect(rects, &rn, SCR_MAX_RECT_VERTS, list_x, ry, list_w, row_h - 4, .05f, .05f, .05f, .88f);
        } else if (hov) {
            srect(rects, &rn, SCR_MAX_RECT_VERTS, list_x - 3.0f, ry - 3.0f, list_w + 6.0f, row_h, 0.20f, 0.20f, 0.22f,
                  1.0f);
        }
        if (clicked) {
            app->menu.select_idx = idx;
            app->menu.delete_armed = false;
        }
        sflush(app, rects, &rn);
        renderer_draw_text(app->renderer, list_x + 10.0f, ry + 6.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B,
                           1.0f, app->menu.worlds[idx].name);
        char sub[128];
        world_subtitle(&app->menu.worlds[idx], sub, sizeof(sub));
        renderer_draw_text(app->renderer, list_x + 10.0f, ry + 30.0f, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B,
                           1.0f, sub);
    }

    /* Bottom action buttons (compact height). */
    float bw = SCR_BTN_W;
    float bh = SCR_BTN_H * 0.72f;
    float bx = cx - bw * 0.5f;
    float by = btn_y;
    bool has = count > 0;
    if (sbutton(app, ui, rects, &rn, bx, by, bw, bh, "Play Selected World", has)) {
        char dir[512];
        path_join(dir, sizeof(dir), SESSION_SAVES_DIR, app->menu.worlds[app->menu.select_idx].dirname);
        if (session_open_world(app, dir) == 0) {
            app_enter_state(app, GAME_STATE_LOADING);
            return;
        }
        snprintf(app->menu.error, sizeof(app->menu.error), "Could not open world (see log).");
    }
    by += bh + 10.0f;
    if (sbutton(app, ui, rects, &rn, bx, by, bw, bh, "Create New World", true)) {
        app->menu.name_buf[0] = '\0';
        app->menu.seed_buf[0] = '\0';
        app->menu.create_mode = 0;
        game_rules_defaults(&app->menu.create_rules);
        app->menu.create_show_rules = false;
        app->menu.name_focused = true;
        app->menu.seed_focused = false;
        app->menu.error[0] = '\0';
        app_enter_state(app, GAME_STATE_CREATE_WORLD);
        return;
    }
    by += bh + 10.0f;
    const char *del_label = app->menu.delete_armed ? "Click again to confirm" : "Delete";
    if (sbutton(app, ui, rects, &rn, bx, by, bw, bh, del_label, has)) {
        if (!app->menu.delete_armed) {
            app->menu.delete_armed = true;
        } else {
            char dir[512];
            path_join(dir, sizeof(dir), SESSION_SAVES_DIR, app->menu.worlds[app->menu.select_idx].dirname);
            if (world_save_delete(dir) == 0) {
                LOG_INFO("screens: deleted world '%s'", app->menu.worlds[app->menu.select_idx].name);
            } else {
                snprintf(app->menu.error, sizeof(app->menu.error), "Delete failed (see log).");
            }
            screens_refresh_worlds(app);
        }
    }
    by += bh + 10.0f;
    if (sbutton(app, ui, rects, &rn, bx, by, bw, bh, "Back", true)) {
        app_enter_state(app, GAME_STATE_MAIN_MENU);
        return;
    }
    sflush(app, rects, &rn);
    if (app->menu.error[0] != '\0') {
        stext_c(app, app->menu.error, cx, by + bh + 12.0f, SCR_TEXT_SCALE, SCR_WARN_R, SCR_WARN_G, SCR_WARN_B);
    }
    if (ui->key_escape) {
        app_enter_state(app, GAME_STATE_MAIN_MENU);
        return;
    }
    if (ui->key_return && app->menu.controls.focus < 0 && has) {
        /* Keyboard shortcut mirrors the Play button. */
        char dir[512];
        path_join(dir, sizeof(dir), SESSION_SAVES_DIR, app->menu.worlds[app->menu.select_idx].dirname);
        if (session_open_world(app, dir) == 0) {
            app_enter_state(app, GAME_STATE_LOADING);
        }
    }
}

/* Labeled text field row: label above, box below. Returns confirm edge. */
static bool sfield(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, const char *label, char *buf,
                   size_t cap, bool *focused, float x, float y, float w)
{
    renderer_draw_text(app->renderer, x, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f, label);
    float fy = y + 22.0f;
    float fh = 34.0f;
    UiControl control = ui_control(&app->menu.controls, ui, x, fy, w, fh, true);
    if (focused && app->menu.controls.focus >= 0)
        *focused = control.focused;
    bool confirm = ui_text_field(ui, focused, buf, cap, x, fy, w, fh);
    bool foc = focused != NULL && *focused;
    float border = foc ? 1.0f : .50f;
    srect(rects, rn, SCR_MAX_RECT_VERTS, x, fy, w, fh, border, border, border, 1);
    srect(rects, rn, SCR_MAX_RECT_VERTS, x + 2, fy + 2, w - 4, fh - 4, .015f, .015f, .015f, 1);
    sflush(app, rects, rn);
    char shown[96];
    snprintf(shown, sizeof(shown), "%s%s", buf, (foc ? "_" : ""));
    char *visible = shown;
    float shown_width = 0;
    renderer_measure_text(app->renderer, visible, SCR_TEXT_SCALE, &shown_width, NULL);
    while (*visible && shown_width > w - 16) {
        ++visible;
        renderer_measure_text(app->renderer, visible, SCR_TEXT_SCALE, &shown_width, NULL);
    }
    renderer_draw_text(app->renderer, x + 8.0f, fy + 9.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B, 1.0f,
                       visible);
    return confirm;
}

/* Required first-run profile screen. The user may choose a valid account
 * name or leave it blank to receive the generated local name. */
static void screen_profile(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)screen_width(app) * 0.5f;
    sbackground(app, rects, &rn);
    float y = (float)screen_height(app) * 0.24f;
    y = stitle(app, "Choose a Username", "This name appears to other players on your LAN", cx, y);
    float fw = SCR_BTN_W + 80.0f;
    float fx = cx - fw * 0.5f;
    bool confirm = sfield(app, ui, rects, &rn, "Username (3-24 letters, numbers, underscore)", app->menu.profile_buf,
                          sizeof(app->menu.profile_buf), &app->menu.name_focused, fx, y + 28.0f, fw);
    y += 112.0f;
    if (sbutton(app, ui, rects, &rn, fx, y, fw, SCR_BTN_H, "Continue", true) || confirm) {
        (void)app_commit_profile(app);
        return;
    }
    sflush(app, rects, &rn);
    if (app->menu.error[0] != '\0') {
        stext_c(app, app->menu.error, cx, y + SCR_BTN_H + 14.0f, SCR_TEXT_SCALE, SCR_WARN_R, SCR_WARN_G, SCR_WARN_B);
    } else if (app->menu.profile_buf[0] == '\0') {
        stext_c(app, "Leave blank and TerraCraft will generate a name for you.", cx, y + SCR_BTN_H + 14.0f,
                SCR_SMALL_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
    }
    if (ui->key_escape && app->username[0] != '\0') {
        app->menu.error[0] = '\0';
        app_enter_state(app, GAME_STATE_MAIN_MENU);
    }
}

/* LAN worlds found automatically on the local network. Hosts announce
 * themselves (see the pause-menu share option); this screen lists live
 * beacons so players pick a world instead of typing an address. */
static void screen_lan_menu(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)screen_width(app) * 0.5f;
    sbackground(app, rects, &rn);

    /* Pump discovery on wall-clock time (menus have no sim tick). */
    if (app->lan_discover != NULL) {
        double now = app->last_frame_time;
        float dt = (float)(now - app->menu.lan_poll_at);
        if (!(dt >= 0.0f) || dt > 1.0f) {
            dt = 0.0f;
        }
        app->menu.lan_poll_at = now;
        lan_discover_poll(app->lan_discover, dt);
    }
    size_t count = lan_discover_count(app->lan_discover);
    if (count > 0 && (app->menu.lan_server_idx < 0 || (size_t)app->menu.lan_server_idx >= count)) {
        app->menu.lan_server_idx = 0;
    }
    if (count == 0) {
        app->menu.lan_server_idx = -1;
    }

    /* Keyboard navigation edges. */
    if (ui->key_down && count > 0) {
        app->menu.lan_server_idx++;
    }
    if (ui->key_up && count > 0) {
        app->menu.lan_server_idx--;
    }
    if (count > 0) {
        app->menu.lan_server_idx = ui_clampi(app->menu.lan_server_idx, 0, (int)count - 1);
    }

    float y = (float)screen_height(app) * 0.10f;
    y = stitle(app, "Play Multiplayer", NULL, cx, y);

    /* Status line with animated ellipsis while fresh beacons may arrive. */
    {
        char status[96];
        int dots = (int)(app->last_frame_time * 2.0) % 4;
        if (app->lan_discover == NULL) {
            snprintf(status, sizeof(status), "Auto-scan unavailable");
        } else if (count == 0) {
            snprintf(status, sizeof(status), "Scanning for worlds on your network%.*s", dots, "...");
        } else {
            snprintf(status, sizeof(status), "%u world%s found on your network", (unsigned)count,
                     count == 1 ? "" : "s");
        }
        stext_c(app, status, cx, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
        y += 8.0f * SCR_TEXT_SCALE + 12.0f;
    }

    /* Server rows (name + headcount, then address below). Fixed 5
     * visible rows, scroll follows selection. */
    float list_w = SCR_BTN_W + 120.0f;
    float list_x = cx - list_w * 0.5f;
    float row_h = 52.0f;
    int visible = 5;
    float list_y = y;
    int scroll = 0;
    if (count > 0) {
        scroll = app->menu.lan_server_idx - visible + 1;
        if (scroll < 0) {
            scroll = 0;
        }
        if ((size_t)scroll > count - 1u) {
            scroll = (int)count - 1;
        }
    }
    if (count == 0) {
        stext_c(app, "No worlds yet - open a world to LAN on another computer!", cx, list_y + 20.0f, SCR_SMALL_SCALE,
                SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
    }
    for (int r = 0; r < visible; ++r) {
        size_t idx = (size_t)(scroll + r);
        if (idx >= count) {
            break;
        }
        const LanServerInfo *info = lan_discover_at(app->lan_discover, idx);
        if (info == NULL) {
            break;
        }
        float ry = list_y + (float)r * row_h;
        bool sel = (int)idx == app->menu.lan_server_idx;
        bool hov = false;
        bool clicked = ui_button(ui, list_x, ry, list_w, row_h - 6.0f, &hov);
        if (sel) {
            srect(rects, &rn, SCR_MAX_RECT_VERTS, list_x - 3.0f, ry - 3.0f, list_w + 6.0f, row_h, SCR_ACC_R * 0.35f,
                  SCR_ACC_G * 0.35f, SCR_ACC_B * 0.35f, 1.0f);
        } else if (hov) {
            srect(rects, &rn, SCR_MAX_RECT_VERTS, list_x - 3.0f, ry - 3.0f, list_w + 6.0f, row_h, 0.20f, 0.20f, 0.22f,
                  1.0f);
        }
        if (clicked) {
            app->menu.lan_server_idx = (int)idx;
        }
        sflush(app, rects, &rn);
        char head[128];
        snprintf(head, sizeof(head), "%s  %u/%u", info->name, (unsigned)info->players, (unsigned)info->capacity);
        renderer_draw_text(app->renderer, list_x + 10.0f, ry + 5.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B,
                           1.0f, head);
        char sub[128];
        snprintf(sub, sizeof(sub), "%s:%u", info->address, (unsigned)info->port);
        renderer_draw_text(app->renderer, list_x + 10.0f, ry + 29.0f, SCR_SMALL_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B,
                           1.0f, sub);
    }
    y = list_y + (float)visible * row_h + 10.0f;
    /* Action row: join, rescan, direct fallback, back. */
    float bw = (list_w - 18.0f) * 0.5f;
    float bh = SCR_BTN_H * 0.72f;
    bool has = count > 0 && app->menu.lan_server_idx >= 0 && !app->lan_join_pending;
    if (sbutton(app, ui, rects, &rn, list_x, y, bw, bh, app->lan_join_pending ? "Connecting..." : "Join Selected World",
                has)) {
        const LanServerInfo *join_info = lan_discover_at(app->lan_discover, (size_t)app->menu.lan_server_idx);
        if (join_info != NULL && app_lan_join_endpoint(app, join_info->address, join_info->port)) {
            app->menu.error[0] = '\0';
        } else if (app->menu.error[0] == '\0') {
            snprintf(app->menu.error, sizeof(app->menu.error), "Could not join that world.");
        }
    }
    if (sbutton(app, ui, rects, &rn, list_x + bw + 18.0f, y, bw, bh, "Scan Again", true)) {
        if (app->lan_discover != NULL) {
            lan_discover_scan(app->lan_discover);
        }
        app->menu.lan_server_idx = count > 0 ? 0 : -1;
    }
    y += bh + 10.0f;
    if (sbutton(app, ui, rects, &rn, list_x, y, bw, bh,
                app->menu.lan_show_direct ? "Hide Direct Connect" : "Direct Connect", true)) {
        app->menu.lan_show_direct = !app->menu.lan_show_direct;
    }
    bool back =
        sbutton(app, ui, rects, &rn, list_x + bw + 18.0f, y, bw, bh, app->lan_join_pending ? "Cancel" : "Back", true);
    y += bh + 10.0f;
    if (app->menu.lan_show_direct) {
        float fw = list_w;
        app->menu.name_focused = true;
        bool confirm = sfield(app, ui, rects, &rn, "Host IPv4 (e.g. 192.168.1.20)", app->menu.lan_address_buf,
                              sizeof(app->menu.lan_address_buf), &app->menu.name_focused, list_x, y, fw);
        y += 70.0f;
        bool join = sbutton(app, ui, rects, &rn, list_x, y, fw, bh,
                            app->lan_join_pending ? "Connecting..." : "Join Address", !app->lan_join_pending);
        if (join || (confirm && !app->lan_join_pending)) {
            if (app_lan_join(app, app->menu.lan_address_buf)) {
                app->menu.error[0] = '\0';
            } else if (app->menu.error[0] == '\0') {
                snprintf(app->menu.error, sizeof(app->menu.error), "Could not start LAN connection.");
            }
        }
        y += bh + 10.0f;
    }
    sflush(app, rects, &rn);
    if (app->menu.error[0] != '\0') {
        stext_c(app, app->menu.error, cx, y, SCR_SMALL_SCALE, SCR_WARN_R, SCR_WARN_G, SCR_WARN_B);
    }
    if (back || ui->key_escape) {
        bool was_connecting = app->lan_join_pending;
        app_lan_disconnect(app);
        if (was_connecting) {
            snprintf(app->menu.error, sizeof(app->menu.error), "LAN connection cancelled.");
        } else {
            app_enter_state(app, GAME_STATE_MAIN_MENU);
        }
    }
}

/* One game-rule toggle row: label left, ON/OFF button right, dim
 * description below. Compact (fits 7 rules + actions at 720p). When
 * editable is false the button renders disabled and never toggles
 * (guest view of host-owned rules). */
static void srule_row_ex(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, const char *label,
                         const char *desc, bool *value, bool editable, float x, float y, float w)
{
    renderer_draw_text(app->renderer, x, y + 4.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B, 1.0f, label);
    char state[8];
    snprintf(state, sizeof(state), "%s", *value ? "ON" : "OFF");
    if (sbutton(app, ui, rects, rn, x + w - 118.0f, y - 4.0f, 118.0f, 32.0f, state, editable)) {
        *value = !*value;
    }
    renderer_draw_text(app->renderer, x, y + 34.0f, SCR_SMALL_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f, desc);
}

static void srule_row(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, const char *label, const char *desc,
                      bool *value, float x, float y, float w)
{
    srule_row_ex(app, ui, rects, rn, label, desc, value, true, x, y, w);
}

static void screen_create_world_commit(AppContext *app)
{
    char dir[512];
    int rc = session_create_world_ex(SESSION_SAVES_DIR, app->menu.name_buf, app->menu.seed_buf, app->menu.create_mode,
                                     &app->menu.create_rules, dir);
    if (rc != 0) {
        snprintf(app->menu.error, sizeof(app->menu.error), "Could not create world (see log).");
        return;
    }
    window_text_input(false);
    screens_refresh_worlds(app);
    if (session_open_world(app, dir) == 0) {
        app_enter_state(app, GAME_STATE_LOADING);
    } else {
        snprintf(app->menu.error, sizeof(app->menu.error), "Could not open world (see log).");
    }
}

/* Create-world form: name, seed, mode on page one; Game Rules / Cheats on
 * page two (expandable section, not crammed onto one screen). Pending
 * values live in menu.create_* until Create (stored) or Cancel (dropped). */
static void screen_create_world(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)screen_width(app) * 0.5f;
    sbackground(app, rects, &rn);
    float y = 40.0f;
    float fw = SCR_BTN_W + 80.0f;
    /* Wider rules page keeps labels + ON/OFF on one line at small widths. */
    if (app->menu.create_show_rules && fw < 520.0f) {
        fw = 520.0f;
        if (fw > (float)screen_width(app) - 24.0f) {
            fw = (float)screen_width(app) - 24.0f;
        }
    }
    float fx = cx - fw * 0.5f;

    if (!app->menu.create_show_rules) {
        y = stitle(app, "Create New World", NULL, cx, y);

        bool confirm = sfield(app, ui, rects, &rn, "World Name", app->menu.name_buf, sizeof(app->menu.name_buf),
                              &app->menu.name_focused, fx, y, fw);
        y += 22.0f + 34.0f + 14.0f;
        confirm = sfield(app, ui, rects, &rn, "Seed (blank = random)", app->menu.seed_buf, sizeof(app->menu.seed_buf),
                         &app->menu.seed_focused, fx, y, fw) ||
                  confirm;
        y += 22.0f + 34.0f + 14.0f;

        /* Seed preview line (parsed live, never writes anything). */
        {
            bool blank = false;
            long seed = seed_parse(app->menu.seed_buf, &blank);
            char prev[96];
            if (blank) {
                snprintf(prev, sizeof(prev), "Seed: <random on create>");
            } else {
                snprintf(prev, sizeof(prev), "Seed: %ld", seed);
            }
            renderer_draw_text(app->renderer, fx, y, SCR_SMALL_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f, prev);
            y += 22.0f;
        }

        /* Mode cycle button. */
        renderer_draw_text(app->renderer, fx, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f, "Game Mode");
        y += 22.0f;
        const char *mode_label = app->menu.create_mode == 1 ? "Mode: Creative" : "Mode: Survival";
        if (sbutton(app, ui, rects, &rn, fx, y, fw, 40.0f, mode_label, true)) {
            app->menu.create_mode = (app->menu.create_mode == 1) ? 0 : 1;
        }
        y += 40.0f + 14.0f;

        if (sbutton(app, ui, rects, &rn, fx, y, fw, 36.0f, "Game Rules / Cheats...", true)) {
            app->menu.create_show_rules = true;
            app->menu.name_focused = false;
            app->menu.seed_focused = false;
            window_text_input(false);
        }
        y += 36.0f + 14.0f;

        bool do_create = sbutton(app, ui, rects, &rn, fx, y, (fw - 12.0f) * 0.5f, SCR_BTN_H, "Create World", true);
        bool do_cancel =
            sbutton(app, ui, rects, &rn, fx + (fw + 12.0f) * 0.5f, y, (fw - 12.0f) * 0.5f, SCR_BTN_H, "Cancel", true);
        sflush(app, rects, &rn);
        if (app->menu.error[0] != '\0') {
            stext_c(app, app->menu.error, cx, y + SCR_BTN_H + 12.0f, SCR_TEXT_SCALE, SCR_WARN_R, SCR_WARN_G,
                    SCR_WARN_B);
        }
        if (do_cancel || ui->key_escape) {
            window_text_input(false);
            app->menu.create_show_rules = false;
            app_enter_state(app, GAME_STATE_WORLD_SELECT);
            return;
        }
        if (do_create || confirm) {
            screen_create_world_commit(app);
        }
        return;
    }

    /* Rules page: clearly grouped cheats with descriptions. */
    y = stitle(app, "Create New World", "Game Rules - stored in world.meta, host-authoritative", cx, y);
    GameRules *r = &app->menu.create_rules;
    srule_row(app, ui, rects, &rn, "Allow Cheats", "Unlocks debug give/spawn keys (debug builds).", &r->allow_cheats,
              fx, y, fw);
    y += 52.0f;
    srule_row(app, ui, rects, &rn, "Keep Inventory", "Keep hotbar + inventory + held item through death.",
              &r->keep_inventory, fx, y, fw);
    y += 52.0f;
    srule_row(app, ui, rects, &rn, "Player vs Player", "Players can damage each other in multiplayer.", &r->pvp, fx, y,
              fw);
    y += 52.0f;
    srule_row(app, ui, rects, &rn, "Mob Spawning", "New creatures spawn over time.", &r->mob_spawning, fx, y, fw);
    y += 52.0f;
    srule_row(app, ui, rects, &rn, "Daylight Cycle", "The sun/moon clock advances.", &r->daylight_cycle, fx, y, fw);
    y += 52.0f;
    srule_row(app, ui, rects, &rn, "Natural Regeneration", "Heal over time at high hunger (starve still hurts).",
              &r->natural_regen, fx, y, fw);
    y += 52.0f;
    srule_row(app, ui, rects, &rn, "Fall Damage", "Falling more than 3 blocks hurts.", &r->fall_damage, fx, y, fw);
    y += 52.0f + 6.0f;

    bool done = sbutton(app, ui, rects, &rn, fx, y, (fw - 12.0f) * 0.5f, SCR_BTN_H, "Done", true);
    bool do_cancel2 =
        sbutton(app, ui, rects, &rn, fx + (fw + 12.0f) * 0.5f, y, (fw - 12.0f) * 0.5f, SCR_BTN_H, "Cancel", true);
    sflush(app, rects, &rn);
    if (app->menu.error[0] != '\0') {
        stext_c(app, app->menu.error, cx, y + SCR_BTN_H + 12.0f, SCR_TEXT_SCALE, SCR_WARN_R, SCR_WARN_G, SCR_WARN_B);
    }
    if (done) {
        app->menu.create_show_rules = false;
        app->menu.name_focused = true;
        return;
    }
    if (do_cancel2 || ui->key_escape) {
        window_text_input(false);
        app->menu.create_show_rules = false;
        if (do_cancel2) {
            app_enter_state(app, GAME_STATE_WORLD_SELECT);
        } else {
            /* Escape on the rules page steps back to the main form
             * (pending rules kept); Escape there cancels. */
            app->menu.name_focused = true;
        }
        return;
    }
}

/* One slider row: label, track with knob, live value text. */
static void sslider_row(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, const char *label, float *value,
                        float lo, float hi, const char *fmt, bool *held, float x, float y, float w)
{
    renderer_draw_text(app->renderer, x, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f, label);
    float ty = y + 24.0f;
    float th = 10.0f;
    UiControl control = ui_control(&app->menu.controls, ui, x, ty - 8, w, th + 16, true);
    if (control.focused && (ui->key_left || ui->key_right)) {
        float step = strcmp(label, "Render Distance") == 0 ? 1.0f : (hi - lo) * .01f;
        *value = fmaxf(lo, fminf(hi, *value + (ui->key_right ? step : -step)));
    }
    srect(rects, rn, SCR_MAX_RECT_VERTS, x, ty, w, th, 0.10f, 0.10f, 0.12f, 1.0f);
    float t = (*value - lo) / (hi - lo);
    if (!(t >= 0.0f)) {
        t = 0.0f;
    }
    if (!(t <= 1.0f)) {
        t = 1.0f;
    }
    float kx = x + t * w;
    srect(rects, rn, SCR_MAX_RECT_VERTS, kx - 4.0f, ty - 6.0f, 8.0f, th + 12.0f, SCR_ACC_R, SCR_ACC_G, SCR_ACC_B, 1.0f);
    sflush(app, rects, rn);
    char val[48];
    renderer_draw_button(app->renderer, screen_width(app), screen_height(app), kx - 6, ty - 7, 12, th + 14,
                         control.hovered || control.focused ? 1 : 0, *held);
    snprintf(val, sizeof(val), fmt, (double)*value);
    renderer_draw_text(app->renderer, x + w + 12.0f, ty - 4.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B, 1.0f,
                       val);
    ui_slider(ui, value, lo, hi, x, ty - 8.0f, w, th + 16.0f, held);
}

/* Settings screen: sliders, toggles, pack cycle, back (saves). */
static void screen_settings(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)screen_width(app) * 0.5f;
    sbackground(app, rects, &rn);
    float overflow = fmaxf(0, 690.0f - (float)screen_height(app));
    app->menu.settings_scroll = fmaxf(0, fminf(overflow, app->menu.settings_scroll - (float)ui->wheel * 32));
    float y = 56.0f - app->menu.settings_scroll;
    y = stitle(app, "Settings", NULL, cx, y);
    float fw = SCR_BTN_W + 160.0f;
    float fx = cx - fw * 0.5f;
    float sw = fw - 130.0f;

    sslider_row(app, ui, rects, &rn, "Render Distance", &app->menu.sl_rd, 2.0f, 8.0f, "%.0f", &app->menu.held_rd, fx, y,
                sw);
    y += 62.0f;
    sslider_row(app, ui, rects, &rn, "Mouse Sensitivity", &app->menu.sl_sens, 0.0005f, 0.010f, "%.4f",
                &app->menu.held_sens, fx, y, sw);
    y += 62.0f;
    sslider_row(app, ui, rects, &rn, "Field of View", &app->menu.sl_fov, 60.0f, 110.0f, "%.0f", &app->menu.held_fov, fx,
                y, sw);
    y += 62.0f;
    sslider_row(app, ui, rects, &rn, "Master Volume", &app->menu.sl_vol, 0.0f, 100.0f, "%.0f", &app->menu.held_vol, fx,
                y, sw);
    y += 62.0f;
    sslider_row(app, ui, rects, &rn, "Effects Volume", &app->menu.sl_sfx, 0.0f, 100.0f, "%.0f", &app->menu.held_sfx, fx,
                y, sw);
    y += 62.0f;

    /* Live-commit sliders every frame (cheap applies; pack handled below). */
    sliders_to_settings(app);
    app_apply_settings(app);

    /* VSync toggle. */
    renderer_draw_text(app->renderer, fx, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f, "VSync");
    char vsync_label[32];
    snprintf(vsync_label, sizeof(vsync_label), "%s", app->settings.vsync ? "ON" : "OFF");
    if (sbutton(app, ui, rects, &rn, fx + sw + 12.0f, y - 6.0f, 118.0f, 36.0f, vsync_label, true)) {
        app->settings.vsync = !app->settings.vsync;
        app_apply_settings(app);
    }
    y += 52.0f;

    /* Auto-Jump toggle (MC default OFF: 1-block ledges need a jump). */
    renderer_draw_text(app->renderer, fx, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f, "Auto-Jump");
    char autojump_label[32];
    snprintf(autojump_label, sizeof(autojump_label), "%s", app->settings.auto_jump ? "ON" : "OFF");
    if (sbutton(app, ui, rects, &rn, fx + sw + 12.0f, y - 6.0f, 118.0f, 36.0f, autojump_label, true)) {
        app->settings.auto_jump = !app->settings.auto_jump;
        settings_save(&app->settings, SETTINGS_PATH);
    }
    y += 52.0f;

    /* View Bobbing toggle (MC default ON: gentle first-person walk bob). */
    renderer_draw_text(app->renderer, fx, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f, "View Bobbing");
    char bobbing_label[32];
    snprintf(bobbing_label, sizeof(bobbing_label), "%s", app->settings.view_bobbing ? "ON" : "OFF");
    if (sbutton(app, ui, rects, &rn, fx + sw + 12.0f, y - 6.0f, 118.0f, 36.0f, bobbing_label, true)) {
        app->settings.view_bobbing = !app->settings.view_bobbing;
        settings_save(&app->settings, SETTINGS_PATH);
    }
    y += 52.0f;

    /* Pack cycle: "<" + name + ">" (immediate atlas reload on change). */
    renderer_draw_text(app->renderer, fx, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f, "Resource Pack");
    const char *pack_name = "Default";
    if (app->menu.pack_count > 0) {
        pack_name = app->menu.packs[app->menu.pack_idx];
    }
    if (sbutton(app, ui, rects, &rn, fx + sw - 40.0f, y - 6.0f, 40.0f, 36.0f, "<", app->menu.pack_count > 1)) {
        if (app->menu.pack_count > 0) {
            app->menu.pack_idx = (app->menu.pack_idx - 1 + (int)app->menu.pack_count) % (int)app->menu.pack_count;
        }
    }
    char pack_label[80];
    snprintf(pack_label, sizeof(pack_label), "%s", pack_name);
    float pw = 0.0f;
    float ph = 0.0f;
    renderer_measure_text(app->renderer, pack_label, SCR_TEXT_SCALE, &pw, &ph);
    renderer_draw_text(app->renderer, fx + sw + 12.0f, y, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B, 1.0f,
                       pack_label);
    if (sbutton(app, ui, rects, &rn, fx + sw + 24.0f + pw, y - 6.0f, 40.0f, 36.0f, ">", app->menu.pack_count > 1)) {
        if (app->menu.pack_count > 0) {
            app->menu.pack_idx = (app->menu.pack_idx + 1) % (int)app->menu.pack_count;
        }
    }
    /* Detect pack change vs settings and reload the atlas now. */
    if (app->menu.pack_count > 0 && strcmp(app->settings.pack, app->menu.packs[app->menu.pack_idx]) != 0) {
        size_t pl = strlen(app->menu.packs[app->menu.pack_idx]);
        if (pl > SETTINGS_PACK_LEN - 1) {
            pl = SETTINGS_PACK_LEN - 1;
        }
        memcpy(app->settings.pack, app->menu.packs[app->menu.pack_idx], pl);
        app->settings.pack[pl] = '\0';
        app_apply_settings(app);
        if (renderer_reload_atlas(app->renderer, app->settings.pack) != 0) {
            LOG_WARN("screens: resource pack texture reload failed; previous atlas remains active");
        }
    }
    y += 56.0f;

    if (sbutton(app, ui, rects, &rn, cx - SCR_BTN_W * 0.5f, y, SCR_BTN_W, SCR_BTN_H, "Back", true)) {
        sliders_to_settings(app);
        if (settings_save(&app->settings, SETTINGS_PATH) != 0) {
            LOG_WARN("screens: settings save failed");
        }
        app_apply_settings(app);
        app_enter_state(app, app->menu.settings_return);
        return;
    }
    sflush(app, rects, &rn);
    if (ui->key_escape) {
        sliders_to_settings(app);
        if (settings_save(&app->settings, SETTINGS_PATH) != 0) {
            LOG_WARN("screens: settings save failed");
        }
        app_apply_settings(app);
        app_enter_state(app, app->menu.settings_return);
    }
}

/* In-session game rules page (pause sub-view): live authoritative values,
 * host-or-offline editing, guest read-only. Toggles apply instantly
 * (broadcast + world.meta persist per row). */
static void screen_pause_rules(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)screen_width(app) * 0.5f;
    srect(rects, &rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)screen_width(app), (float)screen_height(app), 0.0f, 0.0f,
          0.0f, 0.55f);
    sflush(app, rects, &rn);
    float fw = SCR_BTN_W + 80.0f;
    if (fw < 520.0f) {
        fw = 520.0f;
        if (fw > (float)screen_width(app) - 24.0f) {
            fw = (float)screen_width(app) - 24.0f;
        }
    }
    float fx = cx - fw * 0.5f;
    float y = 36.0f;
    y = stitle(app, "Game Rules", "Host-authoritative - changes save to world.meta", cx, y);
    bool editable = !app->lan_client && app->world_open && app->world != NULL;
    GameRules *r = app->world_open && app->world != NULL ? &app->world->rules : NULL;
    if (r != NULL) {
        /* Each row toggles the live rule directly; a change publishes
         * (broadcast when hosting + world.meta persist) exactly once. */
        bool before = r->allow_cheats;
        srule_row_ex(app, ui, rects, &rn, "Allow Cheats", "Unlocks debug give/spawn keys (debug builds).",
                     &r->allow_cheats, editable, fx, y, fw);
        if (r->allow_cheats != before) {
            app_apply_world_rules(app);
        }
        y += 52.0f;
        before = r->keep_inventory;
        srule_row_ex(app, ui, rects, &rn, "Keep Inventory", "Keep hotbar + inventory + held item through death.",
                     &r->keep_inventory, editable, fx, y, fw);
        if (r->keep_inventory != before) {
            app_apply_world_rules(app);
        }
        y += 52.0f;
        before = r->pvp;
        srule_row_ex(app, ui, rects, &rn, "Player vs Player", "Players can damage each other in multiplayer.", &r->pvp,
                     editable, fx, y, fw);
        if (r->pvp != before) {
            app_apply_world_rules(app);
        }
        y += 52.0f;
        before = r->mob_spawning;
        srule_row_ex(app, ui, rects, &rn, "Mob Spawning", "New creatures spawn over time.", &r->mob_spawning, editable,
                     fx, y, fw);
        if (r->mob_spawning != before) {
            app_apply_world_rules(app);
        }
        y += 52.0f;
        before = r->daylight_cycle;
        srule_row_ex(app, ui, rects, &rn, "Daylight Cycle", "The sun/moon clock advances.", &r->daylight_cycle,
                     editable, fx, y, fw);
        if (r->daylight_cycle != before) {
            app_apply_world_rules(app);
        }
        y += 52.0f;
        before = r->natural_regen;
        srule_row_ex(app, ui, rects, &rn, "Natural Regeneration", "Heal over time at high hunger (starve still hurts).",
                     &r->natural_regen, editable, fx, y, fw);
        if (r->natural_regen != before) {
            app_apply_world_rules(app);
        }
        y += 52.0f;
        before = r->fall_damage;
        srule_row_ex(app, ui, rects, &rn, "Fall Damage", "Falling more than 3 blocks hurts.", &r->fall_damage, editable,
                     fx, y, fw);
        if (r->fall_damage != before) {
            app_apply_world_rules(app);
        }
        y += 52.0f + 2.0f;
    }
    if (!editable) {
        stext_c(app, "Only the hosting player can change rules.", cx, y, SCR_TEXT_SCALE, SCR_WARN_R, SCR_WARN_G,
                SCR_WARN_B);
        y += 8.0f * SCR_TEXT_SCALE + 6.0f;
    } else {
        stext_c(app, "Allow Cheats unlocks debug keys; it never grants rule editing to guests.", cx, y, SCR_SMALL_SCALE,
                SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
        y += 8.0f * SCR_SMALL_SCALE + 8.0f;
    }
    if (sbutton(app, ui, rects, &rn, fx, y, fw, SCR_BTN_H, "Done", true)) {
        app->menu.pause_show_rules = false;
        return;
    }
    sflush(app, rects, &rn);
    if (ui->key_escape) {
        app->menu.pause_show_rules = false;
    }
}

/* Pause menu over the frozen world: Resume / Settings / Save+Quit. */
static void screen_paused(AppContext *app, const UiFrame *ui)
{
    if (app->menu.pause_show_rules) {
        screen_pause_rules(app, ui);
        return;
    }
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)screen_width(app) * 0.5f;
    /* Dim veil so the frozen world shows through faintly. */
    srect(rects, &rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)screen_width(app), (float)screen_height(app), 0.0f, 0.0f,
          0.0f, 0.55f);
    sflush(app, rects, &rn);
    float y = (float)screen_height(app) * 0.28f;
    y = stitle(app, "Game Menu", app->lan_host ? "LAN host active - TCP port 25566" : NULL, cx, y);
    float bx = cx - SCR_BTN_W * 0.5f;
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, "Resume Game", true)) {
        app_enter_state(app, GAME_STATE_PLAYING);
        return;
    }
    y += SCR_BTN_H + SCR_GAP;
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, "Settings", true)) {
        app->menu.settings_return = GAME_STATE_PAUSED;
        settings_to_sliders(app);
        screens_refresh_packs(app);
        app_enter_state(app, GAME_STATE_SETTINGS);
        return;
    }
    y += SCR_BTN_H + SCR_GAP;
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, "Game Rules...", true)) {
        app->menu.pause_show_rules = true;
        return;
    }
    y += SCR_BTN_H + SCR_GAP;
    const char *lan_label = app->lan_host ? "Close LAN World" : "Open to LAN";
    bool lan_enabled = !app->lan_client;
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, lan_label, lan_enabled)) {
        if (app->lan_host) {
            app_lan_disconnect(app);
        } else if (app_lan_host(app)) {
            app_enter_state(app, GAME_STATE_PLAYING);
        }
        return;
    }
    y += SCR_BTN_H + SCR_GAP;
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, "Save and Quit to Title", true)) {
        app_lan_disconnect(app);
        app_resolve_cursor(app);
        app_resolve_crafting(app);
        session_close_world(app, true);
        screens_refresh_worlds(app);
        app_enter_state(app, GAME_STATE_MAIN_MENU);
        return;
    }
    sflush(app, rects, &rn);
    if (app->menu.error[0] != '\0') {
        stext_c(app, app->menu.error, cx, y + SCR_BTN_H + 10.0f, SCR_SMALL_SCALE, SCR_WARN_R, SCR_WARN_G, SCR_WARN_B);
    } else if (app->lan_host) {
        stext_c(app, "Share this computer's local IPv4 address with your friends.", cx, y + SCR_BTN_H + 10.0f,
                SCR_SMALL_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
    }
    if (ui->key_escape) {
        app_enter_state(app, GAME_STATE_PLAYING);
    }
}

/* ---- Vanilla container screens ----------------------------------------------------------------------------
 * The UI canvas is the 2x-baseline canvas, so one Minecraft GUI pixel is SG canvas units and every slot, label
 * and scrollbar below sits at its exact vanilla pixel coordinate inside the container sheet (176x166 for the
 * player inventory and crafting table, 195x136 for the creative item tab). A slot is a 16x16 item area inside
 * an 18x18 cell; the cell art is part of the converted sheet (a flat fallback is drawn without it).
 */
#define SG 2.0f
#define SG_ITEM (16.0f * SG)
#define SINV_COLS 9
#define SG_MAX_COUNTS 64
#define SG_LABEL 0.251f /* 0x404040, the vanilla container label colour. */

typedef struct SgScreen {
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn;
    IconBatch icons;
    float px, py;  /* Panel top-left in canvas units (even, so GUI pixels land on device pixels). */
    bool textured; /* The vanilla sheet drew the panel and the slot cells. */
    int ncounts;
    float count_x[SG_MAX_COUNTS], count_y[SG_MAX_COUNTS];
    uint16_t count_n[SG_MAX_COUNTS];
    bool hov; /* A slot is under the cursor. */
    float hov_x, hov_y;
    ItemStack hov_stack;
} SgScreen;

/* Flat stand-in for the vanilla panel when the converted sheet is absent (CI, no owner assets). */
static void sg_fallback_panel(SgScreen *sg, float w, float h)
{
    float *r = sg->rects;
    size_t *n = &sg->rn;
    srect(r, n, SCR_MAX_RECT_VERTS, sg->px, sg->py, w, h, 0.0f, 0.0f, 0.0f, 1.0f);
    srect(r, n, SCR_MAX_RECT_VERTS, sg->px + 2.0f, sg->py + 2.0f, w - 4.0f, h - 4.0f, 0.776f, 0.776f, 0.776f, 1.0f);
    srect(r, n, SCR_MAX_RECT_VERTS, sg->px + 2.0f, sg->py + 2.0f, w - 4.0f, 4.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    srect(r, n, SCR_MAX_RECT_VERTS, sg->px + 2.0f, sg->py + 2.0f, 4.0f, h - 4.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    srect(r, n, SCR_MAX_RECT_VERTS, sg->px + 2.0f, sg->py + h - 6.0f, w - 4.0f, 4.0f, 0.545f, 0.545f, 0.545f, 1.0f);
    srect(r, n, SCR_MAX_RECT_VERTS, sg->px + w - 6.0f, sg->py + 2.0f, 4.0f, h - 4.0f, 0.545f, 0.545f, 0.545f, 1.0f);
}

/* Dim the world, centre the panel and draw its background. kind: 0 inventory, 1 crafting table, 2 creative. */
static void sg_begin(AppContext *app, SgScreen *sg, int kind, float gui_w, float gui_h)
{
    sg->rn = 0;
    icons_clear(&sg->icons);
    sg->ncounts = 0;
    sg->hov = false;
    sg->hov_x = sg->hov_y = 0.0f;
    stack_clear(&sg->hov_stack);
    float w = gui_w * SG, h = gui_h * SG;
    sg->px = 2.0f * floorf(((float)screen_width(app) - w) * 0.25f);
    sg->py = 2.0f * floorf(((float)screen_height(app) - h) * 0.25f);
    srect(sg->rects, &sg->rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)screen_width(app), (float)screen_height(app), 0.0f,
          0.0f, 0.0f, 0.55f);
    sflush(app, sg->rects, &sg->rn);
    sg->textured = renderer_draw_container_panel(app->renderer, screen_width(app), screen_height(app), kind, sg->px,
                                                 sg->py, SG);
    if (!sg->textured) {
        sg_fallback_panel(sg, w, h);
        sflush(app, sg->rects, &sg->rn);
    }
}

/* Container label at a GUI-pixel position inside the panel (no shadow, vanilla dark grey). */
static void sg_label(AppContext *app, const SgScreen *sg, float gx, float gy, const char *text)
{
    renderer_draw_text(app->renderer, sg->px + gx * SG, sg->py + gy * SG, SCR_TEXT_SCALE, SG_LABEL, SG_LABEL, SG_LABEL,
                       1.0f, text);
}

/* Slot cell for the fallback panel: dark top/left edge, light bottom/right edge, mid-grey fill. */
static void sg_fallback_cell(SgScreen *sg, float x, float y)
{
    srect(sg->rects, &sg->rn, SCR_MAX_RECT_VERTS, x - 2.0f, y - 2.0f, SG_ITEM + 4.0f, SG_ITEM + 4.0f, 0.216f, 0.216f,
          0.216f, 1.0f);
    srect(sg->rects, &sg->rn, SCR_MAX_RECT_VERTS, x, y, SG_ITEM + 2.0f, SG_ITEM + 2.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    srect(sg->rects, &sg->rn, SCR_MAX_RECT_VERTS, x, y, SG_ITEM, SG_ITEM, 0.545f, 0.545f, 0.545f, 1.0f);
}

/* Queue one slot at the canvas position of its 16x16 item area: cell (fallback only), hover highlight behind the
 * item, the item sprite and its count. Returns true while the cursor is over the slot. Interaction stays with the
 * caller (survival vs creative differ).
 */
static bool sg_slot(const UiFrame *ui, SgScreen *sg, float x, float y, const ItemStack *slot)
{
    bool hov = ui_hit(ui->mouse_x, ui->mouse_y, x, y, SG_ITEM, SG_ITEM);
    if (!sg->textured) {
        sg_fallback_cell(sg, x, y);
    }
    if (hov) {
        /* slot_highlight_back: translucent white behind the item. */
        srect(sg->rects, &sg->rn, SCR_MAX_RECT_VERTS, x, y, SG_ITEM, SG_ITEM, 1.0f, 1.0f, 1.0f, 0.376f);
        sg->hov = true;
        sg->hov_x = x;
        sg->hov_y = y;
        sg->hov_stack = *slot;
    }
    if (!stack_is_empty(slot)) {
        icons_push(&sg->icons, x, y, SG_ITEM, item_get_info(slot->item)->tile);
        if (slot->count > 1 && sg->ncounts < SG_MAX_COUNTS) {
            sg->count_x[sg->ncounts] = x;
            sg->count_y[sg->ncounts] = y;
            sg->count_n[sg->ncounts] = slot->count;
            sg->ncounts++;
        }
    }
    return hov;
}

/* Stack count at the bottom-right of a slot, white over a dark drop shadow (vanilla font rendering). */
static void sg_count_text(AppContext *app, float x, float y, unsigned count)
{
    char cb[8];
    snprintf(cb, sizeof(cb), "%u", count);
    float tw = 0.0f;
    float th = 0.0f;
    renderer_measure_text(app->renderer, cb, SCR_TEXT_SCALE, &tw, &th);
    float tx = x + 34.0f - tw;
    float ty = y + 18.0f;
    renderer_draw_text(app->renderer, tx + 2.0f, ty + 2.0f, SCR_TEXT_SCALE, 0.247f, 0.247f, 0.247f, 1.0f, cb);
    renderer_draw_text(app->renderer, tx, ty, SCR_TEXT_SCALE, 1.0f, 1.0f, 1.0f, 1.0f, cb);
}

/* Item-name tooltip next to the cursor: dark purple box with the vanilla two-tone border, white shadowed text. */
static void sg_tooltip(AppContext *app, int mx, int my, const char *text)
{
    float tw = 0.0f;
    float th = 0.0f;
    renderer_measure_text(app->renderer, text, SCR_TEXT_SCALE, &tw, &th);
    float tx = (float)mx + 12.0f * SG;
    float ty = (float)my - 12.0f * SG;
    float maxx = (float)screen_width(app) - tw - 10.0f;
    float maxy = (float)screen_height(app) - th - 10.0f;
    tx = tx > maxx ? maxx : tx;
    ty = ty > maxy ? maxy : (ty < 10.0f ? 10.0f : ty);
    float rects[24 * 6];
    size_t rn = 0;
    const float bw = tw + 16.0f, bh = th + 16.0f;
    srect(rects, &rn, 24, tx - 8.0f, ty - 8.0f, bw, bh * 0.5f, 0.16f, 0.0f, 0.36f, 0.96f);
    srect(rects, &rn, 24, tx - 8.0f, ty - 8.0f + bh * 0.5f, bw, bh * 0.5f, 0.10f, 0.0f, 0.22f, 0.96f);
    srect(rects, &rn, 24, tx - 6.0f, ty - 6.0f, bw - 4.0f, bh - 4.0f, 0.0627f, 0.0f, 0.0627f, 0.94f);
    renderer_draw_rects(app->renderer, screen_width(app), screen_height(app), rects, rn);
    renderer_draw_text(app->renderer, tx + 2.0f, ty + 2.0f, SCR_TEXT_SCALE, 0.247f, 0.247f, 0.247f, 1.0f, text);
    renderer_draw_text(app->renderer, tx, ty, SCR_TEXT_SCALE, 1.0f, 1.0f, 1.0f, 1.0f, text);
}

/* Flush the queued rects, draw item sprites, counts, the front highlight, then the held stack at the cursor (or
 * the hovered item's tooltip when the cursor is empty). */
static void sg_finish(AppContext *app, const UiFrame *ui, SgScreen *sg)
{
    sflush(app, sg->rects, &sg->rn);
    renderer_draw_item_icons(app->renderer, screen_width(app), screen_height(app), &sg->icons);
    for (int i = 0; i < sg->ncounts; ++i) {
        sg_count_text(app, sg->count_x[i], sg->count_y[i], (unsigned)sg->count_n[i]);
    }
    if (sg->hov) {
        /* slot_highlight_front: a faint white veil over the item. */
        srect(sg->rects, &sg->rn, SCR_MAX_RECT_VERTS, sg->hov_x, sg->hov_y, SG_ITEM, SG_ITEM, 1.0f, 1.0f, 1.0f, 0.125f);
        sflush(app, sg->rects, &sg->rn);
    }
    if (!stack_is_empty(&app->cursor)) {
        IconBatch cur;
        icons_clear(&cur);
        float x = (float)ui->mouse_x - SG_ITEM * 0.5f;
        float y = (float)ui->mouse_y - SG_ITEM * 0.5f;
        icons_push(&cur, x, y, SG_ITEM, item_get_info(app->cursor.item)->tile);
        renderer_draw_item_icons(app->renderer, screen_width(app), screen_height(app), &cur);
        if (app->cursor.count > 1) {
            sg_count_text(app, x, y, (unsigned)app->cursor.count);
        }
    } else if (sg->hov && !stack_is_empty(&sg->hov_stack)) {
        sg_tooltip(app, ui->mouse_x, ui->mouse_y, item_get_info(sg->hov_stack.item)->name);
    }
}

/* Survival click handling for one storage slot (pick up / place / merge /
 * swap on LMB; half-split / place-one on RMB). Both stacks stay canonical.
 * Accepted actions click (menu feedback sound); denied clicks are silent.
 */
static void sslot_survival(AppContext *app, const UiFrame *ui, float x, float y, ItemStack *slot, ItemStack *cursor)
{
    if (!ui_hit(ui->mouse_x, ui->mouse_y, x, y, SG_ITEM, SG_ITEM)) {
        return;
    }
    bool acted = false;
    if (ui->mouse_clicked) {
        if (stack_is_empty(cursor)) {
            if (!stack_is_empty(slot)) {
                *cursor = *slot;
                stack_clear(slot);
                acted = true;
            }
        } else if (stack_is_empty(slot)) {
            *slot = *cursor;
            stack_clear(cursor);
            acted = true;
        } else if (slot->item == cursor->item) {
            /* Merges what fits (wear must match inside stack_add); rest
             * stays held. A swap is NOT attempted here: same-item stacks
             * only merge, never exchange. */
            acted = stack_add(slot, cursor) > 0;
        } else {
            ItemStack tmp = *slot;
            *slot = *cursor;
            *cursor = tmp;
            acted = true;
        }
    } else if (ui->mouse_rclicked) {
        if (stack_is_empty(cursor)) {
            if (!stack_is_empty(slot)) {
                stack_split_half(slot, cursor);
                acted = true;
            }
        } else if (stack_is_empty(slot)) {
            slot->item = cursor->item;
            slot->count = 1;
            slot->durability = cursor->durability;
            stack_remove(cursor, 1);
            acted = true;
        } else if (slot->item == cursor->item && slot->durability == cursor->durability) {
            const ItemInfo *info = item_get_info(slot->item);
            if (slot->count < info->max_stack) {
                slot->count++;
                stack_remove(cursor, 1);
                acted = true;
            }
        }
    }
    if (acted) {
        audio_play(&app->audio, AUDIO_UI_CLICK);
    }
}

/* One interactive storage slot: queue its art, then handle the click. */
static void sg_storage_slot(AppContext *app, const UiFrame *ui, SgScreen *sg, float gx, float gy, ItemStack *slot)
{
    float x = sg->px + gx * SG;
    float y = sg->py + gy * SG;
    (void)sg_slot(ui, sg, x, y, slot);
    sslot_survival(app, ui, x, y, slot, &app->cursor);
}

/* The 27 storage slots + hotbar of the player inventory at their vanilla GUI-pixel coordinates (shared by the
 * inventory and crafting-table screens). gx0 is the left edge of the slot grid; main_y / hotbar_y its rows. */
static void sg_player_slots(AppContext *app, const UiFrame *ui, SgScreen *sg, float main_y, float hotbar_y)
{
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < SINV_COLS; ++col) {
            sg_storage_slot(app, ui, sg, 8.0f + 18.0f * (float)col, main_y + 18.0f * (float)row,
                            &app->player.inv.slots[INV_MAIN_START + row * SINV_COLS + col]);
        }
    }
    for (int col = 0; col < SINV_COLS; ++col) {
        sg_storage_slot(app, ui, sg, 8.0f + 18.0f * (float)col, hotbar_y, &app->player.inv.slots[col]);
    }
}

/* Crafting output recompute (change-guarded): hash the grid, re-match
 * only when inputs changed. One hash slot per grid size class.
 */
static uint32_t scraft_hash_grid(const ItemStack *grid, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) {
        h ^= (uint32_t)grid[i].item;
        h *= 16777619u;
        h ^= (uint32_t)grid[i].count;
        h *= 16777619u;
    }
    return h;
}

static void scraft_recompute(AppContext *app, ItemStack *grid, int gw, int gh, uint32_t *hashslot)
{
    uint32_t h = scraft_hash_grid(grid, gw * gh) ^ (uint32_t)(gw * 4 + gh);
    if (h == *hashslot) {
        return;
    }
    *hashslot = h;
    RecipeMatch m;
    if (recipe_match(grid, gw, gh, &m)) {
        app->craft_out.item = m.recipe->out_item;
        app->craft_out.count = m.recipe->out_count;
        app->craft_out.durability = 0; /* Crafted tools are brand-new. */
    } else {
        stack_clear(&app->craft_out);
    }
}

/* Draw the derived output slot and handle taking it (empty cursor takes
 * all; matching cursor merges when the whole output fits; anything else
 * is denied without consuming ingredients). Ingredients are consumed
 * only after the take is fully accepted — never partial, never lost.
 */
static void scraft_take(AppContext *app, const UiFrame *ui, SgScreen *sg, float gx, float gy, ItemStack *grid, int gw,
                        int gh, uint32_t *hashslot)
{
    float x = sg->px + gx * SG;
    float y = sg->py + gy * SG;
    (void)sg_slot(ui, sg, x, y, &app->craft_out);
    if (!ui_hit(ui->mouse_x, ui->mouse_y, x, y, SG_ITEM, SG_ITEM) || !ui->mouse_clicked) {
        return;
    }
    if (stack_is_empty(&app->craft_out)) {
        return;
    }
    /* Re-match defensively: the displayed output must equal a live match. */
    RecipeMatch m;
    if (!recipe_match(grid, gw, gh, &m)) {
        stack_clear(&app->craft_out);
        return;
    }
    if (app->craft_out.item != m.recipe->out_item || app->craft_out.count != m.recipe->out_count) {
        stack_clear(&app->craft_out);
        return;
    }
    const ItemInfo *info = item_get_info(m.recipe->out_item);
    if (stack_is_empty(&app->cursor)) {
        recipe_consume(grid, gw, gh, &m);
        app->cursor.item = m.recipe->out_item;
        app->cursor.count = m.recipe->out_count;
        app->cursor.durability = 0;
        audio_play(&app->audio, AUDIO_CRAFT);
    } else if (app->cursor.item == m.recipe->out_item &&
               (unsigned)app->cursor.count + (unsigned)m.recipe->out_count <= (unsigned)info->max_stack) {
        recipe_consume(grid, gw, gh, &m);
        app->cursor.count = (uint16_t)((unsigned)app->cursor.count + (unsigned)m.recipe->out_count);
        audio_play(&app->audio, AUDIO_CRAFT);
    } else {
        return; /* Denied: cursor occupied by something else (no consume). */
    }
    *hashslot = 0; /* Force output recompute next frame. */
}

/* Survival inventory (vanilla InventoryScreen layout): live player preview, 2x2 crafting grid with its output,
 * 27 storage slots + hotbar; drag-free click model (LMB move/merge/swap, RMB split/place-one). The armour and
 * off-hand cells are part of the vanilla sheet and stay empty. E/ESC closes via the app run loop, which resolves
 * the cursor first.
 */
static void screen_inventory(AppContext *app, const UiFrame *ui)
{
    SgScreen sg;
    sg_begin(app, &sg, 0, 176.0f, 166.0f);
    if (!sg.textured) {
        srect(sg.rects, &sg.rn, SCR_MAX_RECT_VERTS, sg.px + 26.0f * SG, sg.py + 8.0f * SG, 49.0f * SG, 70.0f * SG, 0.0f,
              0.0f, 0.0f, 1.0f);
        sflush(app, sg.rects, &sg.rn);
    }
    /* The preview turns toward the cursor (vanilla: offsets from the box centre, 1 GUI pixel = SG units). */
    float look_x = (sg.px + 50.5f * SG - (float)ui->mouse_x) / SG;
    float look_y = (sg.py + 43.0f * SG - (float)ui->mouse_y) / SG;
    renderer_draw_player_preview(app->renderer, sg.px + 26.0f * SG, sg.py + 8.0f * SG, 49.0f * SG, 70.0f * SG,
                                 &app->player, look_x, look_y);
    sg_label(app, &sg, 97.0f, 8.0f, "Crafting");

    scraft_recompute(app, app->craft2, 2, 2, &app->craft_hash);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            sg_storage_slot(app, ui, &sg, 98.0f + 18.0f * (float)col, 18.0f + 18.0f * (float)row,
                            &app->craft2[row * 2 + col]);
        }
    }
    scraft_take(app, ui, &sg, 154.0f, 28.0f, app->craft2, 2, 2, &app->craft_hash);
    sg_player_slots(app, ui, &sg, 84.0f, 142.0f);
    sg_finish(app, ui, &sg);
}

/* Creative item tab (vanilla CreativeModeInventoryScreen layout): five rows of the scrollable catalogue (LMB grabs
 * a full stack to the cursor) over the editable hotbar row (LMB assigns the held stack, empty cursor clears).
 * Wheel or the scrollbar scrolls. Closing discards the cursor (infinite sources).
 */
static void screen_creative(AppContext *app, const UiFrame *ui)
{
    /* Catalogue: all valid items in registry order (blocks 1..19 sans
     * water, then materials, then tools). Bounded and allocation-free. */
    ItemId cata[64];
    int ncata = 0;
    for (uint16_t id = 1; id <= 19 && ncata < (int)(sizeof(cata) / sizeof(cata[0])); ++id) {
        if (item_is_valid(id)) {
            cata[ncata++] = id;
        }
    }
    const uint16_t extra[] = {ITEM_COAL,          ITEM_APPLE,        ITEM_STICK,           ITEM_BOW,
                              ITEM_ARROW,         ITEM_BONE,         ITEM_RAW_BEEF,        ITEM_LEATHER,
                              ITEM_ROTTEN_FLESH,  ITEM_IRON_INGOT,   ITEM_DIAMOND,         ITEM_WHEAT_SEEDS,
                              ITEM_SHEARS,        ITEM_WOOD_PICKAXE, ITEM_STONE_PICKAXE,   ITEM_IRON_PICKAXE,
                              ITEM_DIAMOND_PICKAXE, ITEM_WOOD_AXE,   ITEM_STONE_AXE,       ITEM_IRON_AXE,
                              ITEM_DIAMOND_AXE,   ITEM_WOOD_SHOVEL,  ITEM_STONE_SHOVEL,    ITEM_IRON_SHOVEL,
                              ITEM_DIAMOND_SHOVEL, ITEM_WOOD_SWORD,  ITEM_STONE_SWORD,     ITEM_IRON_SWORD,
                              ITEM_DIAMOND_SWORD};
    for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]) && ncata < (int)(sizeof(cata) / sizeof(cata[0])); ++i) {
        if (item_is_valid(extra[i])) {
            cata[ncata++] = extra[i];
        }
    }
    const int visible_rows = 5;
    int rows = (ncata + SINV_COLS - 1) / SINV_COLS;
    int max_scroll = rows > visible_rows ? rows - visible_rows : 0;

    SgScreen sg;
    sg_begin(app, &sg, 2, 195.0f, 136.0f);

    /* Scrolling: wheel ticks and dragging inside the scrollbar column. */
    if (ui->wheel != 0) {
        app->menu.creative_scroll -= ui->wheel;
    }
    const float track_x = sg.px + 174.0f * SG, track_y = sg.py + 17.0f * SG;
    if (max_scroll > 0 && ui->mouse_down && ui_hit(ui->mouse_x, ui->mouse_y, track_x, track_y, 16.0f * SG, 114.0f * SG)) {
        float frac = ((float)ui->mouse_y - (sg.py + 25.5f * SG)) / (97.0f * SG);
        frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
        app->menu.creative_scroll = (int)floorf(frac * (float)max_scroll + 0.5f);
    }
    if (app->menu.creative_scroll > max_scroll) {
        app->menu.creative_scroll = max_scroll;
    }
    if (app->menu.creative_scroll < 0) {
        app->menu.creative_scroll = 0;
    }
    int scroll = app->menu.creative_scroll;

    sg_label(app, &sg, 8.0f, 6.0f, "Items");
    for (int row = 0; row < visible_rows; ++row) {
        for (int col = 0; col < SINV_COLS; ++col) {
            int i = (scroll + row) * SINV_COLS + col;
            float x = sg.px + (9.0f + 18.0f * (float)col) * SG;
            float y = sg.py + (18.0f + 18.0f * (float)row) * SG;
            if (i >= ncata) {
                ItemStack none;
                stack_clear(&none);
                (void)sg_slot(ui, &sg, x, y, &none);
                continue;
            }
            const ItemInfo *info = item_get_info(cata[i]);
            ItemStack cell = {cata[i], 1, 0}; /* The catalogue shows single items; the grab is a full stack. */
            bool hov = sg_slot(ui, &sg, x, y, &cell);
            if (hov && ui->mouse_clicked) {
                app->cursor.item = cata[i];
                app->cursor.count = info->max_stack > 0 ? info->max_stack : 1;
                app->cursor.durability = 0;
                audio_play(&app->audio, AUDIO_UI_CLICK);
            }
        }
    }
    for (int col = 0; col < SINV_COLS; ++col) {
        float x = sg.px + (9.0f + 18.0f * (float)col) * SG;
        float y = sg.py + 112.0f * SG;
        ItemStack *slot = &app->player.inv.slots[col];
        bool hov = sg_slot(ui, &sg, x, y, slot);
        if (hov && ui->mouse_clicked) {
            if (!stack_is_empty(&app->cursor)) {
                const ItemInfo *info = item_get_info(app->cursor.item);
                slot->item = app->cursor.item;
                slot->count = info->max_stack > 0 ? info->max_stack : 1;
                slot->durability = 0; /* Conjured stacks are always brand-new. */
                stack_clear(&app->cursor);
            } else {
                stack_clear(slot);
            }
            audio_play(&app->audio, AUDIO_UI_CLICK);
        }
    }
    /* Scroller thumb (12x15 GUI px at x=175, travelling 97 px over the 112 px track); grey when nothing scrolls. */
    {
        float frac = max_scroll > 0 ? (float)scroll / (float)max_scroll : 0.0f;
        float tx = sg.px + 175.0f * SG;
        float ty = sg.py + (18.0f + floorf(97.0f * frac)) * SG;
        float c = max_scroll > 0 ? 0.776f : 0.545f;
        srect(sg.rects, &sg.rn, SCR_MAX_RECT_VERTS, tx, ty, 12.0f * SG, 15.0f * SG, 0.216f, 0.216f, 0.216f, 1.0f);
        srect(sg.rects, &sg.rn, SCR_MAX_RECT_VERTS, tx, ty, 12.0f * SG - 2.0f, 15.0f * SG - 2.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        srect(sg.rects, &sg.rn, SCR_MAX_RECT_VERTS, tx + 2.0f, ty + 2.0f, 12.0f * SG - 4.0f, 15.0f * SG - 4.0f, c, c, c,
              1.0f);
    }
    sg_finish(app, ui, &sg);
}

/* Crafting-table screen (vanilla CraftingScreen layout): 3x3 ingredient grid + derived output, then the full
 * inventory (27 storage + hotbar). Same click model as the inventory (grids are plain ItemStacks); closing returns
 * ingredients via app_resolve_crafting.
 */
static void screen_crafting(AppContext *app, const UiFrame *ui)
{
    SgScreen sg;
    sg_begin(app, &sg, 1, 176.0f, 166.0f);
    sg_label(app, &sg, 28.0f, 6.0f, "Crafting");
    sg_label(app, &sg, 8.0f, 72.0f, "Inventory");
    scraft_recompute(app, app->craft3, 3, 3, &app->craft_hash3);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            sg_storage_slot(app, ui, &sg, 30.0f + 18.0f * (float)col, 17.0f + 18.0f * (float)row,
                            &app->craft3[row * 3 + col]);
        }
    }
    scraft_take(app, ui, &sg, 124.0f, 35.0f, app->craft3, 3, 3, &app->craft_hash3);
    sg_player_slots(app, ui, &sg, 84.0f, 142.0f);
    sg_finish(app, ui, &sg);
}

/* Death screen over the frozen world: respawn at the stable spawn or
 * save + quit to title. No ESC dismissal (an explicit choice is required).
 */
static void screen_dead(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)screen_width(app) * 0.5f;
    srect(rects, &rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)screen_width(app), (float)screen_height(app), 0.35f, 0.02f,
          0.02f, 0.72f);
    sflush(app, rects, &rn);
    float y = (float)screen_height(app) * 0.30f;
    y = stitle(app, "You Died", "Your items scattered where you fell", cx, y);
    float bx = cx - SCR_BTN_W * 0.5f;
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, "Respawn", true)) {
        app_respawn_player(app);
        app_enter_state(app, GAME_STATE_PLAYING);
        return;
    }
    y += SCR_BTN_H + SCR_GAP;
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, "Save and Quit to Title", true)) {
        /* Death already scattered inventory + grids as entities; resolve
         * any leftovers (kept cursor/grid slots) before the save. */
        app_lan_disconnect(app);
        app_resolve_cursor(app);
        app_resolve_crafting(app);
        session_close_world(app, true);
        screens_refresh_worlds(app);
        app_enter_state(app, GAME_STATE_MAIN_MENU);
        return;
    }
    sflush(app, rects, &rn);
}

/* Loading overlay: progress text over the generating world. */
static void screen_loading(AppContext *app)
{
    char line1[64];
    char line2[64];
    snprintf(line1, sizeof(line1), "Loading world...");
    if (app->load_total > 0) {
        snprintf(line2, sizeof(line2), "Preparing terrain... %d / %d chunks", app->load_done, app->load_total);
    } else {
        snprintf(line2, sizeof(line2), "Preparing terrain...");
    }
    float cx = (float)screen_width(app) * 0.5f;
    float cy = (float)screen_height(app) * 0.5f;
    stext_c(app, line1, cx, cy - 20.0f, SCR_TEXT_SCALE + 1.0f, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B);
    stext_c(app, line2, cx, cy + 24.0f, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
    /* Green progress bar under the text (fills with chunk progress). */
    if (app->load_total > 0) {
        float frac = (float)app->load_done / (float)app->load_total;
        if (frac < 0.0f) {
            frac = 0.0f;
        }
        if (frac > 1.0f) {
            frac = 1.0f;
        }
        float bw = fminf(420.0f, (float)screen_width(app) - 80.0f);
        float bx = cx - bw * 0.5f;
        float by = cy + 56.0f;
        float rects[SCR_MAX_RECT_VERTS * 6];
        size_t rn = 0;
        srect(rects, &rn, SCR_MAX_RECT_VERTS, bx - 2.0f, by - 2.0f, bw + 4.0f, 10.0f, 0.05f, 0.05f, 0.05f, 1.0f);
        srect(rects, &rn, SCR_MAX_RECT_VERTS, bx, by, bw * frac, 6.0f, 0.25f, 0.75f, 0.25f, 1.0f);
        sflush(app, rects, &rn);
    }
}

/* F3 debug overlay lines (PLAYING only). */
void screens_draw_debug(AppContext *app)
{
    if (app == NULL || !app->world_open || app->world == NULL) {
        return;
    }
    char lines[14][128];
    Vec3 p = app->player.pos;
    RendererPerf perf = renderer_get_perf(app->renderer);
    int sel_idx = app->player.hotbar_sel;
    if (sel_idx < 0) {
        sel_idx = 0;
    }
    if (sel_idx > 8) {
        sel_idx = 8;
    }
    const ItemStack *sel_slot = &app->player.inv.slots[sel_idx];
    const ItemInfo *sel = item_get_info(sel_slot->item);
    snprintf(lines[0], sizeof(lines[0]), "TerraCraft debug - %.1f FPS  %.2f ms/frame", (double)app->fps_smooth,
             (double)app->frame_ms_avg);
    snprintf(lines[1], sizeof(lines[1]), "XYZ: %.2f / %.2f / %.2f", (double)p.x, (double)p.y, (double)p.z);
    snprintf(lines[2], sizeof(lines[2]), "Chunk: %d %d", (int)floorf(p.x / 16.0f), (int)floorf(p.z / 16.0f));
    snprintf(lines[3], sizeof(lines[3]), "Chunks: %zu loaded, %zu drawn (+%zu transp), %zu culled",
             world_chunk_count(app->world), perf.drawn, perf.drawn_t, perf.culled);
    snprintf(lines[4], sizeof(lines[4]), "Seed: %lld", (long long)app->world->seed);
    snprintf(lines[5], sizeof(lines[5]), "Day: %.3f (%s)", (double)app->clock.day_progress,
             app->player.flying ? "creative" : "survival");
    snprintf(lines[6], sizeof(lines[6]), "Held: %s x%u", sel != NULL ? sel->name : "?",
             stack_is_empty(sel_slot) ? 0u : (unsigned)sel_slot->count);
    snprintf(lines[7], sizeof(lines[7]), "Pack: %s", app->settings.pack);
    snprintf(lines[8], sizeof(lines[8]), "Mesh: %.2f ms  Up: %.2f ms  Draw: %.2f ms", perf.mesh_ms, perf.upload_ms,
             perf.draw_ms);
    snprintf(lines[9], sizeof(lines[9]), "Mobs: %d living (%d cow %d zomb %d skel), %d drawn %d culled",
             mob_active_count(&app->mobs), mob_count_type(&app->mobs, ENTITY_COW),
             mob_count_type(&app->mobs, ENTITY_ZOMBIE), mob_count_type(&app->mobs, ENTITY_SKELETON), app->mobs_drawn,
             app->mobs_culled);
    snprintf(lines[10], sizeof(lines[10]), "AI: thinks %u, paths %u (pool rng strain)", (unsigned)app->mobs.ai_thinks,
             (unsigned)app->mobs.path_reqs);
    snprintf(lines[11], sizeof(lines[11]), "Arrows: %d flying (%d stuck), fired %u hits %u, drawn %d",
             projectile_active_count(&app->projectiles), projectile_embedded_count(&app->projectiles),
             (unsigned)app->projectiles.fired, (unsigned)app->projectiles.impacts, app->arrows_drawn);
    snprintf(lines[12], sizeof(lines[12]), "Simulation: %.2f ms/tick, %.1f ticks/s", (double)app->sim_ms_avg,
             (double)app->sim_tps);
    if (app->process_working_set_bytes > 0) {
        snprintf(lines[13], sizeof(lines[13]), "Process resident memory: %llu MiB",
                 (unsigned long long)(app->process_working_set_bytes / (1024u * 1024u)));
    } else {
        snprintf(lines[13], sizeof(lines[13]), "Process resident memory: unavailable");
    }
    float y = 12.0f;
    for (int i = 0; i < 14 && lines[i][0] != '\0'; ++i) {
        renderer_draw_text(app->renderer, 12.0f, y, SCR_SMALL_SCALE + 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, lines[i]);
        y += 22.0f;
    }
}

/* Dispatch the active menu/overlay state. PLAYING/QUIT are app-owned. */
void screens_update(AppContext *app, const UiFrame *ui)
{
    if (app == NULL || ui == NULL) {
        return;
    }
    UiFrame input = *ui;
    bool scaled = screen_scaled(app);
    if (scaled) {
        input.mouse_x =
            app->width > 0 ? (int)floorf((float)ui->mouse_x * (float)screen_width(app) / (float)app->width) : 0;
        input.mouse_y =
            app->height > 0 ? (int)floorf((float)ui->mouse_y * (float)screen_height(app) / (float)app->height) : 0;
        renderer_set_ui_canvas(app->renderer, screen_width(app), screen_height(app));
    }
    ui = &input;
    bool arrows = app->state != GAME_STATE_WORLD_SELECT && app->state != GAME_STATE_LAN_MENU &&
                  app->state != GAME_STATE_CREATE_WORLD && app->state != GAME_STATE_PROFILE;
    int page = (app->state == GAME_STATE_CREATE_WORLD && app->menu.create_show_rules) ||
               (app->state == GAME_STATE_PAUSED && app->menu.pause_show_rules);
    ui_controls_begin(&app->menu.controls, ui, (int)app->state * 4 + 1 + page, arrows);
    switch (app->state) {
    case GAME_STATE_PROFILE:
        screen_profile(app, ui);
        break;
    case GAME_STATE_MAIN_MENU:
        screen_main_menu(app, ui);
        break;
    case GAME_STATE_LAN_MENU:
        screen_lan_menu(app, ui);
        break;
    case GAME_STATE_WORLD_SELECT:
        screen_world_select(app, ui);
        break;
    case GAME_STATE_CREATE_WORLD:
        screen_create_world(app, ui);
        break;
    case GAME_STATE_SETTINGS:
        screen_settings(app, ui);
        break;
    case GAME_STATE_PAUSED:
        screen_paused(app, ui);
        break;
    case GAME_STATE_INVENTORY:
        if (survival_is_creative(&app->player)) {
            screen_creative(app, ui);
        } else {
            screen_inventory(app, ui);
        }
        break;
    case GAME_STATE_CRAFTING:
        screen_crafting(app, ui);
        break;
    case GAME_STATE_DEAD:
        screen_dead(app, ui);
        break;
    case GAME_STATE_LOADING:
        screen_loading(app);
        break;
    default:
        break;
    }
    ui_controls_end(&app->menu.controls);
    renderer_set_ui_canvas(app->renderer, 0, 0);
}
