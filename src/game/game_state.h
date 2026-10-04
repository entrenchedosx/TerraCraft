#pragma once

/* Explicit game-state machine (M5): menu -> world -> pause flows without
 * scattered booleans. All state still lives in AppContext; this module
 * owns the transition rules. Pure logic, headless-testable.
 */

#include <stdbool.h>

/* Game states (ordered for UI flow, values are not persisted).
 * INVENTORY freezes simulation like PAUSED but keeps the play HUD context
 * (cursor stack, catalogue); CRAFTING is the workbench 3x3 overlay (same
 * freeze rules as INVENTORY); DEAD freezes on the death screen.
 */
typedef enum GameState {
    GAME_STATE_MAIN_MENU = 0,   /* Title screen (entry point). */
    GAME_STATE_WORLD_SELECT,    /* Saved-world list. */
    GAME_STATE_CREATE_WORLD,    /* New-world form. */
    GAME_STATE_LOADING,         /* Staged world generation. */
    GAME_STATE_PLAYING,         /* In-world gameplay. */
    GAME_STATE_PAUSED,          /* Pause menu over a live world. */
    GAME_STATE_SETTINGS,        /* Settings (remembers its return state). */
    GAME_STATE_INVENTORY,       /* Inventory/catalogue overlay (sim frozen). */
    GAME_STATE_CRAFTING,        /* Workbench 3x3 overlay (sim frozen). */
    GAME_STATE_DEAD,            /* Death screen over a live world. */
    GAME_STATE_QUIT             /* Terminal: exit the process. */
} GameState;

/* Human-readable state name for logs (never NULL).
 *
 * Args:
 *   s: state value.
 *
 * Returns: static name string ("UNKNOWN" for invalid values).
 */
const char *game_state_name(GameState s);

/* Check whether a direct transition is legal.
 * Legal edges: MENU->{SELECT,SETTINGS,QUIT}, SELECT->{PLAYING(via load),
 * CREATE,MENU}, CREATE->{LOADING,MENU(SELECT)}, LOADING->PLAYING,
 * PLAYING->{PAUSED,INVENTORY,CRAFTING,DEAD,QUIT}, INVENTORY->PLAYING,
 * CRAFTING->PLAYING, PAUSED->{PLAYING,SETTINGS,MENU},
 * DEAD->{PLAYING(respawn),MENU}, SETTINGS->return. QUIT accepts nothing
 * (terminal). Else rejected.
 *
 * Args:
 *   from: current state.
 *   to: requested state.
 *
 * Returns: true when the edge is legal.
 */
bool game_state_can_transition(GameState from, GameState to);

/* Check whether gameplay simulation runs in a state (world tick, physics,
 * clock, streaming, entities). True only for PLAYING and LOADING.
 * INVENTORY/CRAFTING/PAUSED/DEAD freeze the sim (documented M6 decision).
 *
 * Args:
 *   s: state value.
 *
 * Returns: true when the world simulates.
 */
bool game_state_is_live(GameState s);

/* Check whether a state needs an open world session (world, player,
 * streamer must exist). True for LOADING, PLAYING, PAUSED, INVENTORY,
 * CRAFTING, DEAD.
 *
 * Args:
 *   s: state value.
 *
 * Returns: true when a world must be open.
 */
bool game_state_needs_world(GameState s);
