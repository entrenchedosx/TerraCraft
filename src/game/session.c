#include "game/session.h"
#include "core/app.h"
#include "core/log.h"
#include "core/path.h"
#include "core/seed.h"
#include "core/time.h"
#include "game/entity.h"
#include "game/inventory.h"
#include "game/survival.h"
#include "world/entity_save.h"
#include "game/player.h"
#include "game/time_system.h"
#include "platform/window.h"
#include "render/camera.h"
#include "render/renderer.h"
#include "world/block.h"
#include "world/streamer.h"
#include "world/world.h"
#include "world/world_gen.h"
#include "world/world_meta.h"
#include "world/world_save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Enumerate saved worlds, most-recently-played first (insertion sort). */
int session_list_worlds(const char *saves_root, WorldEntry *out, size_t cap)
{
    if (saves_root == NULL || out == NULL || cap == 0) {
        return -1;
    }
    char dirs[SESSION_MAX_WORLDS][64];
    size_t ndirs = 0;
    if (path_list_dirs(saves_root, dirs, SESSION_MAX_WORLDS, &ndirs) != 0) {
        return 0; /* Missing saves/ simply means no worlds yet. */
    }
    size_t n = 0;
    char full[PATH_MAX_LEN];
    for (size_t i = 0; i < ndirs && n < cap; ++i) {
        if (path_join(full, sizeof(full), saves_root, dirs[i]) != 0) {
            continue;
        }
        WorldMeta m;
        if (world_meta_read(full, &m) != 0) {
            continue; /* Skip dirs without valid metadata (never crash). */
        }
        WorldEntry *e = &out[n];
        size_t nl = strlen(m.name);
        if (nl > 63) {
            nl = 63;
        }
        memcpy(e->name, m.name, nl);
        e->name[nl] = '\0';
        size_t dl = strlen(dirs[i]);
        if (dl > 63) {
            dl = 63;
        }
        memcpy(e->dirname, dirs[i], dl);
        e->dirname[dl] = '\0';
        e->seed = m.seed;
        e->mode = m.mode;
        e->played = m.last_played;
        /* Insertion sort by played descending. */
        size_t j = n;
        while (j > 0 && out[j - 1].played < e->played) {
            out[j] = out[j - 1];
            --j;
        }
        if (j != n) {
            out[j] = *e;
        }
        ++n;
    }
    return (int)n;
}

/* Create a world directory + initial metadata. */
int session_create_world(const char *saves_root, const char *name, const char *seed_field, int mode,
                         char out_dir[512])
{
    if (saves_root == NULL || name == NULL || seed_field == NULL || out_dir == NULL) {
        return -1;
    }
    if (mode != WORLD_MODE_SURVIVAL && mode != WORLD_MODE_CREATIVE) {
        mode = WORLD_MODE_SURVIVAL;
    }
    char safe[64];
    if (path_sanitize_name(safe, sizeof(safe), name) != 0) {
        return -2;
    }
    bool blank = false;
    long seed = seed_parse(seed_field, &blank);
    if (blank) {
        /* Wall-clock-derived seed (mixed for same-second uniqueness). */
        long t = (long)time(NULL);
        size_t existing = 0;
        char probe[64][64];
        if (path_list_dirs(saves_root, probe, 64, &existing) != 0) {
            existing = 0;
        }
        seed = (long)((unsigned long)t ^ ((unsigned long)existing * 0x85ebca6bu) ^ 0x9e3779b9ul);
        if (seed == 0L) {
            seed = 1L;
        }
    }
    /* Uniquify: "Name", "Name (2)", "Name (3)", ... */
    char dir[PATH_MAX_LEN];
    char candidate[96];
    memcpy(candidate, safe, strlen(safe) + 1);
    for (int attempt = 1; attempt <= 100; ++attempt) {
        if (path_join(dir, sizeof(dir), saves_root, candidate) != 0) {
            return -3;
        }
        if (!path_is_dir(dir) && !path_is_file(dir)) {
            break;
        }
        int n = snprintf(candidate, sizeof(candidate), "%s (%d)", safe, attempt + 1);
        if (n <= 0 || (size_t)n >= sizeof(candidate)) {
            return -3;
        }
    }
    if (path_is_dir(dir) || path_is_file(dir)) {
        return -3;
    }
    WorldMeta m;
    world_meta_defaults(&m);
    size_t nl = strlen(safe);
    if (nl > 63) {
        nl = 63;
    }
    memcpy(m.name, safe, nl);
    m.name[nl] = '\0';
    m.seed = (int64_t)seed;
    m.mode = mode;
    m.has_player = false; /* Spawn finder runs on first load. */
    if (world_meta_write(dir, &m) != 0) {
        return -4;
    }
    size_t dl = strlen(dir);
    if (dl >= 512) {
        return -5;
    }
    memcpy(out_dir, dir, dl + 1);
    LOG_INFO("session: created world '%s' seed %ld (%s)", safe, seed, dir);
    return 0;
}

