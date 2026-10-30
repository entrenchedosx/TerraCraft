#include "test_main.h"
#include "core/bmp.h"
#include "core/path.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define menu_getcwd _getcwd
#define menu_chdir _chdir
#else
#include <unistd.h>
#define menu_getcwd getcwd
#define menu_chdir chdir
#endif

static void menu_u32(unsigned char *out, uint32_t value)
{
    out[0] = (unsigned char)value;
    out[1] = (unsigned char)(value >> 8);
    out[2] = (unsigned char)(value >> 16);
    out[3] = (unsigned char)(value >> 24);
}

static void menu_bmp_header(unsigned char *out, int width, int height, size_t length)
{
    memset(out, 0, 54);
    out[0] = 'B';
    out[1] = 'M';
    menu_u32(out + 2, (uint32_t)length);
    menu_u32(out + 10, 54);
    menu_u32(out + 14, 40);
    menu_u32(out + 18, (uint32_t)width);
    menu_u32(out + 22, (uint32_t)height);
    out[26] = 1;
    out[28] = 32;
    menu_u32(out + 34, (uint32_t)(length - 54u));
}

static unsigned char *menu_native_fixture(size_t *length)
{
    *length = 54u + 1024u * 1024u * 4u;
    unsigned char *data = (unsigned char *)calloc(*length, 1);
    if (data == NULL) {
        return NULL;
    }
    menu_bmp_header(data, 1024, 1024, *length);
    /* First file row is the bottom of a positive-height BMP. */
    const unsigned char bottom[4] = {9, 8, 7, 6};
    const unsigned char top[4] = {2, 3, 4, 5};
    memcpy(data + 54, bottom, 4);
    memcpy(data + 54u + 1023u * 1024u * 4u, top, 4);
    return data;
}

int test_menu_native_bmp(void)
{
    int failures = 0;
    size_t length = 0;
    unsigned char *data = menu_native_fixture(&length);
    TEST_ASSERT(data != NULL);
    if (data == NULL) {
        return failures;
    }
    /* The valid native-size face exceeds the old 4 MiB cap by its header. */
    BmpImage image = {0};
    int rc = bmp_parse(data, length, &image);
    TEST_ASSERT(rc == 0);
    if (rc == 0) {
        TEST_ASSERT(image.width == 1024 && image.height == 1024);
        TEST_ASSERT(image.px[0] == 4 && image.px[1] == 3 && image.px[2] == 2 && image.px[3] == 5);
        size_t bottom = 1023u * 1024u * 4u;
        TEST_ASSERT(image.px[bottom] == 7 && image.px[bottom + 1] == 8 && image.px[bottom + 3] == 6);
        TEST_ASSERT(!bmp_is_tile(&image));
    }
    bmp_free(&image);
    /* Native top-down rows retain their orientation and alpha as well. */
    menu_u32(data + 22, (uint32_t)(int32_t)-1024);
    rc = bmp_parse(data, length, &image);
    TEST_ASSERT(rc == 0);
    if (rc == 0) {
        TEST_ASSERT(image.px[0] == 7 && image.px[1] == 8 && image.px[2] == 9 && image.px[3] == 6);
    }
    bmp_free(&image);
    menu_bmp_header(data, 1024, 1024, length);
    TEST_ASSERT(bmp_parse(data, length - 1, &image) != 0);
    TEST_ASSERT(image.px == NULL && image.width == 0 && image.height == 0);
    menu_u32(data + 10, 53); /* Pixel data cannot overlap the header. */
    TEST_ASSERT(bmp_parse(data, length, &image) != 0);
    bmp_free(&image);
    menu_u32(data + 10, UINT32_MAX);
    TEST_ASSERT(bmp_parse(data, length, &image) != 0);
    bmp_free(&image);
    menu_bmp_header(data, 1024, 1024, length);
    menu_u32(data + 22, UINT32_C(0x80000000)); /* INT32_MIN must not be negated. */
    TEST_ASSERT(bmp_parse(data, length, &image) != 0);
    bmp_free(&image);
    menu_bmp_header(data, 4097, 1024, length);
    TEST_ASSERT(bmp_parse(data, length, &image) != 0);
    bmp_free(&image);
    free(data);

    /* Dimensions are individually allowed, but the allocation/file size is
     * beyond the documented native-menu bound (16 MiB plus header slack). */
    size_t oversized_length = 54u + 4096u * 1025u * 4u;
    unsigned char *oversized = (unsigned char *)calloc(oversized_length, 1);
    TEST_ASSERT(oversized != NULL);
    if (oversized != NULL) {
        menu_bmp_header(oversized, 4096, 1025, oversized_length);
        TEST_ASSERT(bmp_parse(oversized, oversized_length, &image) != 0);
        TEST_ASSERT(image.px == NULL);
        bmp_free(&image);
        free(oversized);
    }
    return failures;
}

