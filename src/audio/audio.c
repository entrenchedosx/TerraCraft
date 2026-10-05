#include "audio/audio.h"
#include "core/log.h"
#include "core/path.h"
#include "world/block.h"

/* Portable SDL include (same convention as platform/window.c). */
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WAV parser caps (load-time only). */
#define WAV_MAX_BYTES (4u * 1024u * 1024u)
#define WAV_MAX_SECONDS 10u

/* LCG for deterministic synth noise (fixed seeds per event). */
static uint32_t synth_rand(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

/* Sine with linear frequency sweep (phase-integrated, no clicks). */
static void synth_tone(int16_t *dst, size_t frames, double f0, double f1, float vol, bool square)
{
    double phase = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        double t = frames > 1 ? (double)i / (double)(frames - 1) : 0.0;
        double f = f0 + (f1 - f0) * t;
        phase += 2.0 * 3.141592653589793 * f / (double)AUDIO_SAMPLE_RATE;
        double v = sin(phase);
        if (square) {
            v = v >= 0.0 ? 0.8 : -0.8;
        }
        double env = 1.0 - t;
        env *= env;
        double mixed = (double)dst[i] + v * (double)vol * env * 32767.0;
        if (mixed > 32767.0) {
            mixed = 32767.0;
        }
        if (mixed < -32768.0) {
            mixed = -32768.0;
        }
        dst[i] = (int16_t)mixed;
    }
}

/* Filtered noise burst (one-pole lowpass: bright 1.0, muffled 0.0). */
static void synth_noise(int16_t *dst, size_t frames, float vol, float bright, uint32_t seed)
{
    double y = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        double t = frames > 1 ? (double)i / (double)(frames - 1) : 0.0;
        double x = (double)(int32_t)(synth_rand(&seed) & 0xFFFFu) / 32768.0 - 1.0;
        y += (double)bright * (x - y);
        double env = 1.0 - t;
        env *= env;
        double mixed = (double)dst[i] + y * (double)vol * env * 32767.0;
        if (mixed > 32767.0) {
            mixed = 32767.0;
        }
        if (mixed < -32768.0) {
            mixed = -32768.0;
        }
        dst[i] = (int16_t)mixed;
    }
}

/* Mix a second primitive into a region (for layered sounds). */
static void synth_layer_tone(int16_t *dst, size_t start, size_t frames, double f0, double f1, float vol,
                             bool square)
{
    if (frames == 0) {
        return;
    }
    int16_t *tmp = (int16_t *)calloc(frames, sizeof(int16_t));
    if (tmp == NULL) {
        return;
    }
    synth_tone(tmp, frames, f0, f1, vol, square);
    for (size_t i = 0; i < frames; ++i) {
        double mixed = (double)dst[start + i] + (double)tmp[i];
        if (mixed > 32767.0) {
            mixed = 32767.0;
        }
        if (mixed < -32768.0) {
            mixed = -32768.0;
        }
        dst[start + i] = (int16_t)mixed;
    }
    free(tmp);
}

/* Allocate a zeroed PCM buffer for `seconds` of audio. */
static int16_t *synth_buffer(double seconds, size_t *out_frames)
{
    size_t frames = (size_t)(seconds * (double)AUDIO_SAMPLE_RATE + 0.5);
    if (frames == 0) {
        frames = 1;
    }
    int16_t *pcm = (int16_t *)calloc(frames, sizeof(int16_t));
    if (pcm == NULL) {
        return NULL;
    }
    if (out_frames != NULL) {
        *out_frames = frames;
    }
    return pcm;
}

