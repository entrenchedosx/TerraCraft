#include "test_main.h"

#include "audio/audio.h"
#include "core/path.h"
#include "game/item.h"
#include "game/particle.h"
#include "world/block.h"

#include <stdio.h>
#include <string.h>

/* Portable SDL include (for the dummy-audio-driver test only). */
#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#elif __has_include(<SDL.h>)
#include <SDL.h>
#endif
#else
#include <SDL2/SDL.h>
#endif

/* Test: spawn bound, expiry, slot reuse.
 *
 * Returns: failure count.
 */
int test_particle_pool(void)
{
    int failures = 0;
    ParticlePool pool;
    particle_pool_clear(&pool);
    TEST_ASSERT(particle_active_count(&pool) == 0);
    Vec3 at = mmath_vec3(0.0f, 70.0f, 0.0f);
    Vec3 vel = mmath_vec3(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < PARTICLE_MAX; ++i) {
        TEST_ASSERT(particle_spawn(&pool, at, vel, 1.0f, 0.1f, 1.0f, 1.0f, 1.0f, 3, 9.0f) >= 0);
    }
    TEST_ASSERT(particle_active_count(&pool) == PARTICLE_MAX);
    TEST_ASSERT(particle_spawn(&pool, at, vel, 1.0f, 0.1f, 1.0f, 1.0f, 1.0f, 3, 9.0f) < 0);
    /* Age out everything (updates clamp dt to 0.25 s slices, so pump). */
    for (int i = 0; i < 8; ++i) {
        particle_update(&pool, 0.25f);
    }
    TEST_ASSERT(particle_active_count(&pool) == 0);
    TEST_ASSERT(particle_spawn(&pool, at, vel, 1.0f, 0.1f, 1.0f, 1.0f, 1.0f, 3, 9.0f) >= 0);
    /* Invalid requests rejected safely. */
    TEST_ASSERT(particle_spawn(NULL, at, vel, 1.0f, 0.1f, 1.0f, 1.0f, 1.0f, 3, 9.0f) < 0);
    TEST_ASSERT(particle_spawn(&pool, at, vel, 0.0f, 0.1f, 1.0f, 1.0f, 1.0f, 3, 9.0f) < 0);
    TEST_ASSERT(particle_spawn(&pool, at, vel, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 3, 9.0f) < 0);
    TEST_ASSERT(particle_burst_block(NULL, (uint16_t)BLOCK_STONE, 0, 64, 0, 4) == 0);
    TEST_ASSERT(particle_burst_block(&pool, (uint16_t)BLOCK_STONE, 0, 64, 0, 0) == 0);
    TEST_ASSERT(particle_burst_item(&pool, 9999, at, 4) == 0);
    particle_update(NULL, 1.0f);
    particle_update(&pool, 0.0f);
    particle_update(&pool, -1.0f);
    TEST_ASSERT(particle_active_count(NULL) == 0);
    particle_pool_clear(NULL);
    return failures;
}

/* Test: gravity integrates, bursts are deterministic, block/item colors.
 *
 * Returns: failure count.
 */
