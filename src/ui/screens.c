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
#include "platform/window.h"
#include "render/renderer.h"
#include "render/texture_atlas.h"
#include "ui/hud.h"
#include "ui/ui.h"
#include "world/block.h"
#include "world/world.h"
#include "world/world_meta.h"
#include "world/world_save.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
#define SCR_BTN_H 44.0f
#define SCR_GAP 12.0f
#define SCR_TITLE_SCALE 4.0f
#define SCR_TEXT_SCALE 2.0f
#define SCR_SMALL_SCALE 1.0f

/* Scratch rect buffer for one screen (stack-friendly, bounded). */
#define SCR_MAX_RECT_VERTS 2048

/* Append one filled quad to a rect buffer (HUD verts, 6 floats each). */
static void srect(float *dst, size_t *n, size_t cap, float x, float y, float w, float h, float r, float g,
                  float b, float a)
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
    renderer_draw_rects(app->renderer, app->width, app->height, verts, *n);
    *n = 0;
}

/* Centered text helper (scale-aware). */
static void stext_c(AppContext *app, const char *s, float cx, float y, float scale, float r, float g, float b)
{
    float w = 0.0f;
    float h = 0.0f;
    renderer_measure_text(s, scale, &w, &h);
    renderer_draw_text(app->renderer, cx - w * 0.5f, y, scale, r, g, b, 1.0f, s);
}

/* Button: background + hover tint + top-edge highlight + centered label.
 * Returns true on click.
 */
static bool sbutton(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, float x, float y, float w,
                    float h, const char *label, bool enabled)
{
    bool hovered = false;
    bool clicked = false;
    if (enabled) {
        clicked = ui_button(ui, x, y, w, h, &hovered);
    }
    float br = enabled ? (hovered ? SCR_BTN_HOV_R : SCR_BTN_R) : 0.13f;
    float bg = enabled ? (hovered ? SCR_BTN_HOV_G : SCR_BTN_G) : 0.13f;
    float bb = enabled ? (hovered ? SCR_BTN_HOV_B : SCR_BTN_B) : 0.14f;
    srect(rects, rn, SCR_MAX_RECT_VERTS, x, y, w, h, br, bg, bb, 1.0f);
    srect(rects, rn, SCR_MAX_RECT_VERTS, x, y, w, 2.0f, br + 0.12f, bg + 0.12f, bb + 0.12f, 1.0f);
    srect(rects, rn, SCR_MAX_RECT_VERTS, x, y + h - 3.0f, w, 3.0f, 0.05f, 0.05f, 0.06f, 1.0f);
    sflush(app, rects, rn);
    float tw = 0.0f;
    float th = 0.0f;
    renderer_measure_text(label, SCR_TEXT_SCALE, &tw, &th);
    float tr = enabled ? SCR_TXT_R : SCR_DIM_R;
    float tg = enabled ? SCR_TXT_G : SCR_DIM_G;
    float tb = enabled ? SCR_TXT_B : SCR_DIM_B;
    renderer_draw_text(app->renderer, x + (w - tw) * 0.5f, y + (h - th) * 0.5f, SCR_TEXT_SCALE, tr, tg, tb,
                       1.0f, label);
    if (clicked && enabled) {
        audio_play(&app->audio, AUDIO_UI_CLICK);
    }
    return clicked && enabled;
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

/* Full-screen dim background. Clears first so menus never present
 * uninitialized backbuffer contents (pause/loading overlays draw over the
 * world instead, which clears itself).
 */
static void sbackground(AppContext *app, float *rects, size_t *rn)
{
    renderer_set_clear_color(app->renderer, SCR_BG_R, SCR_BG_G, SCR_BG_B, 1.0f);
    renderer_clear(app->renderer);
    srect(rects, rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)app->width, (float)app->height, SCR_BG_R,
          SCR_BG_G, SCR_BG_B, 1.0f);
    sflush(app, rects, rn);
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

/* Main menu: title + three buttons. */
static void screen_main_menu(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)app->width * 0.5f;
    sbackground(app, rects, &rn);
    float y = (float)app->height * 0.24f;
    y = stitle(app, "TerraCraft", "An original voxel sandbox", cx, y);
    float bx = cx - SCR_BTN_W * 0.5f;
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, "Singleplayer", true)) {
        app_enter_state(app, GAME_STATE_WORLD_SELECT);
        return;
    }
    y += SCR_BTN_H + SCR_GAP;
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, "Settings", true)) {
        app->menu.settings_return = GAME_STATE_MAIN_MENU;
        settings_to_sliders(app);
        screens_refresh_packs(app);
        app_enter_state(app, GAME_STATE_SETTINGS);
        return;
    }
    y += SCR_BTN_H + SCR_GAP;
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, "Quit", true)) {
        app_enter_state(app, GAME_STATE_QUIT);
        return;
    }
    sflush(app, rects, &rn);
    stext_c(app, "TerraCraft v0.9.1 - original engine, no Mojang assets", cx, (float)app->height - 30.0f,
            SCR_SMALL_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
    if (ui->key_escape) {
        app_enter_state(app, GAME_STATE_QUIT);
    }
}

