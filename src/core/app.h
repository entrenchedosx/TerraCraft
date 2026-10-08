#pragma once

/* Main application context/state (M5).
 *
 * RULE: No global mutable state. All engine state lives in AppContext.
 * The app boots into GAME_STATE_MAIN_MENU (never straight into a world).
 * World sessions (world/streamer/player/clock) exist only while a world
 * is open; menus run on window+renderer+settings alone.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/settings.h"
#include "core/profile.h"
#include "audio/audio.h"
#include "game/entity.h"
#include "game/mob.h"
#include "game/particle.h"
#include "game/projectile.h"
#include "game/game_state.h"
#include "game/inventory.h"
#include "game/player.h"
#include "game/player_animation.h"
#include "game/session.h"
#include "game/simulation_clock.h"
#include "game/time_system.h"
#include "world/streamer.h"

/* Forward declarations to avoid pulling subsystem headers into this header. */
typedef struct Window Window;
typedef struct GlContext GlContext;
typedef struct Renderer Renderer;
typedef struct Camera Camera;
typedef struct World World;
typedef struct LanSession LanSession;
typedef struct LanDiscover LanDiscover;

#define MINEC_LAN_MAX_PLAYERS 8
#define MINEC_LAN_BLOCK_QUEUE 4096
#define APP_CHAT_HISTORY_LINES 64

typedef struct LanRemotePlayer {
    bool active;
    uint32_t peer_id;
    char username[PROFILE_NAME_MAX_LEN + 1];
    Vec3 pos;
    float yaw;
    float pitch;
    float walk_phase;
    bool sneaking;
    bool moving;
} LanRemotePlayer;

/* Default world seed when no session overrides it (legacy constant). */
#define MINEC_DEFAULT_SEED 1337L

/* Streaming per-frame generation budget during play. */
#define MINEC_STREAM_BUDGET 4

/* Staged loading budget (chunks per frame while in LOADING). */
#define MINEC_LOAD_BUDGET 8

/* Autosave interval in seconds during play. */
#define MINEC_AUTOSAVE_SECONDS 30.0

/* Per-frame mouse sensitivity base (scaled by settings.sensitivity). */
#define MINEC_MOUSE_SENSITIVITY 0.0025f

/* World list / pack list capacities (mirror session/atlas limits). */
#define MINEC_MENU_WORLDS 48
#define MINEC_MENU_PACKS 16

/* Menu/screen scratch state (all owned here, no globals). */
typedef struct MenuData {
    char profile_buf[PROFILE_NAME_MAX_LEN + 1]; /* First-run account name. */
    char lan_address_buf[64]; /* IPv4 address of the LAN host. */
    int lan_server_idx;   /* Selected discovered server (-1 = none). */
    bool lan_show_direct; /* Direct-IP fallback field visible. */
    double lan_poll_at;   /* Last discovery poll timestamp. */
    double menu_anim_at;  /* State-enter time (menu transitions). */
    int main_focus;       /* Keyboard focus on the title-screen buttons. */
    char name_buf[64];      /* Create-world name field. */
    char seed_buf[32];      /* Create-world seed field. */
    int create_mode;        /* 0 survival, 1 creative. */
    bool name_focused;      /* Text-field focus flags. */
    bool seed_focused;      /* Text-field focus flags. */
    char error[128];        /* Last menu error line ("" = none). */
    int select_idx;         /* World-select cursor. */
    int select_scroll;      /* World-select scroll offset (rows). */
    bool delete_armed;      /* Delete-confirmation arming. */
    GameState settings_return; /* Where SETTINGS returns. */
    WorldEntry worlds[MINEC_MENU_WORLDS]; /* Cached world list. */
    size_t world_count;     /* Cached entries. */
    float sl_rd;            /* Settings slider scratch values. */
    float sl_sens;
    float sl_fov;
    float sl_vol;
    float sl_sfx;
    bool held_rd;           /* Slider drag states. */
    bool held_sens;
    bool held_fov;
    bool held_vol;
    bool held_sfx;
    char packs[MINEC_MENU_PACKS][64]; /* Discovered pack names. */
    size_t pack_count;      /* Discovered entries. */
    int pack_idx;           /* Active pack index into packs[]. */
} MenuData;

