#include "core/app.h"
#include "audio/audio.h"
#include "core/log.h"
#include "core/time.h"
#include "game/audio.h"
#include "game/entity.h"
#include "game/particle.h"
#include "game/interaction.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/player.h"
#include "game/raycast.h"
#include "game/session.h"
#include "game/survival.h"
#include "game/time_system.h"
#include "platform/gl_ctx.h"
#include "platform/window.h"
#include "render/camera.h"
#include "render/renderer.h"
#include "ui/screens.h"
#include "ui/ui.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/streamer.h"
#include "world/world.h"
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
    window_clear_input_edges(app->window);
    if (!game_state_ticks_world(from) && game_state_ticks_world(to)) {
        app->discard_next_simulation_elapsed = true;
    }
    if (game_state_ticks_world(from) != game_state_ticks_world(to) || to == GAME_STATE_PLAYING) {
        simulation_clock_reset_phase(&app->simulation);
    }
    bool capture = (to == GAME_STATE_PLAYING);
    window_set_relative_mouse(app->window, capture);
    window_text_input(to == GAME_STATE_CREATE_WORLD);
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
    app->show_debug = false;
    app->autosave_timer = 0.0;
    simulation_clock_init(&app->simulation);
    app->discard_next_simulation_elapsed = false;
    app->load_done = 0;
    app->load_total = 0;
    app->state = GAME_STATE_MAIN_MENU;
    app->sensitivity = MINEC_MOUSE_SENSITIVITY;
    memset(&app->menu, 0, sizeof(app->menu));
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

    LOG_INFO("TerraCraft M9 initialised. GL: %s / %s", gl_ctx_get_vendor(), gl_ctx_get_renderer());
    LOG_INFO("Main menu. Singleplayer to play; F3 toggles debug in game.");
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
static void app_strike_mob(AppContext *app, EntityId eid, float dmg, float knock_power);
static bool app_try_attack(AppContext *app);
static void app_debug_spawn_mob(AppContext *app, EntityType type);
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

    /* Dev helpers (documented): F6 gives 64 of the selected stack (or
     * stone when the slot is empty); F8 deals 5 damage (survival only);
     * F7 hurts the aimed mob; F9/F10/F11 spawn cow/gloomstalker/
     * skeleton; F12 clears all arrows (shift+F6 gives bow + arrows). */
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
            ItemStack give = {ITEM_NONE, 0};
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
        audio_play(&app->audio, AUDIO_PLAYER_HURT);
        LOG_INFO("Dev hurt: 5 damage (HP %.1f)", (double)app->player.health);
    }
    app->prev_f8_key = f8_down;
    bool f7_down = window_is_key_down(SDL_SCANCODE_F7);
    bool f7_pressed = window_take_key_pressed(app->window, SDL_SCANCODE_F7);
    if (f7_pressed) {
        EntityId eid = ENTITY_ID_NULL;
        if (app_aim_mob(app, &eid)) {
            app_strike_mob(app, eid, 5.0f, 5.0f);
            LOG_INFO("Dev mob hurt: 5 damage");
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
        app_debug_spawn_mob(app, ENTITY_GLOOMSTALKER);
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

    /* Hotbar digits 1..9 (SDL scancodes are consecutive). Slot switches
     * reset survival mining (spec: changing slots updates mining) and
     * cancel bow draws (never fires, never consumes). */
    for (int i = 0; i < 9; ++i) {
        bool down = window_is_key_down(SDL_SCANCODE_1 + i);
        if (window_take_key_pressed(app->window, SDL_SCANCODE_1 + i)) {
            app->player.hotbar_sel = i;
            survival_mine_reset(&app->player);
            survival_bow_reset(&app->player);
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
        const ItemInfo *info = item_get_info(app->player.inv.slots[sel].item);
        LOG_INFO("Hotbar: slot %d (%s)", sel + 1, info ? info->name : "?");
    }

    /* Mouse actions are consumed by the fixed world tick so a press between
     * ticks remains queued until gameplay can act on it. */
}

/* Creative block actions are instantaneous, but still run on a world tick. */
static void app_tick_creative_actions(AppContext *app)
{
    bool left_pressed = window_take_mouse_pressed(app->window, MINEC_MOUSE_LEFT);
    bool right_pressed = window_take_mouse_pressed(app->window, MINEC_MOUSE_RIGHT);
    if (!left_pressed && !right_pressed) {
        return;
    }
    Vec3 eye = player_eye_pos(&app->player);
    HitResult hit = raycast_from_eye(eye, app->player.yaw, app->player.pitch, app->world,
                                     survival_reach(true));
    if (left_pressed && !app_try_attack(app)) {
        int broken = interaction_break(app->world, &hit);
        if (broken >= 0) {
            audio_play_block(&app->audio, AUDIO_BLOCK_BREAK, (uint16_t)broken);
            particle_burst_block(&app->particles, (uint16_t)broken, hit.block[0], hit.block[1], hit.block[2], 16);
            LOG_DEBUG("break ok: id %d", broken);
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
    if (hit.hit && hit.dist < edist) {
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
static void app_strike_mob(AppContext *app, EntityId eid, float dmg, float knock_power)
{
    Mob *m = mob_resolve(&app->mobs, eid);
    if (m == NULL) {
        return;
    }
    Vec3 kdir = mmath_vec3(m->pos.x - app->player.pos.x, 0.0f, m->pos.z - app->player.pos.z);
    Vec3 from = mmath_vec3(app->player.pos.x - m->pos.x, 0.0f, app->player.pos.z - m->pos.z);
    Vec3 chest = mmath_vec3(m->pos.x, m->pos.y + m->height * 0.6f, m->pos.z);
    float before = m->health;
    bool killed = living_entity_damage(&app->mobs, &app->entities, eid, dmg, kdir, knock_power, &from);
    if (killed) {
        audio_play(&app->audio, AUDIO_MOB_DIE);
        particle_burst_item(&app->particles, ITEM_APPLE, chest, 8);
    } else if (m->health < before) {
        /* Gated strikes (hurt window) stay silent: no damage, no lie. */
        audio_play(&app->audio, AUDIO_MOB_HURT);
        particle_burst_item(&app->particles, ITEM_APPLE, chest, 5);
    }
}

/* Melee swing on the LMB edge: entity-first targeting; on a hit the
 * block action is skipped this tick (mining resumes on hold).
 * Returns true when a mob was struck.
 */
static bool app_try_attack(AppContext *app)
{
    if (app->player.attack_cd > 0.0f || app->player.dead) {
        return false;
    }
    EntityId eid = ENTITY_ID_NULL;
    if (!app_aim_mob(app, &eid)) {
        return false;
    }
    float dmg = 1.0f;
    float cd = 0.4f;
    mob_tool_stats(app->player.inv.slots[app->player.hotbar_sel].item, &dmg, &cd);
    app_strike_mob(app, eid, dmg, 5.0f);
    app->player.attack_cd = cd;
    return true;
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
    if (pressed && app_try_attack(app)) {
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
    /* Pool full (should not happen at 128): bank into inventory so
     * nothing is ever lost, else the drop vanishes with a loud log. */
    ItemStack rest = *drop;
    if (inv_insert(&app->player.inv, &rest) != 0) {
        LOG_WARN("mining: entity pool full and inventory full; lost %u x item %u", (unsigned)drop->count,
                 (unsigned)drop->item);
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
    bool controllable = app->state == GAME_STATE_PLAYING;

    time_system_update(&app->clock, dt);

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

    /* Fall damage (survival, non-flying): consume the landing report. */
    if (!survival_is_creative(&app->player) && !app->player.flying && app->player.last_fall >= 0.0f) {
        float dist = app->player.last_fall;
        app->player.last_fall = -1.0f;
        float dmg = survival_fall_damage(dist);
        if (dmg > 0.0f) {
            survival_damage_player(&app->player, dmg);
            audio_play(&app->audio, AUDIO_PLAYER_HURT);
            LOG_INFO("fall: %.1f blocks -> %.1f damage (HP %.1f)", (double)dist, (double)dmg,
                     (double)app->player.health);
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
            app->player.vel.x += mev.player_knock.x;
            app->player.vel.y += mev.player_knock.y;
            app->player.vel.z += mev.player_knock.z;
            app->player.hurt_t = PLAYER_HURT_WINDOW;
            audio_play(&app->audio, AUDIO_PLAYER_HURT);
            LOG_INFO("mob hit: %d strike(s) %.1f damage (HP %.1f)", mev.player_hits,
                     (double)mev.player_damage, (double)app->player.health);
        }
        if (mev.mobs_died > 0) {
            audio_play(&app->audio, AUDIO_MOB_DIE);
            particle_burst_item(&app->particles, (uint16_t)mev.last_death_item, mev.last_death_pos, 8);
        } else if (mev.mobs_hurt > 0) {
            audio_play(&app->audio, AUDIO_MOB_HURT);
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
            app->player.vel.x += pev.player_knock.x;
            app->player.vel.y += pev.player_knock.y;
            app->player.vel.z += pev.player_knock.z;
            app->player.hurt_t = PLAYER_HURT_WINDOW;
            audio_play(&app->audio, AUDIO_PLAYER_HURT);
            LOG_INFO("arrow hit: %d strike(s) %.1f damage (HP %.1f)", pev.player_hits,
                     (double)pev.player_damage, (double)app->player.health);
        }
        if (pev.mobs_died > 0) {
            audio_play(&app->audio, AUDIO_MOB_DIE);
            particle_burst_item(&app->particles, (uint16_t)pev.last_death_item, pev.last_death_pos, 8);
        } else if (pev.mobs_hit > 0) {
            audio_play(&app->audio, AUDIO_MOB_HURT);
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

/* Refresh streamed geometry once and draw the current play presentation. */
static void app_render_playing(AppContext *app)
{
    if (app == NULL || !app->world_open) {
        return;
    }
    app_prepare_world_render(app);

    float aspect = app->height > 0 ? (float)app->width / (float)app->height : 16.0f / 9.0f;
    renderer_draw_world(app->renderer, app->world, app->camera, aspect, &app->clock);
    renderer_draw_mobs(app->renderer, &app->mobs, app->camera, aspect, &app->clock,
                       (float)(app->last_frame_time - app->start_time), &app->mobs_drawn, &app->mobs_culled);
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
    bool vitals = !survival_is_creative(&app->player);
    float eat_frac = app->player.eat_active ? app->player.eat_t / SURVIVAL_EAT_TIME : -1.0f;
    float bow_frac = app->player.bow_drawing ? survival_bow_charge(app->player.bow_t) : -1.0f;
    renderer_draw_hud(app->renderer, app->width, app->height, app->player.inv.slots, app->player.hotbar_sel,
                      app->player.health, app->player.max_health, app->player.hunger, app->player.max_hunger,
                      vitals, eat_frac, bow_frac, app->hurt_flash);
    if (app->show_debug) {
        screens_draw_debug(app);
    }
}

/* Refresh streaming and camera state once before rendering any live world
 * view, including inventory and crafting overlays. The camera rides the
 * 60 Hz smoothed render position (player_eye_pos reads render_pos):
 * simulation still steps at 20 Hz, but the view never stair-steps.
 */
static void app_prepare_world_render(AppContext *app)
{
    if (app == NULL || !app->world_open) {
        return;
    }
    /* Render smoothing (critically damped, 60 Hz frame-rate independent):
     * render_pos chases the authoritative sim pos with zero overshoot.
     * Snap on teleports (respawn/load > 4 blocks) so fast-travel never
     * smears across the world. */
    {
        Vec3 rp = app->player.render_pos;
        Vec3 sp = app->player.pos;
        float dx = sp.x - rp.x;
        float dy = sp.y - rp.y;
        float dz = sp.z - rp.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 16.0f) {
            app->player.render_pos = sp;
        } else if (d2 > 0.0f) {
            float k = 1.0f - expf(-12.0f / 60.0f);
            app->player.render_pos =
                mmath_vec3(rp.x + dx * k, rp.y + dy * k, rp.z + dz * k);
        }
    }
    camera_set_position(app->camera, player_eye_pos(&app->player));
    camera_set_yaw_pitch(app->camera, app->player.yaw, app->player.pitch);
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
        if (!app->running || app->state == GAME_STATE_QUIT) {
            break;
        }
        ui.key_escape = esc_edge;
        /* Central ESC routing for live states (menus handle their own Back). */
        if (esc_edge && app->state == GAME_STATE_PLAYING) {
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
        if (app->state == GAME_STATE_PLAYING) {
            app_poll_discrete_input(app);
        }

        /* Look remains responsive at render rate, while world actions use
         * latched input edges and held movement is sampled on fixed ticks.
         */
        if (app->state == GAME_STATE_PLAYING) {
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
            for (unsigned tick = 0; tick < advance.ticks && game_state_ticks_world(app->state); ++tick) {
                app_tick_playing(app, (float)SIMULATION_TICK_SECONDS);
            }
        }

        app_build_uiframe(app, &ui, text_accum);

        switch (app->state) {
        case GAME_STATE_MAIN_MENU:
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
                renderer_draw_mobs(app->renderer, &app->mobs, app->camera, aspect, &app->clock,
                                   (float)(app->last_frame_time - app->start_time), NULL, NULL);
                renderer_draw_projectiles(app->renderer, &app->projectiles, app->camera, aspect,
                                          &app->clock, NULL);
                renderer_draw_entities(app->renderer, &app->entities, app->camera, aspect, &app->clock);
                renderer_draw_particles(app->renderer, &app->particles, app->camera, aspect, &app->clock);
                screens_update(app, &ui);
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
                renderer_draw_mobs(app->renderer, &app->mobs, app->camera, aspect, &app->clock,
                                   (float)(app->last_frame_time - app->start_time), NULL, NULL);
                renderer_draw_projectiles(app->renderer, &app->projectiles, app->camera, aspect,
                                          &app->clock, NULL);
                renderer_draw_entities(app->renderer, &app->entities, app->camera, aspect, &app->clock);
                renderer_draw_particles(app->renderer, &app->particles, app->camera, aspect, &app->clock);
                screens_update(app, &ui);
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
            if (app->world_open) {
                Vec3 p = app->player.pos;
                RendererPerf perf = renderer_get_perf(app->renderer);
                const ItemStack *sel = &app->player.inv.slots[app->player.hotbar_sel];
                const ItemInfo *sel_info = item_get_info(sel->item);
                LOG_INFO("FPS: %.1f | %s pos (%.1f, %.1f, %.1f)%s HP %.0f/%.0f HU %.0f day %.3f | hotbar %d (%s x%u) | chunks %zu drawn %zu(+%zu) culled %zu | "
                         "mesh %.2fms up %.2fms draw %.2fms",
                         fps, app->player.flying ? "Creative" : "Survival", p.x, p.y, p.z,
                         app->player.grounded ? " grounded" : "", (double)app->player.health,
                         (double)app->player.max_health, (double)app->player.hunger, app->clock.day_progress,
                         app->player.hotbar_sel + 1, sel_info ? sel_info->name : "?",
                         stack_is_empty(sel) ? 0u : (unsigned)sel->count, world_chunk_count(app->world),
                         perf.drawn, perf.drawn_t, perf.culled, perf.mesh_ms, perf.upload_ms, perf.draw_ms);
            } else {
                LOG_INFO("FPS: %.1f | state %s", fps, game_state_name(app->state));
            }
            app->frame_count = 0;
            app->last_fps_time = now;
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
