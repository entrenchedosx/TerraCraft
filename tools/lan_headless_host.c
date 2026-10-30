/* Headless instrumented LAN host for live two-client verification.
 *
 * PURPOSE: pair with one real graphical TerraCraft client to prove
 * end-to-end multiplayer gameplay that headless tests cannot: joining via
 * the discovery browser, remote-avatar rendering, real click attacks over
 * real sockets, authoritative death, and respawn convergence.
 *
 * METHOD: opens (or creates) a survival world headlessly, hosts it on LAN,
 * pumps the real replication update, and logs every observable state
 * transition (joins, health changes, deaths, respawns, disconnects).
 * A graphical client joins through its server browser (mouse only), sees
 * the host avatar, and clicks to attack; this harness logs the damage.
 *
 * This is a manual verification tool (like worldgen_probe), not part of
 * ctest: it needs an operator-driven graphical peer. It uses only public
 * production APIs and changes no game behavior.
 *
 * Usage: lan_headless_host [seed] [seconds]  (defaults: 12345, 240)
 */

#include "core/app.h"
#include "core/log.h"
#include "core/path.h"
#include "core/time.h"
#include "game/session.h"
#include "platform/lan.h"
#include "world/world.h"
#include "world/world_meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static void harness_sleep_ms(unsigned ms)
{
    Sleep(ms);
}
#else
#include <time.h>
static void harness_sleep_ms(unsigned ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    (void)nanosleep(&ts, NULL);
}
#endif

int main(int argc, char **argv)
{
    const char *seed_text = argc > 1 ? argv[1] : "12345";
    int seconds = argc > 2 ? atoi(argv[2]) : 240;
    if (seconds <= 0 || seconds > 3600) {
        seconds = 240;
    }
    printf("HARNESS host starting (seed %s, %d s budget)\n", seed_text, seconds);
    fflush(stdout);

    AppContext *app = (AppContext *)calloc(1, sizeof(AppContext));
    if (app == NULL) {
        printf("HARNESS FAIL: out of memory\n");
        return 1;
    }
    snprintf(app->username, sizeof(app->username), "HarnessHost");
    app->state = GAME_STATE_PLAYING;

    /* Reuse the harness world when present (stable spawn, no save spam). */
    char probe[512];
    bool have_world = false;
    WorldMeta check;
    memset(&check, 0, sizeof(check));
    if (path_join(probe, sizeof(probe), SESSION_SAVES_DIR, "HARNESS") == 0 &&
        world_meta_read(probe, &check) == 0) {
        have_world = true;
    }
    char dir[512];
    if (have_world) {
        size_t dl = strlen(probe);
        if (dl >= sizeof(dir)) {
            printf("HARNESS FAIL: world path too long\n");
            free(app);
            return 1;
        }
        memcpy(dir, probe, dl + 1);
        printf("HARNESS reusing world %s\n", dir);
    } else {
        if (session_create_world(SESSION_SAVES_DIR, "HARNESS", seed_text, WORLD_MODE_SURVIVAL,
                                 dir) != 0) {
            printf("HARNESS FAIL: world creation failed\n");
            free(app);
            return 1;
        }
        printf("HARNESS created world %s\n", dir);
    }
    fflush(stdout);
    if (session_open_world(app, dir) != 0) {
        printf("HARNESS FAIL: world open failed\n");
        free(app);
        return 1;
    }
    /* Headless contexts skip the LOADING tick; place the host directly. */
    if (session_find_spawn(app) != 0) {
        printf("HARNESS FAIL: no spawn\n");
        session_close_world(app, false);
        free(app);
        return 1;
    }
    if (!app_lan_host(app)) {
        printf("HARNESS FAIL: LAN host failed: %s\n", app->menu.error);
        session_close_world(app, false);
        free(app);
        return 1;
    }
    printf("HARNESS hosting '%s' at spawn (%.1f, %.1f, %.1f); waiting for clients\n",
           app->world->name, (double)app->player.pos.x, (double)app->player.pos.y,
           (double)app->player.pos.z);
    fflush(stdout);

    float last_health = app->player.health;
    bool was_dead = false;
    double dead_since = 0.0;
    double start = time_now_seconds();
    double last_report = 0.0;
    while (time_now_seconds() - start < (double)seconds) {
        app_lan_update(app, 0.05);
        double now = time_now_seconds();
        if (app->player.health != last_health || app->player.dead != was_dead) {
            printf("HARNESS host health %.1f -> %.1f dead %d -> %d\n", (double)last_health,
                   (double)app->player.health, (int)was_dead, (int)app->player.dead);
            fflush(stdout);
            last_health = app->player.health;
            was_dead = app->player.dead;
            if (was_dead) {
                dead_since = now;
            }
        }
        if (was_dead && now - dead_since > 5.0) {
            app_respawn_player(app);
            printf("HARNESS host respawned at (%.1f, %.1f, %.1f) hp %.1f\n",
                   (double)app->player.pos.x, (double)app->player.pos.y,
                   (double)app->player.pos.z, (double)app->player.health);
            fflush(stdout);
            last_health = app->player.health;
            was_dead = false;
        }
        if (now - last_report > 5.0) {
            last_report = now;
            printf("HARNESS t=%.0f peers=%u health=%.1f dead=%d\n", now - start,
                   (unsigned)app->lan_player_count, (double)app->player.health,
                   (int)app->player.dead);
            for (size_t i = 0; i < MINEC_LAN_MAX_PLAYERS; ++i) {
                const LanRemotePlayer *rp = &app->lan_players[i];
                if (rp->active) {
                    printf("HARNESS peer %u '%s' at (%.1f, %.1f, %.1f) hp %.1f dead %d\n",
                           (unsigned)rp->peer_id, rp->username, (double)rp->pos.x,
                           (double)rp->pos.y, (double)rp->pos.z, (double)rp->health,
                           (int)rp->dead);
                }
            }
            fflush(stdout);
        }
        harness_sleep_ms(50);
    }
    printf("HARNESS done; closing\n");
    fflush(stdout);
    app_lan_disconnect(app);
    session_close_world(app, true);
    free(app);
    return 0;
}
