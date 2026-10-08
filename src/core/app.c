#include "core/app.h"
#include "audio/audio.h"
#include "core/log.h"
#include "core/mem.h"
#include "core/profile.h"
#include "core/time.h"
#include "game/audio.h"
#include "game/entity.h"
#include "game/particle.h"
#include "game/interaction.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/player.h"
#include "game/player_animation.h"
#include "game/raycast.h"
#include "game/session.h"
#include "game/survival.h"
#include "game/time_system.h"
#include "platform/gl_ctx.h"
#include "platform/lan.h"
#include "platform/lan_discover.h"
#include "platform/window.h"
#include "render/camera.h"
#include "render/renderer.h"
#include "ui/screens.h"
#include "ui/ui.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/streamer.h"
#include "world/world.h"
#include "world/world_gen.h"
#include "world/world_meta.h"
#include "world/water.h"
#include "world/world_save.h"

/* Portable SDL include for scancodes (app polls keyboard state directly;
 * window.h wraps the state query to keep SDL out of its header). */
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

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Apply tunables to live systems (vsync, FOV, sensitivity, streamer).
 * Atlas pack reloads are owned by settings-screen/init paths (pack change
 * detection needs the cached list), not here.
 *
 * Args:
 *   app: context.
 */
void app_apply_settings(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    settings_clamp(&app->settings);
    window_set_vsync(app->window, app->settings.vsync);
    if (app->camera != NULL) {
        camera_set_fov_y(app->camera, app->settings.fov);
    }
    app->sensitivity = app->settings.sensitivity;
    audio_set_volumes(&app->audio, (float)app->settings.volume / 100.0f,
                      (float)app->settings.sfx_volume / 100.0f);
    if (audio_load_pack(&app->audio, app->settings.pack) != 0) {
        LOG_WARN("app_apply_settings: audio pack load failed (synth kept)");
    }
    if (app->streamer_ready) {
        int rd = app->settings.render_distance;
        if (rd < 2) {
            rd = 2;
        }
        if (rd > 8) {
            rd = 8;
        }
        app->streamer.render_distance = rd;
    }
}

/* Request a state transition (validated, side effects applied).
 *
 * Args:
 *   app: context.
 *   to: requested state.
 */
void app_enter_state(AppContext *app, GameState to)
{
    if (app == NULL) {
        return;
    }
    if (to == app->state) {
        return;
    }
    if (!game_state_can_transition(app->state, to)) {
        LOG_WARN("app_enter_state: illegal %s -> %s", game_state_name(app->state), game_state_name(to));
        return;
    }
    LOG_INFO("state: %s -> %s", game_state_name(app->state), game_state_name(to));
    GameState from = app->state;
    app->state = to;
    app->menu.menu_anim_at = app->last_frame_time;
    if (to == GAME_STATE_MAIN_MENU) {
        app->menu.main_focus = -1;
    }
    if (to == GAME_STATE_LAN_MENU) {
        /* Fresh scan every visit: stale entries never greet the player. */
        app->menu.lan_server_idx = -1;
        app->menu.lan_show_direct = false;
        app->menu.lan_poll_at = app->last_frame_time;
        app->menu.error[0] = '\0';
        lan_discover_destroy(app->lan_discover);
        app->lan_discover = lan_discover_create();
        if (app->lan_discover == NULL) {
            snprintf(app->menu.error, sizeof(app->menu.error),
                     "Auto-scan unavailable; use direct connect below.");
        }
    }
    if (from == GAME_STATE_LAN_MENU && (to == GAME_STATE_PLAYING || to == GAME_STATE_MAIN_MENU)) {
        lan_discover_destroy(app->lan_discover);
        app->lan_discover = NULL;
    }
    window_clear_input_edges(app->window);
    if (to != GAME_STATE_PLAYING && app->chat_open) {
        app->chat_open = false;
        app->chat_input[0] = '\0';
    }
    if (!game_state_ticks_world(from) && game_state_ticks_world(to)) {
        app->discard_next_simulation_elapsed = true;
    }
    if (game_state_ticks_world(from) != game_state_ticks_world(to) || to == GAME_STATE_PLAYING) {
        simulation_clock_reset_phase(&app->simulation);
    }
    bool capture = (to == GAME_STATE_PLAYING);
    window_set_relative_mouse(app->window, capture);
    window_text_input(to == GAME_STATE_CREATE_WORLD || to == GAME_STATE_PROFILE || to == GAME_STATE_LAN_MENU);
    if (game_state_ticks_world(from) && to != GAME_STATE_PLAYING) {
        /* Eating and bow draws never survive a state change (interrupted
         * uses consume nothing): pause, inventory, death, and quit all
         * cancel them. */
        app->player.eat_active = false;
        app->player.eat_t = 0.0f;
        survival_bow_reset(&app->player);
        survival_mine_reset(&app->player);
    }
    if (to == GAME_STATE_PLAYING) {
        app->chat_open = false;
        /* Re-entering play (resume/respawn/inventory-close): the menu click
         * that got us here must not replay as a gameplay edge (instant
         * creative break, survival mine/place), and held keys must not
         * retrigger toggles. Sync every prev edge to live hardware.
         */
        bool ml = window_is_mouse_down(MINEC_MOUSE_LEFT);
        bool mr = window_is_mouse_down(MINEC_MOUSE_RIGHT);
        app->prev_mouse_l = ml;
        app->prev_mouse_r = mr;
        app->prev_menu_click = ml;
        app->prev_menu_rclick = mr;
        app->prev_fly_key = window_is_key_down(SDL_SCANCODE_F);
        app->prev_f3_key = window_is_key_down(SDL_SCANCODE_F3);
        app->prev_e_key = window_is_key_down(SDL_SCANCODE_E);
        app->prev_f6_key = window_is_key_down(SDL_SCANCODE_F6);
        app->prev_f8_key = window_is_key_down(SDL_SCANCODE_F8);
        app->prev_f7_key = window_is_key_down(SDL_SCANCODE_F7);
        app->prev_f9_key = window_is_key_down(SDL_SCANCODE_F9);
        app->prev_f10_key = window_is_key_down(SDL_SCANCODE_F10);
        app->prev_f11_key = window_is_key_down(SDL_SCANCODE_F11);
        app->prev_f12_key = window_is_key_down(SDL_SCANCODE_F12);
        for (int i = 0; i < 9; ++i) {
            app->prev_digit[i] = window_is_key_down(SDL_SCANCODE_1 + i);
        }
        app->prev_return = window_is_key_down(SDL_SCANCODE_RETURN) ||
                           window_is_key_down(SDL_SCANCODE_KP_ENTER);
        app->prev_backspace = window_is_key_down(SDL_SCANCODE_BACKSPACE);
        app->prev_up = window_is_key_down(SDL_SCANCODE_UP);
        app->prev_down = window_is_key_down(SDL_SCANCODE_DOWN);
        app->prev_delete = window_is_key_down(SDL_SCANCODE_DELETE);
        app->prev_jump = window_is_key_down(SDL_SCANCODE_SPACE);
    }
    if (to == GAME_STATE_WORLD_SELECT) {
        screens_refresh_worlds(app);
    }
    if (to == GAME_STATE_PAUSED) {
        /* Pause insurance: persist before idling (cheap: meta + dirty). */
        session_save_now(app);
        app->autosave_timer = 0.0;
    }
    if (to == GAME_STATE_LOADING) {
        /* Vista camera while chunks stream in behind the overlay. */
        camera_set_position(app->camera, mmath_vec3(8.0f, 110.0f, 40.0f));
        camera_set_yaw_pitch(app->camera, 0.0f, -0.7f);
    }
}

/* Initialise the app: settings, window, GL, renderer, camera.
 * Boots into MAIN_MENU with no world open (M5 game shell).
 *
 * Args:
 *   app: context to initialise.
 *   width, height: initial window size.
 *   title: window title.
 *
 * Returns: 0 on success, non-zero on failure.
 */
int app_init(AppContext *app, int width, int height, const char *title)
{
    if (app == NULL || title == NULL || width <= 0 || height <= 0) {
        return -1;
    }
    /* Zero everything first: optionals (audio device, pointers, timers)
     * must read sane before their subsystems initialise. Individual
     * fields are still set explicitly below for readability. */
    memset(app, 0, sizeof(*app));

    app->window = NULL;
    app->gl = NULL;
    app->renderer = NULL;
    app->camera = NULL;
    app->world = NULL;
    app->world_open = false;
    app->world_dir[0] = '\0';
    app->player_ready = false;
    app->streamer_ready = false;
    app->running = false;
    app->width = width;
    app->height = height;
    app->start_time = 0.0;
    app->last_fps_time = 0.0;
    app->last_frame_time = 0.0;
    app->frame_count = 0;
    app->fps_smooth = 0.0f;
    app->frame_ms_avg = 0.0f;
    app->sim_ms_avg = 0.0f;
    app->sim_tps = 0.0f;
    app->process_working_set_bytes = 0;
    app->show_debug = false;
    app->autosave_timer = 0.0;
    simulation_clock_init(&app->simulation);
    app->discard_next_simulation_elapsed = false;
    app->load_done = 0;
    app->load_total = 0;
    app->state = GAME_STATE_PROFILE;
    app->sensitivity = MINEC_MOUSE_SENSITIVITY;
    memset(&app->menu, 0, sizeof(app->menu));
    app->menu.main_focus = -1;
    app->menu.lan_server_idx = -1;
    if (profile_load(PROFILE_PATH, app->username, sizeof(app->username)) == PROFILE_OK) {
        app->state = GAME_STATE_MAIN_MENU;
    } else {
        app->menu.name_focused = true;
    }
    entity_pool_clear(&app->entities);
    mob_pool_init(&app->mobs, 0x85EBCA6Bu);
    projectile_pool_clear(&app->projectiles);
    particle_pool_clear(&app->particles);
    app->mine_fx_t = 0.0f;
    stack_clear(&app->cursor);
    app->spawn_point = mmath_vec3(8.5f, 120.0f, 8.5f);
    app->has_spawn_point = false;
    app->player_from_save = false;
    app->prev_mouse_l = false;
    app->prev_mouse_r = false;
    app->prev_menu_click = false;
    app->prev_fly_key = false;
    app->prev_f3_key = false;
    app->prev_f5_key = false;
    app->third_person = false;
    app->prev_e_key = false;
    app->prev_f6_key = false;
    app->prev_f8_key = false;
    app->prev_f7_key = false;
    app->prev_f9_key = false;
    app->prev_f10_key = false;
    app->prev_menu_rclick = false;
    app->prev_return = false;
    app->prev_backspace = false;
    app->prev_up = false;
    app->prev_down = false;
    app->prev_delete = false;
    for (int i = 0; i < 9; ++i) {
        app->prev_digit[i] = false;
    }
    app->prev_jump = false;
    app->step_dist = 0.0f;
    app->hurt_flash = 0.0f;
    app->last_health = 20.0f;
    app->mobs_drawn = 0;
    app->mobs_culled = 0;
    for (int i = 0; i < 4; ++i) {
        stack_clear(&app->craft2[i]);
    }
    for (int i = 0; i < 9; ++i) {
        stack_clear(&app->craft3[i]);
    }
    app->craft_hash = 0;
    app->craft_hash3 = 0;
    stack_clear(&app->craft_out);
    settings_load(&app->settings, SETTINGS_PATH);
    app->sensitivity = app->settings.sensitivity;
    /* Audio backend (never fatal: silent when no device). Volumes come
     * from settings; pack overrides load on first apply below. */
    if (audio_init(&app->audio) != 0) {
        LOG_WARN("app_init: audio init failed (continuing silent)");
    }
    audio_set_volumes(&app->audio, (float)app->settings.volume / 100.0f,
                      (float)app->settings.sfx_volume / 100.0f);

    if (log_init() != 0) {
        return -2;
    }
    app->lan = lan_create();
    if (app->lan == NULL) {
        LOG_WARN("app_init: LAN transport unavailable; singleplayer remains usable");
    }
    app->lan_discover = NULL;
    LOG_INFO("TerraCraft M9 initialising (%dx%d) \"%s\"", width, height, title);

    app->window = window_create(title, width, height);
    if (app->window == NULL) {
        LOG_ERROR("app_init: window_create failed");
        app_shutdown(app);
        return -3;
    }
    window_get_size(app->window, &app->width, &app->height);

    app->gl = gl_ctx_init(app->window);
    if (app->gl == NULL) {
        LOG_ERROR("app_init: gl_ctx_init failed");
        app_shutdown(app);
        return -4;
    }

    app->renderer = renderer_create(app->gl);
    if (app->renderer == NULL) {
        LOG_ERROR("app_init: renderer_create failed");
        app_shutdown(app);
        return -5;
    }
    renderer_set_viewport(app->renderer, app->width, app->height);
    if (strcmp(app->settings.pack, "Default") != 0) {
        renderer_reload_atlas(app->renderer, app->settings.pack);
    }

    app->camera = camera_create();
    if (app->camera == NULL) {
        LOG_ERROR("app_init: camera_create failed");
        app_shutdown(app);
        return -6;
    }
    app_apply_settings(app);

    app->start_time = time_now_seconds();
    app->last_fps_time = app->start_time;
    app->last_frame_time = app->start_time;
    app->frame_count = 0;
    app->running = true;
    window_text_input(app->state == GAME_STATE_PROFILE);

    LOG_INFO("TerraCraft M9 initialised. GL: %s / %s", gl_ctx_get_vendor(), gl_ctx_get_renderer());
    LOG_INFO("Startup screen: %s", game_state_name(app->state));
    return 0;
}

/* Build the physics input snapshot from held keys.
 *
 * Args:
 *   in: receiver (must not be NULL).
 */
static void app_poll_move_input(PlayerInput *in)
{
    in->fwd = 0.0f;
    in->strafe = 0.0f;
    in->jump = false;
    in->sneak = false;
    in->sprint = false;
    if (window_is_key_down(SDL_SCANCODE_W)) {
        in->fwd += 1.0f;
    }
    if (window_is_key_down(SDL_SCANCODE_S)) {
        in->fwd -= 1.0f;
    }
    if (window_is_key_down(SDL_SCANCODE_D)) {
        in->strafe += 1.0f;
    }
    if (window_is_key_down(SDL_SCANCODE_A)) {
        in->strafe -= 1.0f;
    }
    if (window_is_key_down(SDL_SCANCODE_SPACE)) {
        in->jump = true;
    }
    if (window_is_key_down(SDL_SCANCODE_LSHIFT) || window_is_key_down(SDL_SCANCODE_RSHIFT)) {
        in->sneak = true;
    }
    if (window_is_key_down(SDL_SCANCODE_LCTRL) || window_is_key_down(SDL_SCANCODE_RCTRL)) {
        in->sprint = true;
    }
}

/* Handle discrete in-game actions: fly/F3 toggles, hotbar keys/wheel,
 * E inventory toggle, F6/F8 dev helpers, creative instant break/place.
 * Survival mining consumes buffered press edges and held state on sim ticks
 * (see app_tick_mining). PLAYING only; call after the event drain.
 *
 * Args:
 *   app: context.
 */
static bool app_aim_mob(AppContext *app, EntityId *out_id);
static bool app_strike_mob(AppContext *app, EntityId eid, float dmg, float knock_power);
static void app_start_arm_swing(AppContext *app);
static bool app_try_attack(AppContext *app);
static bool app_drop_stack_from_player(AppContext *app, ItemStack *stack, bool all);
static void app_poll_cursor_drop(AppContext *app);
static void app_debug_spawn_mob(AppContext *app, EntityType type);
static void app_position_camera(AppContext *app);
static void app_draw_player_third(AppContext *app, float aspect);
static void app_poll_discrete_input(AppContext *app)
{
    bool creative = survival_is_creative(&app->player);

    /* Fly toggle on F press (creative only; survival has no flight). */
    bool f_down = window_is_key_down(SDL_SCANCODE_F);
    bool f_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F);
    if (f_pressed && creative) {
        app->player.flying = !app->player.flying;
        app->player.vel = mmath_vec3(0.0f, 0.0f, 0.0f);
        LOG_INFO("Game mode: %s", app->player.flying ? "Creative (fly/no-clip)" : "Survival (gravity/collision)");
    }
    app->prev_fly_key = f_down;

    /* F3 debug overlay toggle. */
    bool f3_down = window_is_key_down(SDL_SCANCODE_F3);
    bool f3_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F3);
    if (f3_pressed) {
        app->show_debug = !app->show_debug;
        LOG_INFO("Debug overlay: %s", app->show_debug ? "on" : "off");
    }
    app->prev_f3_key = f3_down;

    /* F5 third-person toggle (chase camera + visible Steve body). */
    bool f5_down = window_is_key_down(SDL_SCANCODE_F5);
    bool f5_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F5);
    if (f5_pressed) {
        app->third_person = !app->third_person;
        LOG_INFO("Camera: %s", app->third_person ? "third-person" : "first-person");
    }
    app->prev_f5_key = f5_down;

    /* E toggles the inventory (pauses sim; cursor resolved on close). */
    bool e_down = window_is_key_down(SDL_SCANCODE_E);
    bool e_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_E);
    if (e_pressed) {
        survival_mine_reset(&app->player);
        app_enter_state(app, GAME_STATE_INVENTORY);
    }
    app->prev_e_key = e_down;
    if (app->state != GAME_STATE_PLAYING) {
        return; /* E opened the inventory: no hotbar/wheel/click this tick. */
    }

    /* Q drops one item; Ctrl+Q drops the selected stack. Edge-triggering
     * keeps keyboard repeat from scattering several stacks per press. */
    if (window_take_key_pressed(app->window, SDL_SCANCODE_Q)) {
        bool all = window_is_key_down(SDL_SCANCODE_LCTRL) || window_is_key_down(SDL_SCANCODE_RCTRL);
        ItemStack *held = &app->player.inv.slots[app->player.hotbar_sel];
        if (!stack_is_empty(held) && !app_drop_stack_from_player(app, held, all)) {
            LOG_WARN("drop: item pool full; selected stack kept");
        }
    }

    /* Dev helpers (debug builds only: NDEBUG strips them from Release so
     * shipped games cannot spawn mobs, deal self-damage, or conjure
     * stacks). F6 gives 64 of the selected stack (or stone when the slot
     * is empty); F8 deals 5 damage (survival only); F7 hurts the aimed
     * mob; F9/F10/F11 spawn cow/zombie/skeleton; F12 clears all arrows
     * (shift+F6 gives bow + arrows). */
