#pragma once

/* Local player profile (username only). Stored independently of world saves
 * in config/profile.cfg so one name is shared by all local worlds and LAN
 * sessions. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROFILE_NAME_MIN_LEN 3
#define PROFILE_NAME_MAX_LEN 24
#define PROFILE_PATH "config/profile.cfg"

/* Stable result codes returned by profile_load/profile_save/generate. */
typedef enum ProfileResult {
    PROFILE_OK = 0,
    PROFILE_ERR_ARGUMENT = -1,
    PROFILE_ERR_IO = -2,
    PROFILE_ERR_INVALID_NAME = -3,
    PROFILE_ERR_FORMAT = -4,
    PROFILE_ERR_BUFFER = -5
} ProfileResult;

/* Validate the supported account-name alphabet and length: 3-24 ASCII
 * letters, digits, or underscores. NULL is invalid. */
bool profile_name_valid(const char *name);

/* Load `username=...` from path. The destination is cleared on failure.
 * Returns PROFILE_OK for a valid stored username, PROFILE_ERR_IO when the
 * file cannot be read, PROFILE_ERR_FORMAT for malformed/missing fields, or
 * PROFILE_ERR_INVALID_NAME for a value outside the username contract. */
int profile_load(const char *path, char *out_name, size_t out_cap);

/* Save one validated username as `username=...`, creating parent folders
 * as needed. Uses a temporary file and replace/rename for safe updates. */
int profile_save(const char *path, const char *name);

/* Generate a deterministic two-word username with a four-digit numeric
 * suffix from an explicit seed, e.g. `AmberFox0427`. */
int profile_generate_name(char *out_name, size_t out_cap, uint64_t seed);