/* Build one event's default sample (original TerraCraft synth, deterministic). */
static int synth_event(AudioEvent ev, int16_t **out_pcm, size_t *out_frames)
{
    int16_t *pcm = NULL;
    size_t frames = 0;
    switch (ev) {
    case AUDIO_BLOCK_BREAK:
        pcm = synth_buffer(0.18, &frames);
        if (pcm != NULL) {
            synth_noise(pcm, frames, 0.9f, 0.35f, 0x12345678u);
            synth_layer_tone(pcm, 0, frames, 120.0, 70.0, 0.5f, false);
        }
        break;
    case AUDIO_BLOCK_PLACE:
        pcm = synth_buffer(0.12, &frames);
        if (pcm != NULL) {
            synth_tone(pcm, frames, 170.0, 120.0, 0.8f, false);
        }
        break;
    case AUDIO_ITEM_PICKUP:
        pcm = synth_buffer(0.16, &frames);
        if (pcm != NULL) {
            size_t half = frames / 2;
            synth_layer_tone(pcm, 0, half, 660.0, 660.0, 0.6f, false);
            synth_layer_tone(pcm, half, frames - half, 990.0, 990.0, 0.6f, false);
        }
        break;
    case AUDIO_PLAYER_HURT:
        pcm = synth_buffer(0.25, &frames);
        if (pcm != NULL) {
            synth_tone(pcm, frames, 140.0, 90.0, 0.8f, true);
        }
        break;
    case AUDIO_PLAYER_DIE:
        pcm = synth_buffer(0.50, &frames);
        if (pcm != NULL) {
            synth_tone(pcm, frames, 420.0, 70.0, 0.8f, false);
        }
        break;
    case AUDIO_UI_CLICK:
        pcm = synth_buffer(0.035, &frames);
        if (pcm != NULL) {
            synth_tone(pcm, frames, 1250.0, 1250.0, 0.5f, false);
        }
        break;
    case AUDIO_TOOL_BREAK:
        pcm = synth_buffer(0.30, &frames);
        if (pcm != NULL) {
            synth_noise(pcm, frames, 0.9f, 0.7f, 0xC0FFEE01u);
            synth_layer_tone(pcm, 0, frames, 900.0, 200.0, 0.5f, false);
        }
        break;
    case AUDIO_EAT:
        pcm = synth_buffer(0.22, &frames);
        if (pcm != NULL) {
            /* Two chomps: noise burst, then a lower second bite. */
            size_t chomp = (size_t)((double)frames * 0.36);
            synth_noise(pcm, chomp, 0.8f, 0.25f, 0xEA75EED1u);
            size_t off = chomp + (frames - chomp) / 3;
            if (off < frames) {
                size_t tail = frames - off;
                if (tail > chomp) {
                    tail = chomp;
                }
                synth_layer_tone(pcm, off, tail, 150.0, 110.0, 0.5f, false);
            }
        }
        break;
    case AUDIO_CRAFT:
        pcm = synth_buffer(0.24, &frames);
        if (pcm != NULL) {
            size_t third = frames / 3;
            synth_layer_tone(pcm, 0, third, 523.0, 523.0, 0.6f, false);
            synth_layer_tone(pcm, third, third, 659.0, 659.0, 0.6f, false);
            synth_layer_tone(pcm, third * 2, frames - third * 2, 784.0, 784.0, 0.6f, false);
        }
        break;
    case AUDIO_STEP_STONE:
        pcm = synth_buffer(0.07, &frames);
        if (pcm != NULL) {
            synth_noise(pcm, frames, 0.5f, 0.5f, 0x57EED1u);
            synth_layer_tone(pcm, 0, frames, 200.0, 150.0, 0.4f, false);
        }
        break;
    case AUDIO_STEP_DIRT:
        pcm = synth_buffer(0.07, &frames);
        if (pcm != NULL) {
            synth_noise(pcm, frames, 0.45f, 0.3f, 0xD12700D1u);
        }
        break;
    case AUDIO_STEP_WOOD:
        pcm = synth_buffer(0.08, &frames);
        if (pcm != NULL) {
            synth_noise(pcm, frames, 0.5f, 0.55f, 0xB00D0000u);
            synth_layer_tone(pcm, 0, frames, 140.0, 110.0, 0.5f, false);
        }
        break;
    case AUDIO_STEP_SAND:
        pcm = synth_buffer(0.09, &frames);
        if (pcm != NULL) {
            synth_noise(pcm, frames, 0.35f, 0.2f, 0x5A4D00u);
        }
        break;
    case AUDIO_MOB_HURT:
        pcm = synth_buffer(0.15, &frames);
        if (pcm != NULL) {
            synth_tone(pcm, frames, 300.0, 180.0, 0.7f, true);
        }
        break;
    case AUDIO_MOB_DIE:
        pcm = synth_buffer(0.35, &frames);
        if (pcm != NULL) {
            synth_tone(pcm, frames, 260.0, 60.0, 0.8f, false);
        }
        break;
    case AUDIO_COW_HURT:
        pcm = synth_buffer(0.30, &frames);
        if (pcm != NULL) {
            /* Moo-ish fallback: lowFM-ish wobble (converted moo wins). */
            synth_tone(pcm, frames, 170.0, 120.0, 0.7f, false);
            synth_layer_tone(pcm, 0, frames / 2, 340.0, 240.0, 0.3f, false);
        }
        break;
    case AUDIO_COW_DIE:
        pcm = synth_buffer(0.55, &frames);
        if (pcm != NULL) {
            synth_tone(pcm, frames, 200.0, 70.0, 0.8f, false);
            synth_layer_tone(pcm, 0, frames / 2, 400.0, 140.0, 0.3f, false);
        }
        break;
    case AUDIO_BOW_DRAW:
        pcm = synth_buffer(0.15, &frames);
        if (pcm != NULL) {
            synth_tone(pcm, frames, 180.0, 420.0, 0.5f, false);
        }
        break;
    case AUDIO_BOW_FIRE:
        pcm = synth_buffer(0.20, &frames);
        if (pcm != NULL) {
            synth_noise(pcm, frames, 0.6f, 0.8f, 0xB0AF1EEDu);
            synth_layer_tone(pcm, 0, frames, 520.0, 140.0, 0.6f, false);
        }
        break;
    case AUDIO_ARROW_STICK:
        pcm = synth_buffer(0.09, &frames);
        if (pcm != NULL) {
            synth_noise(pcm, frames, 0.7f, 0.3f, 0x57C0FF1u);
            synth_layer_tone(pcm, 0, frames, 300.0, 180.0, 0.5f, false);
        }
        break;
    default:
        break;
    }
    if (pcm == NULL) {
        return -1;
    }
    *out_pcm = pcm;
    *out_frames = frames;
    return 0;
}