/* Format a world row subtitle: "Seed 123456 - Survival - <played>". */
static void world_subtitle(const WorldEntry *e, char *out, size_t cap)
{
    const char *mode = e->mode == 1 ? "Creative" : "Survival";
    if (e->played > 0) {
        /* Played stamp as raw epoch (portable, no locale/timezone text). */
        snprintf(out, cap, "Seed %lld - %s - played %lld", (long long)e->seed, mode, (long long)e->played);
    } else {
        snprintf(out, cap, "Seed %lld - %s", (long long)e->seed, mode);
    }
}

/* World select: cached list + Play/Create/Delete/Back. */
static void screen_world_select(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)app->width * 0.5f;
    sbackground(app, rects, &rn);
    float list_w = SCR_BTN_W + 120.0f;
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
    if (count > 0) {
        app->menu.select_idx = ui_clampi(app->menu.select_idx, 0, count - 1);
    } else {
        app->menu.select_idx = 0;
    }

    /* List box geometry: rows of 56px, visible count from space. */
    float list_y = y;
    float btn_y = (float)app->height - 4.0f * (SCR_BTN_H * 0.72f + 10.0f) - 20.0f;
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
            srect(rects, &rn, SCR_MAX_RECT_VERTS, list_x - 3.0f, ry - 3.0f, list_w + 6.0f, row_h,
                  SCR_ACC_R * 0.35f, SCR_ACC_G * 0.35f, SCR_ACC_B * 0.35f, 1.0f);
        } else if (hov) {
            srect(rects, &rn, SCR_MAX_RECT_VERTS, list_x - 3.0f, ry - 3.0f, list_w + 6.0f, row_h,
                  0.20f, 0.20f, 0.22f, 1.0f);
        }
        if (clicked) {
            app->menu.select_idx = idx;
            app->menu.delete_armed = false;
        }
        sflush(app, rects, &rn);
        renderer_draw_text(app->renderer, list_x + 10.0f, ry + 6.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G,
                           SCR_TXT_B, 1.0f, app->menu.worlds[idx].name);
        char sub[128];
        world_subtitle(&app->menu.worlds[idx], sub, sizeof(sub));
        renderer_draw_text(app->renderer, list_x + 10.0f, ry + 30.0f, SCR_SMALL_SCALE, SCR_DIM_R, SCR_DIM_G,
                           SCR_DIM_B, 1.0f, sub);
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
        stext_c(app, app->menu.error, cx, by + bh + 12.0f, SCR_TEXT_SCALE, SCR_WARN_R, SCR_WARN_G,
                SCR_WARN_B);
    }
    if (ui->key_escape) {
        app_enter_state(app, GAME_STATE_MAIN_MENU);
        return;
    }
    if (ui->key_return && has) {
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
    bool foc = focused != NULL && *focused;
    srect(rects, rn, SCR_MAX_RECT_VERTS, x, fy, w, fh, 0.03f, 0.03f, 0.04f, 1.0f);
    if (foc) {
        srect(rects, rn, SCR_MAX_RECT_VERTS, x, fy, w, 2.0f, SCR_ACC_R, SCR_ACC_G, SCR_ACC_B, 1.0f);
        srect(rects, rn, SCR_MAX_RECT_VERTS, x, fy + fh - 2.0f, w, 2.0f, SCR_ACC_R, SCR_ACC_G, SCR_ACC_B,
              1.0f);
    }
    sflush(app, rects, rn);
    char shown[96];
    snprintf(shown, sizeof(shown), "%s%s", buf, (foc ? "_" : ""));
    renderer_draw_text(app->renderer, x + 8.0f, fy + 9.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B,
                       1.0f, shown);
    return ui_text_field(ui, focused, buf, cap, x, fy, w, fh);
}

/* Create-world form: name, seed, mode, create/cancel. */
static void screen_create_world(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)app->width * 0.5f;
    sbackground(app, rects, &rn);
    float y = 56.0f;
    y = stitle(app, "Create New World", NULL, cx, y);
    float fw = SCR_BTN_W + 80.0f;
    float fx = cx - fw * 0.5f;

    bool confirm = sfield(app, ui, rects, &rn, "World Name", app->menu.name_buf, sizeof(app->menu.name_buf),
                          &app->menu.name_focused, fx, y, fw);
    y += 22.0f + 34.0f + 14.0f;
    confirm = sfield(app, ui, rects, &rn, "Seed (blank = random)", app->menu.seed_buf,
                     sizeof(app->menu.seed_buf), &app->menu.seed_focused, fx, y, fw) ||
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
        renderer_draw_text(app->renderer, fx, y, SCR_SMALL_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f,
                           prev);
        y += 22.0f;
    }

    /* Mode cycle button. */
    renderer_draw_text(app->renderer, fx, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f,
                       "Game Mode");
    y += 22.0f;
    const char *mode_label = app->menu.create_mode == 1 ? "Mode: Creative" : "Mode: Survival";
    if (sbutton(app, ui, rects, &rn, fx, y, fw, 40.0f, mode_label, true)) {
        app->menu.create_mode = (app->menu.create_mode == 1) ? 0 : 1;
    }
    y += 40.0f + 18.0f;

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
        app_enter_state(app, GAME_STATE_WORLD_SELECT);
        return;
    }
    if (do_create || confirm) {
        char dir[512];
        int rc = session_create_world(SESSION_SAVES_DIR, app->menu.name_buf, app->menu.seed_buf,
                                      app->menu.create_mode, dir);
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
}