typedef struct AppContext {
    Window *window;       /* Owned SDL window wrapper. NULL when shut down. */
    GlContext *gl;        /* Owned GL context wrapper. NULL when shut down. */
    Renderer *renderer;   /* Owned renderer (shader + atlas + GPU + HUD). */
    Camera *camera;       /* Owned view camera (mirrors the player eye). */
    Settings settings;    /* Persistent tunables (config/settings.cfg). */
    GameState state;      /* Current game state (menus + play). */
    float sensitivity;    /* Runtime mouse rad/px (from settings). */
    /* Open world session (valid only when world_open is true). */
    World *world;         /* Owned world (NULL when no session). */
    Streamer streamer;    /* Streaming state (borrows world). */
    Player player;        /* Physical player body + inventory. */
    TimeSystem clock;     /* Day/night cycle state. */
    SimulationClock simulation; /* Authoritative world tick scheduler (60 TPS). */
    bool discard_next_simulation_elapsed; /* Drop wall time spanning a frozen-to-live transition. */
    EntityPool entities;  /* Dropped-item entities (bounded pool). */
    MobPool mobs;         /* Living mobs (bounded pool, M8). */
    ProjectilePool projectiles; /* Arrows in flight/embed (bounded, M9; never saved). */
    ParticlePool particles; /* M7 feedback particles (bounded pool). */
    float mine_fx_t;      /* Seconds since last mining impact puff. */
    float water_contact_prev; /* Player water contact last tick (splash edge). */
    double arm_swing_started; /* Start time of the current first-person punch. */
    float arm_swing_repeat_t; /* Repeat gate for held mining/attacking. */
    bool arm_swing_active; /* A first-person swing has been triggered. */
    PlayerAnim panim;    /* First-person hand animation controller. */
    ItemStack cursor;     /* Inventory cursor-held stack (UI drag). */
    Vec3 spawn_point;     /* Stable world spawn feet (persisted). */
    bool has_spawn_point; /* True once a spawn was recorded. */
    bool player_from_save; /* True when player pos came from metadata. */
    char world_dir[512];  /* Active session directory ("" = none). */
    char username[PROFILE_NAME_MAX_LEN + 1]; /* Persistent local profile. */
    LanSession *lan;     /* Optional direct LAN host/client transport. */
    LanDiscover *lan_discover; /* Optional discovery beacons/scans. */
    LanRemotePlayer lan_players[MINEC_LAN_MAX_PLAYERS];
    unsigned lan_player_count;
    bool lan_client;      /* True while this session is a LAN client. */
    bool lan_host;        /* True while this process hosts a LAN world. */
    bool lan_join_pending; /* Connection accepted, waiting for world hello. */
    bool lan_applying_remote_block; /* Suppress echo while applying host changes. */
    uint32_t lan_local_player_id; /* Host is 0; clients receive an ID in WELCOME. */
    double lan_player_send_timer; /* Snapshot cadence for remote avatars. */
    double lan_connect_timer; /* Timeout for pending LAN joins. */
    uint8_t lan_block_queue[MINEC_LAN_BLOCK_QUEUE][15]; /* Bounded reliable edit backlog. */
    size_t lan_block_queue_head;
    size_t lan_block_queue_count;
    bool lan_block_queue_warned;
    bool lan_gravity_echo_pending; /* A gravity event bundles its paired setter callback. */
    int lan_gravity_echo_x;
    int lan_gravity_echo_y;
    int lan_gravity_echo_z;
    uint16_t lan_gravity_echo_block_id;
    bool chat_open;       /* Chat owns keyboard/mouse input while true. */
    char chat_input[192]; /* UTF-8 outgoing chat draft. */
    char chat_lines[APP_CHAT_HISTORY_LINES][224]; /* Current-world local/LAN chat history. */
    double chat_line_times[APP_CHAT_HISTORY_LINES]; /* Monotonic arrival times for fade-out. */
    unsigned chat_line_count;
    unsigned chat_line_head;
    unsigned chat_scroll; /* Older-line offset while the expanded chat is open. */
    bool world_open;      /* True while a session is open. */
    bool player_ready;    /* True once the player was spawned. */
    bool streamer_ready;  /* True once streamer_init succeeded. */
    bool running;         /* Main-loop flag. False requests shutdown. */
    int width;            /* Current drawable width in pixels. */
    int height;           /* Current drawable height in pixels. */
    double start_time;    /* time_now_seconds() at init, for elapsed clock. */
    double last_fps_time; /* Last time FPS was logged. */
    double last_frame_time; /* Last frame timestamp (for dt). */
    int frame_count;      /* Frames since last FPS log. */
    float fps_smooth;     /* Last logged FPS (for the F3 overlay). */
    float frame_ms_avg;   /* Average frame duration over the last FPS window. */
    float sim_ms_avg;     /* Average CPU time per fixed simulation tick. */
    float sim_tps;        /* Executed fixed simulation ticks per second. */
    size_t process_working_set_bytes; /* OS resident memory sample, or 0 if unavailable. */
    bool show_debug;      /* F3 debug overlay toggle. */
    bool third_person;    /* F5 camera: chase view + visible player body. */
    double autosave_timer; /* Seconds since last autosave (PLAYING only). */
    int load_done;        /* LOADING progress: chunks resident. */
    int load_total;       /* LOADING progress: chunks expected. */
    MenuData menu;        /* Menu/screen scratch state. */
    /* Edge-detection state for discrete actions (pressed-this-frame). */
    bool prev_mouse_l;    /* Left button state last frame. */
    bool prev_mouse_r;    /* Right button state last frame. */
    bool prev_menu_click; /* Menu click edge state last frame. */
    bool prev_fly_key;    /* F key state last frame. */
    bool prev_f3_key;     /* F3 key state last frame. */
    bool prev_f5_key;     /* F5 camera-toggle key state last frame. */
    bool prev_e_key;      /* E key state last frame (inventory toggle). */
    bool prev_f6_key;     /* F6 dev-give key state last frame. */
    bool prev_f8_key;     /* F8 dev-hurt key state last frame. */
    bool prev_f7_key;     /* F7 dev mob-hurt key state last frame. */
    bool prev_f9_key;     /* F9 dev spawn-passive key state last frame. */
    bool prev_f10_key;    /* F10 dev spawn-hostile key state last frame. */
    bool prev_f11_key;    /* F11 dev spawn-skeleton key state last frame. */
    bool prev_f12_key;    /* F12 dev clear-projectiles key state last frame. */
    bool prev_menu_rclick; /* Menu right-click edge state last frame. */
    bool prev_return;     /* Return key state last frame. */
    bool prev_backspace;  /* Backspace key state last frame. */
    bool prev_up;         /* Up-arrow state last frame. */
    bool prev_down;       /* Down-arrow state last frame. */
    bool prev_delete;     /* Delete key state last frame. */
    bool prev_digit[9];   /* Digit 1..9 states last frame. */
    bool prev_jump;       /* Jump key state last frame (exhaustion edge). */
    float step_dist;      /* Distance walked since last footstep (M7). */
    float hurt_flash;     /* Damage flash strength 0..1 (decays in PLAYING). */
    float last_health;    /* Health last tick (flash edge detection). */
    int mobs_drawn;       /* Living mobs drawn last PLAYING frame (debug). */
    int mobs_culled;      /* Living mobs frustum-culled last frame (debug). */
    int arrows_drawn;     /* Arrows drawn last PLAYING frame (debug). */
    AudioSystem audio;    /* M7 procedural audio backend (silent if no device). */
    /* Crafting grids (M7): 2x2 player grid + 3x3 bench grid. Contents are
     * caller-owned ingredients (never outputs); closing any crafting UI
     * returns them to the inventory, never deleted. */
    ItemStack craft2[4];  /* Player 2x2 grid, row-major. */
    ItemStack craft3[9];  /* Workbench 3x3 grid, row-major. */
    uint32_t craft_hash;  /* Last-seen 2x2 grid hash (output recompute guard). */
    uint32_t craft_hash3; /* Last-seen 3x3 grid hash (output recompute guard). */
    ItemStack craft_out;  /* Current derived output (display/take only). */
} AppContext;