/* ApplyTunables from settings to a live session (safe anytime). */
static void session_apply_settings(AppContext *app)
{
    if (app == NULL) {
        return;
    }
    window_set_vsync(app->window, app->settings.vsync);
    if (app->camera != NULL) {
        camera_set_fov_y(app->camera, app->settings.fov);
    }
    app->sensitivity = app->settings.sensitivity;
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
    if (app->renderer != NULL) {
        renderer_reload_atlas(app->renderer, app->settings.pack);
    }
}

/* Open a world session from its directory. */
int session_open_world(AppContext *app, const char *world_dir)
{
    if (app == NULL || world_dir == NULL || world_dir[0] == '\0') {
        return -1;
    }
    if (app->world_open) {
        LOG_ERROR("session_open_world: another session is open");
        return -2;
    }
    WorldMeta m;
    if (world_meta_read(world_dir, &m) != 0) {
        LOG_ERROR("session_open_world: unreadable metadata (%s)", world_dir);
        return -3;
    }
    World *w = world_create();
    if (w == NULL) {
        return -4;
    }
    w->seed = (long)m.seed;
    w->terrain_version = m.terrain_version;
    size_t nl = strlen(m.name);
    if (nl >= sizeof(w->name)) {
        nl = sizeof(w->name) - 1;
    }
    memcpy(w->name, m.name, nl);
    w->name[nl] = '\0';
    w->mode = m.mode;
    world_set_save_dir(w, world_dir);

    size_t dl = strlen(world_dir);
    if (dl >= sizeof(app->world_dir)) {
        world_destroy(w);
        return -5;
    }
    memcpy(app->world_dir, world_dir, dl + 1);
    app->world = w;

    int rd = app->settings.render_distance;
    if (rd < 2) {
        rd = 2;
    }
    if (rd > 8) {
        rd = 8;
    }
    if (streamer_init(&app->streamer, w, (long)m.seed, rd) != 0) {
        world_destroy(w);
        app->world = NULL;
        return -6;
    }
    app->streamer_ready = true;

    player_init(&app->player);
    player_anim_init(&app->panim);
    if (m.mode == WORLD_MODE_CREATIVE) {
        app->player.flying = true;
    }
    app->player.mode = m.mode;
    if (m.has_player) {
        app->player.pos = mmath_vec3(m.px, m.py, m.pz);
        app->player.render_pos = app->player.pos;
        app->player.yaw = m.yaw;
        app->player.pitch = m.pitch;
    }
    /* Remember whether the position came from the save: the post-load
     * spawn search must not teleport a restored player back to spawn. */
    app->player_from_save = m.has_player;
    /* Restore vitals + inventory (M5 files lack them: sanitize to defaults).
     * Health/hunger clamp into range; dead is never persisted. */
    if (m.health > 0.0f && m.health <= 100.0f) {
        app->player.health = m.health > app->player.max_health ? app->player.max_health : m.health;
    }
    if (m.hunger >= 0.0f && m.hunger <= 100.0f) {
        app->player.hunger = m.hunger > app->player.max_hunger ? app->player.max_hunger : m.hunger;
    }
    for (int i = 0; i < WORLD_META_INV_SLOTS && i < INV_SIZE; ++i) {
        app->player.inv.slots[i].item = m.inv_items[i];
        app->player.inv.slots[i].count = m.inv_counts[i];
        app->player.inv.slots[i].durability = m.inv_dur[i];
    }
    inv_sanitize(&app->player.inv);
    app->player.dead = false;
    /* Fresh session state: entities load from the entity save when present
     * (M5/M6 worlds predate it and start empty); any held cursor stack
     * belongs to the previous world (inventory is per-session). */
    if (entity_save_read(world_dir, &app->entities, &app->mobs) != 0) {
        LOG_ERROR("session: entity save corrupt, starting with no drops (%s)", world_dir);
    }
    /* NOTE: no entity_pool_clear here — the read above already clears,
     * then loads persisted drops. Clearing would wipe them. */
    particle_pool_clear(&app->particles);
    /* Projectiles never persist (M9 policy): a fresh session starts with
     * an empty sky even if arrows were flying at save time. */
    projectile_pool_clear(&app->projectiles);
    survival_bow_reset(&app->player);
    app->mine_fx_t = 0.0f;
    stack_clear(&app->cursor);
    for (int i = 0; i < 4; ++i) {
        stack_clear(&app->craft2[i]);
    }
    for (int i = 0; i < 9; ++i) {
        stack_clear(&app->craft3[i]);
    }
    stack_clear(&app->craft_out);
    app->craft_hash = 0;
    app->craft_hash3 = 0;
    survival_mine_reset(&app->player);
    /* Restore the stable spawn when present; the loader records a fresh
     * one below when the meta predates it (M5 migration path). */
    if (m.has_spawn) {
        app->spawn_point = mmath_vec3(m.spawn_x, m.spawn_y, m.spawn_z);
        app->has_spawn_point = true;
    } else {
        app->has_spawn_point = false;
    }
    app->player_ready = true;
    time_system_init(&app->clock, m.day, TIME_DEFAULT_SPEED);

    /* Staged loader target: full square around the spawn column. */
    app->load_total = (2 * rd + 1) * (2 * rd + 1);
    app->load_done = 0;
    app->world_open = true;
    session_apply_settings(app);
    /* Pack audio overrides follow the active pack (no-op when unchanged). */
    audio_load_pack(&app->audio, app->settings.pack);
    LOG_INFO("session: opened '%s' seed %lld mode %s (expecting ~%d chunks)", m.name, (long long)m.seed,
             m.mode == WORLD_MODE_CREATIVE ? "creative" : "survival", app->load_total);
    return 0;
}