/* One slider row: label, track with knob, live value text. */
static void sslider_row(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, const char *label,
                        float *value, float lo, float hi, const char *fmt, bool *held, float x, float y,
                        float w)
{
    renderer_draw_text(app->renderer, x, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f, label);
    float ty = y + 24.0f;
    float th = 10.0f;
    srect(rects, rn, SCR_MAX_RECT_VERTS, x, ty, w, th, 0.10f, 0.10f, 0.12f, 1.0f);
    float t = (*value - lo) / (hi - lo);
    if (!(t >= 0.0f)) {
        t = 0.0f;
    }
    if (!(t <= 1.0f)) {
        t = 1.0f;
    }
    float kx = x + t * w;
    srect(rects, rn, SCR_MAX_RECT_VERTS, kx - 4.0f, ty - 6.0f, 8.0f, th + 12.0f, SCR_ACC_R, SCR_ACC_G,
          SCR_ACC_B, 1.0f);
    sflush(app, rects, rn);
    char val[48];
    snprintf(val, sizeof(val), fmt, (double)*value);
    renderer_draw_text(app->renderer, x + w + 12.0f, ty - 4.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G,
                       SCR_TXT_B, 1.0f, val);
    ui_slider(ui, value, lo, hi, x, ty - 8.0f, w, th + 16.0f, held);
}

/* Settings screen: sliders, toggles, pack cycle, back (saves). */
static void screen_settings(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)app->width * 0.5f;
    sbackground(app, rects, &rn);
    float y = 56.0f;
    y = stitle(app, "Settings", NULL, cx, y);
    float fw = SCR_BTN_W + 160.0f;
    float fx = cx - fw * 0.5f;
    float sw = fw - 130.0f;

    sslider_row(app, ui, rects, &rn, "Render Distance", &app->menu.sl_rd, 2.0f, 8.0f, "%.0f",
                &app->menu.held_rd, fx, y, sw);
    y += 62.0f;
    sslider_row(app, ui, rects, &rn, "Mouse Sensitivity", &app->menu.sl_sens, 0.0005f, 0.010f, "%.4f",
                &app->menu.held_sens, fx, y, sw);
    y += 62.0f;
    sslider_row(app, ui, rects, &rn, "Field of View", &app->menu.sl_fov, 60.0f, 110.0f, "%.0f",
                &app->menu.held_fov, fx, y, sw);
    y += 62.0f;
    sslider_row(app, ui, rects, &rn, "Master Volume", &app->menu.sl_vol, 0.0f, 100.0f, "%.0f",
                &app->menu.held_vol, fx, y, sw);
    y += 62.0f;
    sslider_row(app, ui, rects, &rn, "Effects Volume", &app->menu.sl_sfx, 0.0f, 100.0f, "%.0f",
                &app->menu.held_sfx, fx, y, sw);
    y += 62.0f;

    /* Live-commit sliders every frame (cheap applies; pack handled below). */
    sliders_to_settings(app);
    app_apply_settings(app);

    /* VSync toggle. */
    renderer_draw_text(app->renderer, fx, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f,
                       "VSync");
    char vsync_label[32];
    snprintf(vsync_label, sizeof(vsync_label), "%s", app->settings.vsync ? "ON" : "OFF");
    if (sbutton(app, ui, rects, &rn, fx + sw + 12.0f, y - 6.0f, 118.0f, 36.0f, vsync_label, true)) {
        app->settings.vsync = !app->settings.vsync;
        app_apply_settings(app);
    }
    y += 52.0f;

    /* Pack cycle: "<" + name + ">" (immediate atlas reload on change). */
    renderer_draw_text(app->renderer, fx, y, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B, 1.0f,
                       "Resource Pack");
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
    renderer_measure_text(pack_label, SCR_TEXT_SCALE, &pw, &ph);
    renderer_draw_text(app->renderer, fx + sw + 12.0f, y, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B,
                       1.0f, pack_label);
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

/* Pause menu over the frozen world: Resume / Settings / Save+Quit. */
static void screen_paused(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)app->width * 0.5f;
    /* Dim veil so the frozen world shows through faintly. */
    srect(rects, &rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)app->width, (float)app->height, 0.0f, 0.0f,
          0.0f, 0.55f);
    sflush(app, rects, &rn);
    float y = (float)app->height * 0.28f;
    y = stitle(app, "Game Menu", NULL, cx, y);
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
    if (sbutton(app, ui, rects, &rn, bx, y, SCR_BTN_W, SCR_BTN_H, "Save and Quit to Title", true)) {
        app_resolve_cursor(app);
        app_resolve_crafting(app);
        session_close_world(app, true);
        screens_refresh_worlds(app);
        app_enter_state(app, GAME_STATE_MAIN_MENU);
        return;
    }
    sflush(app, rects, &rn);
    if (ui->key_escape) {
        app_enter_state(app, GAME_STATE_PLAYING);
    }
}