/* Per-event base gain (footsteps sit lower in the mix). */
static float event_gain(AudioEvent ev)
{
    switch (ev) {
    case AUDIO_STEP_STONE:
    case AUDIO_STEP_DIRT:
    case AUDIO_STEP_WOOD:
    case AUDIO_STEP_SAND:
        return 0.7f;
    case AUDIO_UI_CLICK:
        return 0.8f;
    default:
        return 1.0f;
    }
}

/* Sound file stems (NULL for NONE/COUNT). */
const char *audio_event_stem(AudioEvent ev)
{
    switch (ev) {
    case AUDIO_BLOCK_BREAK:
        return "break";
    case AUDIO_BLOCK_PLACE:
        return "place";
    case AUDIO_ITEM_PICKUP:
        return "pickup";
    case AUDIO_PLAYER_HURT:
        return "hurt";
    case AUDIO_PLAYER_DIE:
        return "die";
    case AUDIO_UI_CLICK:
        return "click";
    case AUDIO_TOOL_BREAK:
        return "tool_break";
    case AUDIO_EAT:
        return "eat";
    case AUDIO_CRAFT:
        return "craft";
    case AUDIO_STEP_STONE:
        return "step_stone";
    case AUDIO_STEP_DIRT:
        return "step_dirt";
    case AUDIO_STEP_WOOD:
        return "step_wood";
    case AUDIO_STEP_SAND:
        return "step_sand";
    case AUDIO_MOB_HURT:
        return "mob_hurt";
    case AUDIO_MOB_DIE:
        return "mob_die";
    case AUDIO_COW_HURT:
        return "cow_hurt";
    case AUDIO_COW_DIE:
        return "cow_die";
    case AUDIO_BOW_DRAW:
        return "bow_draw";
    case AUDIO_BOW_FIRE:
        return "bow_fire";
    case AUDIO_ARROW_STICK:
        return "arrow_stick";
    default:
        return NULL;
    }
}

/* Free one variant set. */
static void audio_free_set(AudioSet *set)
{
    for (int i = 0; i < AUDIO_MAX_VARIANTS; ++i) {
        free(set->pcm[i]);
        set->pcm[i] = NULL;
        set->frames[i] = 0;
    }
    set->count = 0;
    set->rr = 0;
}

/* Free the whole sample bank. */
static void audio_free_bank(AudioSystem *sys)
{
    for (int i = 0; i < AUDIO_EVENT_COUNT; ++i) {
        audio_free_set(&sys->sets[i]);
    }
    for (int e = 0; e < 3; ++e) {
        for (int m = 0; m < AUDIO_MAT_COUNT; ++m) {
            audio_free_set(&sys->msets[e][m]);
        }
    }
}

/* Store one PCM blob as a single-variant set (takes ownership). */
static void audio_set_single(AudioSet *set, int16_t *pcm, size_t frames)
{
    audio_free_set(set);
    if (pcm != NULL && frames > 0) {
        set->pcm[0] = pcm;
        set->frames[0] = frames;
        set->count = 1;
    }
}

/* Duplicate a PCM blob into a set slot (for material copies). */
static int audio_set_dup(AudioSet *set, const int16_t *pcm, size_t frames)
{
    if (frames == 0 || set->count >= AUDIO_MAX_VARIANTS) {
        return -1;
    }
    int16_t *copy = (int16_t *)malloc(frames * sizeof(int16_t));
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, pcm, frames * sizeof(int16_t));
    set->pcm[set->count] = copy;
    set->frames[set->count] = frames;
    set->count++;
    return 0;
}

/* Synthesize the full default bank (original sounds, deterministic).
 * Material sets each get their own copy of the generic event synth
 * (freed independently, so layers can replace per-material freely).
 */
static int audio_synth_bank(AudioSystem *sys)
{
    audio_free_bank(sys);
    for (int i = 0; i < AUDIO_EVENT_COUNT; ++i) {
        if (i == AUDIO_NONE) {
            continue;
        }
        int16_t *pcm = NULL;
        size_t frames = 0;
        if (synth_event((AudioEvent)i, &pcm, &frames) != 0) {
            LOG_ERROR("audio: synth failed for event %d", i);
            audio_free_bank(sys);
            return -1;
        }
        audio_set_single(&sys->sets[i], pcm, frames);
    }
    const AudioEvent mev[3] = {AUDIO_BLOCK_BREAK, AUDIO_BLOCK_PLACE, AUDIO_STEP_STONE};
    for (int e = 0; e < 3; ++e) {
        AudioSet *src = &sys->sets[mev[e]];
        if (src->count == 0) {
            audio_free_bank(sys);
            return -1;
        }
        for (int m = 0; m < AUDIO_MAT_COUNT; ++m) {
            if (audio_set_dup(&sys->msets[e][m], src->pcm[0], src->frames[0]) != 0) {
                audio_free_bank(sys);
                return -1;
            }
        }
    }
    return 0;
}