#ifndef NDEBUG
    bool f6_down = window_is_key_down(SDL_SCANCODE_F6);
    bool f6_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F6);
    if (f6_pressed) {
        bool shift = window_is_key_down(SDL_SCANCODE_LSHIFT) || window_is_key_down(SDL_SCANCODE_RSHIFT);
        if (shift) {
            /* Shift+F6: ranged kit (bow + full arrow stack). */
            ItemStack bow = {ITEM_BOW, 1, 0};
            ItemStack arrows = {ITEM_ARROW, 64, 0};
            ItemStack rb = bow;
            ItemStack ra = arrows;
            uint16_t lb = inv_insert(&app->player.inv, &rb);
            uint16_t la = inv_insert(&app->player.inv, &ra);
            LOG_INFO("Dev give: bow + arrows (%u/%u leftover)", (unsigned)lb, (unsigned)la);
        } else {
            ItemStack give = {ITEM_NONE, 0, 0};
            const ItemStack *sel = &app->player.inv.slots[app->player.hotbar_sel];
            give.item = stack_is_empty(sel) ? (ItemId)BLOCK_STONE : sel->item;
            give.count = item_get_info(give.item)->max_stack;
            if (give.count == 0) {
                give.count = 1;
            }
            ItemStack rest = give;
            uint16_t left = inv_insert(&app->player.inv, &rest);
            LOG_INFO("Dev give: %u x %s (%u leftover)", (unsigned)give.count,
                     item_get_info(give.item)->name, left);
        }
    }
    app->prev_f6_key = f6_down;
    bool f8_down = window_is_key_down(SDL_SCANCODE_F8);
    bool f8_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F8);
    if (f8_pressed && !creative) {
        survival_damage_player(&app->player, 5.0f);
        player_anim_notify_hurt(&app->panim);
        audio_play(&app->audio, AUDIO_PLAYER_HURT);
        LOG_INFO("Dev hurt: 5 damage (HP %.1f)", (double)app->player.health);
    }
    app->prev_f8_key = f8_down;
    bool f7_down = window_is_key_down(SDL_SCANCODE_F7);
    bool f7_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F7);
    if (f7_pressed) {
        EntityId eid = ENTITY_ID_NULL;
        if (app_aim_mob(app, &eid)) {
            LOG_INFO("Dev mob hurt: %s", app_strike_mob(app, eid, 5.0f, 5.0f) ? "5 damage" : "no damage");
        } else {
            LOG_INFO("Dev mob hurt: no mob aimed");
        }
    }
    app->prev_f7_key = f7_down;
    bool f9_down = window_is_key_down(SDL_SCANCODE_F9);
    bool f9_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F9);
    if (f9_pressed) {
        app_debug_spawn_mob(app, ENTITY_COW);
    }
    app->prev_f9_key = f9_down;
    bool f10_down = window_is_key_down(SDL_SCANCODE_F10);
    bool f10_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F10);
    if (f10_pressed) {
        app_debug_spawn_mob(app, ENTITY_ZOMBIE);
    }
    app->prev_f10_key = f10_down;
    bool f11_down = window_is_key_down(SDL_SCANCODE_F11);
    bool f11_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F11);
    if (f11_pressed) {
        app_debug_spawn_mob(app, ENTITY_SKELETON);
    }
    app->prev_f11_key = f11_down;
    bool f12_down = window_is_key_down(SDL_SCANCODE_F12);
    bool f12_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F12);
    if (f12_pressed) {
        int n = projectile_active_count(&app->projectiles);
        projectile_pool_clear(&app->projectiles);
        LOG_INFO("Dev projectiles cleared: %d arrow(s)", n);
    }
    app->prev_f12_key = f12_down;
#endif /* NDEBUG: dev helpers are debug-only. */

    /* Hotbar digits 1..9 (SDL scancodes are consecutive). Slot switches
     * reset survival mining (spec: changing slots updates mining) and
     * cancel bow draws (never fires, never consumes). */
    for (int i = 0; i < 9; ++i) {
        bool down = window_is_key_down(SDL_SCANCODE_1 + i);
if (window_take_key_pressed(app->window, SDL_SCANCODE_1 + i)) {
            app->player.hotbar_sel = i;
            survival_mine_reset(&app->player);
            survival_bow_reset(&app->player);
            audio_play(&app->audio, AUDIO_UI_CLICK);
            const ItemInfo *info = item_get_info(app->player.inv.slots[i].item);
            LOG_INFO("Hotbar: slot %d (%s)", i + 1, info ? info->name : "?");
        }
        app->prev_digit[i] = down;
    }

    /* Mouse wheel cycles the selection (consumes the accumulated ticks). */
    int wheel = window_take_wheel_delta(app->window);
    if (wheel != 0) {
        int sel = app->player.hotbar_sel - wheel; /* Wheel up -> previous slot. */
        sel %= 9;
        if (sel < 0) {
            sel += 9;
        }
        app->player.hotbar_sel = sel;
        survival_mine_reset(&app->player);
        survival_bow_reset(&app->player);
        audio_play(&app->audio, AUDIO_UI_CLICK);
        const ItemInfo *info = item_get_info(app->player.inv.slots[sel].item);
        LOG_INFO("Hotbar: slot %d (%s)", sel + 1, info ? info->name : "?");
    }

    /* Mouse actions are consumed by the fixed world tick so a press between
     * ticks remains queued until gameplay can act on it. */
}

bool app_commit_profile(AppContext *app)
{
    if (app == NULL || app->state != GAME_STATE_PROFILE) {
        return false;
    }
    const char *chosen = app->menu.profile_buf;
    char generated[PROFILE_NAME_MAX_LEN + 1];
    if (chosen[0] == '\0') {
        uint64_t seed = (uint64_t)(time_now_seconds() * 1000000000.0) ^
                        (uint64_t)SDL_GetPerformanceCounter() ^ (uint64_t)(uintptr_t)app;
        if (profile_generate_name(generated, sizeof(generated), seed) != PROFILE_OK) {
            snprintf(app->menu.error, sizeof(app->menu.error), "Could not generate a username.");
            return false;
        }
        chosen = generated;
    }
    if (!profile_name_valid(chosen)) {
        snprintf(app->menu.error, sizeof(app->menu.error),
                 "Use 3-24 letters, numbers, or underscores.");
        return false;
    }
    if (profile_save(PROFILE_PATH, chosen) != PROFILE_OK) {
        snprintf(app->menu.error, sizeof(app->menu.error), "Could not save username in config/profile.cfg.");
        return false;
    }
    size_t len = strlen(chosen);
    memcpy(app->username, chosen, len + 1);
    memcpy(app->menu.profile_buf, chosen, len + 1);
    app->menu.error[0] = '\0';
    app_enter_state(app, GAME_STATE_MAIN_MENU);
    return app->state == GAME_STATE_MAIN_MENU;
}

enum {
    APP_LAN_MSG_HELLO = 1,
    APP_LAN_MSG_WELCOME = 2,
    APP_LAN_MSG_PLAYER = 3,
    APP_LAN_MSG_CHAT = 4,
    APP_LAN_MSG_BLOCK = 5,
    APP_LAN_MSG_PLAYER_LEFT = 6,
    APP_LAN_MSG_GRAVITY_START = 7,
    APP_LAN_MSG_GRAVITY_LAND = 8,
    APP_LAN_PROTOCOL_VERSION = 2,
    APP_LAN_PORT = 25566,
    APP_CHAT_MAX_BYTES = 160
};

#define APP_CHAT_LINE_HEIGHT 20.0f
#define APP_CHAT_HIDE_AFTER_SECONDS 5.0
#define APP_CHAT_FADE_SECONDS 1.0
#define APP_CHAT_RECENT_VISIBLE 6u

static void app_net_write_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static uint16_t app_net_read_u16(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
}

static void app_net_write_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static uint32_t app_net_read_u32(const uint8_t *in)
{
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) | (uint32_t)in[3];
}

static void app_net_write_u64(uint8_t *out, uint64_t value)
{
    for (int i = 7; i >= 0; --i) {
        out[i] = (uint8_t)value;
        value >>= 8;
    }
}

static uint64_t app_net_read_u64(const uint8_t *in)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | in[i];
    }
    return value;
}

static void app_net_write_f32(uint8_t *out, float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    app_net_write_u32(out, bits);
}