/* Inventory slot metrics (shared by survival + creative screens). */
#define SINV_SLOT 44.0f
#define SINV_GAP 4.0f
#define SINV_PITCH (SINV_SLOT + SINV_GAP)
#define SINV_COLS 9
#define SINV_GRID_W ((float)SINV_COLS * SINV_SLOT + (float)(SINV_COLS - 1) * SINV_GAP)
#define SINV_PAD 16.0f

/* Draw one slot cell background and report hover. The item sprite (if
 * any) goes into the screen's icon batch (one pass after the grids);
 * the stack count goes on top via sslot_count after the icons.
 * Interaction is owned by the caller (survival vs creative differ).
 */
static bool sslot_draw(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, float x, float y,
                       const ItemStack *slot, IconBatch *icons)
{
    bool hov = ui_hit(ui->mouse_x, ui->mouse_y, x, y, SINV_SLOT, SINV_SLOT);
    float b = hov ? 0.30f : 0.16f;
    srect(rects, rn, SCR_MAX_RECT_VERTS, x, y, SINV_SLOT, SINV_SLOT, b, b, b + 0.02f, 0.92f);
    if (!stack_is_empty(slot) && icons != NULL) {
        const ItemInfo *info = item_get_info(slot->item);
        icons_push(icons, x + 8.0f, y + 8.0f, SINV_SLOT - 16.0f, info->tile);
    }
    sflush(app, rects, rn);
    return hov;
}

/* Draw a stack count label (bottom-right of the slot). Called after the
 * icon pass so counts always read over sprites (Minecraft convention).
 */
static void sslot_count(AppContext *app, float x, float y, const ItemStack *slot)
{
    if (stack_is_empty(slot) || slot->count <= 1) {
        return;
    }
    char cb[8];
    snprintf(cb, sizeof(cb), "%u", (unsigned)slot->count);
    float tw = 0.0f;
    float th = 0.0f;
    renderer_measure_text(cb, SCR_SMALL_SCALE + 1.0f, &tw, &th);
    renderer_draw_text(app->renderer, x + SINV_SLOT - tw - 3.0f, y + SINV_SLOT - th - 2.0f,
                       SCR_SMALL_SCALE + 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, cb);
}

/* Survival click handling for one storage slot (pick up / place / merge /
 * swap on LMB; half-split / place-one on RMB). Both stacks stay canonical.
 * Accepted actions click (menu feedback sound); denied clicks are silent.
 */