int test_particle_sim(void)
{
    int failures = 0;
    ParticlePool pool;
    particle_pool_clear(&pool);
    Vec3 at = mmath_vec3(0.0f, 70.0f, 0.0f);
    /* Gravity pulls velocity down (semi-implicit Euler, 0.25 s steps). */
    int idx = particle_spawn(&pool, at, mmath_vec3(0.0f, 0.0f, 0.0f), 2.0f, 0.1f, 1.0f, 1.0f, 1.0f,
                             3, 10.0f);
    TEST_ASSERT(idx >= 0);
    particle_update(&pool, 0.25f);
    particle_update(&pool, 0.25f);
    TEST_ASSERT(pool.items[idx].active);
    TEST_ASSERT_FLOAT_EQ(pool.items[idx].vel.y, -5.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(pool.items[idx].pos.y, 68.125f, 1e-4f);
    /* Block burst: block color, requested count. */
    particle_pool_clear(&pool);
    TEST_ASSERT(particle_burst_block(&pool, (uint16_t)BLOCK_STONE, 4, 64, 4, 12) == 12);
    TEST_ASSERT_FLOAT_EQ(pool.items[0].r, 0.5f, 1e-4f);
    /* Determinism: two fresh pools spray identically. */
    ParticlePool other;
    particle_pool_clear(&other);
    particle_burst_block(&other, (uint16_t)BLOCK_STONE, 4, 64, 4, 12);
    int same = 1;
    for (int i = 0; i < PARTICLE_MAX; ++i) {
        if ((pool.items[i].active != other.items[i].active) ||
            (pool.items[i].active &&
             (pool.items[i].vel.x != other.items[i].vel.x || pool.items[i].vel.y != other.items[i].vel.y ||
              pool.items[i].vel.z != other.items[i].vel.z))) {
            same = 0;
            break;
        }
    }
    TEST_ASSERT(same == 1);
    /* Item burst carries the item tile. */
    particle_pool_clear(&pool);
    TEST_ASSERT(particle_burst_item(&pool, ITEM_COAL, at, 4) == 4);
    TEST_ASSERT(pool.items[0].tile == item_get_info(ITEM_COAL)->tile);
    return failures;
}

/* Test: event stems cover every event; volumes clamp; dead system is safe.
 *
 * Returns: failure count.
 */
int test_audio_basics(void)
{
    int failures = 0;
    TEST_ASSERT(audio_event_stem(AUDIO_NONE) == NULL);
    for (int i = 1; i < (int)AUDIO_EVENT_COUNT; ++i) {
        const char *stem = audio_event_stem((AudioEvent)i);
        TEST_ASSERT(stem != NULL && stem[0] != '\0');
    }
    TEST_ASSERT(audio_event_stem(AUDIO_EVENT_COUNT) == NULL);
    AudioSystem sys;
    memset(&sys, 0, sizeof(sys));
    TEST_ASSERT(audio_is_ready(&sys) == false);
    TEST_ASSERT(audio_is_ready(NULL) == false);
    audio_play(NULL, AUDIO_BLOCK_BREAK);
    audio_play(&sys, AUDIO_BLOCK_BREAK); /* Not ready: silent no-op. */
    audio_play(&sys, AUDIO_NONE);
    audio_play(&sys, AUDIO_EVENT_COUNT);
    audio_set_volumes(NULL, 1.0f, 1.0f);
    audio_set_volumes(&sys, -1.0f, 2.0f);
    TEST_ASSERT_FLOAT_EQ(sys.master, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(sys.sfx, 1.0f, 1e-6f);
    audio_set_volumes(&sys, 0.5f, 0.25f);
    TEST_ASSERT_FLOAT_EQ(sys.master, 0.5f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(sys.sfx, 0.25f, 1e-6f);
    TEST_ASSERT(audio_init(NULL) != 0);
    audio_shutdown(NULL);
    TEST_ASSERT(audio_load_pack(NULL, "Default") != 0);
    TEST_ASSERT(audio_load_pack(&sys, "Default") == 0);
    TEST_ASSERT(audio_load_pack(&sys, "../evil") == 0); /* Refused, synth kept. */
    TEST_ASSERT(audio_parse_wav(NULL, 100, NULL, NULL) != 0);
    return failures;
}

/* Build a minimal WAV buffer (PCM 8/16-bit, mono/stereo, any rate). */
static size_t fx_wav_build(unsigned char *dst, size_t cap, uint16_t bits, uint16_t ch, uint32_t rate,
                           int nsamples)
{
    size_t frame_bytes = (size_t)ch * (bits == 16 ? 2u : 1u);
    size_t data_bytes = (size_t)nsamples * frame_bytes;
    size_t total = 12 + 8 + 16 + 8 + data_bytes;
    if (total > cap) {
        return 0;
    }
    memcpy(dst, "RIFF", 4);
    dst[4] = (unsigned char)((total - 8) & 0xFFu);
    dst[5] = (unsigned char)(((total - 8) >> 8) & 0xFFu);
    dst[6] = (unsigned char)(((total - 8) >> 16) & 0xFFu);
    dst[7] = (unsigned char)(((total - 8) >> 24) & 0xFFu);
    memcpy(dst + 8, "WAVE", 4);
    memcpy(dst + 12, "fmt ", 4);
    dst[16] = 16;
    dst[17] = 0;
    dst[18] = 0;
    dst[19] = 0;
    dst[20] = 1; /* PCM. */
    dst[21] = 0;
    dst[22] = (unsigned char)ch;
    dst[23] = 0;
    dst[24] = (unsigned char)(rate & 0xFFu);
    dst[25] = (unsigned char)((rate >> 8) & 0xFFu);
    dst[26] = (unsigned char)((rate >> 16) & 0xFFu);
    dst[27] = (unsigned char)((rate >> 24) & 0xFFu);
    uint32_t byte_rate = rate * (uint32_t)ch * (bits == 16 ? 2u : 1u);
    dst[28] = (unsigned char)(byte_rate & 0xFFu);
    dst[29] = (unsigned char)((byte_rate >> 8) & 0xFFu);
    dst[30] = (unsigned char)((byte_rate >> 16) & 0xFFu);
    dst[31] = (unsigned char)((byte_rate >> 24) & 0xFFu);
    dst[32] = (unsigned char)frame_bytes;
    dst[33] = 0;
    dst[34] = (unsigned char)bits;
    dst[35] = 0;
    memcpy(dst + 36, "data", 4);
    dst[40] = (unsigned char)(data_bytes & 0xFFu);
    dst[41] = (unsigned char)((data_bytes >> 8) & 0xFFu);
    dst[42] = (unsigned char)((data_bytes >> 16) & 0xFFu);
    dst[43] = (unsigned char)((data_bytes >> 24) & 0xFFu);
    for (int i = 0; i < nsamples; ++i) {
        for (int c = 0; c < ch; ++c) {
            size_t o = 44 + ((size_t)i * (size_t)ch + (size_t)c) * (bits == 16 ? 2u : 1u);
            if (bits == 16) {
                int16_t s = (int16_t)(i * 1000 - 20000);
                dst[o] = (unsigned char)(s & 0xFF);
                dst[o + 1] = (unsigned char)((s >> 8) & 0xFF);
            } else {
                dst[o] = (unsigned char)((i * 7) & 0xFF);
            }
        }
    }
    return total;
}

/* Test: WAV parsing (8/16-bit, mono/stereo, resample) + safe rejection.
 *
 * Returns: failure count.
 */
int test_audio_wav(void)
{
    int failures = 0;
    unsigned char buf[8192];
    int16_t *pcm = NULL;
    size_t frames = 0;
    /* 16-bit mono 8 kHz, 80 samples -> ~220 resampled frames. */
    size_t n = fx_wav_build(buf, sizeof(buf), 16, 1, 8000, 80);
    TEST_ASSERT(n > 0);
    TEST_ASSERT(audio_parse_wav(buf, n, &pcm, &frames) == 0);
    TEST_ASSERT(frames >= 200 && frames <= 240);
    TEST_ASSERT(pcm != NULL);
    if (pcm != NULL) {
        int nonzero = 0;
        for (size_t i = 0; i < frames; ++i) {
            if (pcm[i] != 0) {
                nonzero = 1;
                break;
            }
        }
        TEST_ASSERT(nonzero == 1);
        free(pcm);
        pcm = NULL;
    }
    /* 8-bit stereo 44100 Hz parses too. */
    n = fx_wav_build(buf, sizeof(buf), 8, 2, 44100, 100);
    TEST_ASSERT(n > 0);
    TEST_ASSERT(audio_parse_wav(buf, n, &pcm, &frames) == 0);
    TEST_ASSERT(frames > 0);
    free(pcm);
    pcm = NULL;
    /* Rejections: bad magic, float format, truncation, oversize claim. */
    unsigned char bad[64];
    memset(bad, 0, sizeof(bad));
    TEST_ASSERT(audio_parse_wav(bad, sizeof(bad), &pcm, &frames) != 0);
    n = fx_wav_build(buf, sizeof(buf), 16, 1, 8000, 40);
    buf[20] = 3; /* IEEE float: unsupported. */
    TEST_ASSERT(audio_parse_wav(buf, n, &pcm, &frames) != 0);
    n = fx_wav_build(buf, sizeof(buf), 16, 1, 8000, 40);
    TEST_ASSERT(audio_parse_wav(buf, n - 7, &pcm, &frames) != 0);
    TEST_ASSERT(audio_parse_wav(buf, 4u * 1024u * 1024u + 1u, &pcm, &frames) != 0);
    TEST_ASSERT(pcm == NULL);
    return failures;
}

/* Test: full init/play/shutdown against the SDL dummy driver (no hardware).
 *
 * Returns: failure count.
 */
int test_audio_dummy(void)
{
    int failures = 0;
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    AudioSystem sys;
    memset(&sys, 0, sizeof(sys)); /* init reads device: never pass garbage. */
    int rc = audio_init(&sys);
    TEST_ASSERT(rc == 0);
    if (rc == 0) {
        for (int i = 1; i < (int)AUDIO_EVENT_COUNT; ++i) {
            audio_play(&sys, (AudioEvent)i);
        }
        TEST_ASSERT(audio_load_pack(&sys, "Default") == 0);
        audio_shutdown(&sys);
        TEST_ASSERT(audio_is_ready(&sys) == false);
    }
    SDL_setenv("SDL_AUDIODRIVER", "", 1);
    return failures;
}

/* Test: pack no-op on same effective pack + voice cut on real swap.
 * Regression: the settings screen re-applied every frame and every
 * world open rebuilt the bank ("Default" never matched ""), freeing
 * samples under live voices — menu clicks surfaced as random wrong
 * sounds (e.g. phantom eating) from recycled heap.
 *
 * Returns: failure count.
 */
int test_audio_pack_swap(void)
{
    int failures = 0;
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    AudioSystem sys;
    memset(&sys, 0, sizeof(sys));
    int rc = audio_init(&sys);
    TEST_ASSERT(rc == 0);
    if (rc != 0) {
        SDL_setenv("SDL_AUDIODRIVER", "", 1);
        return failures + 1;
    }
    /* Same effective pack ("Default"/""/NULL) never rebuilds: buffer
     * addresses and variant counts are stable across repeats. */
    int16_t *eat_before = sys.sets[AUDIO_EAT].pcm[0];
    size_t eat_frames = sys.sets[AUDIO_EAT].frames[0];
    int eat_count = sys.sets[AUDIO_EAT].count;
    TEST_ASSERT(eat_before != NULL && eat_count >= 1);
    TEST_ASSERT(audio_load_pack(&sys, "Default") == 0);
    TEST_ASSERT(audio_load_pack(&sys, "") == 0);
    TEST_ASSERT(audio_load_pack(&sys, NULL) == 0);
    TEST_ASSERT(sys.sets[AUDIO_EAT].pcm[0] == eat_before);
    TEST_ASSERT(sys.sets[AUDIO_EAT].frames[0] == eat_frames);
    TEST_ASSERT(sys.sets[AUDIO_EAT].count == eat_count);
    /* A no-op swap never touches live voices. */
    SDL_PauseAudioDevice((SDL_AudioDeviceID)sys.device, 1);
    sys.voices[0].active = true;
    sys.voices[0].data = eat_before;
    sys.voices[0].frames = eat_frames;
    sys.voices[0].pos = 0;
    sys.voices[0].vol = 1.0f;
    TEST_ASSERT(audio_load_pack(&sys, "Default") == 0);
    TEST_ASSERT(sys.voices[0].active == true);
    TEST_ASSERT(sys.sets[AUDIO_EAT].pcm[0] == eat_before);
    /* A real swap cuts every live voice (no stale sample pointers). */
    TEST_ASSERT(audio_load_pack(&sys, "../evil") == 0); /* Refused, but rebuilt. */
    for (int v = 0; v < AUDIO_MAX_VOICES; ++v) {
        TEST_ASSERT(sys.voices[v].active == false);
    }
    /* Bank still functional after the swap. */
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_EAT, 0) >= 1);
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_BLOCK_BREAK, AUDIO_MAT_STONE) >= 1);
    /* Cow events exist with synth fallback even without converted files. */
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_COW_HURT, 0) >= 1);
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_COW_DIE, 0) >= 1);
    TEST_ASSERT(audio_event_stem(AUDIO_COW_HURT) != NULL);
    TEST_ASSERT(audio_event_stem(AUDIO_COW_DIE) != NULL);
    SDL_PauseAudioDevice((SDL_AudioDeviceID)sys.device, 0);
    audio_shutdown(&sys);
    SDL_setenv("SDL_AUDIODRIVER", "", 1);
    return failures;
}

