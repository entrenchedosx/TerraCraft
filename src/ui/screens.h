#pragma once

/* Menu screens (M5): main menu, world select, create world, pause, settings,
 * loading overlay, and the F3 debug overlay. Immediate-mode over UiFrame
 * input; all drawing goes through renderer rect/text calls. Screen-local
 * state lives in AppContext.menu (no globals). Pure layout + actions;
 * session/world I/O goes through game/session.h.
 */

#include <stdbool.h>

/* Forward declarations (full types in their headers). */
typedef struct AppContext AppContext;
typedef struct UiFrame UiFrame;

/* Refresh the cached world list from saves/ (call on entering SELECT and
 * after create/delete). Resets cursor/scroll/armed state.
 *
 * Args:
 *   app: application context (must not be NULL).
 */
void screens_refresh_worlds(AppContext *app);

/* Refresh the discovered pack list and resolve the active index.
 *
 * Args:
 *   app: application context (must not be NULL).
 */
void screens_refresh_packs(AppContext *app);

/* Update + draw the current menu/overlay state (MAIN_MENU, WORLD_SELECT,
 * CREATE_WORLD, SETTINGS, PAUSED, LOADING overlay). PLAYING/QUIT are no-ops.
 *
 * Args:
 *   app: application context (must not be NULL).
 *   ui: per-frame input snapshot (must not be NULL).
 */
void screens_update(AppContext *app, const UiFrame *ui);

/* Draw the F3 debug overlay (PLAYING only, when enabled by the app).
 * No-op on bad args.
 *
 * Args:
 *   app: application context (must not be NULL).
 */
void screens_draw_debug(AppContext *app);