static void sslot_survival(AppContext *app, const UiFrame *ui, float x, float y, ItemStack *slot,
                           ItemStack *cursor)
{
    if (!ui_hit(ui->mouse_x, ui->mouse_y, x, y, SINV_SLOT, SINV_SLOT)) {
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

/* Draw the held cursor stack at the mouse pointer (backdrop box + count).
 * The item sprite goes into the screen's icon batch (drawn in one pass).
 */
static void scursor_draw(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, IconBatch *icons)
{
    if (stack_is_empty(&app->cursor)) {
        return;
    }
    const ItemInfo *info = item_get_info(app->cursor.item);
    float x = (float)ui->mouse_x - 14.0f;
    float y = (float)ui->mouse_y - 14.0f;
    srect(rects, rn, SCR_MAX_RECT_VERTS, x - 2.0f, y - 2.0f, 32.0f, 32.0f, 0.0f, 0.0f, 0.0f, 0.55f);
    if (icons != NULL) {
        icons_push(icons, x, y, 28.0f, info->tile);
    }
    sflush(app, rects, rn);
    if (app->cursor.count > 1) {
        char cb[8];
        snprintf(cb, sizeof(cb), "%u", (unsigned)app->cursor.count);
        renderer_draw_text(app->renderer, x + 30.0f, y + 16.0f, SCR_SMALL_SCALE + 1.0f, 1.0f, 1.0f,
                           1.0f, 1.0f, cb);
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
static void scraft_take(AppContext *app, const UiFrame *ui, float *rects, size_t *rn, float x, float y,
                        ItemStack *grid, int gw, int gh, uint32_t *hashslot, IconBatch *icons)
{
    sslot_draw(app, ui, rects, rn, x, y, &app->craft_out, icons);
    if (!ui_hit(ui->mouse_x, ui->mouse_y, x, y, SINV_SLOT, SINV_SLOT) || !ui->mouse_clicked) {
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

/* Survival inventory: 27 storage slots + hotbar row, drag-free click
 * model (LMB move/merge/swap, RMB split/place-one). E/ESC closes via the
 * app run loop, which resolves the cursor first.
 */
static void screen_inventory(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)app->width * 0.5f;
    float grid_h = 3.0f * SINV_PITCH + 12.0f + SINV_SLOT;
    float craft_h = 2.0f * SINV_PITCH + 12.0f;
    float panel_w = SINV_GRID_W + SINV_PAD * 2.0f;
    /* +28 bottom pad: the hint line must sit below the hotbar row, not on it. */
    float panel_h = grid_h + craft_h + 88.0f + 28.0f;
    float px = cx - panel_w * 0.5f;
    float py = ((float)app->height - panel_h) * 0.5f;
    if (py < 8.0f) {
        py = 8.0f;
    }
    srect(rects, &rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)app->width, (float)app->height, 0.0f,
          0.0f, 0.0f, 0.55f);
    srect(rects, &rn, SCR_MAX_RECT_VERTS, px, py, panel_w, panel_h, 0.10f, 0.10f, 0.12f, 0.96f);
    sflush(app, rects, &rn);

    stext_c(app, "Inventory", cx, py + 10.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B);
    char vit[96];
    snprintf(vit, sizeof(vit), "Health %.0f/%.0f  Hunger %.0f/%.0f", (double)app->player.health,
             (double)app->player.max_health, (double)app->player.hunger,
             (double)app->player.max_hunger);
    stext_c(app, vit, cx, py + 10.0f + 8.0f * SCR_TEXT_SCALE + 6.0f, SCR_SMALL_SCALE + 1.0f,
            SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);

    float gx = cx - SINV_GRID_W * 0.5f;
    float gy = py + 88.0f;
    IconBatch icons;
    icons_clear(&icons);
    /* Player 2x2 crafting (output derived, take-only). */
    stext_c(app, "Crafting", gx + SINV_PITCH, gy - 20.0f, SCR_SMALL_SCALE + 1.0f, SCR_DIM_R, SCR_DIM_G,
            SCR_DIM_B);
    scraft_recompute(app, app->craft2, 2, 2, &app->craft_hash);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            float x = gx + (float)col * SINV_PITCH;
            float y = gy + (float)row * SINV_PITCH;
            ItemStack *slot = &app->craft2[row * 2 + col];
            sslot_draw(app, ui, rects, &rn, x, y, slot, &icons);
            sslot_survival(app, ui, x, y, slot, &app->cursor);
        }
    }
    scraft_take(app, ui, rects, &rn, gx + 2.0f * SINV_PITCH + 24.0f, gy + SINV_PITCH * 0.5f, app->craft2,
                2, 2, &app->craft_hash, &icons);
    gy += craft_h;
    const char *hov_name = NULL;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < SINV_COLS; ++col) {
            int slot_idx = INV_MAIN_START + row * SINV_COLS + col;
            float x = gx + (float)col * SINV_PITCH;
            float y = gy + (float)row * SINV_PITCH;
            ItemStack *slot = &app->player.inv.slots[slot_idx];
            if (sslot_draw(app, ui, rects, &rn, x, y, slot, &icons)) {
                const ItemInfo *info = item_get_info(slot->item);
                if (!stack_is_empty(slot)) {
                    hov_name = info->name;
                }
            }
            sslot_survival(app, ui, x, y, slot, &app->cursor);
        }
    }
    float hy = gy + 3.0f * SINV_PITCH + 12.0f;
    for (int col = 0; col < SINV_COLS; ++col) {
        float x = gx + (float)col * SINV_PITCH;
        ItemStack *slot = &app->player.inv.slots[col];
        if (sslot_draw(app, ui, rects, &rn, x, hy, slot, &icons)) {
            const ItemInfo *info = item_get_info(slot->item);
            if (!stack_is_empty(slot)) {
                hov_name = info->name;
            }
        }
        if (col == app->player.hotbar_sel) {
            /* Selection outline (4 bars; srect has no outline primitive). */
            srect(rects, &rn, SCR_MAX_RECT_VERTS, x - 2.0f, hy - 2.0f, SINV_SLOT + 4.0f, 2.0f, 1.0f,
                  1.0f, 1.0f, 1.0f);
            srect(rects, &rn, SCR_MAX_RECT_VERTS, x - 2.0f, hy + SINV_SLOT, SINV_SLOT + 4.0f, 2.0f,
                  1.0f, 1.0f, 1.0f, 1.0f);
            srect(rects, &rn, SCR_MAX_RECT_VERTS, x - 2.0f, hy - 2.0f, 2.0f, SINV_SLOT + 4.0f, 1.0f,
                  1.0f, 1.0f, 1.0f);
            srect(rects, &rn, SCR_MAX_RECT_VERTS, x + SINV_SLOT, hy - 2.0f, 2.0f, SINV_SLOT + 4.0f,
                  1.0f, 1.0f, 1.0f, 1.0f);
            sflush(app, rects, &rn);
        }
        sslot_survival(app, ui, x, hy, slot, &app->cursor);
    }
    sflush(app, rects, &rn);
    scursor_draw(app, ui, rects, &rn, &icons);
    /* Icons first, counts over them (Minecraft convention). */
    renderer_draw_item_icons(app->renderer, app->width, app->height, &icons);
    for (int i = 0; i < 4; ++i) {
        int cx2 = i % 2;
        int cy2 = i / 2;
        sslot_count(app, gx + (float)cx2 * SINV_PITCH, gy - craft_h + (float)cy2 * SINV_PITCH,
                    &app->craft2[i]);
    }
    sslot_count(app, gx + 2.0f * SINV_PITCH + 24.0f, gy - craft_h + SINV_PITCH * 0.5f, &app->craft_out);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < SINV_COLS; ++col) {
            sslot_count(app, gx + (float)col * SINV_PITCH, gy + (float)row * SINV_PITCH,
                        &app->player.inv.slots[INV_MAIN_START + row * SINV_COLS + col]);
        }
    }
    for (int col = 0; col < SINV_COLS; ++col) {
        sslot_count(app, gx + (float)col * SINV_PITCH, hy, &app->player.inv.slots[col]);
    }

    if (!stack_is_empty(&app->cursor)) {
        const ItemInfo *info = item_get_info(app->cursor.item);
        char held[96];
        snprintf(held, sizeof(held), "Holding: %s x%u  (LMB place, RMB place one)", info->name,
                 (unsigned)app->cursor.count);
        stext_c(app, held, cx, py + panel_h - 24.0f, SCR_SMALL_SCALE + 1.0f, SCR_ACC_R, SCR_ACC_G,
                SCR_ACC_B);
    } else if (hov_name != NULL) {
        stext_c(app, hov_name, cx, py + panel_h - 24.0f, SCR_SMALL_SCALE + 1.0f, SCR_TXT_R, SCR_TXT_G,
                SCR_TXT_B);
    } else {
        stext_c(app, "LMB move / RMB split  -  E or ESC to close", cx, py + panel_h - 24.0f,
                SCR_SMALL_SCALE + 1.0f, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
    }
}