/* Initialise the app: logger, settings, window, GL context, renderer,
 * camera. Boots into GAME_STATE_MAIN_MENU with no world open.
 *
 * Args:
 *   app: context to initialise (must not be NULL).
 *   width: initial window width (> 0).
 *   height: initial window height (> 0).
 *   title: window title (must not be NULL).
 *
 * Returns: 0 on success, non-zero on failure (app left safe to shutdown).
 */
int app_init(AppContext *app, int width, int height, const char *title);

/* Enter the main loop: state machine driving menus, loading, and play.
 *
 * Args:
 *   app: initialised context (must not be NULL).
 *
 * Returns: 0 on clean exit, non-zero if the context was invalid.
 */
int app_run(AppContext *app);

/* Shut down all subsystems and release resources. Safe to call on a
 * partially-initialised or already-shut-down context. Saves the open
 * session first (best effort).
 *
 * Args:
 *   app: context to shut down (must not be NULL).
 */
void app_shutdown(AppContext *app);

/* Request a state transition (validated; side effects applied).
 * Handles mouse capture, text input, list refreshes, and pack scans.
 * Illegal edges are rejected with a log (state unchanged).
 *
 * Args:
 *   app: context (must not be NULL).
 *   to: requested state.
 */
void app_enter_state(AppContext *app, GameState to);

