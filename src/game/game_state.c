#include "game/game_state.h"

/* State name for logs. */
const char *game_state_name(GameState s)
{
    switch (s) {
    case GAME_STATE_MAIN_MENU:
        return "MAIN_MENU";
    case GAME_STATE_WORLD_SELECT:
        return "WORLD_SELECT";
    case GAME_STATE_CREATE_WORLD:
        return "CREATE_WORLD";
    case GAME_STATE_LOADING:
        return "LOADING";
    case GAME_STATE_PLAYING:
        return "PLAYING";
    case GAME_STATE_PAUSED:
        return "PAUSED";
    case GAME_STATE_SETTINGS:
        return "SETTINGS";
    case GAME_STATE_INVENTORY:
        return "INVENTORY";
    case GAME_STATE_CRAFTING:
        return "CRAFTING";
    case GAME_STATE_DEAD:
        return "DEAD";
    case GAME_STATE_QUIT:
        return "QUIT";
    default:
        return "UNKNOWN";
    }
}

/* Legal transition edges. */
bool game_state_can_transition(GameState from, GameState to)
{
    switch (from) {
    case GAME_STATE_MAIN_MENU:
        return to == GAME_STATE_WORLD_SELECT || to == GAME_STATE_SETTINGS || to == GAME_STATE_QUIT;
    case GAME_STATE_WORLD_SELECT:
        return to == GAME_STATE_CREATE_WORLD || to == GAME_STATE_LOADING || to == GAME_STATE_MAIN_MENU ||
               to == GAME_STATE_QUIT;
    case GAME_STATE_CREATE_WORLD:
        return to == GAME_STATE_LOADING || to == GAME_STATE_WORLD_SELECT || to == GAME_STATE_QUIT;
    case GAME_STATE_LOADING:
        return to == GAME_STATE_PLAYING || to == GAME_STATE_QUIT;
    case GAME_STATE_PLAYING:
        return to == GAME_STATE_PAUSED || to == GAME_STATE_INVENTORY || to == GAME_STATE_CRAFTING ||
               to == GAME_STATE_DEAD || to == GAME_STATE_QUIT;
    case GAME_STATE_INVENTORY:
        return to == GAME_STATE_PLAYING || to == GAME_STATE_QUIT;
    case GAME_STATE_CRAFTING:
        return to == GAME_STATE_PLAYING || to == GAME_STATE_QUIT;
    case GAME_STATE_DEAD:
        return to == GAME_STATE_PLAYING || to == GAME_STATE_MAIN_MENU || to == GAME_STATE_QUIT;
    case GAME_STATE_PAUSED:
        return to == GAME_STATE_PLAYING || to == GAME_STATE_SETTINGS || to == GAME_STATE_MAIN_MENU ||
               to == GAME_STATE_QUIT;
    case GAME_STATE_SETTINGS:
        /* SETTINGS returns to its origin (menu or pause); QUIT always ok. */
        return to == GAME_STATE_MAIN_MENU || to == GAME_STATE_PAUSED || to == GAME_STATE_QUIT;
    case GAME_STATE_QUIT:
        return false;
    default:
        return false;
    }
}

/* Live simulation states. */
bool game_state_is_live(GameState s)
{
    return s == GAME_STATE_PLAYING || s == GAME_STATE_LOADING;
}

/* States requiring an open world. */
bool game_state_needs_world(GameState s)
{
    return s == GAME_STATE_LOADING || s == GAME_STATE_PLAYING || s == GAME_STATE_PAUSED ||
           s == GAME_STATE_INVENTORY || s == GAME_STATE_CRAFTING || s == GAME_STATE_DEAD;
}