/* Creative catalogue: every registered item as a free full-stack source
 * (LMB grabs to cursor), plus an editable hotbar row (LMB assigns a copy,
 * empty cursor clears). Closing discards the cursor (infinite sources).
 */
static void screen_creative(AppContext *app, const UiFrame *ui)
{
    /* Catalogue: all valid items in registry order (blocks 1..19 sans
     * water, then materials, then tools). Bounded and allocation-free. */
    ItemId cata[48];
    int ncata = 0;
    for (uint16_t id = 1; id <= 19 && ncata < (int)(sizeof(cata) / sizeof(cata[0])); ++id) {
        if (item_is_valid(id)) {
            cata[ncata++] = id;
        }
    }
    const uint16_t extra[] = {ITEM_COAL, ITEM_APPLE, ITEM_STICK, ITEM_BOW, ITEM_ARROW,
                              ITEM_BONE, ITEM_RAW_BEEF, ITEM_LEATHER, ITEM_ROTTEN_FLESH,
                              ITEM_WOOD_PICKAXE, ITEM_STONE_PICKAXE, ITEM_WOOD_AXE,
                              ITEM_STONE_AXE, ITEM_WOOD_SHOVEL, ITEM_STONE_SHOVEL};
    for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]) && ncata < (int)(sizeof(cata) / sizeof(cata[0])); ++i) {
        if (item_is_valid(extra[i])) {
            cata[ncata++] = extra[i];
        }
    }
    int rows = (ncata + SINV_COLS - 1) / SINV_COLS;
    if (rows < 1) {
        rows = 1;
    }

    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)app->width * 0.5f;
    float grid_h = (float)rows * SINV_PITCH + 12.0f + SINV_SLOT;
    float panel_w = SINV_GRID_W + SINV_PAD * 2.0f;
    float panel_h = grid_h + 88.0f + 28.0f; /* Bottom pad: hint below hotbar. */
    float px = cx - panel_w * 0.5f;
    float py = ((float)app->height - panel_h) * 0.5f;
    if (py < 8.0f) {
        py = 8.0f;
    }
    srect(rects, &rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)app->width, (float)app->height, 0.0f,
          0.0f, 0.0f, 0.55f);
    srect(rects, &rn, SCR_MAX_RECT_VERTS, px, py, panel_w, panel_h, 0.10f, 0.10f, 0.12f, 0.96f);
    sflush(app, rects, &rn);

    stext_c(app, "Creative Inventory", cx, py + 10.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G,
            SCR_TXT_B);
    stext_c(app, "Click an item for a full stack, then click a hotbar slot",
            cx, py + 10.0f + 8.0f * SCR_TEXT_SCALE + 6.0f, SCR_SMALL_SCALE + 1.0f, SCR_DIM_R,
            SCR_DIM_G, SCR_DIM_B);

    float gx = cx - SINV_GRID_W * 0.5f;
    float gy = py + 88.0f;
    IconBatch icons;
    icons_clear(&icons);
    const char *hov_name = NULL;
    for (int i = 0; i < ncata; ++i) {
        int col = i % SINV_COLS;
        int row = i / SINV_COLS;
        float x = gx + (float)col * SINV_PITCH;
        float y = gy + (float)row * SINV_PITCH;
        const ItemInfo *info = item_get_info(cata[i]);
        uint16_t full = info->max_stack > 0 ? info->max_stack : 1;
        ItemStack cell = {cata[i], full, 0};
        if (sslot_draw(app, ui, rects, &rn, x, y, &cell, &icons)) {
            hov_name = info->name;
        }
        if (ui_hit(ui->mouse_x, ui->mouse_y, x, y, SINV_SLOT, SINV_SLOT) && ui->mouse_clicked) {
            app->cursor.item = cata[i];
            app->cursor.count = full;
            app->cursor.durability = 0;
            audio_play(&app->audio, AUDIO_UI_CLICK);
        }
    }
    float hy = gy + (float)rows * SINV_PITCH + 12.0f;
    for (int col = 0; col < SINV_COLS; ++col) {
        float x = gx + (float)col * SINV_PITCH;
        ItemStack *slot = &app->player.inv.slots[col];
        if (sslot_draw(app, ui, rects, &rn, x, hy, slot, &icons)) {
            const ItemInfo *info = item_get_info(slot->item);
            if (!stack_is_empty(slot)) {
                hov_name = info->name;
            }
        }
        if (ui_hit(ui->mouse_x, ui->mouse_y, x, hy, SINV_SLOT, SINV_SLOT) && ui->mouse_clicked) {
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
    sflush(app, rects, &rn);
    scursor_draw(app, ui, rects, &rn, &icons);
    /* Icons first, counts over them. */
    renderer_draw_item_icons(app->renderer, app->width, app->height, &icons);
    for (int i = 0; i < ncata; ++i) {
        int col = i % SINV_COLS;
        int row = i / SINV_COLS;
        const ItemInfo *info = item_get_info(cata[i]);
        uint16_t full = info->max_stack > 0 ? info->max_stack : 1;
        ItemStack cell = {cata[i], full, 0};
        sslot_count(app, gx + (float)col * SINV_PITCH, gy + (float)row * SINV_PITCH, &cell);
    }
    for (int col = 0; col < SINV_COLS; ++col) {
        sslot_count(app, gx + (float)col * SINV_PITCH, hy, &app->player.inv.slots[col]);
    }

    if (!stack_is_empty(&app->cursor)) {
        const ItemInfo *info = item_get_info(app->cursor.item);
        char held[96];
        snprintf(held, sizeof(held), "Holding: %s x%u  (click a hotbar slot)", info->name,
                 (unsigned)app->cursor.count);
        stext_c(app, held, cx, py + panel_h - 24.0f, SCR_SMALL_SCALE + 1.0f, SCR_ACC_R, SCR_ACC_G,
                SCR_ACC_B);
    } else if (hov_name != NULL) {
        stext_c(app, hov_name, cx, py + panel_h - 24.0f, SCR_SMALL_SCALE + 1.0f, SCR_TXT_R, SCR_TXT_G,
                SCR_TXT_B);
    } else {
        stext_c(app, "E or ESC to close", cx, py + panel_h - 24.0f, SCR_SMALL_SCALE + 1.0f,
                SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
    }
}

/* Workbench crafting overlay (survival): 3x3 ingredient grid + derived
 * output, then the full inventory (27 storage + hotbar) below. Same click
 * model as the inventory (grids are plain ItemStacks); closing returns
 * ingredients via app_resolve_crafting.
 */
static void screen_crafting(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)app->width * 0.5f;
    float bench_h = 3.0f * SINV_PITCH;                       /* 3x3 grid block. */
    float stor_h = 3.0f * SINV_PITCH + 12.0f + SINV_SLOT;    /* 27 slots + hotbar. */
    float panel_w = SINV_GRID_W + SINV_PAD * 2.0f;
    float panel_h = bench_h + 12.0f + stor_h + 88.0f + 28.0f; /* Bottom line below hotbar. */
    float px = cx - panel_w * 0.5f;
    float py = ((float)app->height - panel_h) * 0.5f;
    if (py < 8.0f) {
        py = 8.0f;
    }
    srect(rects, &rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)app->width, (float)app->height, 0.0f,
          0.0f, 0.0f, 0.55f);
    srect(rects, &rn, SCR_MAX_RECT_VERTS, px, py, panel_w, panel_h, 0.10f, 0.10f, 0.12f, 0.96f);
    sflush(app, rects, &rn);

    stext_c(app, "Crafting Table", cx, py + 10.0f, SCR_TEXT_SCALE, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B);

    float gx = cx - SINV_GRID_W * 0.5f;
    float gy = py + 88.0f;
    IconBatch icons;
    icons_clear(&icons);
    scraft_recompute(app, app->craft3, 3, 3, &app->craft_hash3);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            float x = gx + (float)col * SINV_PITCH;
            float y = gy + (float)row * SINV_PITCH;
            ItemStack *slot = &app->craft3[row * 3 + col];
            sslot_draw(app, ui, rects, &rn, x, y, slot, &icons);
            sslot_survival(app, ui, x, y, slot, &app->cursor);
        }
    }
    scraft_take(app, ui, rects, &rn, gx + 3.0f * SINV_PITCH + 24.0f, gy + SINV_PITCH, app->craft3, 3, 3,
                &app->craft_hash3, &icons);

    float sy = gy + bench_h + 12.0f;
    const char *hov_name = NULL;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < SINV_COLS; ++col) {
            int slot_idx = INV_MAIN_START + row * SINV_COLS + col;
            float x = gx + (float)col * SINV_PITCH;
            float y = sy + (float)row * SINV_PITCH;
            ItemStack *slot = &app->player.inv.slots[slot_idx];
            if (sslot_draw(app, ui, rects, &rn, x, y, slot, &icons)) {
                const ItemInfo *info = item_get_info(slot->item);
                if (!stack_is_empty(slot)) {
                    hov_name = info->name;
                }
            }
            sslot_survival(app, ui, x, y, slot, &app->cursor);
        }
    }
    float hy = sy + 3.0f * SINV_PITCH + 12.0f;
    for (int col = 0; col < SINV_COLS; ++col) {
        float x = gx + (float)col * SINV_PITCH;
        ItemStack *slot = &app->player.inv.slots[col];
        if (sslot_draw(app, ui, rects, &rn, x, hy, slot, &icons)) {
            const ItemInfo *info = item_get_info(slot->item);
            if (!stack_is_empty(slot)) {
                hov_name = info->name;
            }
        }
        sslot_survival(app, ui, x, hy, slot, &app->cursor);
    }
    sflush(app, rects, &rn);
    scursor_draw(app, ui, rects, &rn, &icons);
    /* Icons first, counts over them. */
    renderer_draw_item_icons(app->renderer, app->width, app->height, &icons);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            sslot_count(app, gx + (float)col * SINV_PITCH, gy + (float)row * SINV_PITCH,
                        &app->craft3[row * 3 + col]);
        }
    }
    sslot_count(app, gx + 3.0f * SINV_PITCH + 24.0f, gy + SINV_PITCH, &app->craft_out);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < SINV_COLS; ++col) {
            sslot_count(app, gx + (float)col * SINV_PITCH, sy + (float)row * SINV_PITCH,
                        &app->player.inv.slots[INV_MAIN_START + row * SINV_COLS + col]);
        }
    }
    for (int col = 0; col < SINV_COLS; ++col) {
        sslot_count(app, gx + (float)col * SINV_PITCH, hy, &app->player.inv.slots[col]);
    }
    if (!stack_is_empty(&app->cursor)) {
        const ItemInfo *info = item_get_info(app->cursor.item);
        char held[96];
        snprintf(held, sizeof(held), "Holding: %s x%u", info->name, (unsigned)app->cursor.count);
        stext_c(app, held, cx, py + panel_h - 22.0f, SCR_SMALL_SCALE + 1.0f, SCR_ACC_R, SCR_ACC_G,
                SCR_ACC_B);
    } else if (hov_name != NULL) {
        stext_c(app, hov_name, cx, py + panel_h - 22.0f, SCR_SMALL_SCALE + 1.0f, SCR_TXT_R, SCR_TXT_G,
                SCR_TXT_B);
    }
}