/* Apply settings to live systems (vsync, FOV, sensitivity, streamer
 * radius, atlas pack). Safe anytime after renderer/camera exist.
 *
 * Args:
 *   app: context (must not be NULL).
 */
void app_apply_settings(AppContext *app);

/* Respawn the player at the stable spawn (death screen action).
 * Restores vitals/velocity/state via the survival policy. No-op without
 * an open world.
 *
 * Args:
 *   app: context (must not be NULL).
 */
void app_respawn_player(AppContext *app);

/* Resolve a held cursor stack (quit/menu paths in screens.c): back into
 * the inventory first; survival remainder becomes a world drop, creative
 * remainder is voided (infinite sources make it moot).
 *
 * Args:
 *   app: context (must not be NULL).
 */
void app_resolve_cursor(AppContext *app);

/* Return crafting-grid contents to the inventory (M7 crafting safety).
 * Survival overflow becomes world drops (creative overflow is voided);
 * when both are full the remainder stays gridded (retrievable by reopening
 * a crafting UI). Idempotent: empty grids are a no-op. Safe without an
 * open world (grids simply clear to inventory when possible).
 *
 * Args:
 *   app: context (must not be NULL).
 */
void app_resolve_crafting(AppContext *app);

/* Save the required local profile and leave the first-run username screen.
 * A blank field receives a generated two-word/four-digit name. */
bool app_commit_profile(AppContext *app);

/* LAN actions used by the menus. Hosting uses the already-open world; joining
 * completes asynchronously when the host sends its world metadata. */
bool app_lan_host(AppContext *app);
bool app_lan_join(AppContext *app, const char *address);
bool app_lan_join_endpoint(AppContext *app, const char *address, uint16_t port);
void app_lan_disconnect(AppContext *app);
