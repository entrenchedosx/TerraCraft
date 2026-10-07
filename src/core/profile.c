#include "core/profile.h"

#include "core/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static bool profile_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool profile_name_valid(const char *name)
{
    if (name == NULL) {
        return false;
    }
    size_t len = strlen(name);
    if (len < PROFILE_NAME_MIN_LEN || len > PROFILE_NAME_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

int profile_load(const char *path, char *out_name, size_t out_cap)
{
    if (out_name != NULL && out_cap > 0) {
        out_name[0] = '\0';
    }
    if (path == NULL || out_name == NULL || out_cap == 0) {
        return PROFILE_ERR_ARGUMENT;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return PROFILE_ERR_IO;
    }

    char line[128];
    char parsed[PROFILE_NAME_MAX_LEN + 1] = {0};
    bool saw_username = false;
    bool malformed = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n' && !feof(f)) {
            malformed = true;
            break;
        }
        if (ferror(f)) {
            malformed = true;
            break;
        }

        char *start = line;
        while (*start == ' ' || *start == '\t') {
            ++start;
        }
        if (*start == '\0' || *start == '#' || *start == '\r' || *start == '\n') {
            continue;
        }
        size_t line_len = strlen(start);
        while (line_len > 0 && profile_ascii_space(start[line_len - 1])) {
            start[--line_len] = '\0';
        }
        if (strncmp(start, "username=", 9) != 0) {
            /* Ignore unknown keys so this small file can grow compatibly. */
            continue;
        }
        if (saw_username) {
            malformed = true;
            break;
        }
        saw_username = true;
        const char *value = start + 9;
        size_t value_len = strlen(value);
        if (value_len > PROFILE_NAME_MAX_LEN) {
            malformed = true;
            break;
        }
        memcpy(parsed, value, value_len + 1);
    }

    if (ferror(f)) {
        malformed = true;
    }
    fclose(f);
    if (malformed || !saw_username) {
        return PROFILE_ERR_FORMAT;
    }
    if (!profile_name_valid(parsed)) {
        return PROFILE_ERR_INVALID_NAME;
    }
    size_t name_len = strlen(parsed);
    if (out_cap <= name_len) {
        return PROFILE_ERR_BUFFER;
    }
    memcpy(out_name, parsed, name_len + 1);
    return PROFILE_OK;
}

static int profile_make_parent(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *sep = slash;
    if (backslash != NULL && (sep == NULL || backslash > sep)) {
        sep = backslash;
    }
    if (sep == NULL) {
        return 0;
    }
    size_t parent_len = (size_t)(sep - path);
    if (parent_len == 0) {
        return 0;
    }
    char parent[PATH_MAX_LEN];
    if (parent_len >= sizeof(parent)) {
        return PROFILE_ERR_BUFFER;
    }
    memcpy(parent, path, parent_len);
    parent[parent_len] = '\0';
    return path_mkdir_p(parent) == 0 ? PROFILE_OK : PROFILE_ERR_IO;
}

int profile_save(const char *path, const char *name)
{
    if (path == NULL || name == NULL) {
        return PROFILE_ERR_ARGUMENT;
    }
    if (!profile_name_valid(name)) {
        return PROFILE_ERR_INVALID_NAME;
    }
    int parent_rc = profile_make_parent(path);
    if (parent_rc != PROFILE_OK) {
        return parent_rc;
    }

    char tmp[PATH_MAX_LEN];
    size_t path_len = strlen(path);
    if (path_len + sizeof(".tmp") > sizeof(tmp)) {
        return PROFILE_ERR_BUFFER;
    }
    memcpy(tmp, path, path_len);
    memcpy(tmp + path_len, ".tmp", sizeof(".tmp"));

    FILE *f = fopen(tmp, "wb");
    if (f == NULL) {
        return PROFILE_ERR_IO;
    }
    int write_failed = fprintf(f, "# TerraCraft local profile\nusername=%s\n", name) < 0;
    if (fclose(f) != 0) {
        write_failed = 1;
    }
    if (write_failed) {
        remove(tmp);
        return PROFILE_ERR_IO;
    }

#if defined(_WIN32)
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(tmp);
        return PROFILE_ERR_IO;
    }
#else
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return PROFILE_ERR_IO;
    }
#endif
    return PROFILE_OK;
}

static uint64_t profile_next_random(uint64_t *state)
{
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static uint32_t profile_random_bounded(uint64_t *state, uint32_t bound)
{
    /* Rejection sampling avoids modulo bias while preserving deterministic
     * output for any explicit seed. */
    uint64_t threshold = (UINT64_C(0) - (uint64_t)bound) % (uint64_t)bound;
    for (;;) {
        uint64_t value = profile_next_random(state);
        if (value >= threshold) {
            return (uint32_t)(value % bound);
        }
    }
}

int profile_generate_name(char *out_name, size_t out_cap, uint64_t seed)
{
    static const char *const first_words[] = {
        "Amber", "Brisk", "Clever", "Daring", "Ember", "Frost", "Golden", "Hidden",
        "Jolly", "Kind", "Lucky", "Misty", "Nimble", "Quiet", "Rustic", "Sunny"};
    static const char *const second_words[] = {
        "Badger", "Birch", "Comet", "Coyote", "Falcon", "Fox", "Harbor", "Heron",
        "Maple", "Meadow", "Otter", "Pine", "Raven", "River", "Willow", "Wolf"};
    const size_t first_count = sizeof(first_words) / sizeof(first_words[0]);
    const size_t second_count = sizeof(second_words) / sizeof(second_words[0]);

    if (out_name != NULL && out_cap > 0) {
        out_name[0] = '\0';
    }
    if (out_name == NULL || out_cap == 0) {
        return PROFILE_ERR_ARGUMENT;
    }

    uint64_t state = seed;
    const char *first = first_words[profile_random_bounded(&state, (uint32_t)first_count)];
    const char *second = second_words[profile_random_bounded(&state, (uint32_t)second_count)];
    unsigned suffix = profile_random_bounded(&state, 10000);
    int written = snprintf(out_name, out_cap, "%s_%s%04u", first, second, suffix);
    if (written < 0 || (size_t)written >= out_cap) {
        out_name[0] = '\0';
        return PROFILE_ERR_BUFFER;
    }
    return profile_name_valid(out_name) ? PROFILE_OK : PROFILE_ERR_INVALID_NAME;
}
