#include "test_main.h"

#include "core/bmp.h"
#include "core/seed.h"
#include "core/settings.h"

#include <stdio.h>
#include <string.h>

/* Test: defaults and clamping.
 *
 * Returns: failure count.
 */
int test_settings_defaults(void)
{
    int failures = 0;
    Settings s;
    settings_defaults(&s);
    TEST_ASSERT(s.render_distance == 4);
    TEST_ASSERT_FLOAT_EQ(s.sensitivity, 0.0025f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(s.fov, 70.0f, 1e-5f);
    TEST_ASSERT(s.vsync == true);
    TEST_ASSERT(s.volume == 80);
    TEST_ASSERT(s.sfx_volume == 80);
    TEST_ASSERT(strcmp(s.pack, "Default") == 0);

    s.render_distance = 99;
    s.sensitivity = 5.0f;
    s.fov = -10.0f;
    s.volume = 1000;
    s.sfx_volume = -5;
    s.pack[0] = '\0';
    settings_clamp(&s);
    TEST_ASSERT(s.render_distance == 8);
    TEST_ASSERT_FLOAT_EQ(s.sensitivity, 0.010f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(s.fov, 60.0f, 1e-5f);
    TEST_ASSERT(s.volume == 100);
    TEST_ASSERT(s.sfx_volume == 0);
    TEST_ASSERT(strcmp(s.pack, "Default") == 0);

    s.render_distance = -3;
    s.volume = -1;
    settings_clamp(&s);
    TEST_ASSERT(s.render_distance == 2);
    TEST_ASSERT(s.volume == 0);
    settings_clamp(NULL); /* NULL-safe. */
    return failures;
}

/* Test: file parse tolerates comments, unknowns, and garbage.
 *
 * Returns: failure count.
 */
int test_settings_parse(void)
{
    int failures = 0;
    const char *path = "test_tmp_m5_settings.cfg";
    FILE *f = fopen(path, "w");
    TEST_ASSERT(f != NULL);
    if (f == NULL) {
        return failures + 1;
    }
    fprintf(f, "# comment\nrender_distance=6\nsensitivity=0.005\nfov=90\nvsync=0\nvolume=42\n");
    fprintf(f, "sfx_volume=33\n");
    fprintf(f, "pack=My Pack\nevil_key=1\ngarbage without equals\n");
    fclose(f);

    Settings s;
    TEST_ASSERT(settings_load(&s, path) == 0);
    TEST_ASSERT(s.render_distance == 6);
    TEST_ASSERT_FLOAT_EQ(s.sensitivity, 0.005f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(s.fov, 90.0f, 1e-4f);
    TEST_ASSERT(s.vsync == false);
    TEST_ASSERT(s.volume == 42);
    TEST_ASSERT(s.sfx_volume == 33);
    TEST_ASSERT(strcmp(s.pack, "My Pack") == 0);
    remove(path);

    /* Pre-M7 files omit sfx_volume: effects default to 80. */
    f = fopen(path, "w");
    TEST_ASSERT(f != NULL);
    if (f == NULL) {
        return failures + 1;
    }
    fprintf(f, "volume=42\n");
    fclose(f);
    TEST_ASSERT(settings_load(&s, path) == 0);
    TEST_ASSERT(s.volume == 42);
    TEST_ASSERT(s.sfx_volume == 80);
    remove(path);

    /* Missing file keeps defaults with an error code. */
    TEST_ASSERT(settings_load(&s, "test_tmp_m5_no_such_file.cfg") != 0);
    TEST_ASSERT(s.render_distance == 4);
    TEST_ASSERT(settings_load(NULL, path) != 0);
    return failures;
}

/* Test: save/load round trip preserves values.
 *
 * Returns: failure count.
 */
int test_settings_roundtrip(void)
{
    int failures = 0;
    const char *path = "test_tmp_m5_settings2.cfg";
    Settings s;
    settings_defaults(&s);
    s.render_distance = 7;
    s.sensitivity = 0.008f;
    s.fov = 100.0f;
    s.vsync = false;
    s.volume = 11;
    s.sfx_volume = 22;
    memcpy(s.pack, "Retro", 6);
    TEST_ASSERT(settings_save(&s, path) == 0);

    Settings back;
    memset(&back, 0, sizeof(back));
    TEST_ASSERT(settings_load(&back, path) == 0);
    TEST_ASSERT(back.render_distance == 7);
    TEST_ASSERT_FLOAT_EQ(back.sensitivity, 0.008f, 1e-6f);
    TEST_ASSERT_FLOAT_EQ(back.fov, 100.0f, 1e-4f);
    TEST_ASSERT(back.vsync == false);
    TEST_ASSERT(back.volume == 11);
    TEST_ASSERT(back.sfx_volume == 22);
    TEST_ASSERT(strcmp(back.pack, "Retro") == 0);
    remove(path);
    TEST_ASSERT(settings_save(NULL, path) != 0);
    return failures;
}

/* Test: seed field parsing (blank/int/hex/text, deterministic).
 *
 * Returns: failure count.
 */
int test_seed_parse(void)
{
    int failures = 0;
    bool blank = false;
    TEST_ASSERT(seed_parse("", &blank) == 0L && blank == true);
    TEST_ASSERT(seed_parse("   ", &blank) == 0L && blank == true);
    TEST_ASSERT(seed_parse(NULL, &blank) == 0L && blank == true);
    TEST_ASSERT(seed_parse("123456", &blank) == 123456L && blank == false);
    TEST_ASSERT(seed_parse("  -5  ", &blank) == -5L && blank == false);
    TEST_ASSERT(seed_parse("0x10", &blank) == 16L && blank == false);
    long t1 = seed_parse("Test", &blank);
    long t2 = seed_parse("Test", &blank);
    TEST_ASSERT(blank == false && t1 == t2 && t1 != 0L);
    TEST_ASSERT(seed_parse("Test2", &blank) != t1);
    /* FNV-1a known vector: empty string = offset basis. */
    TEST_ASSERT(seed_hash_text(NULL, 0) == 14695981039346656037ULL);
    TEST_ASSERT(seed_hash_text("", 0) == 14695981039346656037ULL);
    TEST_ASSERT(seed_hash_text("a", 1) != seed_hash_text("b", 1));
    return failures;
}

/* Build a minimal BMP in memory (headers + pixel rows, bottom-up). */
static size_t bmp_build(unsigned char *dst, size_t cap, int w, int h, int bpp, bool top_down)
{
    size_t stride = ((size_t)w * (size_t)(bpp / 8) + 3u) & ~(size_t)3u;
    size_t px = stride * (size_t)(h < 0 ? -h : h);
    size_t total = 54 + px;
    if (total > cap) {
        return 0;
    }
    memset(dst, 0, total);
    dst[0] = 'B';
    dst[1] = 'M';
    dst[10] = 54;
    dst[14] = 40;
    /* Full 4-byte LE width/height (negative height = top-down). */
    dst[18] = (unsigned char)(w & 0xFF);
    dst[19] = (unsigned char)((w >> 8) & 0xFF);
    dst[20] = (unsigned char)((w >> 16) & 0xFF);
    dst[21] = (unsigned char)((w >> 24) & 0xFF);
    int hs = top_down ? -h : h;
    dst[22] = (unsigned char)(hs & 0xFF);
    dst[23] = (unsigned char)((hs >> 8) & 0xFF);
    dst[24] = (unsigned char)((hs >> 16) & 0xFF);
    dst[25] = (unsigned char)((hs >> 24) & 0xFF);
    dst[26] = 1;
    dst[28] = (unsigned char)bpp;
    /* Pixel rows: marker pattern (row index in R, col in G, white B). */
    for (int y = 0; y < h; ++y) {
        int file_row = top_down ? y : (h - 1 - y);
        unsigned char *row = dst + 54 + (size_t)file_row * stride;
        for (int x = 0; x < w; ++x) {
            row[x * (bpp / 8) + 2] = (unsigned char)(y * 10);     /* R = top-down row. */
            row[x * (bpp / 8) + 1] = (unsigned char)(x * 10);     /* G = col. */
            row[x * (bpp / 8) + 0] = 200;                         /* B. */
            if (bpp == 32) {
                row[x * 4 + 3] = (unsigned char)(100 + x + y);    /* A. */
            }
        }
    }
    return total;
}

/* Test: BMP parser (24/32-bit, flip, alpha) and rejections.
 *
 * Returns: failure count.
 */
int test_bmp_parse(void)
{
    int failures = 0;
    unsigned char buf[4096];
    BmpImage img;
    img.px = NULL;

    /* 2x2 24-bit round trip: output must equal top-down content. */
    size_t n = bmp_build(buf, sizeof(buf), 2, 2, 24, false);
    TEST_ASSERT(n > 0);
    TEST_ASSERT(bmp_parse(buf, n, &img) == 0);
    TEST_ASSERT(img.width == 2 && img.height == 2 && img.px != NULL);
    /* Output (0,0) = top-down (0,0): R=0, G=0, B=200, A=255. */
    TEST_ASSERT(img.px[0] == 0 && img.px[1] == 0 && img.px[2] == 200 && img.px[3] == 255);
    /* Output (1,1) = top-down (1,1): R=10, G=10. */
    TEST_ASSERT(img.px[(1 * 2 + 1) * 4 + 0] == 10 && img.px[(1 * 2 + 1) * 4 + 1] == 10);
    TEST_ASSERT(bmp_is_tile(&img) == false); /* 2x2 is not a tile. */
    bmp_free(&img);

    /* 32-bit preserves alpha. */
    n = bmp_build(buf, sizeof(buf), 2, 2, 32, false);
    TEST_ASSERT(bmp_parse(buf, n, &img) == 0);
    TEST_ASSERT(img.px[3] == 100); /* Output (0,0) = top-down (0,0): A=100+0+0. */
    bmp_free(&img);

    /* Top-down flag honored. */
    n = bmp_build(buf, sizeof(buf), 2, 2, 24, true);
    TEST_ASSERT(bmp_parse(buf, n, &img) == 0);
    TEST_ASSERT(img.px[0] == 0); /* Output row 0 = file row 0 (R=0). */
    bmp_free(&img);

    /* Rejections. */
    TEST_ASSERT(bmp_parse(NULL, 100, &img) != 0);
    TEST_ASSERT(bmp_parse(buf, 10, &img) != 0); /* Truncated. */
    buf[0] = 'X';
    TEST_ASSERT(bmp_parse(buf, n, &img) != 0); /* Bad magic. */
    buf[0] = 'B';
    buf[28] = 8;
    TEST_ASSERT(bmp_parse(buf, n, &img) != 0); /* 8-bit unsupported. */
    buf[28] = 24;
    buf[30] = 1;
    TEST_ASSERT(bmp_parse(buf, n, &img) != 0); /* Compressed unsupported. */
    buf[30] = 0;
    bmp_parse(buf, n, &img); /* Re-parse valid for the file test below. */
    bmp_free(&img);
    TEST_ASSERT(bmp_load_file("test_tmp_m5_no_such.bmp", &img) != 0);
    TEST_ASSERT(bmp_is_tile(NULL) == false);
    bmp_free(NULL);
    return failures;
}