/* Death screen over the frozen world: respawn at the stable spawn or
 * save + quit to title. No ESC dismissal (an explicit choice is required).
 */
static void screen_dead(AppContext *app, const UiFrame *ui)
{
    float rects[SCR_MAX_RECT_VERTS * 6];
    size_t rn = 0;
    float cx = (float)app->width * 0.5f;
    srect(rects, &rn, SCR_MAX_RECT_VERTS, 0.0f, 0.0f, (float)app->width, (float)app->height, 0.35f,
          0.02f, 0.02f, 0.72f);
    sflush(app, rects, &rn);
    float y = (float)app->height * 0.30f;
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
    float cx = (float)app->width * 0.5f;
    float cy = (float)app->height * 0.5f;
    stext_c(app, line1, cx, cy - 20.0f, SCR_TEXT_SCALE + 1.0f, SCR_TXT_R, SCR_TXT_G, SCR_TXT_B);
    stext_c(app, line2, cx, cy + 24.0f, SCR_TEXT_SCALE, SCR_DIM_R, SCR_DIM_G, SCR_DIM_B);
}

/* F3 debug overlay lines (PLAYING only). */
void screens_draw_debug(AppContext *app)
{
    if (app == NULL || !app->world_open || app->world == NULL) {
        return;
    }
    char lines[13][128];
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
    snprintf(lines[0], sizeof(lines[0]), "TerraCraft debug - %.1f FPS", (double)app->fps_smooth);
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
    snprintf(lines[8], sizeof(lines[8]), "Mesh: %.2f ms  Up: %.2f ms  Draw: %.2f ms", perf.mesh_ms,
             perf.upload_ms, perf.draw_ms);
    snprintf(lines[9], sizeof(lines[9]), "Mobs: %d living (%d cow %d zomb %d skel), %d drawn %d culled",
             mob_active_count(&app->mobs), mob_count_type(&app->mobs, ENTITY_COW),
              mob_count_type(&app->mobs, ENTITY_ZOMBIE), mob_count_type(&app->mobs, ENTITY_SKELETON),
             app->mobs_drawn, app->mobs_culled);
    snprintf(lines[10], sizeof(lines[10]), "AI: thinks %u, paths %u (pool rng strain)",
             (unsigned)app->mobs.ai_thinks, (unsigned)app->mobs.path_reqs);
    snprintf(lines[11], sizeof(lines[11]), "Arrows: %d flying (%d stuck), fired %u hits %u, drawn %d",
             projectile_active_count(&app->projectiles),
             projectile_embedded_count(&app->projectiles), (unsigned)app->projectiles.fired,
             (unsigned)app->projectiles.impacts, app->arrows_drawn);
    float y = 12.0f;
    for (int i = 0; i < 12 && lines[i][0] != '\0'; ++i) {
        renderer_draw_text(app->renderer, 12.0f, y, SCR_SMALL_SCALE + 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                           lines[i]);
        y += 22.0f;
    }
}

/* Dispatch the active menu/overlay state. PLAYING/QUIT are app-owned. */
void screens_update(AppContext *app, const UiFrame *ui)
{
    if (app == NULL || ui == NULL) {
        return;
    }
    switch (app->state) {
    case GAME_STATE_MAIN_MENU:
        screen_main_menu(app, ui);
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
}