/* SDL audio callback: mix active voices (locked by SDL around the call). */
static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len)
{
    AudioSystem *sys = (AudioSystem *)userdata;
    int16_t *out = (int16_t *)stream;
    size_t frames = len > 0 ? (size_t)len / 2 : 0;
    for (size_t i = 0; i < frames; ++i) {
        out[i] = 0;
    }
    if (sys == NULL) {
        return;
    }
    float gain = sys->master * sys->sfx;
    if (!(gain > 0.0f)) {
        for (int v = 0; v < AUDIO_MAX_VOICES; ++v) {
            sys->voices[v].active = false;
        }
        return;
    }
    for (int v = 0; v < AUDIO_MAX_VOICES; ++v) {
        AudioVoice *vc = &sys->voices[v];
        if (!vc->active || vc->data == NULL) {
            vc->active = false;
            continue;
        }
        for (size_t i = 0; i < frames && vc->pos < vc->frames; ++i, ++vc->pos) {
            double mixed = (double)out[i] + (double)vc->data[vc->pos] * (double)vc->vol * (double)gain;
            if (mixed > 32767.0) {
                mixed = 32767.0;
            }
            if (mixed < -32768.0) {
                mixed = -32768.0;
            }
            out[i] = (int16_t)mixed;
        }
        if (vc->pos >= vc->frames) {
            vc->active = false;
        }
    }
}

/* Forward: owner-converted layer (defined below; needs no device). */
static void audio_load_mcassets(AudioSystem *sys);

/* Initialise the system (synth bank + device). */
int audio_init(AudioSystem *sys)
{
    if (sys == NULL) {
        return -1;
    }
    if (sys->device != 0) {
        audio_shutdown(sys); /* Never leak a device across re-init. */
    }
    memset(sys, 0, sizeof(*sys));
    sys->master = 0.8f;
    sys->sfx = 0.8f;
    if (audio_synth_bank(sys) != 0) {
        return -2;
    }
    /* Owner-converted layer (no-op without mcassets/generated/). */
    audio_load_mcassets(sys);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        LOG_WARN("audio: SDL audio unavailable (%s); continuing silent", SDL_GetError());
        return 0;
    }
    SDL_AudioSpec want;
    memset(&want, 0, sizeof(want));
    want.freq = AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audio_callback;
    want.userdata = sys;
    SDL_AudioSpec have;
    memset(&have, 0, sizeof(have));
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) {
        LOG_WARN("audio: no audio device (%s); continuing silent", SDL_GetError());
        return 0;
    }
    if (have.freq != AUDIO_SAMPLE_RATE || have.format != AUDIO_S16SYS || have.channels != 1) {
        LOG_WARN("audio: device gave %d Hz fmt %u ch %u (want 22050/S16/mono); pitch may shift",
                 have.freq, (unsigned)have.format, (unsigned)have.channels);
    }
    sys->device = (unsigned int)dev;
    sys->ready = true;
    SDL_PauseAudioDevice(dev, 0);
    LOG_INFO("audio: device open (%d Hz)", have.freq);
    return 0;
}

/* Shut down. */
void audio_shutdown(AudioSystem *sys)
{
    if (sys == NULL) {
        return;
    }
    if (sys->device != 0) {
        SDL_CloseAudioDevice((SDL_AudioDeviceID)sys->device);
        sys->device = 0;
    }
    audio_free_bank(sys);
    memset(sys, 0, sizeof(*sys));
}

/* Play one event (allocates a voice, stealing oldest when full). */
void audio_play(AudioSystem *sys, AudioEvent ev)
{
    if (sys == NULL || !sys->ready || sys->device == 0) {
        return;
    }
    if (ev <= AUDIO_NONE || ev >= AUDIO_EVENT_COUNT) {
        return;
    }
    AudioSet *set = &sys->sets[ev];
    if (set->count <= 0) {
        return;
    }
    unsigned int idx = set->rr++ % (unsigned int)set->count;
    if (set->pcm[idx] == NULL || set->frames[idx] == 0) {
        return;
    }
    SDL_LockAudioDevice((SDL_AudioDeviceID)sys->device);
    int slot = -1;
    for (int v = 0; v < AUDIO_MAX_VOICES; ++v) {
        if (!sys->voices[v].active) {
            slot = v;
            break;
        }
    }
    if (slot < 0) {
        /* Steal the furthest-played voice (simple oldest heuristic). */
        size_t best = 0;
        slot = 0;
        for (int v = 0; v < AUDIO_MAX_VOICES; ++v) {
            if (sys->voices[v].pos > best) {
                best = sys->voices[v].pos;
                slot = v;
            }
        }
    }
    sys->voices[slot].active = true;
    sys->voices[slot].data = set->pcm[idx];
    sys->voices[slot].frames = set->frames[idx];
    sys->voices[slot].pos = 0;
    sys->voices[slot].vol = event_gain(ev);
    SDL_UnlockAudioDevice((SDL_AudioDeviceID)sys->device);
}

