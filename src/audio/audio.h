#pragma once

/* Procedural audio engine (M7): SDL2 audio-device backend with synthesized
 * default sounds (original, generated at init — no audio assets ship in
 * the repo). Gameplay raises AudioEvent values (see game/audio.h); this
 * module resolves them to PCM and mixes a small voice pool. Sound sources
 * layer per event: synthesized defaults, then owner-supplied converted
 * assets (mcassets/generated/sounds/, when present), then user
 * resource-pack WAV overrides — each layer replaces what it provides,
 * missing files keep the layer below (never silent, never fatal).
 * No SDL_mixer: the callback, mixing, and WAV parsing are all local code.
 *
 * Threading: the SDL audio callback runs on its own thread; play/volume
 * calls lock the device around voice allocation. No audio device (or a
 * failed open) degrades to silent no-ops with one warning — the game
 * always runs.
 */

#include "game/audio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Synthesis rate (mono S16). Devices that insist on another rate still
 * play (pitch-shifted, with a warning); resampling is out of scope. */
#define AUDIO_SAMPLE_RATE 22050

/* Simultaneous one-shot voices (oldest stolen when exhausted). */
#define AUDIO_MAX_VOICES 8

/* Variants per sound set (round-robin: dig1..4 and friends). */
#define AUDIO_MAX_VARIANTS 4

/* Block material classes for break/place/step sounds. */
typedef enum AudioMaterial {
    AUDIO_MAT_STONE = 0, /* Stone, ores, bedrock, default. */
    AUDIO_MAT_GRASS,     /* Grass, leaves, plants, flowers. */
    AUDIO_MAT_GRAVEL,    /* Dirt. */
    AUDIO_MAT_WOOD,      /* Wood, workbench, planks, torches. */
    AUDIO_MAT_SAND,      /* Sand. */
    AUDIO_MAT_GLASS,     /* Glass. */
    AUDIO_MAT_SNOW,      /* Snow. */
    AUDIO_MAT_COUNT      /* Sentinel (keep last). */
} AudioMaterial;

/* Sound file stems per event, under resourcepacks/<pack>/sounds/. */
const char *audio_event_stem(AudioEvent ev);

/* Block ID to material class (for break/place/step resolution).
 *
 * Args:
 *   block: BlockType value.
 *
 * Returns: material class (never out of range).
 */
AudioMaterial audio_block_material(uint16_t block);

/* One mixed voice (mixer-private state, but visible for tests). */
typedef struct AudioVoice {
    bool active;
    const int16_t *data; /* Borrowed sample PCM (bank-owned). */
    size_t frames;       /* Sample length. */
    size_t pos;          /* Next frame to mix. */
    float vol;           /* Per-voice gain (event base volume). */
} AudioVoice;

/* One variant set: up to 4 PCM blobs played round-robin (e.g. dig1..4).
 * Empty sets are silent (play skips them).
 */
typedef struct AudioSet {
    int16_t *pcm[AUDIO_MAX_VARIANTS];    /* Owned PCM per variant (or NULL). */
    size_t frames[AUDIO_MAX_VARIANTS];  /* Frames per variant. */
    int count;                           /* Loaded variants (0..4). */
    unsigned int rr;                     /* Round-robin cursor. */
} AudioSet;

/* Owned audio system (lives in AppContext by value; no globals). */
typedef struct AudioSystem {
    bool ready;                            /* True when the device is open. */
    float master;                          /* Master gain 0..1. */
    float sfx;                             /* Effects gain 0..1. */
    unsigned int device;                   /* SDL device id (0 = none). */
    AudioVoice voices[AUDIO_MAX_VOICES];   /* Mixer voices. */
    AudioSet sets[AUDIO_EVENT_COUNT];      /* Per-event sets (BREAK/PLACE/STEP: stone material). */
    AudioSet msets[3][AUDIO_MAT_COUNT];    /* [BREAK,PLACE,STEP][material] variant sets. */
    char pack[64];                         /* Loaded pack ("" = synth only). */
    bool mc_loaded;                        /* True once the mcassets layer applied. */
} AudioSystem;

/* Initialise: synthesize the default bank, then open the default audio
 * device (SDL_INIT_AUDIO subsystem started on demand). No device is NOT
 * fatal: ready stays false, plays are silent no-ops, returns 0 with a
 * warning. Safe to call twice (re-inits).
 *
 * Args:
 *   sys: system to initialise (must not be NULL; must be zeroed memory
 *     or a previous AudioSystem — never uninitialized stack garbage,
 *     since a nonzero device id triggers a shutdown of that device).
 *
 * Returns: 0 on success (device or degraded-silent), non-zero on bad args.
 */
