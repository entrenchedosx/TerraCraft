#include "test_main.h"

#include "core/path.h"
#include "core/profile.h"

#include <stdio.h>
#include <string.h>

int test_profile_name_validation(void)
{
    int failures = 0;
    TEST_ASSERT(profile_name_valid("Alex_42"));
    TEST_ASSERT(profile_name_valid("abc"));
    TEST_ASSERT(profile_name_valid("abcdefghijklmnopqrstuvwx"));
    TEST_ASSERT(!profile_name_valid(NULL));
    TEST_ASSERT(!profile_name_valid(""));
    TEST_ASSERT(!profile_name_valid("ab"));
    TEST_ASSERT(!profile_name_valid("abcdefghijklmnopqrstuvwxy"));
    TEST_ASSERT(!profile_name_valid("has space"));
    TEST_ASSERT(!profile_name_valid("bad-name"));
    TEST_ASSERT(!profile_name_valid("../profile"));
    TEST_ASSERT(!profile_name_valid("name\xC3\xA9"));
    return failures;
}

int test_profile_generated_name(void)
{
    int failures = 0;
    char first[PROFILE_NAME_MAX_LEN + 1];
    char second[PROFILE_NAME_MAX_LEN + 1];
    TEST_ASSERT(profile_generate_name(first, sizeof(first), UINT64_C(0x1234abcd)) == PROFILE_OK);
    TEST_ASSERT(profile_generate_name(second, sizeof(second), UINT64_C(0x1234abcd)) == PROFILE_OK);
    TEST_ASSERT(strcmp(first, second) == 0);
    TEST_ASSERT(profile_name_valid(first));

    char *separator = strchr(first, '_');
    TEST_ASSERT(separator != NULL);
    if (separator != NULL) {
        TEST_ASSERT(separator != first);
        TEST_ASSERT(strlen(separator + 1) >= 5);
        size_t len = strlen(first);
        TEST_ASSERT(len >= 5);
        for (size_t i = len - 4; i < len; ++i) {
            TEST_ASSERT(first[i] >= '0' && first[i] <= '9');
        }
        TEST_ASSERT(separator < first + len - 4);
    }

    char too_small[4] = "xxx";
    TEST_ASSERT(profile_generate_name(too_small, sizeof(too_small), 1) == PROFILE_ERR_BUFFER);
    TEST_ASSERT(too_small[0] == '\0');
    TEST_ASSERT(profile_generate_name(NULL, 0, 1) == PROFILE_ERR_ARGUMENT);
    return failures;
}

static int write_profile_text(const char *path, const char *contents)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    int rc = fputs(contents, f) < 0 ? -1 : 0;
    if (fclose(f) != 0) {
        rc = -1;
    }
    return rc;
}

int test_profile_storage(void)
{
    int failures = 0;
    const char *dir = "test_tmp_profile";
    const char *path = "test_tmp_profile/config/profile.cfg";
    char loaded[PROFILE_NAME_MAX_LEN + 1];
    char small[3] = "x";

    TEST_ASSERT(profile_load(path, loaded, sizeof(loaded)) == PROFILE_ERR_IO);
    TEST_ASSERT(loaded[0] == '\0');
    TEST_ASSERT(profile_load(NULL, loaded, sizeof(loaded)) == PROFILE_ERR_ARGUMENT);
    TEST_ASSERT(profile_load(path, NULL, 0) == PROFILE_ERR_ARGUMENT);

    TEST_ASSERT(profile_save(path, "Amber_Fox0427") == PROFILE_OK);
    TEST_ASSERT(profile_load(path, loaded, sizeof(loaded)) == PROFILE_OK);
    TEST_ASSERT(strcmp(loaded, "Amber_Fox0427") == 0);
    TEST_ASSERT(profile_load(path, small, sizeof(small)) == PROFILE_ERR_BUFFER);
    TEST_ASSERT(small[0] == '\0');

    /* Rejected names cannot overwrite a valid profile. */
    TEST_ASSERT(profile_save(path, "invalid name") == PROFILE_ERR_INVALID_NAME);
    TEST_ASSERT(profile_load(path, loaded, sizeof(loaded)) == PROFILE_OK);
    TEST_ASSERT(strcmp(loaded, "Amber_Fox0427") == 0);
    TEST_ASSERT(profile_save(NULL, "Alex") == PROFILE_ERR_ARGUMENT);
    TEST_ASSERT(profile_save(path, NULL) == PROFILE_ERR_ARGUMENT);

    TEST_ASSERT(write_profile_text(path, "# comment\r\nunknown=value\r\nusername=Bad-Name\r\n") == 0);
    TEST_ASSERT(profile_load(path, loaded, sizeof(loaded)) == PROFILE_ERR_INVALID_NAME);
    TEST_ASSERT(loaded[0] == '\0');
    TEST_ASSERT(write_profile_text(path, "# no account yet\nunknown=value\n") == 0);
    TEST_ASSERT(profile_load(path, loaded, sizeof(loaded)) == PROFILE_ERR_FORMAT);
    TEST_ASSERT(write_profile_text(path, "username=Alex\nusername=Other\n") == 0);
    TEST_ASSERT(profile_load(path, loaded, sizeof(loaded)) == PROFILE_ERR_FORMAT);

    TEST_ASSERT(path_remove_file(path) == 0);
    TEST_ASSERT(path_remove_dir("test_tmp_profile/config") == 0);
    TEST_ASSERT(path_remove_dir(dir) == 0);
    return failures;
}