/* Persist the open session now. */
int session_save_now(AppContext *app)
{
    if (app == NULL) {
        return -1;
    }
    if (!app->world_open || app->world == NULL) {
        return 0;
    }
    int rc = world_save_all(app->world_dir, app->world, &app->player, app->spawn_point, app->has_spawn_point,
                            app->clock.day_progress);
    if (rc != 0) {
        LOG_ERROR("session_save_now: save reported failures");
    }
    /* Dropped-item entities persist as one atomic world-level file (best
     * effort: meta + chunks are the save; a failed entity write only logs). */
    if (entity_save_write(app->world_dir, &app->entities, &app->mobs) != 0) {
        LOG_ERROR("session_save_now: entity save failed (drops will not survive reload)");
    }
    return rc;
}

/* Close the session (save first when asked), freeing world + GPU buffers. */
void session_close_world(AppContext *app, bool save)
{
    if (app == NULL) {
        return;
    }
    if (!app->world_open) {
        return;
    }
    if (save) {
        session_save_now(app);
    }
    if (app->renderer != NULL) {
        renderer_drop_all(app->renderer);
    }
    world_destroy(app->world);
    app->world = NULL;
    app->world_open = false;
    app->streamer_ready = false;
    app->player_ready = false;
    app->world_dir[0] = '\0';
    LOG_INFO("session: world closed");
}

/* Spiral spawn search preferring dry land with headroom. Never moves a
 * player restored from a save (position persists); for those sessions the
 * search only records the stable spawn when the metadata predates it.
 * Fresh sessions (no saved position) are placed on the found spawn. */
int session_find_spawn(AppContext *app)
{
    if (app == NULL || !app->world_open || app->world == NULL) {
        return -1;
    }
    if (app->has_spawn_point && app->player_from_save) {
        return 0; /* M6 save: position + spawn both restored already. */
    }
    World *w = app->world;
    Vec3 fallback = mmath_vec3(8.5f, 120.0f, 8.5f);
    bool have_fallback = false;
    for (int ring = 0; ring <= 6; ++ring) {
        for (int dx = -ring; dx <= ring; ++dx) {
            for (int dz = -ring; dz <= ring; ++dz) {
                int edge = dx == ring || dx == -ring || dz == ring || dz == -ring;
                if (!edge) {
                    continue;
                }
                int sx = dx * 16 + 8;
                int sz = dz * 16 + 8;
                Vec3 out;
                if (!player_find_spawn(w, sx, sz, &out)) {
                    continue;
                }
                if (!have_fallback) {
                    fallback = out;
                    have_fallback = true;
                }
                /* Prefer dry headroom (not water) at feet+1. */
                int fx = (int)out.x;
                int fz = (int)out.z;
                if (world_get_block(w, fx, (int)out.y + 1, fz) == BLOCK_AIR) {
                    if (!app->has_spawn_point) {
                        app->spawn_point = out;
                        app->has_spawn_point = true;
                    }
                    if (!app->player_from_save) {
                        app->player.pos = out;
                        app->player.render_pos = out;
                    }
                    LOG_INFO("session: spawn at (%.1f, %.1f, %.1f)", out.x, out.y, out.z);
                    return 0;
                }
            }
        }
    }
    if (have_fallback) {
        if (!app->has_spawn_point) {
            app->spawn_point = fallback;
            app->has_spawn_point = true;
        }
        if (!app->player_from_save) {
            app->player.pos = fallback;
            app->player.render_pos = fallback;
        }
        return 0;
    }
    if (!app->has_spawn_point) {
        app->spawn_point = mmath_vec3(8.5f, 120.0f, 8.5f);
        app->has_spawn_point = true;
    }
    if (!app->player_from_save) {
        app->player.pos = app->spawn_point;
        app->player.render_pos = app->spawn_point;
    }
    LOG_WARN("session_find_spawn: no ground found; high drop-in");
    return 0;
}