int audio_init(AudioSystem *sys);

/* Shut down: close the device, free the bank, zero voices. NULL-safe.
 *
 * Args:
 *   sys: system to shut down (may be NULL).
 */
void audio_shutdown(AudioSystem *sys);

/* Play a one-shot event (mixes at master*sfx*event gain). No-op when the
 * system is not ready or args are bad. Thread-safe (locks the device).
 *
 * Args:
 *   sys: system (may be NULL).
 *   ev: event to play.
 */
void audio_play(AudioSystem *sys, AudioEvent ev);

/* Play a material sound (BREAK/PLACE/STEP) for a block: resolves the
 * block's material class and plays its variant set round-robin. Other
 * events fall back to audio_play. No-op when not ready or on bad args.
 *
 * Args:
 *   sys: system (may be NULL).
 *   ev: AUDIO_BLOCK_BREAK, AUDIO_BLOCK_PLACE, or footstep event.
 *   block: BlockType value.
 */
void audio_play_block(AudioSystem *sys, AudioEvent ev, uint16_t block);

/* Loaded variant count (for tests/diagnostics; 0 when silent).
 *
 * Args:
 *   sys: system (may be NULL).
 *   ev: event.
 *   material: material class (only used for BREAK/PLACE/STEP).
 *
 * Returns: variant count (0 on bad args or empty set).
 */
int audio_bank_variants(const AudioSystem *sys, AudioEvent ev, int material);

/* Set gains (each clamped 0..1). Thread-safe. No-op on NULL.
 *
 * Args:
 *   sys: system (may be NULL).
 *   master, sfx: gains.
 */
void audio_set_volumes(AudioSystem *sys, float master, float sfx);

/* Check whether the device is open (audible).
 *
 * Args:
 *   sys: system (may be NULL).
 *
 * Returns: true when play() produces sound.
 */
bool audio_is_ready(const AudioSystem *sys);

/* Replace bank entries from a resource pack's sounds/ directory
 * (<stem>.wav each). Unknown pack names, missing dirs/files, and invalid
 * WAVs keep the synth defaults (warning per skipped file, never fatal).
 * "Default"/NULL/empty all mean the no-pack bank and compare equal, so
 * re-calling with the same effective pack is a no-op (no rebuild, live
 * voices untouched). A real pack change rebuilds the bank and cuts all
 * live voices first (stale voices must never outlive their samples).
 * Remembers the pack.
 *
 * Args:
 *   sys: system (must not be NULL; works before/after device open).
 *   pack: pack directory name ("Default"/NULL/empty = synth defaults).
 *
 * Returns: 0 on success (even when nothing overrode), non-zero on bad args.
 */
int audio_load_pack(AudioSystem *sys, const char *pack);

/* Apply an owner-converted sound directory (same layout as the pack
 * sounds/ dir, but with per-material/per-variant names:
 * {break,place,step}_{stone,grass,gravel,wood,sand,glass}1..4 plus
 * pickup, hurt1..3, die, click, tool_break, eat1..3, craft). Missing
 * files keep lower layers. Headless-testable (no device needed).
 *
 * Args:
 *   sys: system (must not be NULL).
 *   dir: sound directory (must not be NULL).
 *
 * Returns: variant files loaded (0 keeps everything below).
 */
int audio_load_mcassets_from(AudioSystem *sys, const char *dir);

/* Parse a WAV buffer into 22050 Hz mono S16 PCM (load-time only; the
 * caller frees *out_pcm). Accepts RIFF/WAVE with a fmt chunk (PCM
 * 8/16-bit, mono/stereo, any standard rate) and a data chunk; stereo is
 * averaged, other rates linearly resampled. Caps: 4 MiB input, 10 s
 * output. Anything else is rejected safely.
 *
 * Args:
 *   data: WAV bytes (must not be NULL).
 *   n: byte count.
 *   out_pcm: receives malloc'd PCM (must not be NULL).
 *   out_frames: receives frame count (must not be NULL).
 *
 * Returns: 0 on success, non-zero on invalid/unsupported data or OOM.
 */
int audio_parse_wav(const unsigned char *data, size_t n, int16_t **out_pcm, size_t *out_frames);