/* Block ID to material class. */
AudioMaterial audio_block_material(uint16_t block)
{
    switch (block) {
    case BLOCK_GRASS:
    case BLOCK_LEAVES:
    case BLOCK_GRASS_PLANT:
    case BLOCK_FLOWER:
        return AUDIO_MAT_GRASS;
    case BLOCK_DIRT:
        return AUDIO_MAT_GRAVEL;
    case BLOCK_WOOD:
    case BLOCK_WORKBENCH:
    case BLOCK_PLANKS:
    case BLOCK_TORCH:
        return AUDIO_MAT_WOOD;
    case BLOCK_SAND:
        return AUDIO_MAT_SAND;
    case BLOCK_SNOW:
        return AUDIO_MAT_SNOW;
    case BLOCK_GLASS:
        return AUDIO_MAT_GLASS;
    default:
        return AUDIO_MAT_STONE;
    }
}

/* Material-set row for an event (BREAK=0, PLACE=1, any STEP=2). */
static int audio_mat_row(AudioEvent ev)
{
    if (ev == AUDIO_BLOCK_BREAK) {
        return 0;
    }
    if (ev == AUDIO_BLOCK_PLACE) {
        return 1;
    }
    return 2;
}

/* Play a material sound for a block. */
void audio_play_block(AudioSystem *sys, AudioEvent ev, uint16_t block)
{
    if (sys == NULL || !sys->ready || sys->device == 0) {
        return;
    }
    if (ev != AUDIO_BLOCK_BREAK && ev != AUDIO_BLOCK_PLACE && ev != AUDIO_STEP_STONE &&
        ev != AUDIO_STEP_DIRT && ev != AUDIO_STEP_WOOD && ev != AUDIO_STEP_SAND) {
        audio_play(sys, ev);
        return;
    }
    AudioSet *set = &sys->msets[audio_mat_row(ev)][audio_block_material(block)];
    if (set->count <= 0) {
        return;
    }
    unsigned int idx = set->rr++ % (unsigned int)set->count;
    if (set->pcm[idx] == NULL || set->frames[idx] == 0) {
        return;
    }
    SDL_LockAudioDevice((SDL_AudioDeviceID)sys->device);
    int slot = -1;
    for (int v = 0; v < AUDIO_MAX_VOICES; ++v) {
        if (!sys->voices[v].active) {
            slot = v;
            break;
        }
    }
    if (slot < 0) {
        size_t best = 0;
        slot = 0;
        for (int v = 0; v < AUDIO_MAX_VOICES; ++v) {
            if (sys->voices[v].pos > best) {
                best = sys->voices[v].pos;
                slot = v;
            }
        }
    }
    sys->voices[slot].active = true;
    sys->voices[slot].data = set->pcm[idx];
    sys->voices[slot].frames = set->frames[idx];
    sys->voices[slot].pos = 0;
    sys->voices[slot].vol = event_gain(ev);
    SDL_UnlockAudioDevice((SDL_AudioDeviceID)sys->device);
}

/* Loaded variant count (tests/diagnostics). */
int audio_bank_variants(const AudioSystem *sys, AudioEvent ev, int material)
{
    if (sys == NULL || ev <= AUDIO_NONE || ev >= AUDIO_EVENT_COUNT) {
        return 0;
    }
    if ((ev == AUDIO_BLOCK_BREAK || ev == AUDIO_BLOCK_PLACE || ev == AUDIO_STEP_STONE ||
         ev == AUDIO_STEP_DIRT || ev == AUDIO_STEP_WOOD || ev == AUDIO_STEP_SAND) &&
        material >= 0 && material < AUDIO_MAT_COUNT) {
        return sys->msets[audio_mat_row(ev)][material].count;
    }
    return sys->sets[ev].count;
}

/* Set gains (clamped). */
void audio_set_volumes(AudioSystem *sys, float master, float sfx)
{
    if (sys == NULL) {
        return;
    }
    if (!(master >= 0.0f)) {
        master = 0.0f;
    }
    if (master > 1.0f) {
        master = 1.0f;
    }
    if (!(sfx >= 0.0f)) {
        sfx = 0.0f;
    }
    if (sfx > 1.0f) {
        sfx = 1.0f;
    }
    if (sys->device != 0) {
        SDL_LockAudioDevice((SDL_AudioDeviceID)sys->device);
    }
    sys->master = master;
    sys->sfx = sfx;
    if (sys->device != 0) {
        SDL_UnlockAudioDevice((SDL_AudioDeviceID)sys->device);
    }
}

/* Ready check. */
bool audio_is_ready(const AudioSystem *sys)
{
    return sys != NULL && sys->ready && sys->device != 0;
}

/* Load one WAV file into fresh PCM (caller frees). Missing/unreadable
 * files and parse failures all return non-zero (caller keeps fallback).
 */
