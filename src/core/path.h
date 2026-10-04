#pragma once

/* Portable filesystem path helpers (M5): joining, recursive directory
 * creation, save-name sanitization, and directory listing. Pure C17 with
 * small Win32/POSIX branches. No global state.
 */

#include <stdbool.h>
#include <stddef.h>

/* Maximum path length this module will produce (incl. terminator). */
#define PATH_MAX_LEN 1024

/* Join up to two segments with the platform separator into out.
 * Overflows truncate safely (always terminates). Empty segments skipped.
 *
 * Args:
 *   out: destination buffer (must not be NULL).
 *   out_cap: capacity in bytes (> 0).
 *   a, b: segments (either may be NULL/empty).
 *
 * Returns: 0 on success, non-zero when truncated or on bad args.
 */
int path_join(char *out, size_t out_cap, const char *a, const char *b);

/* Create a directory and all missing parents (mkdir -p semantics).
 * Existing directories are success. Fails on bad args or OS errors.
 *
 * Args:
 *   path: directory path (must not be NULL).
 *
 * Returns: 0 on success, non-zero on failure.
 */
int path_mkdir_p(const char *path);

/* Check whether a path is an existing directory.
 *
 * Args:
 *   path: path to test (must not be NULL).
 *
 * Returns: true when it exists and is a directory.
 */
bool path_is_dir(const char *path);

/* Check whether a path is an existing regular file.
 *
 * Args:
 *   path: path to test (must not be NULL).
 *
 * Returns: true when it exists and is a regular file.
 */
bool path_is_file(const char *path);

/* Remove a regular file (missing file counts as success).
 *
 * Args:
 *   path: file path (must not be NULL).
 *
 * Returns: 0 on success, non-zero on failure.
 */
int path_remove_file(const char *path);

/* Remove an empty directory.
 *
 * Args:
 *   path: directory path (must not be NULL).
 *
 * Returns: 0 on success, non-zero on failure.
 */
int path_remove_dir(const char *path);

/* Sanitize a user world name into a safe directory name.
 * Keeps [A-Za-z0-9 _-], trims leading/trailing spaces and dots, converts
 * interior spaces to single underscores, caps length. Empty/unsafe results
 * fall back to "World". Never emits ".", "..", separators, or drive specs.
 *
 * Args:
 *   out: destination (must not be NULL).
 *   out_cap: capacity in bytes (> 0; 16+ recommended).
 *   name: raw user input (NULL treated as empty).
 *
 * Returns: 0 on success, non-zero on bad args.
 */
int path_sanitize_name(char *out, size_t out_cap, const char *name);

/* List immediate subdirectories of dir (names only, no "." / "..").
 * Portable Win32/POSIX implementation.
 *
 * Args:
 *   dir: directory to scan (must not be NULL).
 *   out_names: [out_cap][64] name buffer (must not be NULL).
 *   out_cap: max entries.
 *   out_count: receives entry count (must not be NULL).
 *
 * Returns: 0 on success (possibly zero entries), non-zero when the
 * directory cannot be opened or on bad args.
 */
int path_list_dirs(const char *dir, char out_names[][64], size_t out_cap, size_t *out_count);

/* List immediate regular files of dir (names only).
 *
 * Args:
 *   dir: directory to scan (must not be NULL).
 *   out_names: [out_cap][64] name buffer (must not be NULL).
 *   out_cap: max entries.
 *   out_count: receives entry count (must not be NULL).
 *
 * Returns: 0 on success, non-zero when the directory cannot be opened.
 */
int path_list_files(const char *dir, char out_names[][64], size_t out_cap, size_t *out_count);

/* Directory containing the current executable (no trailing separator).
 * Used to locate owner-supplied data next to the binary regardless of
 * the process working directory (Explorer launches, shortcuts, etc.).
 *
 * Args:
 *   out: destination (must not be NULL).
 *   out_cap: capacity in bytes (> 0).
 *
 * Returns: 0 on success, non-zero when the path cannot be determined
 * (callers fall back to working-directory-relative paths).
 */
int path_exe_dir(char *out, size_t out_cap);

/* Resolve an mcassets subdirectory: "<exe>/mcassets/<sub>" when the exe
 * directory is known, else working-directory-relative "mcassets/<sub>".
 * The result may not exist (callers keep lower layers then).
 *
 * Args:
 *   out: destination (must not be NULL).
 *   out_cap: capacity in bytes (> 0).
 *   sub: subdirectory under mcassets/ (must not be NULL, e.g.
 *     "generated/tiles").
 *
 * Returns: 0 on success, non-zero on bad args/truncation.
 */
int path_mcassets_dir(char *out, size_t out_cap, const char *sub);