static float app_net_read_f32(const uint8_t *in)
{
    uint32_t bits = app_net_read_u32(in);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void app_chat_add(AppContext *app, const char *username, const char *message)
{
    if (app == NULL || username == NULL || message == NULL) {
        return;
    }
    unsigned slot = app->chat_line_head % APP_CHAT_HISTORY_LINES;
    snprintf(app->chat_lines[slot], sizeof(app->chat_lines[slot]), "%s: %s", username, message);
    app->chat_line_times[slot] = time_now_seconds();
    app->chat_line_head = (slot + 1u) % APP_CHAT_HISTORY_LINES;
    if (app->chat_line_count < APP_CHAT_HISTORY_LINES) {
        app->chat_line_count++;
    }
    app->chat_scroll = 0;
}

static bool app_chat_encode(uint8_t *out, size_t out_cap, const char *username,
                            const char *message, size_t message_len, size_t *out_len)
{
    size_t name_len = username != NULL ? strlen(username) : 0;
    if (out == NULL || out_len == NULL || !profile_name_valid(username) || name_len > PROFILE_NAME_MAX_LEN ||
        message == NULL || message_len == 0 || message_len > APP_CHAT_MAX_BYTES ||
        4u + name_len + message_len > out_cap) {
        return false;
    }
    size_t at = 0;
    out[at++] = APP_LAN_MSG_CHAT;
    out[at++] = (uint8_t)name_len;
    memcpy(out + at, username, name_len);
    at += name_len;
    app_net_write_u16(out + at, (uint16_t)message_len);
    at += 2;
    memcpy(out + at, message, message_len);
    at += message_len;
    *out_len = at;
    return true;
}

static LanRemotePlayer *app_lan_find_player(AppContext *app, uint32_t player_id, bool create)
{
    if (app == NULL || player_id == app->lan_local_player_id) {
        return NULL;
    }
    LanRemotePlayer *free_slot = NULL;
    for (size_t i = 0; i < MINEC_LAN_MAX_PLAYERS; ++i) {
        LanRemotePlayer *remote = &app->lan_players[i];
        if (remote->active && remote->peer_id == player_id) {
            return remote;
        }
        if (!remote->active && free_slot == NULL) {
            free_slot = remote;
        }
    }
    if (!create || free_slot == NULL) {
        return NULL;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->active = true;
    free_slot->peer_id = player_id;
    app->lan_player_count++;
    return free_slot;
}

static void app_lan_remove_player(AppContext *app, uint32_t player_id)
{
    if (app == NULL) {
        return;
    }
    for (size_t i = 0; i < MINEC_LAN_MAX_PLAYERS; ++i) {
        if (app->lan_players[i].active && app->lan_players[i].peer_id == player_id) {
            memset(&app->lan_players[i], 0, sizeof(app->lan_players[i]));
            if (app->lan_player_count > 0) {
                app->lan_player_count--;
            }
            break;
        }
    }
}

static bool app_lan_send_player(AppContext *app, uint32_t target, uint32_t player_id,
                                const char *username, Vec3 pos, float yaw, float pitch,
                                float walk_phase, bool sneaking, bool moving)
{
    if (app == NULL || app->lan == NULL || !profile_name_valid(username)) {
        return false;
    }
    uint8_t payload[64];
    size_t name_len = strlen(username);
    size_t at = 0;
    payload[at++] = APP_LAN_MSG_PLAYER;
    app_net_write_u32(payload + at, player_id);
    at += 4;
    app_net_write_f32(payload + at, pos.x); at += 4;
    app_net_write_f32(payload + at, pos.y); at += 4;
    app_net_write_f32(payload + at, pos.z); at += 4;
    app_net_write_f32(payload + at, yaw); at += 4;
    app_net_write_f32(payload + at, pitch); at += 4;
    app_net_write_f32(payload + at, walk_phase); at += 4;
    payload[at++] = (uint8_t)((sneaking ? 1u : 0u) | (moving ? 2u : 0u));
    payload[at++] = (uint8_t)name_len;
    memcpy(payload + at, username, name_len);
    at += name_len;
    return lan_send(app->lan, target, payload, at);
}

static void app_lan_send_current_players(AppContext *app, uint32_t target)
{
    if (app == NULL || app->lan == NULL || !app->world_open) {
        return;
    }
    (void)app_lan_send_player(app, target, app->lan_local_player_id, app->username,
                              app->player.render_pos, app->player.yaw, app->player.pitch,
                              app->player.walk_phase, app->player.sneaking,
                              sqrtf(app->player.vel.x * app->player.vel.x + app->player.vel.z * app->player.vel.z) > 0.15f);
    for (size_t i = 0; i < MINEC_LAN_MAX_PLAYERS; ++i) {
        const LanRemotePlayer *remote = &app->lan_players[i];
        if (remote->active && remote->username[0] != '\0') {
            (void)app_lan_send_player(app, target, remote->peer_id, remote->username,
                                      remote->pos, remote->yaw, remote->pitch,
                                      remote->walk_phase, remote->sneaking, remote->moving);
        }
    }
}

static bool app_lan_send_welcome(AppContext *app, uint32_t peer_id)
{
    if (app == NULL || app->lan == NULL || !app->world_open || !profile_name_valid(app->username)) {
        return false;
    }
    uint8_t payload[96];
    size_t name_len = strlen(app->username);
    size_t at = 0;
    payload[at++] = APP_LAN_MSG_WELCOME;
    payload[at++] = APP_LAN_PROTOCOL_VERSION;
    app_net_write_u32(payload + at, peer_id); at += 4;
    app_net_write_u64(payload + at, (uint64_t)(int64_t)app->world->seed); at += 8;
    payload[at++] = (uint8_t)app->world->terrain_version;
    payload[at++] = (uint8_t)app->world->mode;
    app_net_write_f32(payload + at, app->player.pos.x); at += 4;
    app_net_write_f32(payload + at, app->player.pos.y); at += 4;
    app_net_write_f32(payload + at, app->player.pos.z); at += 4;
    app_net_write_f32(payload + at, app->player.yaw); at += 4;
    app_net_write_f32(payload + at, app->clock.day_progress); at += 4;
    payload[at++] = (uint8_t)name_len;
    memcpy(payload + at, app->username, name_len);
    at += name_len;
    return lan_send(app->lan, peer_id, payload, at);
}

static bool app_lan_queue_world_event(AppContext *app, uint8_t type, int wx, int wy, int wz,
                                      uint16_t block_id)
{
    if (app == NULL || app->lan == NULL || app->lan_applying_remote_block ||
        (!app->lan_host && !app->lan_client) || !lan_is_connected(app->lan) ||
        (app->lan_host && lan_peer_count(app->lan) == 0)) {
        return false;
    }
    if (app->lan_block_queue_count >= MINEC_LAN_BLOCK_QUEUE) {
        if (!app->lan_block_queue_warned) {
            LOG_WARN("LAN block update queue is full; further edits may not reach peers until it drains");
            app->lan_block_queue_warned = true;
        }
        return false;
    }
    size_t slot = (app->lan_block_queue_head + app->lan_block_queue_count) % MINEC_LAN_BLOCK_QUEUE;
    uint8_t *payload = app->lan_block_queue[slot];
    payload[0] = type;
    app_net_write_u32(payload + 1, (uint32_t)(int32_t)wx);
    app_net_write_u32(payload + 5, (uint32_t)(int32_t)wy);
    app_net_write_u32(payload + 9, (uint32_t)(int32_t)wz);
    app_net_write_u16(payload + 13, block_id);
    app->lan_block_queue_count++;
    return true;
}

static void app_lan_block_changed(void *context, int wx, int wy, int wz, uint16_t block_id)
{
    AppContext *app = (AppContext *)context;
    if (app != NULL && app->lan_gravity_echo_pending) {
        bool bundled = wx == app->lan_gravity_echo_x && wy == app->lan_gravity_echo_y &&
                       wz == app->lan_gravity_echo_z && block_id == app->lan_gravity_echo_block_id;
        app->lan_gravity_echo_pending = false;
        if (bundled) {
            return;
        }
    }
    (void)app_lan_queue_world_event(app, APP_LAN_MSG_BLOCK, wx, wy, wz, block_id);
}

static bool app_lan_gravity_event(void *context, bool landed, int wx, int wy, int wz, uint16_t block_id)
{
    AppContext *app = (AppContext *)context;
    if (app == NULL || !app->lan_host) {
        return false;
    }
    if (app->lan == NULL || lan_peer_count(app->lan) == 0) {
        return true; /* No remote peer needs this transition. */
    }
    /* START clears the source and LAND installs the destination on clients,
     * so each event also carries its paired canonical block update. */
    bool queued = app_lan_queue_world_event(app,
                                            landed ? APP_LAN_MSG_GRAVITY_LAND : APP_LAN_MSG_GRAVITY_START,
                                            wx, wy, wz, block_id);
    if (queued) {
        app->lan_gravity_echo_pending = true;
        app->lan_gravity_echo_x = wx;
        app->lan_gravity_echo_y = wy;
        app->lan_gravity_echo_z = wz;
        app->lan_gravity_echo_block_id = landed ? block_id : BLOCK_AIR;
    }
    return queued;
}

/* Save-directory leaf for LAN beacons ("saves/sss" -> "sss"). */
static const char *app_world_basename(const AppContext *app)
{
    const char *dir = app->world_dir;
    const char *fwd = strrchr(dir, '/');
    const char *bwd = strrchr(dir, '\\');
    const char *base = dir;
    if (fwd != NULL && (bwd == NULL || fwd > bwd)) {
        base = fwd + 1;
    } else if (bwd != NULL) {
        base = bwd + 1;
    }
    return base[0] != '\0' ? base : "world";
}

bool app_lan_host(AppContext *app)
{
    if (app == NULL || !app->world_open || app->world == NULL || app->lan_client) {
        return false;
    }
    if (app->lan_host && app->lan != NULL && lan_is_host(app->lan)) {
        return true;
    }
    if (app->lan == NULL) {
        app->lan = lan_create();
    }
    if (app->lan == NULL || !lan_host_start(app->lan, APP_LAN_PORT)) {
        snprintf(app->menu.error, sizeof(app->menu.error),
                 "Could not open LAN port %d. Check whether another game is using it.", APP_LAN_PORT);
        return false;
    }
    app->lan_host = true;
    app->lan_local_player_id = 0;
    app->lan_player_send_timer = 0.0;
    app->lan_connect_timer = 0.0;
    app->lan_block_queue_head = 0;
    /* Best-effort beacons so joiners see this world without an IP. */
    lan_discover_destroy(app->lan_discover);
    app->lan_discover = lan_discover_create();
    app->lan_block_queue_count = 0;
    app->lan_block_queue_warned = false;
    app->lan_gravity_echo_pending = false;
    app->menu.error[0] = '\0';
    world_set_block_change_callback(app->world, app_lan_block_changed, app);
    world_set_gravity_replication(app->world, true);
    world_set_gravity_event_callback(app->world, app_lan_gravity_event, app);
    LOG_INFO("LAN host listening on port %d", APP_LAN_PORT);
    return true;
}

bool app_lan_join(AppContext *app, const char *address)
{
    return app_lan_join_endpoint(app, address, APP_LAN_PORT);
}

bool app_lan_join_endpoint(AppContext *app, const char *address, uint16_t port)
{
    if (app == NULL || address == NULL || app->world_open || app->lan_host) {
        return false;
    }
    if (!lan_ipv4_is_local_address(address)) {
        snprintf(app->menu.error, sizeof(app->menu.error), "Enter a valid local IPv4 address.");
        return false;
    }
    if (port == 0u) {
        snprintf(app->menu.error, sizeof(app->menu.error), "Server port is invalid.");
        return false;
    }
    if (app->lan == NULL) {
        app->lan = lan_create();
    }
    if (app->lan == NULL || !lan_client_start(app->lan, address, port)) {
        snprintf(app->menu.error, sizeof(app->menu.error), "Could not start LAN connection to %s.",
                 address);
        return false;
    }
    app->lan_client = true;
    app->lan_join_pending = true;
    app->lan_local_player_id = 0;
    app->lan_player_send_timer = 0.0;
    app->lan_connect_timer = 0.0;
    app->lan_block_queue_head = 0;
    app->lan_block_queue_count = 0;
    app->lan_block_queue_warned = false;
    app->lan_gravity_echo_pending = false;
    return true;
}

void app_lan_disconnect(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    if (app->world != NULL) {
        world_set_block_change_callback(app->world, NULL, NULL);
        world_set_gravity_event_callback(app->world, NULL, NULL);
        world_set_gravity_replication(app->world, true);
    }
    if (app->lan != NULL) {
        lan_close(app->lan);
    }
    lan_discover_destroy(app->lan_discover);
    app->lan_discover = NULL;
    app->lan_host = false;
    app->lan_client = false;
    app->lan_join_pending = false;
    app->lan_applying_remote_block = false;
    app->lan_local_player_id = 0;
    app->lan_player_send_timer = 0.0;
    app->lan_connect_timer = 0.0;
    app->lan_block_queue_head = 0;
    app->lan_block_queue_count = 0;
    app->lan_block_queue_warned = false;
    app->lan_gravity_echo_pending = false;
    app->lan_player_count = 0;
    memset(app->lan_players, 0, sizeof(app->lan_players));
    app->chat_open = false;
    app->chat_input[0] = '\0';
    app->chat_line_count = 0;
    app->chat_line_head = 0;
    memset(app->chat_lines, 0, sizeof(app->chat_lines));
    memset(app->chat_line_times, 0, sizeof(app->chat_line_times));
    app->chat_scroll = 0;
}

static bool app_lan_open_client_world(AppContext *app, const LanEvent *event)
{
    if (app == NULL || event == NULL || event->size < 40 || event->payload[0] != APP_LAN_MSG_WELCOME ||
        event->payload[1] != APP_LAN_PROTOCOL_VERSION) {
        return false;
    }
    const uint8_t *p = event->payload;
    uint32_t local_id = app_net_read_u32(p + 2);
    int64_t seed = (int64_t)app_net_read_u64(p + 6);
    int terrain_version = p[14];
    int mode = p[15];
    Vec3 host_pos = mmath_vec3(app_net_read_f32(p + 16), app_net_read_f32(p + 20),
                               app_net_read_f32(p + 24));
    float host_yaw = app_net_read_f32(p + 28);
    float day = app_net_read_f32(p + 32);
    size_t name_len = p[36];
    if (local_id == 0 || terrain_version < 1 || terrain_version > WORLD_TERRAIN_VERSION_CURRENT ||
        (mode != WORLD_MODE_SURVIVAL && mode != WORLD_MODE_CREATIVE) || !isfinite(host_pos.x) ||
        !isfinite(host_pos.y) || !isfinite(host_pos.z) || !isfinite(host_yaw) || !isfinite(day) ||
        host_pos.y < 0.0f || host_pos.y > 255.0f || name_len < PROFILE_NAME_MIN_LEN ||
        name_len > PROFILE_NAME_MAX_LEN || event->size != 37u + name_len) {
        return false;
    }
    char host_name[PROFILE_NAME_MAX_LEN + 1];
    memcpy(host_name, p + 37, name_len);
    host_name[name_len] = '\0';
    if (!profile_name_valid(host_name)) {
        return false;
    }
    char world_name[64];
    char seed_text[32];
    char world_dir[512];
    snprintf(world_name, sizeof(world_name), "LAN - %s", host_name);
    snprintf(seed_text, sizeof(seed_text), "%lld", (long long)seed);
    if (session_create_world(SESSION_SAVES_DIR, world_name, seed_text, mode, world_dir) != 0) {
        snprintf(app->menu.error, sizeof(app->menu.error), "Could not prepare a local LAN world copy.");
        return false;
    }
    WorldMeta meta;
    if (world_meta_read(world_dir, &meta) != 0) {
        snprintf(app->menu.error, sizeof(app->menu.error), "Could not read the new LAN world metadata.");
        (void)world_save_delete(world_dir);
        return false;
    }
    meta.terrain_version = terrain_version;
    if (world_meta_write(world_dir, &meta) != 0 || session_open_world(app, world_dir) != 0) {
        snprintf(app->menu.error, sizeof(app->menu.error), "Could not open the local LAN world copy.");
        if (!app->world_open) {
            (void)world_save_delete(world_dir);
        }
        return false;
    }
    app->lan_local_player_id = local_id;
    app->lan_join_pending = false;
    app->lan_client = true;
    app->clock.day_progress = day - floorf(day);
    if (app->clock.day_progress < 0.0f) {
        app->clock.day_progress += 1.0f;
    }
    Vec3 join_pos = mmath_vec3(host_pos.x + 1.25f, host_pos.y + 0.02f, host_pos.z + 1.25f);
    app->player.pos = join_pos;
    app->player.render_pos = join_pos;
    app->player.yaw = host_yaw + MMATH_PI;
    app->spawn_point = join_pos;
    app->has_spawn_point = true;
    app->player_from_save = true;
    world_set_block_change_callback(app->world, app_lan_block_changed, app);
    world_set_gravity_replication(app->world, false);
    LanRemotePlayer *host = app_lan_find_player(app, 0, true);
    if (host != NULL) {
        memcpy(host->username, host_name, name_len + 1);
        host->pos = host_pos;
        host->yaw = host_yaw;
    }
    snprintf(app->menu.error, sizeof(app->menu.error), "Connected to %s. Loading shared terrain...", host_name);
    app_enter_state(app, GAME_STATE_LOADING);
    return app->state == GAME_STATE_LOADING;
}

static void app_lan_handle_chat(AppContext *app, const LanEvent *event)
{
    if (app == NULL || event == NULL || event->size < 5 || event->payload[0] != APP_LAN_MSG_CHAT) {
        return;
    }
    size_t name_len = event->payload[1];
    if (name_len < PROFILE_NAME_MIN_LEN || name_len > PROFILE_NAME_MAX_LEN || event->size < 4u + name_len) {
        return;
    }
    size_t at = 2;
    char sender[PROFILE_NAME_MAX_LEN + 1];
    memcpy(sender, event->payload + at, name_len);
    sender[name_len] = '\0';
    at += name_len;
    if (!profile_name_valid(sender) || event->size < at + 2u) {
        return;
    }
    size_t message_len = app_net_read_u16(event->payload + at);
    at += 2;
    if (message_len == 0 || message_len > APP_CHAT_MAX_BYTES || event->size != at + message_len) {
        return;
    }
    char message[APP_CHAT_MAX_BYTES + 1];
    for (size_t i = 0; i < message_len; ++i) {
        unsigned char c = event->payload[at + i];
        if (c < 32 || c > 126) {
            return;
        }
        message[i] = (char)c;
    }
    message[message_len] = '\0';
    if (app->lan_host) {
        LanRemotePlayer *remote = app_lan_find_player(app, event->peer_id, false);
        if (remote == NULL || !profile_name_valid(remote->username)) {
            return;
        }
        size_t size = 0;
        uint8_t payload[LAN_MAX_FRAME_SIZE];
        if (!app_chat_encode(payload, sizeof(payload), remote->username, message, message_len, &size) ||
            !lan_send(app->lan, LAN_BROADCAST_PEER, payload, size)) {
            return;
        }
        app_chat_add(app, remote->username, message);
    } else {
        app_chat_add(app, sender, message);
    }
}

static int app_floor_div16(int value)
{
    int q = value / 16;
    int r = value % 16;
    return r < 0 ? q - 1 : q;
}

static void app_lan_handle_block(AppContext *app, const LanEvent *event)
{
    if (app == NULL || event == NULL || event->size != 15 || event->payload[0] != APP_LAN_MSG_BLOCK ||
        app->world == NULL) {
        return;
    }
    if ((app->lan_host && app_lan_find_player(app, event->peer_id, false) == NULL) ||
        (app->lan_client && event->peer_id != LAN_SERVER_PEER_ID)) {
        return;
    }
    int32_t wx = (int32_t)app_net_read_u32(event->payload + 1);
    int32_t wy = (int32_t)app_net_read_u32(event->payload + 5);
    int32_t wz = (int32_t)app_net_read_u32(event->payload + 9);
    uint16_t block_id = app_net_read_u16(event->payload + 13);
    if (wx < -30000000 || wx > 30000000 || wz < -30000000 || wz > 30000000 ||
        wy < 0 || wy >= CHUNK_Y || block_id >= BLOCK_COUNT) {
        return;
    }
    if (app->lan_host) {
        const LanRemotePlayer *sender = app_lan_find_player(app, event->peer_id, false);
        if (sender == NULL) {
            return;
        }
        float dx = (float)wx + 0.5f - sender->pos.x;
        float dy = (float)wy + 0.5f - (sender->pos.y + 1.62f);
        float dz = (float)wz + 0.5f - sender->pos.z;
        float reach = (app->world->mode == WORLD_MODE_CREATIVE ? 5.0f : 4.5f) + 1.0f;
        if (dx * dx + dy * dy + dz * dz > reach * reach) {
            return;
        }
    }
    int cx = app_floor_div16(wx);
    int cz = app_floor_div16(wz);
    if (world_get_chunk(app->world, cx, cz) == NULL) {
        if (world_generate_chunk(app->world, cx, cz) != 0) {
            return;
        }
    }
    if (app->lan_host) {
        /* Host is authoritative for accepting and relaying client edits. */
        (void)world_set_block(app->world, wx, wy, wz, block_id);
    } else if (app->lan_client) {
        app->lan_applying_remote_block = true;
        (void)world_set_block(app->world, wx, wy, wz, block_id);
        app->lan_applying_remote_block = false;
    }
}

static void app_lan_handle_gravity(AppContext *app, const LanEvent *event)
{
    if (app == NULL || event == NULL || !app->lan_client || app->world == NULL ||
        event->size != 15 || event->peer_id != LAN_SERVER_PEER_ID ||
        (event->payload[0] != APP_LAN_MSG_GRAVITY_START && event->payload[0] != APP_LAN_MSG_GRAVITY_LAND)) {
        return;
    }
    int32_t wx = (int32_t)app_net_read_u32(event->payload + 1);
    int32_t wy = (int32_t)app_net_read_u32(event->payload + 5);
    int32_t wz = (int32_t)app_net_read_u32(event->payload + 9);
    uint16_t block_id = app_net_read_u16(event->payload + 13);
    if (wx < -30000000 || wx > 30000000 || wz < -30000000 || wz > 30000000 ||
        wy < 0 || wy >= CHUNK_Y || !block_has_gravity(block_id)) {
        return;
    }
    int cx = app_floor_div16(wx);
    int cz = app_floor_div16(wz);
    if (world_get_chunk(app->world, cx, cz) == NULL && world_generate_chunk(app->world, cx, cz) != 0) {
        return;
    }
    if (event->payload[0] == APP_LAN_MSG_GRAVITY_START) {
        (void)world_gravity_apply_remote_start(app->world, wx, wy, wz, block_id);
    } else {
        (void)world_gravity_apply_remote_landing(app->world, wx, wy, wz, block_id);
        /* Keep landing atomic in the reliable queue: the LAND frame contains
         * both the end of the visual fall and its authoritative block cell. */
        (void)world_set_block_unreplicated(app->world, wx, wy, wz, block_id);
    }
}

static bool app_lan_parse_player(AppContext *app, const LanEvent *event)
{
    if (app == NULL || event == NULL || event->size < 32 || event->payload[0] != APP_LAN_MSG_PLAYER) {
        return false;
    }
    const uint8_t *p = event->payload;
    uint32_t player_id = app_net_read_u32(p + 1);
    Vec3 pos = mmath_vec3(app_net_read_f32(p + 5), app_net_read_f32(p + 9), app_net_read_f32(p + 13));
    float yaw = app_net_read_f32(p + 17);
    float pitch = app_net_read_f32(p + 21);
    float phase = app_net_read_f32(p + 25);
    uint8_t flags = p[29];
    size_t name_len = p[30];
    if (player_id == app->lan_local_player_id || !isfinite(pos.x) || !isfinite(pos.y) || !isfinite(pos.z) ||
        !isfinite(yaw) || !isfinite(pitch) || !isfinite(phase) || pos.x < -30000000.0f ||
        pos.x > 30000000.0f || pos.z < -30000000.0f || pos.z > 30000000.0f ||
        pos.y < -64.0f || pos.y > 512.0f || name_len < PROFILE_NAME_MIN_LEN ||
        name_len > PROFILE_NAME_MAX_LEN || event->size != 31u + name_len) {
        return false;
    }
    char name[PROFILE_NAME_MAX_LEN + 1];
    memcpy(name, p + 31, name_len);
    name[name_len] = '\0';
    if (!profile_name_valid(name)) {
        return false;
    }
    if (app->lan_host) {
        /* Ignore client-provided identity; bind the snapshot to its socket. */
        player_id = event->peer_id;
        LanRemotePlayer *remote = app_lan_find_player(app, player_id, false);
        if (remote == NULL) {
            return false;
        }
        memcpy(name, remote->username, sizeof(name));
    }
    LanRemotePlayer *remote = app_lan_find_player(app, player_id, true);
    if (remote == NULL) {
        return false;
    }
    memcpy(remote->username, name, strlen(name) + 1);
    remote->pos = pos;
    remote->yaw = yaw;
    remote->pitch = pitch;
    remote->walk_phase = phase;
    remote->sneaking = (flags & 1u) != 0;
    remote->moving = (flags & 2u) != 0;
    if (app->lan_host) {
        (void)app_lan_send_player(app, LAN_BROADCAST_PEER, player_id, name, pos, yaw, pitch,
                                  phase, remote->sneaking, remote->moving);
    }
    return true;
}

static void app_lan_handle_welcome(AppContext *app, const LanEvent *event)
{
    if (app == NULL || event == NULL || !app->lan_client || !app->lan_join_pending) {
        return;
    }
    if (!app_lan_open_client_world(app, event)) {
        snprintf(app->menu.error, sizeof(app->menu.error),
                 "Host sent invalid world data or the local copy could not be created.");
        app_lan_disconnect(app);
    }
}

static void app_lan_handle_message(AppContext *app, const LanEvent *event)
{
    if (app == NULL || event == NULL || event->size == 0) {
        return;
    }
    if (app->lan_host && event->payload[0] != APP_LAN_MSG_HELLO &&
        app_lan_find_player(app, event->peer_id, false) == NULL) {
        (void)lan_disconnect_peer(app->lan, event->peer_id);
        return;
    }
    switch (event->payload[0]) {
    case APP_LAN_MSG_HELLO: {
        if (!app->lan_host) {
            return;
        }
        if (event->size < 3 || event->payload[1] != APP_LAN_PROTOCOL_VERSION) {
            (void)lan_disconnect_peer(app->lan, event->peer_id);
            return;
        }
        size_t name_len = event->payload[2];
        if (name_len < PROFILE_NAME_MIN_LEN || name_len > PROFILE_NAME_MAX_LEN || event->size != 3u + name_len) {
            (void)lan_disconnect_peer(app->lan, event->peer_id);
            return;
        }
        char name[PROFILE_NAME_MAX_LEN + 1];
        memcpy(name, event->payload + 3, name_len);
        name[name_len] = '\0';
        if (!profile_name_valid(name)) {
            (void)lan_disconnect_peer(app->lan, event->peer_id);
            return;
        }
        if (app_lan_find_player(app, event->peer_id, false) != NULL) {
            /* A peer may register exactly once; later HELLO frames must not
             * replace the identity that was bound to its connection. */
            (void)lan_disconnect_peer(app->lan, event->peer_id);
            return;
        }
        LanRemotePlayer *remote = app_lan_find_player(app, event->peer_id, true);
        if (remote == NULL) {
            (void)lan_disconnect_peer(app->lan, event->peer_id);
            return;
        }
        memcpy(remote->username, name, name_len + 1);
        remote->pos = app->player.pos;
        (void)app_lan_send_welcome(app, event->peer_id);
        app_lan_send_current_players(app, event->peer_id);
        app_chat_add(app, name, "joined the LAN world");
        break;
    }
    case APP_LAN_MSG_WELCOME:
        app_lan_handle_welcome(app, event);
        break;
    case APP_LAN_MSG_PLAYER:
        (void)app_lan_parse_player(app, event);
        break;
    case APP_LAN_MSG_CHAT:
        app_lan_handle_chat(app, event);
        break;
    case APP_LAN_MSG_BLOCK:
        app_lan_handle_block(app, event);
        break;
    case APP_LAN_MSG_GRAVITY_START:
    case APP_LAN_MSG_GRAVITY_LAND:
        app_lan_handle_gravity(app, event);
        break;
    case APP_LAN_MSG_PLAYER_LEFT:
        if (app->lan_client && event->size == 5) {
            app_lan_remove_player(app, app_net_read_u32(event->payload + 1));
        }
        break;
    default:
        break;
    }
}

static void app_lan_client_disconnected(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    bool was_joining = app->lan_join_pending;
    if (was_joining) {
        snprintf(app->menu.error, sizeof(app->menu.error), "Could not connect to that LAN world.");
        app_lan_disconnect(app);
        return;
    }
    app_lan_disconnect(app);
    if (app->world_open) {
        session_close_world(app, true);
    }
    snprintf(app->menu.error, sizeof(app->menu.error), "The LAN host disconnected.");
    if (app->state == GAME_STATE_LOADING || app->state == GAME_STATE_PLAYING ||
        app->state == GAME_STATE_PAUSED || app->state == GAME_STATE_INVENTORY ||
        app->state == GAME_STATE_CRAFTING || app->state == GAME_STATE_DEAD) {
        app_enter_state(app, GAME_STATE_MAIN_MENU);
    }
}

static void app_lan_update(AppContext *app, double dt)
{
    if (app == NULL || app->lan == NULL) {
        return;
    }
    LanEvent event;
    while (lan_poll(app->lan, &event)) {
        switch (event.type) {
        case LAN_EVENT_PEER_CONNECTED:
            if (app->lan_host) {
                char peer_address[48];
                if (!lan_peer_address(app->lan, event.peer_id, peer_address, sizeof(peer_address)) ||
                    !lan_ipv4_is_local_endpoint(peer_address)) {
                    (void)lan_disconnect_peer(app->lan, event.peer_id);
                    break;
                }
            }
            if (app->lan_client && event.peer_id == LAN_SERVER_PEER_ID) {
                size_t name_len = strlen(app->username);
                uint8_t hello[3 + PROFILE_NAME_MAX_LEN];
                hello[0] = APP_LAN_MSG_HELLO;
                hello[1] = APP_LAN_PROTOCOL_VERSION;
                hello[2] = (uint8_t)name_len;
                memcpy(hello + 3, app->username, name_len);
                if (!lan_send(app->lan, LAN_SERVER_PEER_ID, hello, 3 + name_len)) {
                    app_lan_client_disconnected(app);
                }
            }
            break;
        case LAN_EVENT_PEER_DISCONNECTED:
            if (app->lan_host) {
                app_lan_remove_player(app, event.peer_id);
                uint8_t left[5] = {APP_LAN_MSG_PLAYER_LEFT, 0, 0, 0, 0};
                app_net_write_u32(left + 1, event.peer_id);
                (void)lan_send(app->lan, LAN_BROADCAST_PEER, left, sizeof(left));
            } else if (app->lan_client && event.peer_id == LAN_SERVER_PEER_ID) {
                app_lan_client_disconnected(app);
                break;
            }
            break;
        case LAN_EVENT_CONNECT_FAILED:
            if (app->lan_client) {
                app_lan_client_disconnected(app);
            }
            break;
        case LAN_EVENT_MESSAGE:
            app_lan_handle_message(app, &event);
            break;
        case LAN_EVENT_NONE:
        default:
            break;
        }
        if (app->lan == NULL) {
            return;
        }
    }
    if (app->lan_join_pending) {
        app->lan_connect_timer += dt;
        if (app->lan_connect_timer > 10.0) {
            app_lan_client_disconnected(app);
            return;
        }
    }
    if (app->lan_host && lan_peer_count(app->lan) == 0) {
        app->lan_block_queue_head = 0;
        app->lan_block_queue_count = 0;
        app->lan_block_queue_warned = false;
        app->lan_gravity_echo_pending = false;
    } else if ((app->lan_host || app->lan_client) && lan_is_connected(app->lan)) {
        uint32_t target = app->lan_host ? LAN_BROADCAST_PEER : LAN_SERVER_PEER_ID;
        while (app->lan_block_queue_count > 0) {
            uint8_t *payload = app->lan_block_queue[app->lan_block_queue_head];
            if (!lan_send(app->lan, target, payload, 15)) {
                break;
            }
            app->lan_block_queue_head = (app->lan_block_queue_head + 1u) % MINEC_LAN_BLOCK_QUEUE;
            app->lan_block_queue_count--;
        }
        if (app->lan_block_queue_count == 0) {
            app->lan_block_queue_warned = false;
        }
    }
    /* Hosting announcements (answers queries, beacons the world name);
     * silent when discovery is unavailable. */
    if (app->lan_host && app->lan != NULL && lan_is_host(app->lan)) {
        size_t peers = lan_peer_count(app->lan);
        unsigned announced = peers > 255u ? 255u : (unsigned)peers;
        lan_discover_host(app->lan_discover, app_world_basename(app), announced, LAN_MAX_PEERS,
                          APP_LAN_PORT, (float)dt);
    }
    if (!app->world_open || app->state != GAME_STATE_PLAYING || !lan_is_connected(app->lan)) {
        return;
    }
    app->lan_player_send_timer += dt;
    if (app->lan_player_send_timer < 0.10) {
        return;
    }
    app->lan_player_send_timer = fmod(app->lan_player_send_timer, 0.10);
    uint32_t target = app->lan_host ? LAN_BROADCAST_PEER : LAN_SERVER_PEER_ID;
    (void)app_lan_send_player(app, target, app->lan_local_player_id, app->username,
                              app->player.render_pos, app->player.yaw, app->player.pitch,
                              app->player.walk_phase, app->player.sneaking,
                              sqrtf(app->player.vel.x * app->player.vel.x + app->player.vel.z * app->player.vel.z) > 0.15f);
}

static void app_chat_close(AppContext *app)
{
    if (app == NULL || !app->chat_open) {
        return;
    }
    app->chat_open = false;
    window_text_input(false);
    window_set_relative_mouse(app->window, true);
    window_clear_input_edges(app->window);
    bool ml = window_is_mouse_down(MINEC_MOUSE_LEFT);
    bool mr = window_is_mouse_down(MINEC_MOUSE_RIGHT);
    app->prev_mouse_l = ml;
    app->prev_mouse_r = mr;
}

static void app_chat_submit(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    size_t len = strlen(app->chat_input);
    while (len > 0 && app->chat_input[len - 1] == ' ') {
        app->chat_input[--len] = '\0';
    }
    if (len > 0) {
        uint8_t payload[LAN_MAX_FRAME_SIZE];
        size_t payload_len = 0;
        if (app_chat_encode(payload, sizeof(payload), app->username, app->chat_input, len, &payload_len)) {
            if (app->lan_host && app->lan != NULL && lan_peer_count(app->lan) > 0) {
                app_chat_add(app, app->username, app->chat_input);
                if (!lan_send(app->lan, LAN_BROADCAST_PEER, payload, payload_len)) {
                    app_chat_add(app, "TerraCraft", "chat message could not be queued");
                }
            } else if (app->lan_client && app->lan != NULL && lan_is_connected(app->lan)) {
                if (!lan_send(app->lan, LAN_SERVER_PEER_ID, payload, payload_len)) {
                    app_chat_add(app, "TerraCraft", "chat message could not be queued");
                }
            } else {
                app_chat_add(app, app->username, app->chat_input);
            }
        }
    }
    app->chat_input[0] = '\0';
    app_chat_close(app);
}

static void app_chat_append_ascii(AppContext *app, const char *text)
{
    if (app == NULL || text == NULL) {
        return;
    }
    size_t len = strlen(app->chat_input);
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; ++p) {
        if (*p < 32 || *p > 126 || len >= APP_CHAT_MAX_BYTES) {
            continue;
        }
        app->chat_input[len++] = (char)*p;
    }
    app->chat_input[len] = '\0';
}

static void app_chat_backspace(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    size_t len = strlen(app->chat_input);
    if (len > 0) {
        app->chat_input[len - 1] = '\0';
    }
}

static void app_chat_add_rect(float *rects, size_t *n, float x, float y, float width, float height, float alpha)
{
    static const int corner[6] = {0, 1, 2, 2, 1, 3};
    for (int v = 0; v < 6; ++v) {
        int c = corner[v];
        rects[(*n)++] = x + ((c == 1 || c == 3) ? width : 0.0f);
        rects[(*n)++] = y + ((c >= 2) ? height : 0.0f);
        rects[(*n)++] = 0.0f;
        rects[(*n)++] = 0.0f;
        rects[(*n)++] = 0.0f;
        rects[(*n)++] = alpha;
    }
}

static unsigned app_chat_history_visible(const AppContext *app)
{
    if (app == NULL || app->height <= 0) {
        return 1u;
    }
    int total_rows = (app->height - 120) / (int)APP_CHAT_LINE_HEIGHT;
    unsigned visible = total_rows > 2 ? (unsigned)(total_rows - 2) : 1u;
    if (visible > APP_CHAT_HISTORY_LINES) {
        visible = APP_CHAT_HISTORY_LINES;
    }
    return visible;
}

static float app_chat_line_alpha(double now, double received_at)
{
    double age = now - received_at;
    if (age < 0.0) {
        age = 0.0;
    }
    if (age >= APP_CHAT_HIDE_AFTER_SECONDS) {
        return 0.0f;
    }
    double fade_start = APP_CHAT_HIDE_AFTER_SECONDS - APP_CHAT_FADE_SECONDS;
    if (age <= fade_start) {
        return 1.0f;
    }
    return (float)((APP_CHAT_HIDE_AFTER_SECONDS - age) / APP_CHAT_FADE_SECONDS);
}

static void app_draw_chat(AppContext *app)
{
    if (app == NULL || app->renderer == NULL || (!app->chat_open && app->chat_line_count == 0)) {
        return;
    }
    float x = 12.0f;
    float line_h = APP_CHAT_LINE_HEIGHT;
    float panel_w = (float)app->width - 24.0f;
    if (panel_w > 720.0f) {
        panel_w = 720.0f;
    }
    if (panel_w < 1.0f) {
        panel_w = 1.0f;
    }
    float bottom_pad = 92.0f; /* Keep the log clear of the hotbar and vitals. */

    if (app->chat_open) {
        unsigned visible = app->chat_line_count;
        unsigned capacity = app_chat_history_visible(app);
        if (visible > capacity) {
            visible = capacity;
        }
        unsigned max_scroll = app->chat_line_count > visible ? app->chat_line_count - visible : 0u;
        if (app->chat_scroll > max_scroll) {
            app->chat_scroll = max_scroll;
        }
        float h = (float)(visible + 2u) * line_h + 12.0f; /* title, history, input */
        float y = (float)app->height - h - bottom_pad;
        if (y < 8.0f) {
            y = 8.0f;
        }
        float rects[36];
        size_t n = 0;
        app_chat_add_rect(rects, &n, x, y, panel_w, h, 0.76f);
        renderer_draw_rects(app->renderer, app->width, app->height, rects, 6);
        renderer_draw_text(app->renderer, x + 8.0f, y + 5.0f, 0.85f,
                           0.72f, 0.82f, 0.92f, 1.0f,
                           "Chat history - wheel or Page Up/Down to scroll");

        unsigned oldest_age = app->chat_scroll + (visible > 0u ? visible - 1u : 0u);
        unsigned start = (app->chat_line_head + APP_CHAT_HISTORY_LINES - 1u - oldest_age) %
                         APP_CHAT_HISTORY_LINES;
        for (unsigned i = 0; i < visible; ++i) {
            unsigned index = (start + i) % APP_CHAT_HISTORY_LINES;
            renderer_draw_text(app->renderer, x + 8.0f, y + 6.0f + (float)(i + 1u) * line_h,
                               1.0f, 1.0f, 1.0f, 1.0f, 1.0f, app->chat_lines[index]);
        }
        char prompt[APP_CHAT_MAX_BYTES + 8];
        /* Precision-capped: provably fits (GCC -Wformat-truncation). */
        snprintf(prompt, sizeof(prompt), "> %.*s_", (int)(sizeof(prompt) - 4), app->chat_input);
        renderer_draw_text(app->renderer, x + 8.0f, y + 6.0f + (float)(visible + 1u) * line_h,
                           1.0f, 1.0f, 0.90f, 0.70f, 1.0f, prompt);
        return;
    }

    unsigned indices[APP_CHAT_HISTORY_LINES];
    float alphas[APP_CHAT_HISTORY_LINES];
    unsigned recent_count = 0;
    double now = time_now_seconds();
    unsigned oldest = (app->chat_line_head + APP_CHAT_HISTORY_LINES - app->chat_line_count) %
                      APP_CHAT_HISTORY_LINES;
    for (unsigned i = 0; i < app->chat_line_count; ++i) {
        unsigned index = (oldest + i) % APP_CHAT_HISTORY_LINES;
        float alpha = app_chat_line_alpha(now, app->chat_line_times[index]);
        if (alpha > 0.0f) {
            indices[recent_count] = index;
            alphas[recent_count++] = alpha;
        }
    }
    unsigned first = recent_count > APP_CHAT_RECENT_VISIBLE ? recent_count - APP_CHAT_RECENT_VISIBLE : 0u;
    unsigned visible = recent_count - first;
    if (visible == 0u) {
        return;
    }
    float h = (float)visible * line_h + 12.0f;
    float y = (float)app->height - h - bottom_pad;
    if (y < 8.0f) {
        y = 8.0f;
    }
    float rects[APP_CHAT_RECENT_VISIBLE * 36u];
    size_t n = 0;
    for (unsigned i = 0; i < visible; ++i) {
        float row_y = y + 6.0f + (float)i * line_h;
        app_chat_add_rect(rects, &n, x, row_y - 2.0f, panel_w, line_h, 0.64f * alphas[first + i]);
    }
    renderer_draw_rects(app->renderer, app->width, app->height, rects, n / 6u);
    for (unsigned i = 0; i < visible; ++i) {
        unsigned item = first + i;
        renderer_draw_text(app->renderer, x + 8.0f, y + 6.0f + (float)i * line_h,
                           1.0f, 1.0f, 1.0f, 1.0f, alphas[item], app->chat_lines[indices[item]]);
    }
}

/* Creative block actions are instantaneous, but still run on a world tick. */
static void app_tick_creative_actions(AppContext *app)
{
    bool left_pressed = window_take_mouse_pressed(app->window, MINEC_MOUSE_LEFT);
    bool right_pressed = window_take_mouse_pressed(app->window, MINEC_MOUSE_RIGHT);
    bool left_held = window_is_mouse_down(MINEC_MOUSE_LEFT);
    if (!left_pressed && !left_held && !right_pressed) {
        return;
    }
    Vec3 eye = player_eye_pos(&app->player);
    HitResult hit = raycast_from_eye(eye, app->player.yaw, app->player.pitch, app->world,
                                     survival_reach(true));
    if (left_pressed || left_held) {
        if (left_pressed) {
            app_start_arm_swing(app);
        }
        if (!app_try_attack(app) && left_pressed) {
            int broken = interaction_break(app->world, &hit);
            if (broken >= 0) {
                audio_play_block(&app->audio, AUDIO_BLOCK_BREAK, (uint16_t)broken);
                particle_burst_block(&app->particles, (uint16_t)broken, hit.block[0], hit.block[1], hit.block[2], 16);
                LOG_DEBUG("break ok: id %d", broken);
            }
        }
    }
    if (right_pressed) {
        ItemId sel = app->player.inv.slots[app->player.hotbar_sel].item;
        if (interaction_place(app->world, &app->player, &hit, item_to_block(sel), NULL)) {
            audio_play_block(&app->audio, AUDIO_BLOCK_PLACE, item_to_block(sel));
        }
    }
}

/* Forward: drop spawner shared by mining + bonus rolls (defined below). */
static void app_spawn_drop(AppContext *app, Vec3 at, ItemStack *drop);

/* Aim at a living mob: raycast mobs vs blocks, entity wins only when
 * strictly nearer (never through walls). Used by melee + F7.
 *
 * Args:
 *   app: context with an open world.
 *   out_id: receives the aimed handle (may be NULL).
 *
 * Returns: true when a mob is the nearest valid hit.
 */
static bool app_aim_mob(AppContext *app, EntityId *out_id)
{
    if (out_id != NULL) {
        *out_id = ENTITY_ID_NULL;
    }
    Vec3 eye = player_eye_pos(&app->player);
    float cp = cosf(app->player.pitch);
    Vec3 dir =
        mmath_vec3(-sinf(app->player.yaw) * cp, sinf(app->player.pitch), -cosf(app->player.yaw) * cp);
    float edist = 0.0f;
    EntityId eid = ENTITY_ID_NULL;
    if (!mob_raycast(&app->mobs, eye, dir, PLAYER_ATTACK_REACH, &edist, &eid)) {
        return false;
    }
    HitResult hit =
        raycast_from_eye(eye, app->player.yaw, app->player.pitch, app->world, RAYCAST_MAX_DIST);
    /* Only solid blocks shield a swing: grass tufts, flowers, and
     * torches stop the mining ray by design, but swinging through a
     * flower at a cow must still connect (previously any plant between
     * crosshair and mob made plains mobs unhittable). */
    if (hit.hit && hit.dist < edist &&
        mob_block_shields(world_get_block(app->world, hit.block[0], hit.block[1], hit.block[2]))) {
        return false;
    }
    if (out_id != NULL) {
        *out_id = eid;
    }
    return true;
}

/* Strike an aimed mob: damage + knockback + feedback (shared by melee
 * swings and the F7 debug key). Drops spawn inside damage exactly once.
 */
static bool app_strike_mob(AppContext *app, EntityId eid, float dmg, float knock_power)
{
    Mob *m = mob_resolve(&app->mobs, eid);
    if (m == NULL) {
        return false;
    }
    Vec3 kdir = mmath_vec3(m->pos.x - app->player.pos.x, 0.0f, m->pos.z - app->player.pos.z);
    Vec3 from = mmath_vec3(app->player.pos.x - m->pos.x, 0.0f, app->player.pos.z - m->pos.z);
    Vec3 chest = mmath_vec3(m->pos.x, m->pos.y + m->height * 0.6f, m->pos.z);
    float before = m->health;
    int mtype = (int)m->type;
    bool killed = living_entity_damage(&app->mobs, &app->entities, eid, dmg, kdir, knock_power, &from);
    if (killed) {
        audio_play(&app->audio, mob_die_sound(mtype));
        particle_burst_item(&app->particles, ITEM_APPLE, chest, 8);
    } else if (m->health < before) {
        /* Gated strikes (hurt window) stay silent: no damage, no lie. */
        audio_play(&app->audio, mob_hurt_sound(mtype));
        particle_burst_item(&app->particles, ITEM_APPLE, chest, 5);
    }
    return killed || m->health < before;
}

/* Start the visible first-person swing and its held-button repeat window. */
static void app_start_arm_swing(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    app->arm_swing_started = app->last_frame_time;
    app->arm_swing_active = true;
    app->arm_swing_repeat_t = PLAYER_SWING_DURATION;
    player_anim_notify_attacked(&app->panim);
}

/* Resolve a melee click against mobs before blocks. Returns true whenever a
 * mob owns the crosshair, including during cooldown, so blocks behind it do
 * not start mining or break through the creature. */
static bool app_try_attack(AppContext *app)
{
    EntityId eid = ENTITY_ID_NULL;
    if (!app_aim_mob(app, &eid)) {
        return false;
    }
    if (app->player.attack_cd > 0.0f || app->player.dead) {
        return true;
    }
    ItemStack *held = &app->player.inv.slots[app->player.hotbar_sel];
    float dmg = 1.0f;
    float cd = 0.4f;
    mob_tool_stats(held->item, &dmg, &cd);
    bool damaged = app_strike_mob(app, eid, dmg, 5.0f);
    if (damaged && stack_use_melee_hit(held)) {
        audio_play(&app->audio, AUDIO_TOOL_BREAK);
    }
    app->player.attack_cd = cd;
    app_start_arm_swing(app);
    return true;
}

/* Spawn a held stack just ahead of the player with a forward hand toss.
 * entity_drop_stack commits the source only after reserving a pool slot. */
static bool app_drop_stack_from_player(AppContext *app, ItemStack *stack, bool all)
{
    if (app == NULL || stack == NULL || stack_is_empty(stack)) {
        return false;
    }
    Vec3 eye = player_eye_pos(&app->player);
    float cp = cosf(app->player.pitch);
    Vec3 forward = mmath_vec3(-sinf(app->player.yaw) * cp, sinf(app->player.pitch),
                              -cosf(app->player.yaw) * cp);
    Vec3 pos = mmath_vec3_add(eye, mmath_vec3_scale(forward, 0.65f));
    pos.y -= 0.45f;
    Vec3 velocity = mmath_vec3_add(mmath_vec3_scale(forward, 3.0f), mmath_vec3(0.0f, 1.5f, 0.0f));
uint16_t count = all ? stack->count : 1;
    int id = entity_drop_stack(&app->entities, stack, count, pos, velocity);
    if (id < 0) {
        return false;
    }
    audio_play(&app->audio, AUDIO_ITEM_PICKUP);
    return true;
}

/* Inventory screens bind Q to their cursor stack, matching the slot currently
 * being carried. Control selects the whole cursor stack. */
static void app_poll_cursor_drop(AppContext *app)
{
    if (app == NULL || !window_take_key_pressed(app->window, SDL_SCANCODE_Q) ||
        stack_is_empty(&app->cursor)) {
        return;
    }
    bool all = window_is_key_down(SDL_SCANCODE_LCTRL) || window_is_key_down(SDL_SCANCODE_RCTRL);
    if (!app_drop_stack_from_player(app, &app->cursor, all)) {
        LOG_WARN("drop: item pool full; cursor stack kept");
    }
}

/* Spawn a mob at the crosshair (F9/F10 debug): targeted face cell, else
 * 3 blocks ahead, dropped down up to 6 for solid ground + headroom.
 */
static void app_debug_spawn_mob(AppContext *app, EntityType type)
{
    Vec3 eye = player_eye_pos(&app->player);
    HitResult hit =
        raycast_from_eye(eye, app->player.yaw, app->player.pitch, app->world, RAYCAST_MAX_DIST);
    int tx, ty, tz;
    if (hit.hit) {
        tx = hit.block[0] + hit.normal[0];
        ty = hit.block[1] + hit.normal[1];
        tz = hit.block[2] + hit.normal[2];
    } else {
        float cp = cosf(app->player.pitch);
        tx = (int)floorf(eye.x - sinf(app->player.yaw) * cp * 3.0f);
        ty = (int)floorf(eye.y + sinf(app->player.pitch) * 3.0f);
        tz = (int)floorf(eye.z - cosf(app->player.yaw) * cp * 3.0f);
    }
    for (int dy = 0; dy <= 6; ++dy) {
        int cy = ty - dy;
        if (cy < 1 || cy + 1 >= CHUNK_Y) {
            continue;
        }
        if (block_is_solid(world_get_block(app->world, tx, cy, tz)) ||
            block_is_solid(world_get_block(app->world, tx, cy + 1, tz))) {
            continue;
        }
        if (!block_is_solid(world_get_block(app->world, tx, cy - 1, tz))) {
            continue;
        }
        EntityId id = mob_spawn(&app->mobs, type, mmath_vec3((float)tx + 0.5f, (float)cy, (float)tz + 0.5f),
                                app->player.yaw);
        if (id != ENTITY_ID_NULL) {
            LOG_INFO("debug spawn: %s at (%d,%d,%d)", mob_definition(type)->name, tx, cy, tz);
        } else {
            LOG_WARN("debug spawn: mob pool full");
        }
        return;
    }
    LOG_INFO("debug spawn: no ground near crosshair");
}

/* Survival held-button mining: raycast on each simulation tick, advance
 * progress on a stable target, break + spawn a drop entity on completion. Any
 * target,
 * held-item, reach, or button change resets progress.
 *
 * Args:
 *   app: context with an open world.
 */
static void app_tick_mining(AppContext *app, float dt)
{
    bool pressed = window_take_mouse_pressed(app->window, MINEC_MOUSE_LEFT);
    bool held = window_is_mouse_down(MINEC_MOUSE_LEFT);
    if (pressed) {
        app_start_arm_swing(app);
    } else if (held && app->arm_swing_repeat_t <= 0.0f) {
        app_start_arm_swing(app);
    }
    if ((pressed || held) && app_try_attack(app)) {
        survival_mine_reset(&app->player);
        app->prev_mouse_l = held;
        return;
    }
    if (!held) {
        survival_mine_reset(&app->player);
        app->prev_mouse_l = false;
        return;
    }
    app->prev_mouse_l = true;
    Vec3 eye = player_eye_pos(&app->player);
    HitResult hit = raycast_from_eye(eye, app->player.yaw, app->player.pitch, app->world,
                                     survival_reach(false));
    if (!hit.hit) {
        survival_mine_reset(&app->player);
        return;
    }
    ItemId hand = app->player.inv.slots[app->player.hotbar_sel].item;
    MineResult done;
    survival_mine_update(&app->player, app->world, hit.block[0], hit.block[1], hit.block[2], hand, dt, &done);
    if (!done.finished) {
        return;
    }
    HitResult broke;
    broke.hit = true;
    broke.block[0] = done.bx;
    broke.block[1] = done.by;
    broke.block[2] = done.bz;
    broke.normal[0] = 0;
    broke.normal[1] = 1;
    broke.normal[2] = 0;
    int broken = interaction_break(app->world, &broke);
    if (broken < 0) {
        return;
    }
    /* Material break sound (dig/stone, dig/grass, ...) + debris burst
     * (block-textured cubes from the cell). */
    audio_play_block(&app->audio, AUDIO_BLOCK_BREAK, (uint16_t)broken);
    particle_burst_block(&app->particles, (uint16_t)broken, done.bx, done.by, done.bz, 16);
    /* Tool wear: one use per successful break with a damageable held tool
     * (swinging air never reaches here). A spent tool breaks loudly. */
    ItemStack *tool_slot = &app->player.inv.slots[app->player.hotbar_sel];
    if (tool_slot->item == hand && stack_use_tool(tool_slot)) {
        audio_play(&app->audio, AUDIO_TOOL_BREAK);
        LOG_INFO("tool broke: %s", item_get_info(hand)->name);
    }
    Vec3 at = mmath_vec3((float)done.bx + 0.5f, (float)done.by + 0.5f, (float)done.bz + 0.5f);
    ItemStack drop = survival_block_drop((uint16_t)broken, hand);
    if (!stack_is_empty(&drop)) {
        app_spawn_drop(app, at, &drop);
    }
    /* Deterministic bonus drops (e.g. leaves apples) roll even when the
     * block itself drops nothing. */
    ItemStack bonus = {ITEM_NONE, 0, 0};
    if (survival_bonus_drop(app->world->seed, (uint16_t)broken, done.bx, done.by, done.bz, &bonus)) {
        app_spawn_drop(app, at, &bonus);
    }
}

/* Spawn a drop entity, banking into the inventory when the pool is full
 * (nothing is ever lost while either has room; else a loud log).
 *
 * Args:
 *   app: context with an open world.
 *   at: spawn origin.
 *   drop: non-empty stack to drop (must not be NULL).
 */
static void app_spawn_drop(AppContext *app, Vec3 at, ItemStack *drop)
{
    if (entity_spawn(&app->entities, at, drop) >= 0) {
        return;
    }
    /* Pool full (should not happen at 128): fold into a nearby same-item
     * drop first, then bank into the inventory, so a mined block is only
     * ever lost when both the world and the inventory are completely full. */
    ItemStack rest = *drop;
    if (entity_try_merge(&app->entities, &rest, at, 4.0f) > 0 && stack_is_empty(&rest)) {
        LOG_DEBUG("mining: pool full, merged drop into nearby entity");
        return;
    }
    if (inv_insert(&app->player.inv, &rest) != 0) {
        LOG_WARN("mining: entity pool full and inventory full; lost %u x item %u", (unsigned)rest.count,
                 (unsigned)rest.item);
    } else {
        LOG_DEBUG("mining: pool full, banked drop into inventory");
    }
}

/* Block under the player's feet (for footstep sounds). */
static uint16_t app_footstep_block(AppContext *app)
{
    int bx = (int)floorf(app->player.pos.x);
    int by = (int)floorf(app->player.pos.y - 0.01f);
    int bz = (int)floorf(app->player.pos.z);
    return world_get_block(app->world, bx, by, bz);
}

/* Footstep sound event for a block (material classes only; the block id
 * travels separately into audio_play_block for variant resolution).
 */
static AudioEvent app_footstep_event(uint16_t below)
{
    switch (below) {
    case BLOCK_WOOD:
    case BLOCK_WORKBENCH:
    case BLOCK_PLANKS:
        return AUDIO_STEP_WOOD;
    case BLOCK_SAND:
        return AUDIO_STEP_SAND;
    case BLOCK_DIRT:
    case BLOCK_GRASS:
    case BLOCK_SNOW:
    case BLOCK_LEAVES:
    case BLOCK_GRASS_PLANT:
    case BLOCK_FLOWER:
        return AUDIO_STEP_DIRT;
    default:
        return AUDIO_STEP_STONE;
    }
}

/* Survival hold-to-eat: delegates to the survival policy state machine
 * (unit-tested there); the app only translates input + feedback. Edible
 * held stacks eat on RMB instead of placing (edibles are never
 * placeable, so the paths cannot collide).
 *
 * Args:
 *   app: context with an open world.
 *   dt: fixed simulation time step.
 */
static void app_tick_eat(AppContext *app, float dt)
{
    bool holding = window_is_mouse_down(MINEC_MOUSE_RIGHT);
    if (survival_eat_update(&app->player, holding, dt) == SURVIVAL_EAT_DONE) {
        audio_play(&app->audio, AUDIO_EAT);
        LOG_INFO("ate food (hunger now %.0f/%.0f)", (double)app->player.hunger,
                 (double)app->player.max_hunger);
    }
}

/* Bow draw-and-release (both modes): RMB held with a bow selected draws;
 * release fires a charge-scaled arrow through the shared projectile pool.
 * Bows are neither edible nor placeable, so this never collides with eat
 * or place (place additionally skips bow-held presses explicitly).
 * Survival consumes 1 arrow + 1 durability per successful fire; creative
 * fires free. Cancels never consume anything.
 *
 * Args:
 *   app: context with an open world.
 *   dt: clamped frame time.
 */
static void app_tick_bow(AppContext *app, float dt)
{
    Player *p = &app->player;
    if (p->dead) {
        survival_bow_reset(p);
        return;
    }
    bool creative = survival_is_creative(p);
    int sel = p->hotbar_sel;
    ItemStack *slot = &p->inv.slots[sel];
    bool holding = window_is_mouse_down(MINEC_MOUSE_RIGHT);
    if (!p->bow_drawing) {
        /* Fresh presses start the draw (holding through a slot switch
         * also starts it — holding use with a bow selected means draw). */
        if (holding && survival_bow_is_bow(slot->item) && !stack_is_empty(slot)) {
            if (!creative && survival_bow_find_arrow(&p->inv) < 0) {
                LOG_DEBUG("bow: no arrows (draw denied)");
            } else {
                p->bow_drawing = true;
                p->bow_slot = sel;
                p->bow_t = 0.0f;
                p->bow_full = false;
                audio_play(&app->audio, AUDIO_BOW_DRAW);
            }
        }
        return;
    }
    /* Drawing: any slot/item change cancels (never fires, never consumes). */
    if (p->bow_slot != sel || !survival_bow_is_bow(slot->item) || stack_is_empty(slot)) {
        survival_bow_reset(p);
        return;
    }
    if (!holding) {
        /* Release: weak taps cancel, real draws fire. */
        float charge = survival_bow_charge(p->bow_t);
        survival_bow_reset(p);
        if (charge < SURVIVAL_BOW_MIN_CHARGE) {
            return;
        }
        float speed = 0.0f;
        float damage = 0.0f;
        survival_bow_launch(charge, &speed, &damage);
        Vec3 eye = player_eye_pos(p);
        float cp = cosf(p->pitch);
        Vec3 dir = mmath_vec3(-sinf(p->yaw) * cp, sinf(p->pitch), -cosf(p->yaw) * cp);
        /* Spawn ahead of the eyes (clear of the body AABB) along the
         * crosshair ray: no parallax, owner grace covers the rest. */
        Vec3 origin = mmath_vec3_add(eye, mmath_vec3_scale(dir, 0.6f));
        if (!projectile_fire(&app->projectiles, PROJECTILE_ARROW, origin, dir, speed, damage,
                             SURVIVAL_BOW_KNOCK, ENTITY_PLAYER_ID)) {
            LOG_DEBUG("bow: projectile pool full (no ammo/durability consumed)");
            return;
        }
        if (!creative) {
            survival_bow_consume_arrow(&p->inv);
            if (stack_use_tool(slot)) {
                audio_play(&app->audio, AUDIO_TOOL_BREAK);
                LOG_INFO("tool broke: %s", item_get_info(ITEM_BOW)->name);
            }
        }
        audio_play(&app->audio, AUDIO_BOW_FIRE);
        LOG_DEBUG("bow fired: charge %.2f speed %.1f dmg %.1f", (double)charge, (double)speed,
                  (double)damage);
        return;
    }
    p->bow_t += dt;
    if (!p->bow_full && survival_bow_charge(p->bow_t) >= 1.0f) {
        p->bow_full = true;
        audio_play(&app->audio, AUDIO_BOW_DRAW); /* Full-draw tick. */
    }
}

/* Survival right-edge placement with stack consume-on-success.
 *
 * Args:
 *   app: context with an open world.
 */
static void app_tick_place(AppContext *app)
{
    if (!window_take_mouse_pressed(app->window, MINEC_MOUSE_RIGHT)) {
        return;
    }
    Vec3 eye = player_eye_pos(&app->player);
    HitResult hit = raycast_from_eye(eye, app->player.yaw, app->player.pitch, app->world,
                                     survival_reach(false));
    /* Workbench use (survival): RMB on the station opens the 3x3 crafting
     * UI instead of placing. Sneaking bypasses (places against it). */
    if (hit.hit && world_get_block(app->world, hit.block[0], hit.block[1], hit.block[2]) ==
                       (uint16_t)BLOCK_WORKBENCH &&
        !app->player.sneaking) {
        survival_mine_reset(&app->player);
        app_enter_state(app, GAME_STATE_CRAFTING);
        return;
    }
    int sel = app->player.hotbar_sel;
    ItemId held = app->player.inv.slots[sel].item;
    if (survival_bow_is_bow(held)) {
        return; /* RMB with a bow draws (app_tick_bow), never places. */
    }
    uint16_t block = item_to_block(held);
    if (block == 0) {
        LOG_DEBUG("place: selected item is not placeable");
        return;
    }
    uint16_t replaced = BLOCK_AIR;
    if (interaction_place(app->world, &app->player, &hit, block, &replaced)) {
        audio_play_block(&app->audio, AUDIO_BLOCK_PLACE, block);
        if (!inv_consume(&app->player.inv, sel, 1)) {
            LOG_WARN("place: consume failed after successful placement");
        }
        if (replaced != BLOCK_AIR) {
            audio_play_block(&app->audio, AUDIO_BLOCK_BREAK, replaced);
            ItemStack drop = survival_block_drop(replaced, held);
            if (!stack_is_empty(&drop)) {
                /* The replacement occupies the original cell; start the
                 * drop above it so collision resolution keeps it visible. */
                Vec3 at = mmath_vec3((float)(hit.block[0] + hit.normal[0]) + 0.5f,
                                     (float)(hit.block[1] + hit.normal[1]) + 1.05f,
                                     (float)(hit.block[2] + hit.normal[2]) + 0.5f);
                app_spawn_drop(app, at, &drop);
            }
        }
    }
}

/* Drop the whole inventory as entities at the death position (bounded:
 * 36 stacks always fit the 128 pool unless it is nearly full; leftovers
 * stay equipped so nothing is ever destroyed or duplicated).
 *
 * Args:
 *   app: context with an open world.
 */
static void app_death_drop_inventory(AppContext *app)
{
    Vec3 at = mmath_vec3(app->player.pos.x, app->player.pos.y + 0.5f, app->player.pos.z);
    /* The held cursor stack scatters too: death drops everything, and the
     * HUD never shows a cursor in PLAYING (an invisible kept stack would
     * look like item duplication on re-pickup). */
    if (!stack_is_empty(&app->cursor)) {
        if (entity_spawn(&app->entities, at, &app->cursor) >= 0) {
            stack_clear(&app->cursor);
        } else {
            LOG_WARN("death: entity pool full, keeping cursor stack held");
        }
    }
    /* Crafting grids scatter under the same spawn-or-keep policy (a grid
     * slot that cannot spawn stays gridded, retrievable after respawn). */
    for (int g = 0; g < 4; ++g) {
        ItemStack *s = &app->craft2[g];
        if (!stack_is_empty(s) && entity_spawn(&app->entities, at, s) >= 0) {
            stack_clear(s);
        }
    }
    for (int g = 0; g < 9; ++g) {
        ItemStack *s = &app->craft3[g];
        if (!stack_is_empty(s) && entity_spawn(&app->entities, at, s) >= 0) {
            stack_clear(s);
        }
    }
    stack_clear(&app->craft_out);
    app->craft_hash = 0;
    app->craft_hash3 = 0;
    for (int i = 0; i < INV_SIZE; ++i) {
        ItemStack *s = &app->player.inv.slots[i];
        if (stack_is_empty(s)) {
            continue;
        }
        if (entity_spawn(&app->entities, at, s) >= 0) {
            stack_clear(s);
        } else {
            LOG_WARN("death: entity pool full, keeping remainder in inventory");
            break;
        }
    }
}

/* Respawn the player at the stable spawn (requested by the death screen).
 * Restores vitals/velocity/state; inventory was dropped as entities.
 *
 * Args:
 *   app: context with an open world.
 */
void app_respawn_player(AppContext *app)
{
    if (app == NULL || !app->world_open) {
        return;
    }
    Vec3 spawn = app->has_spawn_point ? app->spawn_point : mmath_vec3(8.5f, 120.0f, 8.5f);
    survival_respawn(&app->player, spawn);
    app->hurt_flash = 0.0f;
    app->last_health = app->player.health;
    camera_set_position(app->camera, player_eye_pos(&app->player));
    LOG_INFO("respawned at (%.1f, %.1f, %.1f)", (double)spawn.x, (double)spawn.y, (double)spawn.z);
}

/* Return one crafting grid's contents (stash, drop, or void). */
static void app_resolve_grid(AppContext *app, ItemStack *grid, int n)
{
    for (int i = 0; i < n; ++i) {
        if (stack_is_empty(&grid[i])) {
            continue;
        }
        ItemStack rest = grid[i];
        uint16_t left = inv_insert(&app->player.inv, &rest);
        if (left == 0) {
            stack_clear(&grid[i]);
            continue;
        }
        if (survival_is_creative(&app->player)) {
            stack_clear(&grid[i]); /* Conjured copies: void the overflow. */
            continue;
        }
        if (app->world_open) {
            Vec3 at = player_eye_pos(&app->player);
            if (entity_spawn(&app->entities, at, &rest) >= 0) {
                stack_clear(&grid[i]);
                continue;
            }
        }
        /* Nowhere to go: keep gridded (reopen a crafting UI to retrieve). */
        grid[i] = rest;
        LOG_WARN("crafting close: grid slot %d kept (%u x %s)", i, (unsigned)rest.count,
                 item_get_info(rest.item)->name);
    }
}

/* Return crafting-grid contents to the inventory (see app.h). */
void app_resolve_crafting(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    app_resolve_grid(app, app->craft2, 4);
    app_resolve_grid(app, app->craft3, 9);
    stack_clear(&app->craft_out);
    app->craft_hash = 0;
    app->craft_hash3 = 0;
}

/* Resolve a held cursor stack when the inventory closes: back into the
 * inventory first; survival remainder becomes a world drop, creative
 * remainder is voided (infinite sources make it moot).
 *
 * Args:
 *   app: context with an open world.
 */
void app_resolve_cursor(AppContext *app)
{
    if (stack_is_empty(&app->cursor)) {
        return;
    }
    ItemStack rest = app->cursor;
    uint16_t left = inv_insert(&app->player.inv, &rest);
    if (left == 0) {
        stack_clear(&app->cursor);
        return;
    }
    if (survival_is_creative(&app->player)) {
        LOG_DEBUG("inventory close: creative cursor remainder voided");
        stack_clear(&app->cursor);
        return;
    }
    Vec3 at = player_eye_pos(&app->player);
    if (entity_spawn(&app->entities, at, &rest) >= 0) {
        stack_clear(&app->cursor);
    } else {
        /* Pool full and inventory full: keep holding it (no loss). */
        app->cursor = rest;
        LOG_WARN("inventory close: nowhere for cursor stack, keeping it held");
    }
}
/* Assemble the menu UiFrame for this frame: drains NO events itself (the
 * caller drains), but consumes latched press edges and samples held state.
 *
 * Args:
 *   app: context.
 *   ui: receiver (must not be NULL).
 *   text: TEXT-event bytes accumulated by the drain loop (may be NULL).
 */
static void app_build_uiframe(AppContext *app, UiFrame *ui, const char *text)
{
    /* key_escape arrives as a drain-loop edge (set by the caller before
     * this); everything else is re-sampled below, so preserve it. */
    bool esc = ui->key_escape;
    ui_frame_clear(ui);
    ui->key_escape = esc;
    window_get_mouse_pos(&ui->mouse_x, &ui->mouse_y);
    ui->mouse_down = window_is_mouse_down(MINEC_MOUSE_LEFT);
    ui->mouse_clicked = app->state == GAME_STATE_PLAYING
                            ? false
                            : window_take_mouse_pressed(app->window, MINEC_MOUSE_LEFT);
    app->prev_menu_click = ui->mouse_down;
    bool rdown = window_is_mouse_down(MINEC_MOUSE_RIGHT);
    ui->mouse_rdown = rdown;
    ui->mouse_rclicked = app->state == GAME_STATE_PLAYING
                             ? false
                             : window_take_mouse_pressed(app->window, MINEC_MOUSE_RIGHT);
    app->prev_menu_rclick = rdown;
    ui->wheel = window_take_wheel_delta(app->window);
    if (text != NULL) {
        size_t n = strlen(text);
        if (n > sizeof(ui->text) - 1) {
            n = sizeof(ui->text) - 1;
        }
        memcpy(ui->text, text, n);
        ui->text[n] = '\0';
    }
    bool ret = window_is_key_down(SDL_SCANCODE_RETURN) || window_is_key_down(SDL_SCANCODE_KP_ENTER);
    bool bs = window_is_key_down(SDL_SCANCODE_BACKSPACE);
    bool up = window_is_key_down(SDL_SCANCODE_UP);
    bool down = window_is_key_down(SDL_SCANCODE_DOWN);
    bool del = window_is_key_down(SDL_SCANCODE_DELETE);
    ui->key_return = window_take_key_pressed(app->window, SDL_SCANCODE_RETURN) ||
                     window_take_key_pressed(app->window, SDL_SCANCODE_KP_ENTER);
    ui->key_backspace = window_take_key_pressed(app->window, SDL_SCANCODE_BACKSPACE);
    ui->key_up = window_take_key_pressed(app->window, SDL_SCANCODE_UP);
    ui->key_down = window_take_key_pressed(app->window, SDL_SCANCODE_DOWN);
    ui->key_delete = window_take_key_pressed(app->window, SDL_SCANCODE_DELETE);
    app->prev_return = ret;
    app->prev_backspace = bs;
    app->prev_up = up;
    app->prev_down = down;
    app->prev_delete = del;
    /* key_escape is set by the event drain (edge by construction). */
}

/* Staged LOADING tick: stream a budget of chunks, remesh, draw behind the
 * overlay. Completes into spawn + PLAYING.
 *
 * Args:
 *   app: context with an opening session.
 */
static void app_tick_loading(AppContext *app)
{
    streamer_update(&app->streamer, app->player.pos, MINEC_LOAD_BUDGET);
    renderer_prune_world(app->renderer, app->world);
    renderer_refresh_world(app->renderer, app->world);
    app->load_done = (int)world_chunk_count(app->world);
    if (app->load_done < app->load_total) {
        return;
    }
    /* Fill complete: spawn, sync the camera, enter play. */
    session_find_spawn(app);
    camera_set_position(app->camera, player_eye_pos(&app->player));
    camera_set_yaw_pitch(app->camera, app->player.yaw, app->player.pitch);
    renderer_refresh_world(app->renderer, app->world);
    app->autosave_timer = 0.0;
    app_enter_state(app, GAME_STATE_PLAYING);
}

/* One authoritative world tick. Rendering and chunk refresh happen once per
 * outer frame in app_render_playing(), after all scheduled ticks.
 *
 * Args:
 *   app: context.
 *   dt: clamped frame time.
 */
static void app_tick_playing(AppContext *app, float dt)
{
    if (!game_state_ticks_world(app->state) || !app->world_open) {
        return;
    }
    bool controllable = app->state == GAME_STATE_PLAYING && !app->chat_open;

    time_system_update(&app->clock, dt);
    /* The host's fluid transitions are canonical. Clients run the same
     * fixed-step simulation for responsiveness but never echo predictions. */
    world_water_tick_mode(app->world, dt, !app->lan_client);
    world_gravity_tick(app->world, dt, !app->lan_client);

    PlayerInput in;
    if (controllable) {
        app_poll_move_input(&in);
        /* Preserve a quick jump tap until one simulation tick can consume it. */
        in.jump = in.jump || window_take_key_pressed(app->window, SDL_SCANCODE_SPACE);
    } else {
        memset(&in, 0, sizeof(in));
    }
    bool was_grounded = app->player.grounded;
    /* Drawing a bow slows movement (both modes, MC-feel anchor): scale
     * the tuned speeds around the update, then restore them. */
    bool drawing = app->player.bow_drawing;
    float saved_walk = 0.0f;
    float saved_sprint = 0.0f;
    if (drawing) {
        saved_walk = app->player.walk_speed;
        saved_sprint = app->player.sprint_speed;
        app->player.walk_speed *= SURVIVAL_BOW_MOVE_SCALE;
        app->player.sprint_speed *= SURVIVAL_BOW_MOVE_SCALE;
    }
    player_update(&app->player, &in, app->world, dt);
    if (drawing) {
        app->player.walk_speed = saved_walk;
        app->player.sprint_speed = saved_sprint;
    }
    /* Splashdown: dry-to-wet edge while falling reads as a real entry
     * (particles + converted splash sound, synth fallback). */
    {
        float contact = player_water_contact(app->world, &app->player);
        if (app->water_contact_prev <= 0.05f && contact > 0.4f && app->player.vel.y < -3.0f) {
            int bx = (int)floorf(app->player.pos.x);
            int by = (int)floorf(app->player.pos.y + 1.0f);
            int bz = (int)floorf(app->player.pos.z);
            particle_burst_block(&app->particles, (uint16_t)BLOCK_WATER, bx, by, bz, 14);
            audio_play(&app->audio, AUDIO_SPLASH);
        }
        app->water_contact_prev = contact;
    }
    bool creative = survival_is_creative(&app->player);

    /* Activity strain (survival only): sprinting while moving and jump
     * takeoffs accumulate exhaustion (burned into hunger by the policy). */
    if (!creative && controllable) {
        if (in.sprint && (in.fwd != 0.0f || in.strafe != 0.0f)) {
            survival_add_exhaustion(&app->player, SURVIVAL_EXHAUST_SPRINT * dt);
        }
        if (in.jump && !app->prev_jump && was_grounded) {
            survival_add_exhaustion(&app->player, SURVIVAL_EXHAUST_JUMP);
        }
    }
    app->prev_jump = controllable && in.jump;

    /* Melee + mob-damage cooldowns decay on the play tick. */
    if (app->player.hurt_t > 0.0f) {
        app->player.hurt_t -= dt;
        if (app->player.hurt_t < 0.0f) {
            app->player.hurt_t = 0.0f;
        }
    }
    if (app->player.attack_cd > 0.0f) {
        app->player.attack_cd -= dt;
        if (app->player.attack_cd < 0.0f) {
            app->player.attack_cd = 0.0f;
        }
    }
    if (app->arm_swing_repeat_t > 0.0f) {
        app->arm_swing_repeat_t -= dt;
    }

    /* First-person hand controller: locomotion facts in, events latched
     * at their gameplay sites, one update per play tick. */
    {
        PlayerAnimInput pai;
        float hspeed =
            sqrtf(app->player.vel.x * app->player.vel.x + app->player.vel.z * app->player.vel.z);
        pai.moving = hspeed > 0.5f;
        pai.sprinting = app->player.sprinting && pai.moving;
        pai.grounded = app->player.grounded;
        pai.sneaking = app->player.sneaking;
        pai.use_hold = app->player.eat_active || app->player.bow_drawing;
        player_anim_update(&app->panim, &pai, dt);
    }

    /* Footsteps: distance cadence on ground (either mode, never flying,
     * never sneaking — Minecraft stays silent while sneaking). */
    if (controllable && app->player.grounded && !app->player.flying && !app->player.sneaking &&
        (in.fwd != 0.0f || in.strafe != 0.0f)) {
        float hspeed = sqrtf(app->player.vel.x * app->player.vel.x + app->player.vel.z * app->player.vel.z);
        app->step_dist += hspeed * dt;
        if (app->step_dist >= 2.2f) {
            app->step_dist -= 2.2f;
            uint16_t stepped = app_footstep_block(app);
            audio_play_block(&app->audio, app_footstep_event(stepped), stepped);
        }
    } else {
        app->step_dist = 0.0f;
    }

    /* Fall damage (survival, non-flying): consume the landing report.
     * Water cushions the landing exactly like Minecraft: any real
     * contact with water at touchdown forgives the fall. */
    if (!survival_is_creative(&app->player) && !app->player.flying && app->player.last_fall >= 0.0f) {
        float dist = app->player.last_fall;
        app->player.last_fall = -1.0f;
        player_anim_notify_landed(&app->panim, dist);
bool splashed = player_water_contact(app->world, &app->player) > 0.0f;
        float dmg = splashed ? 0.0f : survival_fall_damage(dist);
        if (dmg > 0.0f) {
            survival_damage_player(&app->player, dmg);
            player_anim_notify_hurt(&app->panim);
            audio_play(&app->audio, AUDIO_PLAYER_HURT);
            LOG_INFO("fall: %.1f blocks -> %.1f damage (HP %.1f)", (double)dist, (double)dmg,
                     (double)app->player.health);
        } else if (dist >= 1.5f && !splashed) {
            /* Hard but harmless landing: a footstep thud on the ground
             * material (no new assets, just the step bank). */
            audio_play_block(&app->audio, AUDIO_STEP_STONE,
                             world_get_block(app->world, (int)floorf(app->player.pos.x),
                                             (int)floorf(app->player.pos.y - 0.01f),
                                             (int)floorf(app->player.pos.z)));
        }
    } else {
        app->player.last_fall = -1.0f;
    }

    /* Mining (survival hold) + placement (survival edge, consume-on-success).
     * Eating runs first: edible held stacks eat on RMB instead of placing
     * (edibles are never placeable, so the paths cannot collide). The bow
     * owns RMB in both modes whenever a bow is held (bows are neither
     * edible nor placeable, so no path collides).
     * Creative uses instant break/place inside discrete input instead. */
    if (controllable && !survival_is_creative(&app->player)) {
        app_tick_eat(app, dt);
        app_tick_bow(app, dt);
        app_tick_mining(app, dt);
        app_tick_place(app);
    } else if (controllable) {
        app_tick_bow(app, dt);
        app_tick_creative_actions(app);
    } else {
        survival_mine_reset(&app->player);
        survival_bow_reset(&app->player);
    }

    /* Living mobs simulate (both modes; frozen by pause/death). Mob strikes
     * apply through the hurt-window gate (creative players immune).
     * Requested skeleton shots fire through the shared pool right here,
     * before projectiles simulate — same-tick arrows fly immediately. */
    {
        MobPlayerInfo mpi;
        mpi.pos = app->player.pos;
        mpi.eye_height = app->player.eye_height;
        mpi.alive = !app->player.dead;
        mpi.creative = creative;
        mpi.day_progress = app->clock.day_progress;
        MobFrameEvents mev;
        mob_update_all(&app->mobs, &app->entities, app->world, &mpi, dt, &mev);
        for (int i = 0; i < mev.fire_requests; ++i) {
            const ProjectileShot *s = &mev.shots[i];
            if (projectile_fire(&app->projectiles, PROJECTILE_ARROW, s->origin, s->dir, s->speed,
                                s->damage, s->knock_power, s->owner)) {
                audio_play(&app->audio, AUDIO_BOW_FIRE);
            }
        }
        if (mev.shots_dropped > 0) {
            LOG_WARN("projectiles: %d skeleton shot(s) dropped past the cap", mev.shots_dropped);
        }
        if (mev.player_hits > 0 && !app->player.dead && !creative && app->player.hurt_t <= 0.0f) {
            survival_damage_player(&app->player, mev.player_damage);
            player_anim_notify_hurt(&app->panim);
            player_apply_knockback(&app->player, mev.player_knock.x, mev.player_knock.y,
                                   mev.player_knock.z);
            app->player.hurt_t = PLAYER_HURT_WINDOW;
            audio_play(&app->audio, AUDIO_PLAYER_HURT);
            LOG_INFO("mob hit: %d strike(s) %.1f damage (HP %.1f)", mev.player_hits,
                     (double)mev.player_damage, (double)app->player.health);
        }
        if (mev.mobs_died > 0) {
            audio_play(&app->audio, mob_die_sound(mev.last_died_type));
            particle_burst_item(&app->particles, (uint16_t)mev.last_death_item, mev.last_death_pos, 8);
        } else if (mev.mobs_hurt > 0) {
            audio_play(&app->audio, mob_hurt_sound(mev.last_hurt_type));
        }
    }

    /* Projectiles simulate (both modes): swept collision, damage through
     * the M8 APIs, embed/decay/despawn. Player hits gate on hurt_t. */
    {
        ProjectilePlayer pp;
        pp.pos = app->player.pos;
        pp.height = app->player.height;
        pp.width = app->player.width;
        pp.alive = !app->player.dead;
        pp.creative = creative;
        ProjectileFrameEvents pev;
        projectile_update(&app->projectiles, &app->mobs, &app->entities, app->world, &pp, dt, &pev);
        if (pev.player_hits > 0 && !app->player.dead && !creative && app->player.hurt_t <= 0.0f) {
            survival_damage_player(&app->player, pev.player_damage);
            player_anim_notify_hurt(&app->panim);
            player_apply_knockback(&app->player, pev.player_knock.x, pev.player_knock.y,
                                   pev.player_knock.z);
            app->player.hurt_t = PLAYER_HURT_WINDOW;
            audio_play(&app->audio, AUDIO_PLAYER_HURT);
            LOG_INFO("arrow hit: %d strike(s) %.1f damage (HP %.1f)", pev.player_hits,
                     (double)pev.player_damage, (double)app->player.health);
        }
        if (pev.mobs_died > 0) {
            audio_play(&app->audio, mob_die_sound(pev.last_died_type));
            particle_burst_item(&app->particles, (uint16_t)pev.last_death_item, pev.last_death_pos, 8);
        } else if (pev.mobs_hit > 0) {
            audio_play(&app->audio, mob_hurt_sound(pev.last_hurt_type));
        }
        if (pev.blocks_hit > 0) {
            audio_play(&app->audio, AUDIO_ARROW_STICK);
            particle_burst_item(&app->particles, ITEM_ARROW, pev.last_block_pos, 4);
        }
    }

    /* Entities simulate + vacuum up in both modes (harmless in creative). */
    entity_update(&app->entities, app->world, dt);
    ItemId picked = ITEM_NONE;
    int got = entity_try_pickup(&app->entities, &app->player, ENTITY_PICKUP_RADIUS, &picked);
    if (got > 0) {
        audio_play(&app->audio, AUDIO_ITEM_PICKUP);
        Vec3 puff_at =
            mmath_vec3(app->player.pos.x, app->player.pos.y + app->player.height * 0.5f, app->player.pos.z);
        particle_burst_item(&app->particles, (uint16_t)picked, puff_at, 4);
        LOG_DEBUG("pickup: %d entit%s collected", got, got == 1 ? "y" : "ies");
    }
    /* Particle feedback simulates on the play tick (frozen in menus). */
    particle_update(&app->particles, dt);
    /* Restrained mining impact puffs (one small puff per 0.15 s of held
     * mining, tied to the live target — never a per-frame storm). */
    if (app->player.mine_active) {
        app->mine_fx_t += dt;
        if (app->mine_fx_t >= 0.15f) {
    app->mine_fx_t = 0.0f;
    app->water_contact_prev = 0.0f;
            particle_burst_block(&app->particles, app->player.mine_block, app->player.mine_bx,
                                 app->player.mine_by, app->player.mine_bz, 1);
        }
    } else {
        app->mine_fx_t = 0.0f;
    }

    /* Hunger/regen/starve (survival only; timers freeze while paused/dead). */
    if (!survival_is_creative(&app->player)) {
        survival_hunger_update(&app->player, dt);
    }

    /* Damage flash tracks any health loss (fall, starve, dev hurt): the
     * HUD flash decays over ~0.7 s. Respawn restores health silently. */
    if (app->player.health < app->last_health) {
        app->hurt_flash = 1.0f;
    }
    app->last_health = app->player.health;
    if (app->hurt_flash > 0.0f) {
        app->hurt_flash -= dt * 1.5f;
        if (app->hurt_flash < 0.0f) {
            app->hurt_flash = 0.0f;
        }
    }

    /* Death gate: drop inventory as entities, then show the death screen. */
    if (app->player.dead) {
        app_death_drop_inventory(app);
        audio_play(&app->audio, AUDIO_PLAYER_DIE);
        app_enter_state(app, GAME_STATE_DEAD);
        return;
    }

    app->autosave_timer += (double)dt;
    if (app->autosave_timer >= MINEC_AUTOSAVE_SECONDS) {
        app->autosave_timer -= MINEC_AUTOSAVE_SECONDS;
        session_save_now(app);
    }
}

static void app_prepare_world_render(AppContext *app);

static void app_draw_remote_players(AppContext *app, float aspect)
{
    if (app == NULL || app->renderer == NULL || app->camera == NULL) {
        return;
    }
    for (size_t i = 0; i < MINEC_LAN_MAX_PLAYERS; ++i) {
        const LanRemotePlayer *remote = &app->lan_players[i];
        if (!remote->active || remote->username[0] == '\0') {
            continue;
        }
        Player avatar = app->player;
        avatar.pos = remote->pos;
        avatar.render_pos = remote->pos;
        avatar.yaw = remote->yaw;
        avatar.pitch = remote->pitch;
        avatar.walk_phase = remote->walk_phase;
        avatar.sneaking = remote->sneaking;
        renderer_draw_player(app->renderer, app->camera, aspect, &app->clock, &avatar,
                             remote->moving ? 0.9f : 0.0f);
    }
}

/* Refresh streamed geometry once and draw the current play presentation. */
static void app_render_playing(AppContext *app)
{
    if (app == NULL || !app->world_open) {
        return;
    }
    app_prepare_world_render(app);

    float aspect = app->height > 0 ? (float)app->width / (float)app->height : 16.0f / 9.0f;
    renderer_draw_world(app->renderer, app->world, app->camera, aspect, &app->clock);
    renderer_draw_falling_blocks(app->renderer, app->world, app->camera, aspect, &app->clock);
    renderer_draw_mobs(app->renderer, &app->mobs, app->camera, aspect, &app->clock,
                       (float)(app->last_frame_time - app->start_time), &app->mobs_drawn, &app->mobs_culled);
    app_draw_player_third(app, aspect);
    app_draw_remote_players(app, aspect);
    renderer_draw_projectiles(app->renderer, &app->projectiles, app->camera, aspect, &app->clock,
                              &app->arrows_drawn);
    renderer_draw_entities(app->renderer, &app->entities, app->camera, aspect, &app->clock);
    renderer_draw_particles(app->renderer, &app->particles, app->camera, aspect, &app->clock);
    if (app->player.mine_active && app->player.mine_need > 0.0f) {
        float frac = app->player.mine_progress / app->player.mine_need;
        if (frac < 0.0f) {
            frac = 0.0f;
        }
        if (frac >= 1.0f) {
            frac = 0.99f;
        }
        renderer_draw_block_overlay(app->renderer, app->camera, aspect, &app->clock, app->player.mine_bx,
                                    app->player.mine_by, app->player.mine_bz, frac);
    }
    /* Hover highlight: one raycast per frame outlines the crosshair
     * block in reach (both modes; mining aims at the same cell, so the
     * crack overlay and the outline agree by construction). */
    {
        Vec3 eye = player_eye_pos(&app->player);
        HitResult hov = raycast_from_eye(eye, app->player.yaw, app->player.pitch, app->world,
                                         survival_reach(survival_is_creative(&app->player)));
        if (hov.hit) {
            renderer_draw_block_outline(app->renderer, app->camera, aspect, hov.block[0], hov.block[1],
                                        hov.block[2]);
        }
    }
    {
        const ItemStack *held = &app->player.inv.slots[app->player.hotbar_sel];
        int held_tile = stack_is_empty(held) ? -1 : item_get_info(held->item)->tile;
        uint16_t held_block = stack_is_empty(held) ? 0 : item_to_block(held->item);
        /* No floating arm in third-person: the real body is visible. */
        if (!app->third_person) {
            float swing_phase = app->arm_swing_active
                                    ? player_swing_phase((float)(app->last_frame_time - app->arm_swing_started))
                                    : 1.0f;
            PlayerAnimPose pose = player_anim_pose(&app->panim);
            renderer_draw_player_arm(app->renderer, app->camera, aspect, &app->clock, held_tile,
                                     held_block, swing_phase, &pose);
        }
    }
    bool vitals = !survival_is_creative(&app->player);
    float eat_frac = app->player.eat_active ? app->player.eat_t / SURVIVAL_EAT_TIME : -1.0f;
    float bow_frac = app->player.bow_drawing ? survival_bow_charge(app->player.bow_t) : -1.0f;
    renderer_draw_hud(app->renderer, app->width, app->height, app->player.inv.slots, app->player.hotbar_sel,
                      app->player.health, app->player.max_health, app->player.hunger, app->player.max_hunger,
                      vitals, eat_frac, bow_frac, app->hurt_flash);
    app_draw_chat(app);
    if (app->show_debug) {
        screens_draw_debug(app);
    }
}

/* Refresh streaming and camera state once before rendering any live world
 * view, including inventory and crafting overlays. The camera rides the
 * 60 Hz smoothed render position (player_eye_pos reads render_pos):
 * simulation still steps at 20 Hz, but the view never stair-steps.
 */
#define APP_THIRD_PERSON_DIST 4.0f

/* Place the camera: first-person sits at the eye; third-person pulls
 * back along the view direction, clipped 0.3 short of any wall so the
 * camera never embeds in terrain. Same yaw/pitch either way, so the
 * crosshair ray still starts at the eye (gameplay never reads the
 * camera position for targeting). */
static void app_position_camera(AppContext *app)
{
    Vec3 eye = player_eye_pos(&app->player);
    if (!app->third_person) {
        camera_set_position(app->camera, eye);
    } else {
        HitResult back = raycast_from_eye(eye, app->player.yaw + MMATH_PI, -app->player.pitch,
                                          app->world, APP_THIRD_PERSON_DIST);
        float d = back.hit ? back.dist - 0.3f : APP_THIRD_PERSON_DIST;
        if (d < 0.5f) {
            d = 0.5f;
        }
        float cp = cosf(app->player.pitch);
        Vec3 fwd = mmath_vec3(-sinf(app->player.yaw) * cp, sinf(app->player.pitch),
                                    -cosf(app->player.yaw) * cp);
        camera_set_position(app->camera, mmath_vec3_sub(eye, mmath_vec3_scale(fwd, d)));
    }
    camera_set_yaw_pitch(app->camera, app->player.yaw, app->player.pitch);
}

/* Draw the visible Steve body in third-person (all world-view states).
 * Stride blend follows ground speed so limbs rest when still. */
static void app_draw_player_third(AppContext *app, float aspect)
{
    if (!app->third_person) {
        return;
    }
    float hspeed =
        sqrtf(app->player.vel.x * app->player.vel.x + app->player.vel.z * app->player.vel.z);
    float blend = hspeed / 4.0f;
    if (blend > 1.0f) {
        blend = 1.0f;
    }
    renderer_draw_player(app->renderer, app->camera, aspect, &app->clock, &app->player, blend);
}

static void app_prepare_world_render(AppContext *app)
{
    if (app == NULL || !app->world_open) {
        return;
    }
    /* Render positions track the sim exactly: at 60 Hz ticks the steps
     * are frame-sized, so no smoothing lag is needed (the old damped
     * chase added ~80 ms of floaty lag to hide 20 Hz stair-steps). Snap
     * on teleports is now the only special case, handled by the same
     * assignment. */
    app->player.render_pos = app->player.pos;
    /* Mob render positions track the sim exactly (see player note). */
    {
        for (int i = 0; i < MOB_MAX; ++i) {
            Mob *mob = &app->mobs.mobs[i];
            if (!mob->active) {
                continue;
            }
            mob->render_pos = mob->pos;
        }
    }
    app_position_camera(app);
    /* Dense blue fog while the eye is submerged. */
    renderer_set_underwater(app->renderer,
                            player_eye_in_water(app->world, &app->player));
    streamer_update(&app->streamer, app->player.pos, MINEC_STREAM_BUDGET);
    renderer_prune_world(app->renderer, app->world);
    renderer_refresh_world(app->renderer, app->world);
}

/* Enter the main loop: state machine over menus, loading, and play.
 *
 * Args:
 *   app: initialised context.
 *
 * Returns: 0 on clean exit.
 */
int app_run(AppContext *app)
{
    if (app == NULL || app->window == NULL || app->renderer == NULL || app->camera == NULL) {
        return -1;
    }

    LOG_INFO("Entering main menu. Singleplayer to play.");
    app->last_frame_time = time_now_seconds();
    double sim_window_ms = 0.0;
    unsigned sim_window_ticks = 0;
    const Uint64 perf_frequency = SDL_GetPerformanceFrequency();
    UiFrame ui;
    char text_accum[64];

    while (app->running) {
        double frame_start = time_now_seconds();
        double frame_elapsed = frame_start - app->last_frame_time;
        app->last_frame_time = frame_start;
        if (frame_elapsed < 0.0) {
            frame_elapsed = 0.0;
        }

        /* Drain SDL events: system handling + menu input accumulation. */
        ui_frame_clear(&ui);
        text_accum[0] = '\0';
        size_t text_len = 0;
        bool esc_edge = false;
        WindowEvent ev;
        while (window_poll_event(app->window, &ev)) {
            if (ev.kind == WINDOW_EVENT_QUIT) {
                /* Window-close with a held cursor or gridded ingredients:
                 * resolve first so the stacks land in the inventory (saved
                 * below), not in limbo. */
                app_lan_disconnect(app);
                if (app->world_open) {
                    app_resolve_cursor(app);
                    app_resolve_crafting(app);
                    session_close_world(app, true);
                }
                app_enter_state(app, GAME_STATE_QUIT);
                break;
            }
            if (ev.kind == WINDOW_EVENT_RESIZE) {
                app->width = ev.width;
                app->height = ev.height;
                renderer_set_viewport(app->renderer, app->width, app->height);
                LOG_INFO("Window resized to %dx%d", app->width, app->height);
                continue;
            }
            if (ev.kind == WINDOW_EVENT_KEY_ESC) {
                esc_edge = true;
                continue;
            }
            if (ev.kind == WINDOW_EVENT_TEXT) {
                size_t n = strlen(ev.text);
                size_t room = sizeof(text_accum) - 1 - text_len;
                if (n > room) {
                    n = room;
                }
                memcpy(text_accum + text_len, ev.text, n);
                text_len += n;
                text_accum[text_len] = '\0';
                continue;
            }
            if (ev.kind == WINDOW_EVENT_FOCUS_LOST) {
                if (app->state == GAME_STATE_PLAYING) {
                    app_enter_state(app, GAME_STATE_PAUSED);
                }
                continue;
            }
        }
        app_lan_update(app, frame_elapsed);
        if (!app->running || app->state == GAME_STATE_QUIT) {
            break;
        }
        if (app->state == GAME_STATE_PLAYING && !app->chat_open &&
            window_take_key_pressed(app->window, SDL_SCANCODE_T)) {
            app->chat_open = true;
            app->chat_input[0] = '\0';
            app->chat_scroll = 0;
            window_set_relative_mouse(app->window, false);
            window_text_input(true);
            window_clear_input_edges(app->window);
        }
        if (app->chat_open) {
            int wheel = window_take_wheel_delta(app->window);
            int page_up = window_take_key_pressed(app->window, SDL_SCANCODE_PAGEUP) ? 1 : 0;
            int page_down = window_take_key_pressed(app->window, SDL_SCANCODE_PAGEDOWN) ? 1 : 0;
            unsigned visible = app_chat_history_visible(app);
            unsigned max_scroll = app->chat_line_count > visible
                                      ? app->chat_line_count - visible
                                      : 0u;
            if (wheel != 0) {
                int direction = wheel > 0 ? 1 : -1;
                int64_t steps = wheel > 0 ? (int64_t)wheel : -(int64_t)wheel;
                if (steps > 20) {
                    steps = 20;
                }
                unsigned amount = (unsigned)steps * 3u;
                if (direction > 0) {
                    app->chat_scroll = app->chat_scroll + amount > max_scroll
                                           ? max_scroll
                                           : app->chat_scroll + amount;
                } else {
                    app->chat_scroll = app->chat_scroll > amount
                                           ? app->chat_scroll - amount
                                           : 0u;
                }
            }
            unsigned page_amount = visible > 2u ? visible - 2u : 1u;
            if (page_up) {
                app->chat_scroll = app->chat_scroll + page_amount > max_scroll
                                       ? max_scroll
                                       : app->chat_scroll + page_amount;
            }
            if (page_down) {
                app->chat_scroll = app->chat_scroll > page_amount
                                       ? app->chat_scroll - page_amount
                                       : 0u;
            }
            app_chat_append_ascii(app, text_accum);
            text_accum[0] = '\0';
            bool enter = window_take_key_pressed(app->window, SDL_SCANCODE_RETURN) ||
                         window_take_key_pressed(app->window, SDL_SCANCODE_KP_ENTER);
            bool backspace = window_take_key_pressed(app->window, SDL_SCANCODE_BACKSPACE);
            if (esc_edge) {
                app->chat_input[0] = '\0';
                app_chat_close(app);
            } else if (enter) {
                app_chat_submit(app);
            } else if (backspace) {
                app_chat_backspace(app);
            }
            esc_edge = false;
        }
        ui.key_escape = esc_edge;
        /* Central ESC routing for live states (menus handle their own Back). */
        if (esc_edge && app->state == GAME_STATE_PLAYING && !app->chat_open) {
            app_enter_state(app, GAME_STATE_PAUSED);
            ui.key_escape = false;
        } else if (esc_edge && app->state == GAME_STATE_PAUSED) {
            app_enter_state(app, GAME_STATE_PLAYING);
            ui.key_escape = false;
        } else if (esc_edge && app->state == GAME_STATE_INVENTORY) {
            app_resolve_cursor(app);
            app_resolve_crafting(app);
            app_enter_state(app, GAME_STATE_PLAYING);
            ui.key_escape = false;
        } else if (esc_edge && app->state == GAME_STATE_CRAFTING) {
            app_resolve_cursor(app);
            app_resolve_crafting(app);
            app_enter_state(app, GAME_STATE_PLAYING);
            ui.key_escape = false;
        }

        /* Discrete gameplay edges (E/F/digits/wheel/clicks) are consumed
         * before the UI frame: the wheel accumulator is single-take, so
         * polling first keeps hotbar cycling alive (menus ignore wheel).
         * E may leave PLAYING here; the switch below then draws the new
         * state instead of simulating a stale one. */
        if (app->state == GAME_STATE_PLAYING && !app->chat_open) {
            app_poll_discrete_input(app);
        }

        /* Look remains responsive at render rate, while world actions use
         * latched input edges and held movement is sampled on fixed ticks.
         */
        if (app->state == GAME_STATE_PLAYING && !app->chat_open) {
            int mdx = 0;
            int mdy = 0;
            window_get_relative_motion(&mdx, &mdy);
            if (mdx != 0 || mdy != 0) {
                player_add_look(&app->player, (float)mdx, (float)mdy, app->sensitivity);
            }
        }
        if (game_state_ticks_world(app->state)) {
            if (app->discard_next_simulation_elapsed) {
                frame_elapsed = 0.0;
                app->discard_next_simulation_elapsed = false;
            }
            SimulationAdvance advance = simulation_clock_advance(&app->simulation, frame_elapsed);
            if (advance.dropped_ticks > 0) {
                LOG_WARN("simulation: dropped %llu overdue tick(s) (%.3f seconds) after catch-up cap",
                         (unsigned long long)advance.dropped_ticks, advance.dropped_time);
            }
            unsigned executed_ticks = 0;
            for (unsigned tick = 0; tick < advance.ticks && game_state_ticks_world(app->state); ++tick) {
                Uint64 tick_start = SDL_GetPerformanceCounter();
                app_tick_playing(app, (float)SIMULATION_TICK_SECONDS);
                Uint64 tick_end = SDL_GetPerformanceCounter();
                if (perf_frequency > 0 && tick_end >= tick_start) {
                    sim_window_ms += (double)(tick_end - tick_start) * 1000.0 / (double)perf_frequency;
                }
                executed_ticks++;
            }
            sim_window_ticks += executed_ticks;
        } else {
            /* No simulation this frame (menus, pause, death): drop any
             * stale window so the next 1s report starts fresh. */
            sim_window_ms = 0.0;
            sim_window_ticks = 0;
        }

        app_build_uiframe(app, &ui, text_accum);

        switch (app->state) {
        case GAME_STATE_PROFILE:
        case GAME_STATE_MAIN_MENU:
        case GAME_STATE_LAN_MENU:
        case GAME_STATE_WORLD_SELECT:
        case GAME_STATE_CREATE_WORLD:
        case GAME_STATE_SETTINGS:
        case GAME_STATE_PAUSED:
            if (app->state == GAME_STATE_PAUSED && app->world_open) {
                /* Frozen world behind the pause menu (no simulation). */
                float aspect = app->height > 0 ? (float)app->width / (float)app->height : 16.0f / 9.0f;
                renderer_draw_world(app->renderer, app->world, app->camera, aspect, &app->clock);
            }
            screens_update(app, &ui);
            break;
        case GAME_STATE_INVENTORY:
            if (!app->world_open) {
                /* Unreachable in practice (no close path from here); QUIT
                 * is the only legal exit without a world. */
                app_enter_state(app, GAME_STATE_QUIT);
                break;
            }
            {
                /* World simulation continues behind the inventory. */
                app_prepare_world_render(app);
                float aspect = app->height > 0 ? (float)app->width / (float)app->height : 16.0f / 9.0f;
                renderer_draw_world(app->renderer, app->world, app->camera, aspect, &app->clock);
                renderer_draw_falling_blocks(app->renderer, app->world, app->camera, aspect, &app->clock);
                renderer_draw_mobs(app->renderer, &app->mobs, app->camera, aspect, &app->clock,
                                   (float)(app->last_frame_time - app->start_time), NULL, NULL);
                app_draw_player_third(app, aspect);
                app_draw_remote_players(app, aspect);
                renderer_draw_projectiles(app->renderer, &app->projectiles, app->camera, aspect,
                                          &app->clock, NULL);
                renderer_draw_entities(app->renderer, &app->entities, app->camera, aspect, &app->clock);
                renderer_draw_particles(app->renderer, &app->particles, app->camera, aspect, &app->clock);
                screens_update(app, &ui);
                app_poll_cursor_drop(app);
                /* E closes the inventory too (cursor resolved first). The E
                * edge here shares prev_e_key with PLAYING so a held key
                * from opening never double-triggers. */
                bool e_down = window_is_key_down(SDL_SCANCODE_E);
                bool e_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_E);
                if (e_pressed && app->state == GAME_STATE_INVENTORY) {
                    survival_mine_reset(&app->player);
                    app_resolve_cursor(app);
                    app_resolve_crafting(app);
                    app_enter_state(app, GAME_STATE_PLAYING);
                }
                app->prev_e_key = e_down;
            }
            break;
        case GAME_STATE_CRAFTING:
            if (!app->world_open) {
                /* Same fail-safe as INVENTORY (no close path from here). */
                app_enter_state(app, GAME_STATE_QUIT);
                break;
            }
            {
                /* World simulation continues behind the workbench. */
                app_prepare_world_render(app);
                float aspect = app->height > 0 ? (float)app->width / (float)app->height : 16.0f / 9.0f;
                renderer_draw_world(app->renderer, app->world, app->camera, aspect, &app->clock);
                renderer_draw_falling_blocks(app->renderer, app->world, app->camera, aspect, &app->clock);
                renderer_draw_mobs(app->renderer, &app->mobs, app->camera, aspect, &app->clock,
                                   (float)(app->last_frame_time - app->start_time), NULL, NULL);
                app_draw_player_third(app, aspect);
                app_draw_remote_players(app, aspect);
                renderer_draw_projectiles(app->renderer, &app->projectiles, app->camera, aspect,
                                          &app->clock, NULL);
                renderer_draw_entities(app->renderer, &app->entities, app->camera, aspect, &app->clock);
                renderer_draw_particles(app->renderer, &app->particles, app->camera, aspect, &app->clock);
                screens_update(app, &ui);
                app_poll_cursor_drop(app);
                /* E closes the bench too (shares prev_e_key with PLAYING). */
                bool e_down = window_is_key_down(SDL_SCANCODE_E);
                bool e_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_E);
                if (e_pressed && app->state == GAME_STATE_CRAFTING) {
                    survival_mine_reset(&app->player);
                    app_resolve_cursor(app);
                    app_resolve_crafting(app);
                    app_enter_state(app, GAME_STATE_PLAYING);
                }
                app->prev_e_key = e_down;
            }
            break;
        case GAME_STATE_DEAD:
            if (!app->world_open) {
                app_enter_state(app, GAME_STATE_MAIN_MENU);
                break;
            }
            {
                float aspect = app->height > 0 ? (float)app->width / (float)app->height : 16.0f / 9.0f;
                renderer_draw_world(app->renderer, app->world, app->camera, aspect, &app->clock);
                screens_update(app, &ui);
            }
            break;
        case GAME_STATE_LOADING:
            if (!app->world_open) {
                app_enter_state(app, GAME_STATE_QUIT);
                break;
            }
            app_tick_loading(app);
            if (app->state == GAME_STATE_LOADING) {
                float aspect = app->height > 0 ? (float)app->width / (float)app->height : 16.0f / 9.0f;
                renderer_draw_world(app->renderer, app->world, app->camera, aspect, &app->clock);
                screens_update(app, &ui);
            }
            break;
        case GAME_STATE_PLAYING:
            if (!app->world_open) {
                app_enter_state(app, GAME_STATE_QUIT);
                break;
            }
            app_render_playing(app);
            break;
        case GAME_STATE_QUIT:
        default:
            break;
        }
        if (app->state == GAME_STATE_QUIT) {
            if (app->world_open) {
                session_close_world(app, true);
            }
            app->running = false;
            break;
        }
        window_swap(app->window);

        app->frame_count++;
        double now = time_now_seconds();
        double elapsed = now - app->last_fps_time;
        if (elapsed >= 1.0) {
            double fps = (double)app->frame_count / elapsed;
            app->fps_smooth = (float)fps;
            app->frame_ms_avg = app->frame_count > 0 ? (float)(elapsed * 1000.0 / (double)app->frame_count) : 0.0f;
            app->sim_ms_avg = sim_window_ticks > 0 ? (float)(sim_window_ms / (double)sim_window_ticks) : 0.0f;
            app->sim_tps = (float)((double)sim_window_ticks / elapsed);
            app->process_working_set_bytes = mem_process_working_set_bytes();
            if (app->world_open) {
                Vec3 p = app->player.pos;
                RendererPerf perf = renderer_get_perf(app->renderer);
                int sel_idx = app->player.hotbar_sel;
                if (sel_idx < 0) {
                    sel_idx = 0;
                }
                if (sel_idx > 8) {
                    sel_idx = 8;
                }
                const ItemStack *sel = &app->player.inv.slots[sel_idx];
                const ItemInfo *sel_info = item_get_info(sel->item);
                LOG_INFO("FPS: %.1f | %s pos (%.1f, %.1f, %.1f)%s HP %.0f/%.0f HU %.0f day %.3f | hotbar %d (%s x%u) | chunks %zu drawn %zu(+%zu) culled %zu | "
                         "frame %.2fms sim %.2fms/tick %.1f TPS RSS %llu MiB | mesh %.2fms up %.2fms draw %.2fms",
                         fps, app->player.flying ? "Creative" : "Survival", p.x, p.y, p.z,
                         app->player.grounded ? " grounded" : "", (double)app->player.health,
                         (double)app->player.max_health, (double)app->player.hunger, app->clock.day_progress,
                         app->player.hotbar_sel + 1, sel_info ? sel_info->name : "?",
                         stack_is_empty(sel) ? 0u : (unsigned)sel->count, world_chunk_count(app->world),
                         perf.drawn, perf.drawn_t, perf.culled, app->frame_ms_avg, app->sim_ms_avg,
                         app->sim_tps, (unsigned long long)(app->process_working_set_bytes / (1024u * 1024u)),
                         perf.mesh_ms, perf.upload_ms, perf.draw_ms);
            } else {
                LOG_INFO("FPS: %.1f | frame %.2fms | RSS %llu MiB | state %s", fps, app->frame_ms_avg,
                         (unsigned long long)(app->process_working_set_bytes / (1024u * 1024u)),
                         game_state_name(app->state));
            }
            app->frame_count = 0;
            app->last_fps_time = now;
            sim_window_ms = 0.0;
            sim_window_ticks = 0;
        }
    }

    window_set_relative_mouse(app->window, false);
    window_text_input(false);
    LOG_INFO("Main loop exited.");
    return 0;
}

/* Shut down everything (saves settings + open session best-effort).
 *
 * Args:
 *   app: context to shut down.
 */
void app_shutdown(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    app->running = false;
    app_lan_disconnect(app);
    lan_discover_destroy(app->lan_discover);
    app->lan_discover = NULL;
    lan_destroy(app->lan);
    app->lan = NULL;
    if (app->world_open) {
        session_close_world(app, true);
    }
    settings_save(&app->settings, SETTINGS_PATH);
    audio_shutdown(&app->audio);
    app->streamer_ready = false;
    app->player_ready = false;
    world_destroy(app->world);
    app->world = NULL;
    camera_destroy(app->camera);
    app->camera = NULL;
    renderer_destroy(app->renderer);
    app->renderer = NULL;
    gl_ctx_destroy(app->gl);
    app->gl = NULL;
    window_destroy(app->window);
    app->window = NULL;
    LOG_INFO("TerraCraft shut down.");
}
