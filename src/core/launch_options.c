#include "core/launch_options.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static bool launch_dimension(const char **cursor, int maximum, int *out)
{
    const char *text = *cursor;
    if (*text < '0' || *text > '9') {
        return false;
    }
    int value = 0;
    do {
        int digit = *text - '0';
        if (value > (maximum - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        ++text;
    } while (*text >= '0' && *text <= '9');
    *cursor = text;
    *out = value;
    return true;
}

static bool launch_window_size(const char *text, int *width, int *height)
{
    if (text == NULL) {
        return false;
    }
    size_t length = 0;
    while (length < 32u && text[length] != '\0') {
        ++length;
    }
    if (length == 0 || length >= 32u) {
        return false;
    }
    const char *cursor = text;
    int parsed_width = 0, parsed_height = 0;
    if (!launch_dimension(&cursor, 7680, &parsed_width) || *cursor != 'x') {
        return false;
    }
    ++cursor;
    if (!launch_dimension(&cursor, 4320, &parsed_height) || *cursor != '\0' || parsed_width < 320 ||
        parsed_height < 240) {
        return false;
    }
    *width = parsed_width;
    *height = parsed_height;
    return true;
}

int launch_options_parse(int argc, char **argv, int *width, int *height)
{
    if (width == NULL || height == NULL || width == height) {
        return -1;
    }
    *width = 1280;
    *height = 720;
    if (argc < 0 || (argc > 0 && argv == NULL)) {
        return -1;
    }
    int parsed_width = 1280, parsed_height = 720;
    bool size_seen = false, help_seen = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == NULL) {
            return -1;
        }
        if (strcmp(argv[i], "--help") == 0) {
            if (help_seen) {
                return -1;
            }
            help_seen = true;
        } else if (strcmp(argv[i], "--window-size") == 0) {
            if (size_seen || i + 1 >= argc || !launch_window_size(argv[i + 1], &parsed_width, &parsed_height)) {
                return -1;
            }
            size_seen = true;
            ++i;
        } else {
            return -1;
        }
    }
    *width = parsed_width;
    *height = parsed_height;
    return help_seen ? 1 : 0;
}