static int audio_load_wav_file(const char *path, int16_t **out_pcm, size_t *out_frames)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -2;
    }
    long sz = ftell(f);
    if (sz <= 0 || (size_t)sz > WAV_MAX_BYTES) {
        fclose(f);
        return -3;
    }
    rewind(f);
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (buf == NULL) {
        fclose(f);
        return -4;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return -5;
    }
    int rc = audio_parse_wav(buf, (size_t)sz, out_pcm, out_frames);
    free(buf);
    return rc;
}

/* Owner-converted asset subdirectory (resolved exe-relative first, CWD
 * fallback second — see path_mcassets_dir).
 */

/* Material filename infixes, indexed by AudioMaterial. */
static const char *MC_MAT_INFIX[AUDIO_MAT_COUNT] = {"stone", "grass", "gravel",
                                                    "wood",  "sand",  "glass",
                                                    "snow"};

/* Material event rows: 0 = BREAK, 1 = PLACE, 2 = STEP. */
static const char *MC_MAT_EVENT[3] = {"break", "place", "step"};

/* Simple (non-material) event file lists (NULL-terminated). */
static const char *MC_SIMPLE_PICKUP[] = {"pickup", NULL};
static const char *MC_SIMPLE_HURT[] = {"hurt1", "hurt2", "hurt3", NULL};
static const char *MC_SIMPLE_DIE[] = {"die", NULL};
static const char *MC_SIMPLE_CLICK[] = {"click", NULL};
static const char *MC_SIMPLE_TOOL_BREAK[] = {"tool_break", NULL};
static const char *MC_SIMPLE_EAT[] = {"eat1", "eat2", "eat3", NULL};
static const char *MC_SIMPLE_CRAFT[] = {"craft", NULL};
static const char *MC_SIMPLE_COW_HURT[] = {"cow_hurt1", "cow_hurt2", "cow_hurt3", NULL};
static const char *MC_SIMPLE_COW_DIE[] = {"cow_die", NULL};

/* Load owner-converted WAVs from a directory into one set (up to 4
 * variants, in list order). Returns variants loaded (0 keeps fallback).
 */
static int audio_load_set_from_dir(AudioSet *set, const char *dir, const char * const *names)
{
    AudioSet fresh;
    memset(&fresh, 0, sizeof(fresh));
    for (int i = 0; names[i] != NULL && fresh.count < AUDIO_MAX_VARIANTS; ++i) {
        char rel[160];
        int n = snprintf(rel, sizeof(rel), "%s/%s.wav", dir, names[i]);
        if (n <= 0 || (size_t)n >= sizeof(rel)) {
            continue;
        }
        int16_t *pcm = NULL;
        size_t frames = 0;
        if (audio_load_wav_file(rel, &pcm, &frames) != 0) {
            continue;
        }
        fresh.pcm[fresh.count] = pcm;
        fresh.frames[fresh.count] = frames;
        fresh.count++;
    }
    if (fresh.count == 0) {
        return 0;
    }
    audio_free_set(set);
    *set = fresh;
    return fresh.count;
}

/* Apply an owner-converted sound directory (see header). Missing files
 * keep lower layers; returns variant files loaded.
 */
int audio_load_mcassets_from(AudioSystem *sys, const char *dir)
{
    if (sys == NULL || dir == NULL || dir[0] == '\0') {
        return 0;
    }
    if (!path_is_dir(dir)) {
        LOG_DEBUG("audio: no converted sounds dir '%s' (lower layers kept)", dir);
        return 0;
    }
    int total = 0;
    for (int e = 0; e < 3; ++e) {
        for (int m = 0; m < AUDIO_MAT_COUNT; ++m) {
            const char *names[AUDIO_MAX_VARIANTS + 1];
            char variant[AUDIO_MAX_VARIANTS][64];
            for (int i = 0; i < AUDIO_MAX_VARIANTS; ++i) {
                int n = snprintf(variant[i], sizeof(variant[i]), "%s_%s%d", MC_MAT_EVENT[e],
                                 MC_MAT_INFIX[m], i + 1);
                names[i] = (n > 0 && (size_t)n < sizeof(variant[i])) ? variant[i] : "";
            }
            names[AUDIO_MAX_VARIANTS] = NULL;
            total += audio_load_set_from_dir(&sys->msets[e][m], dir, names);
        }
    }
    struct {
        AudioEvent ev;
        const char *const *names;
    } simple[] = {
        {AUDIO_ITEM_PICKUP, MC_SIMPLE_PICKUP}, {AUDIO_PLAYER_HURT, MC_SIMPLE_HURT},
        {AUDIO_PLAYER_DIE, MC_SIMPLE_DIE},     {AUDIO_UI_CLICK, MC_SIMPLE_CLICK},
        {AUDIO_TOOL_BREAK, MC_SIMPLE_TOOL_BREAK}, {AUDIO_EAT, MC_SIMPLE_EAT},
        {AUDIO_CRAFT, MC_SIMPLE_CRAFT},         {AUDIO_COW_HURT, MC_SIMPLE_COW_HURT},
        {AUDIO_COW_DIE, MC_SIMPLE_COW_DIE},
    };
    for (size_t i = 0; i < sizeof(simple) / sizeof(simple[0]); ++i) {
        total += audio_load_set_from_dir(&sys->sets[simple[i].ev], dir, simple[i].names);
    }
    LOG_INFO("audio: converted layer '%s' applied (%d variant files)", dir, total);
    return total;
}