/* Test: block material classes + converted-layer loading counts.
 *
 * Returns: failure count.
 */
int test_audio_material(void)
{
    int failures = 0;
    TEST_ASSERT(audio_block_material(BLOCK_STONE) == AUDIO_MAT_STONE);
    TEST_ASSERT(audio_block_material(BLOCK_COAL_ORE) == AUDIO_MAT_STONE);
    TEST_ASSERT(audio_block_material(BLOCK_BEDROCK) == AUDIO_MAT_STONE);
    TEST_ASSERT(audio_block_material(BLOCK_GRASS) == AUDIO_MAT_GRASS);
    TEST_ASSERT(audio_block_material(BLOCK_SNOW) == AUDIO_MAT_SNOW);
    TEST_ASSERT(audio_block_material(BLOCK_LEAVES) == AUDIO_MAT_GRASS);
    TEST_ASSERT(audio_block_material(BLOCK_GRASS_PLANT) == AUDIO_MAT_GRASS);
    TEST_ASSERT(audio_block_material(BLOCK_FLOWER) == AUDIO_MAT_GRASS);
    TEST_ASSERT(audio_block_material(BLOCK_DIRT) == AUDIO_MAT_GRAVEL);
    TEST_ASSERT(audio_block_material(BLOCK_WOOD) == AUDIO_MAT_WOOD);
    TEST_ASSERT(audio_block_material(BLOCK_WORKBENCH) == AUDIO_MAT_WOOD);
    TEST_ASSERT(audio_block_material(BLOCK_PLANKS) == AUDIO_MAT_WOOD);
    TEST_ASSERT(audio_block_material(BLOCK_TORCH) == AUDIO_MAT_WOOD);
    TEST_ASSERT(audio_block_material(BLOCK_SAND) == AUDIO_MAT_SAND);
    TEST_ASSERT(audio_block_material(BLOCK_GLASS) == AUDIO_MAT_GLASS);
    TEST_ASSERT(audio_block_material(BLOCK_AIR) == AUDIO_MAT_STONE);
    TEST_ASSERT(audio_block_material(9999) == AUDIO_MAT_STONE);

    /* Empty bank reports zero variants (NULL/bad args safe). */
    AudioSystem sys;
    memset(&sys, 0, sizeof(sys));
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_BLOCK_BREAK, AUDIO_MAT_STONE) == 0);
    TEST_ASSERT(audio_bank_variants(NULL, AUDIO_BLOCK_BREAK, AUDIO_MAT_STONE) == 0);
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_NONE, 0) == 0);
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_ITEM_PICKUP, 99) == 0);

    /* Temp converted dir: 2 stone break variants + pickup. */
    const char *dir = "test_tmp_mcsnd";
    TEST_ASSERT(path_mkdir_p(dir) == 0);
    unsigned char wav[512];
    char full[PATH_MAX_LEN];
    size_t n = fx_wav_build(wav, sizeof(wav), 16, 1, 8000, 40);
    TEST_ASSERT(n > 0);
    const char *files[3] = {"break_stone1.wav", "break_stone2.wav", "pickup.wav"};
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT(path_join(full, sizeof(full), dir, files[i]) == 0);
        FILE *f = fopen(full, "wb");
        TEST_ASSERT(f != NULL);
        if (f == NULL) {
            continue;
        }
        TEST_ASSERT(fwrite(wav, 1, n, f) == n);
        fclose(f);
    }
    TEST_ASSERT(audio_load_mcassets_from(&sys, dir) == 3);
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_BLOCK_BREAK, AUDIO_MAT_STONE) == 2);
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_BLOCK_PLACE, AUDIO_MAT_STONE) == 0);
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_ITEM_PICKUP, 0) == 1);
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_BLOCK_BREAK, AUDIO_MAT_WOOD) == 0);
    /* Missing/NULL dir keeps lower layers (0 loaded). */
    TEST_ASSERT(audio_load_mcassets_from(&sys, "test_tmp_nope_xyz") == 0);
    TEST_ASSERT(audio_load_mcassets_from(NULL, dir) == 0);
    TEST_ASSERT(audio_load_mcassets_from(&sys, NULL) == 0);
    /* Pack switch with a refused name still rebuilds the bank from the
     * lower layers (restoring the converted layer, not stranding synth):
     * a past revision wiped it on world open. Expected count follows the
     * converted dir the loader itself resolves (exe-relative first, so it
     * agrees under ctest's build/ CWD too; 4 variants locally, 1 synth
     * fallback without it). */
    char mcdir[PATH_MAX_LEN];
    int fixed = (path_mcassets_dir(mcdir, sizeof(mcdir), "generated/sounds") == 0 &&
                 path_is_dir(mcdir))
                    ? 4
                    : 1;
    TEST_ASSERT(audio_load_pack(&sys, "../evil") == 0); /* Refused, but rebuilt. */
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_BLOCK_BREAK, AUDIO_MAT_STONE) == fixed);
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_ITEM_PICKUP, 0) >= 1);
    audio_shutdown(&sys); /* Frees the loaded bank (device never opened). */
    TEST_ASSERT(audio_bank_variants(&sys, AUDIO_BLOCK_BREAK, AUDIO_MAT_STONE) == 0);
    for (int i = 0; i < 3; ++i) {
        if (path_join(full, sizeof(full), dir, files[i]) == 0) {
            path_remove_file(full);
        }
    }
    path_remove_dir(dir);
    return failures;
}
