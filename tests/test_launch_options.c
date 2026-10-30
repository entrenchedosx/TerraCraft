#include "test_main.h"
#include "core/launch_options.h"

#include <stddef.h>

int test_launch_options(void)
{
    int failures = 0;
    int width = 0, height = 0;
    char *defaults[] = {"terracraft"};
    TEST_ASSERT(launch_options_parse(1, defaults, &width, &height) == 0);
    TEST_ASSERT(width == 1280 && height == 720);
    TEST_ASSERT(launch_options_parse(0, NULL, &width, &height) == 0);
    TEST_ASSERT(width == 1280 && height == 720);

    char *normal[] = {"terracraft", "--window-size", "1920x1080"};
    TEST_ASSERT(launch_options_parse(3, normal, &width, &height) == 0);
    TEST_ASSERT(width == 1920 && height == 1080);
    char *minimum[] = {"terracraft", "--window-size", "320x240"};
    TEST_ASSERT(launch_options_parse(3, minimum, &width, &height) == 0);
    TEST_ASSERT(width == 320 && height == 240);
    char *maximum[] = {"terracraft", "--window-size", "7680x4320"};
    TEST_ASSERT(launch_options_parse(3, maximum, &width, &height) == 0);
    TEST_ASSERT(width == 7680 && height == 4320);
    char *leading_zero[] = {"terracraft", "--window-size", "01920x01080"};
    TEST_ASSERT(launch_options_parse(3, leading_zero, &width, &height) == 0);
    TEST_ASSERT(width == 1920 && height == 1080);
    char *help[] = {"terracraft", "--help"};
    TEST_ASSERT(launch_options_parse(2, help, &width, &height) == 1);
    TEST_ASSERT(width == 1280 && height == 720);
    char *help_size[] = {"terracraft", "--help", "--window-size", "2560x1440"};
    TEST_ASSERT(launch_options_parse(4, help_size, &width, &height) == 1);
    TEST_ASSERT(width == 2560 && height == 1440);

    const char *invalid_dimensions[] = {
        "", "1920", "x1080", "1920x", "1920x1080x720", "1920x1080px", "1920X1080",
        "1920.0x1080", "1e3x720", " 1920x1080", "1920x1080 ", "+1920x1080", "-1920x1080",
        "1920x-1080", "0x720", "319x720", "1280x239", "7681x1080", "1920x4321",
        "999999999999999999999999x720", "1920x999999999999999999999999",
        "000000000000000000000000000000000000001280x720",
    };
    for (size_t i = 0; i < sizeof(invalid_dimensions) / sizeof(invalid_dimensions[0]); ++i) {
        char *args[] = {"terracraft", "--window-size", (char *)invalid_dimensions[i]};
        TEST_ASSERT(launch_options_parse(3, args, &width, &height) == -1);
        TEST_ASSERT(width == 1280 && height == 720);
    }
    char *missing[] = {"terracraft", "--window-size"};
    TEST_ASSERT(launch_options_parse(2, missing, &width, &height) == -1);
    char *unknown[] = {"terracraft", "--unknown"};
    TEST_ASSERT(launch_options_parse(2, unknown, &width, &height) == -1);
    char *extra[] = {"terracraft", "--window-size", "1920x1080", "extra"};
    TEST_ASSERT(launch_options_parse(4, extra, &width, &height) == -1);
    TEST_ASSERT(width == 1280 && height == 720);
    char *duplicate[] = {"terracraft", "--window-size", "1920x1080", "--window-size", "1280x720"};
    TEST_ASSERT(launch_options_parse(5, duplicate, &width, &height) == -1);
    char *help_unknown[] = {"terracraft", "--help", "--unknown"};
    TEST_ASSERT(launch_options_parse(3, help_unknown, &width, &height) == -1);
    char *duplicate_help[] = {"terracraft", "--help", "--help"};
    TEST_ASSERT(launch_options_parse(3, duplicate_help, &width, &height) == -1);
    char *null_arg[] = {"terracraft", "--window-size", NULL};
    TEST_ASSERT(launch_options_parse(3, null_arg, &width, &height) == -1);
    TEST_ASSERT(launch_options_parse(-1, defaults, &width, &height) == -1);
    TEST_ASSERT(launch_options_parse(2, NULL, &width, &height) == -1);
    TEST_ASSERT(launch_options_parse(1, defaults, NULL, &height) == -1);
    TEST_ASSERT(launch_options_parse(1, defaults, &width, NULL) == -1);
    TEST_ASSERT(launch_options_parse(1, defaults, &width, &width) == -1);
    return failures;
}