/* Apply the owner-converted mcassets layer (once per process; missing
 * directory is normal on machines without mcassets, e.g. CI).
 */
static void audio_load_mcassets(AudioSystem *sys)
{
    if (sys->mc_loaded) {
        return;
    }
    char dir[PATH_MAX_LEN];
    if (path_mcassets_dir(dir, sizeof(dir), "generated/sounds") != 0 || !path_is_dir(dir)) {
        LOG_DEBUG("audio: no mcassets sounds dir (synth defaults kept)");
        return;
    }
    sys->mc_loaded = true;
    audio_load_mcassets_from(sys, dir);
}
/* Validate a pack name (plain directory names only: no separators,
 * dots, or drive specs — same traversal policy as tile overrides).
 */
static bool audio_pack_name_ok(const char *pack)
{
    if (pack == NULL || pack[0] == '\0') {
        return false;
    }
    size_t n = strlen(pack);
    if (n >= 64) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        char c = pack[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '-')) {
            return false;
        }
    }
    return true;
}

/* Load pack WAV overrides (missing/invalid files keep synth). */
int audio_load_pack(AudioSystem *sys, const char *pack)
{
    if (sys == NULL) {
        return -1;
    }
    const char *want = (pack != NULL) ? pack : "";
    if (strcmp(want, "Default") == 0) {
        want = ""; /* "Default" is the no-pack bank (synth + converted). */
    }
    if (strcmp(sys->pack, want) == 0) {
        return 0; /* Already loaded (including synth-only ""). */
    }
    /* The mixer callback reads the bank: hold the device lock across the
     * swap so a concurrent callback never sees freed samples. */
    if (sys->device != 0) {
        SDL_LockAudioDevice((SDL_AudioDeviceID)sys->device);
    }
    /* Restore synth defaults first, then overlay whatever parses.
     * NOTE: the synth restore wipes the converted layer too, so it is
     * re-applied on every path below (a previous revision skipped it for
     * "Default", silently reverting all sounds to synth on world open). */
    int rc = 0;
    if (audio_synth_bank(sys) != 0) {
        rc = -2;
    } else {
        sys->mc_loaded = false;
        audio_load_mcassets(sys);
        if (want[0] == '\0' || strcmp(want, "Default") == 0) {
            sys->pack[0] = '\0';
        } else if (!audio_pack_name_ok(want)) {
            LOG_WARN("audio: refusing pack name '%s'", want);
            sys->pack[0] = '\0';
        } else if (strlen(want) >= sizeof(sys->pack)) {
            sys->pack[0] = '\0';
        } else {
        size_t wl = strlen(want);
        memcpy(sys->pack, want, wl + 1);
        /* Owner-converted layer first (missing dir is normal elsewhere). */
        audio_load_mcassets(sys);
        for (int i = 0; i < AUDIO_EVENT_COUNT; ++i) {
            const char *stem = audio_event_stem((AudioEvent)i);
            if (stem == NULL) {
                continue;
            }
            char rel[128];
            int n = snprintf(rel, sizeof(rel), "resourcepacks/%s/sounds/%s.wav", want, stem);
            if (n <= 0 || (size_t)n >= sizeof(rel)) {
                continue;
            }
            int16_t *pcm = NULL;
            size_t frames = 0;
            if (audio_load_wav_file(rel, &pcm, &frames) != 0) {
                continue; /* Missing override: keep lower layers, quietly. */
            }
            /* A pack stem replaces the whole event: the generic set plus,
             * for material events, every material variant (documented). */
            AudioSet single;
            memset(&single, 0, sizeof(single));
            single.pcm[0] = pcm;
            single.frames[0] = frames;
            single.count = 1;
            audio_free_set(&sys->sets[i]);
            sys->sets[i] = single;
            if (i == AUDIO_BLOCK_BREAK || i == AUDIO_BLOCK_PLACE || i == AUDIO_STEP_STONE ||
                i == AUDIO_STEP_DIRT || i == AUDIO_STEP_WOOD || i == AUDIO_STEP_SAND) {
                int row = audio_mat_row((AudioEvent)i);
                for (int m = 0; m < AUDIO_MAT_COUNT; ++m) {
                    int16_t *copy = NULL;
                    size_t cframes = 0;
                    if (audio_load_wav_file(rel, &copy, &cframes) != 0) {
                        break;
                    }
                    audio_free_set(&sys->msets[row][m]);
                    sys->msets[row][m].pcm[0] = copy;
                    sys->msets[row][m].frames[0] = cframes;
                    sys->msets[row][m].count = 1;
                }
            }
            LOG_INFO("audio: pack override '%s' (%s)", rel, stem);
        }
    }
    }
    /* The bank buffers above were freed and replaced: any voice still
     * holding the old pointers would play freed (and quickly recycled)
     * heap — this once surfaced as random wrong sounds after menu
     * clicks. Cut every voice under the lock; callers retrigger. */
    for (int v = 0; v < AUDIO_MAX_VOICES; ++v) {
        sys->voices[v].active = false;
    }
    if (sys->device != 0) {
        SDL_UnlockAudioDevice((SDL_AudioDeviceID)sys->device);
    }
    return rc;
}

