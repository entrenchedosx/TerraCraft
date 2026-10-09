#pragma once

/* Persistent settings (M5): render distance, sensitivity, FOV, vsync,
 * master + effects volumes, resource pack. Stored as key=value text at
 * config/settings.cfg with validation + clamping on load. No globals;
 * AppContext owns one Settings.
 */

#include <stdbool.h>

/* Relative config file path (under the process working directory). */
#define SETTINGS_PATH "config/settings.cfg"

/* Resource pack name capacity (incl. terminator). */
#define SETTINGS_PACK_LEN 64

/* Validated tunables. */
typedef struct Settings {
    int render_distance; /* Chunk radius 2..8 (default 4). */
    float sensitivity;   /* Mouse rad/px 0.0005..0.010 (default 0.0025). */
    float fov;           /* Vertical FOV degrees 60..110 (default 70). */
    bool vsync;          /* Swap interval on/off (default true). */
    bool auto_jump;      /* Auto-step 1-block ledges/shores (MC autoJump, default false). */
    bool view_bobbing;   /* First-person walk bob (MC viewBobbing, default true). */
    int volume;          /* Master 0..100 (default 80). */
    int sfx_volume;      /* Effects 0..100, M7 (default 80; old files omit). */
    char pack[SETTINGS_PACK_LEN]; /* Active resource pack ("Default"). */
} Settings;

/* Fill defaults (pack = "Default").
 *
 * Args:
 *   s: settings to initialise (must not be NULL).
 */
void settings_defaults(Settings *s);

/* Clamp every field into its valid range in place (NULL-safe no-op).
 *
 * Args:
 *   s: settings to clamp (may be NULL).
 */
void settings_clamp(Settings *s);

/* Load from path (defaults first, then overrides; unknown/malformed lines
 * ignored safely; missing file keeps defaults).
 *
 * Args:
 *   s: settings to fill (must not be NULL).
 *   path: config file path (must not be NULL).
 *
 * Returns: 0 when the file loaded (even partially), non-zero when the
 * file was missing/unreadable (defaults kept).
 */
int settings_load(Settings *s, const char *path);

/* Save all keys to path (creates parent dirs; rewrites atomically-ish via
 * temp file + rename; falls back to direct write).
 *
 * Args:
 *   s: settings to persist (must not be NULL).
 *   path: config file path (must not be NULL).
 *
 * Returns: 0 on success, non-zero on failure.
 */
int settings_save(const Settings *s, const char *path);
