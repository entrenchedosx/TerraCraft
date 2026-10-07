#pragma once

/* Audio event hooks (M6 events, M7 backend in audio/audio.h): named
 * gameplay moments where sound attaches. Gameplay raises events via
 * audio_play(&app->audio, ev); the backend resolves resources. No
 * SDL_mixer dependency.
 */

#include <stddef.h>

/* Gameplay moments that will make sound. */
typedef enum AudioEvent {
    AUDIO_NONE = 0,
    AUDIO_BLOCK_BREAK, /* Survival block mined through. */
    AUDIO_BLOCK_PLACE, /* Block placed (either mode). */
    AUDIO_ITEM_PICKUP, /* Drop fully collected. */
    AUDIO_PLAYER_HURT, /* Survival damage taken. */
    AUDIO_PLAYER_DIE,  /* Death. */
    AUDIO_UI_CLICK,    /* Menu/inventory click. */
    AUDIO_TOOL_BREAK,  /* Tool durability spent (M7 backend). */
    AUDIO_EAT,         /* Food item fully eaten (M7 backend). */
    AUDIO_CRAFT,       /* Crafting output taken (M7 backend). */
    AUDIO_STEP_STONE,  /* Footstep on stone-like block (M7 backend). */
    AUDIO_STEP_DIRT,   /* Footstep on dirt/grass-like block (M7 backend). */
    AUDIO_STEP_WOOD,   /* Footstep on wood-like block (M7 backend). */
    AUDIO_STEP_SAND,   /* Footstep on sand-like block (M7 backend). */
    AUDIO_MOB_HURT,    /* Living mob takes damage (M8 backend). */
    AUDIO_MOB_DIE,     /* Living mob dies (M8 backend). */
    AUDIO_COW_HURT,    /* Cow takes damage: moo (converted, synth fallback). */
    AUDIO_COW_DIE,     /* Cow dies: low moo (converted, synth fallback). */
    AUDIO_BOW_DRAW,    /* Bow draw started (M9 backend). */
    AUDIO_BOW_FIRE,    /* Arrow released, player or skeleton (M9 backend). */
    AUDIO_ARROW_STICK, /* Arrow embedded in terrain (M9 backend). */
    AUDIO_SPLASH,      /* Water entry splash (synth fallback, converted splash). */
    AUDIO_EVENT_COUNT  /* Sentinel: number of events (keep last). */
} AudioEvent;