int test_menu_asset_file_fallback(void)
{
    int failures = 0;
    char original[PATH_MAX_LEN], scratch[PATH_MAX_LEN], assets[PATH_MAX_LEN];
    char generated[PATH_MAX_LEN], menu[PATH_MAX_LEN], file[PATH_MAX_LEN];
    const char *relative = "generated/menu/test_tmp_foundation_cwd_only.bmp";
    bool have_cwd = menu_getcwd(original, sizeof(original)) != NULL;
    TEST_ASSERT(have_cwd);
    if (!have_cwd) {
        return failures;
    }
    bool paths_valid = path_join(scratch, sizeof(scratch), original, "test_tmp_menu_bitmap_cwd") == 0 &&
                       path_join(assets, sizeof(assets), scratch, "mcassets") == 0 &&
                       path_join(generated, sizeof(generated), assets, "generated") == 0 &&
                       path_join(menu, sizeof(menu), generated, "menu") == 0 &&
                       path_join(file, sizeof(file), assets, relative) == 0;
    TEST_ASSERT(paths_valid);
    if (!paths_valid) {
        return failures;
    }
    /* Never create or change an executable-relative asset file. A missing
     * unique leaf must be looked up in the isolated working-directory root. */
    char exe[PATH_MAX_LEN], exe_assets[PATH_MAX_LEN], exe_file[PATH_MAX_LEN];
    if (path_exe_dir(exe, sizeof(exe)) == 0 &&
        path_join(exe_assets, sizeof(exe_assets), exe, "mcassets") == 0 &&
        path_join(exe_file, sizeof(exe_file), exe_assets, relative) == 0) {
        bool absent = !path_is_file(exe_file);
        TEST_ASSERT(absent);
        if (!absent) {
            return failures;
        }
    }
    int mkdir_result = path_mkdir_p(menu);
    TEST_ASSERT(mkdir_result == 0);
    if (mkdir_result != 0) {
        return failures;
    }
    size_t length = 0;
    unsigned char *data = menu_native_fixture(&length);
    TEST_ASSERT(data != NULL);
    FILE *output = data != NULL ? fopen(file, "wb") : NULL;
    TEST_ASSERT(output != NULL);
    bool written = false;
    if (output != NULL) {
        written = fwrite(data, 1, length, output) == length;
        int close_result = fclose(output);
        TEST_ASSERT(written && close_result == 0);
        written = written && close_result == 0;
    }
    free(data);
    if (written) {
        int changed = menu_chdir(scratch);
        TEST_ASSERT(changed == 0);
        if (changed == 0) {
            BmpImage image = {0};
            int result = bmp_load_mcasset(relative, 1024, 1024, &image);
            TEST_ASSERT(result == 0);
            if (result == 0) {
                TEST_ASSERT(image.width == 1024 && image.height == 1024 && image.px[0] == 4);
            }
            bmp_free(&image);
            TEST_ASSERT(bmp_load_mcasset(relative, 16, 16, &image) != 0);
            TEST_ASSERT(image.px == NULL);
            bmp_free(&image);
            TEST_ASSERT(bmp_load_mcasset(relative, 0, 0, &image) == 0);
            TEST_ASSERT(image.width == 1024 && image.height == 1024);
            bmp_free(&image);
            TEST_ASSERT(bmp_load_mcasset("../forbidden.bmp", 0, 0, &image) != 0);
            int restored = menu_chdir(original);
            TEST_ASSERT(restored == 0);
        }
    }
    TEST_ASSERT(path_remove_file(file) == 0);
    TEST_ASSERT(path_remove_dir(menu) == 0);
    TEST_ASSERT(path_remove_dir(generated) == 0);
    TEST_ASSERT(path_remove_dir(assets) == 0);
    TEST_ASSERT(path_remove_dir(scratch) == 0);
    return failures;
}