/* WAV chunk helpers (little-endian). */
static uint32_t wav_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t wav_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Parse WAV -> 22050 Hz mono S16 (see header for the contract). */
int audio_parse_wav(const unsigned char *data, size_t n, int16_t **out_pcm, size_t *out_frames)
{
    if (data == NULL || n < 44 || n > WAV_MAX_BYTES || out_pcm == NULL || out_frames == NULL) {
        return -1;
    }
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        return -2;
    }
    /* Walk subchunks (fmt + data in any order, extra chunks skipped). */
    bool have_fmt = false;
    uint16_t channels = 0;
    uint32_t rate = 0;
    uint16_t bits = 0;
    const unsigned char *samples = NULL;
    size_t sample_bytes = 0;
    size_t off = 12;
    while (off + 8 <= n) {
        uint32_t id = wav_u32(data + off);
        uint32_t len = wav_u32(data + off + 4);
        /* id bytes as chars: 'fmt ' = 0x20746D66 LE, 'data' = 0x61746164. */
        size_t body = off + 8;
        if (body + len > n) {
            return -3; /* Truncated chunk. */
        }
        if (id == 0x20746D66u) { /* "fmt " */
            if (len < 16) {
                return -4;
            }
            uint16_t fmt = wav_u16(data + body);
            channels = wav_u16(data + body + 2);
            rate = wav_u32(data + body + 4);
            bits = wav_u16(data + body + 14);
            if (fmt != 1 || (channels != 1 && channels != 2) || rate == 0 || rate > 192000 ||
                (bits != 8 && bits != 16)) {
                return -5;
            }
            have_fmt = true;
        } else if (id == 0x61746164u) { /* "data" */
            samples = data + body;
            sample_bytes = len;
        }
        off = body + len + (len & 1u); /* Chunks pad to even sizes. */
    }
    if (!have_fmt || samples == NULL || sample_bytes == 0) {
        return -6;
    }
    size_t frame_bytes = (size_t)channels * (bits == 16 ? 2u : 1u);
    size_t in_frames = sample_bytes / frame_bytes;
    if (in_frames == 0) {
        return -6;
    }
    /* Resample to 22050 mono (linear interpolation). */
    size_t max_out = (size_t)WAV_MAX_SECONDS * (size_t)AUDIO_SAMPLE_RATE;
    double ratio = (double)rate / (double)AUDIO_SAMPLE_RATE;
    size_t want = (size_t)((double)in_frames / ratio + 0.5);
    if (want == 0) {
        want = 1;
    }
    if (want > max_out) {
        want = max_out;
    }
    int16_t *pcm = (int16_t *)malloc(want * sizeof(int16_t));
    if (pcm == NULL) {
        return -7;
    }
    for (size_t o = 0; o < want; ++o) {
        double src = (double)o * ratio;
        size_t i0 = (size_t)src;
        double frac = src - (double)i0;
        if (i0 >= in_frames) {
            i0 = in_frames - 1;
            frac = 0.0;
        }
        size_t i1 = i0 + 1 < in_frames ? i0 + 1 : i0;
        double v0 = 0.0;
        double v1 = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            size_t b0 = (i0 * (size_t)channels + (size_t)ch) * (bits == 16 ? 2u : 1u);
            size_t b1 = (i1 * (size_t)channels + (size_t)ch) * (bits == 16 ? 2u : 1u);
            double s0;
            double s1;
            if (bits == 16) {
                /* Manual sign extension (avoids implementation-defined
                 * unsigned->signed narrowing). */
                int32_t a0 = (int32_t)wav_u16(samples + b0);
                int32_t a1 = (int32_t)wav_u16(samples + b1);
                if (a0 >= 32768) {
                    a0 -= 65536;
                }
                if (a1 >= 32768) {
                    a1 -= 65536;
                }
                s0 = (double)a0 / 32768.0;
                s1 = (double)a1 / 32768.0;
            } else {
                s0 = ((double)samples[b0] - 128.0) / 128.0;
                s1 = ((double)samples[b1] - 128.0) / 128.0;
            }
            v0 += s0;
            v1 += s1;
        }
        v0 /= (double)channels;
        v1 /= (double)channels;
        double v = v0 + (v1 - v0) * frac;
        if (v > 1.0) {
            v = 1.0;
        }
        if (v < -1.0) {
            v = -1.0;
        }
        pcm[o] = (int16_t)(v * 32767.0);
    }
    *out_pcm = pcm;
    *out_frames = want;
    return 0;
}
