#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
/* Strict ISO C (C_EXTENSIONS OFF) hides POSIX declarations such as
 * readlink on glibc; request them explicitly before any system header. */
#define _POSIX_C_SOURCE 200809L
#endif

#include "core/path.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <io.h>
#include <windows.h>
#define MINEC_MKDIR(p) _mkdir(p)
#define MINEC_RMDIR(p) _rmdir(p)
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MINEC_MKDIR(p) mkdir(p, 0755)
#define MINEC_RMDIR(p) rmdir(p)
#endif

#if defined(_WIN32)
#define MINEC_SEP '\\'
#else
#define MINEC_SEP '/'
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h> /* _NSGetExecutablePath for path_exe_dir. */
#endif

/* Join two segments with the platform separator.
 *
 * Args:
 *   out, out_cap: destination.
 *   a, b: segments.
 *
 * Returns: 0 ok, non-zero on truncation/bad args.
 */
int path_join(char *out, size_t out_cap, const char *a, const char *b)
{
    if (out == NULL || out_cap == 0) {
        return -1;
    }
    out[0] = '\0';
    size_t len = 0;
    const char *segs[2] = {a, b};
    for (int i = 0; i < 2; ++i) {
        const char *s = segs[i];
        if (s == NULL || s[0] == '\0') {
            continue;
        }
        if (len > 0) {
            if (len + 1 >= out_cap) {
                out[out_cap - 1] = '\0';
                return -2;
            }
            out[len++] = MINEC_SEP;
            out[len] = '\0';
        }
        size_t sl = strlen(s);
        if (len + sl >= out_cap) {
            size_t room = out_cap - len - 1;
            memcpy(out + len, s, room);
            len += room;
            out[len] = '\0';
            return -2;
        }
        memcpy(out + len, s, sl + 1);
        len += sl;
    }
    return 0;
}

/* mkdir -p: walk the path, creating each level. Tolerates existing dirs
 * and both separator styles on input.
 */
int path_mkdir_p(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    char buf[PATH_MAX_LEN];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) {
        return -2;
    }
    memcpy(buf, path, n + 1);
    /* Normalize separators to the platform one for prefix walking. */
    for (size_t i = 0; i < n; ++i) {
        if (buf[i] == '/' || buf[i] == '\\') {
            buf[i] = MINEC_SEP;
        }
    }
    for (size_t i = 0; i <= n; ++i) {
        if (buf[i] == MINEC_SEP || buf[i] == '\0') {
            char saved = buf[i];
            /* Skip resolve attempts for drive roots ("C:") and empties. */
            if (i > 0 && !(i == 2 && buf[1] == ':')) {
                buf[i] = '\0';
                if (MINEC_MKDIR(buf) != 0) {
                    /* Exists (or created by a sibling): verify dir-ness. */
                    if (!path_is_dir(buf)) {
                        buf[i] = saved;
                        return -3;
                    }
                }
            }
            buf[i] = saved;
        }
    }
    return 0;
}

/* True when path exists and is a directory. */
bool path_is_dir(const char *path)
{
    if (path == NULL) {
        return false;
    }
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode) != 0;
#endif
}

/* True when path exists and is a regular file. */
bool path_is_file(const char *path)
{
    if (path == NULL) {
        return false;
    }
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode) != 0;
#endif
}

/* Remove a file; missing counts as success. */
int path_remove_file(const char *path)
{
    if (path == NULL) {
        return -1;
    }
    if (remove(path) != 0) {
        /* Tolerate already-gone files; fail on real leftovers. */
        if (path_is_file(path)) {
            return -2;
        }
    }
    return 0;
}

/* Remove an empty directory. */
int path_remove_dir(const char *path)
{
    if (path == NULL) {
        return -1;
    }
    if (MINEC_RMDIR(path) != 0) {
        return -2;
    }
    return 0;
}

/* Keep-list test for sanitize. */
static bool sanitize_keep(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' ||
           c == '_' || c == '-';
}

/* Sanitize a world name into a safe directory name. */
int path_sanitize_name(char *out, size_t out_cap, const char *name)
{
    if (out == NULL || out_cap == 0) {
        return -1;
    }
    out[0] = '\0';
    if (name == NULL) {
        name = "";
    }
    /* Skip leading spaces/dots (kills "", ".", "..", " ..."). */
    while (*name == ' ' || *name == '.') {
        ++name;
    }
    size_t len = 0;
    bool last_space = false;
    for (const char *p = name; *p != '\0' && len + 1 < out_cap && len < 48; ++p) {
        char c = *p;
        if (!sanitize_keep(c)) {
            continue;
        }
        if (c == ' ') {
            if (last_space) {
                continue;
            }
            last_space = true;
            out[len++] = '_';
            continue;
        }
        last_space = false;
        out[len++] = c;
    }
    out[len] = '\0';
    /* Trim trailing underscores/dots/spaces left by filtering. */
    while (len > 0 && (out[len - 1] == '_' || out[len - 1] == '.' || out[len - 1] == ' ')) {
        out[--len] = '\0';
    }
    if (len == 0) {
        const char *fb = "World";
        size_t fl = strlen(fb);
        if (fl + 1 > out_cap) {
            return -2;
        }
        memcpy(out, fb, fl + 1);
    }
    return 0;
}

#if defined(_WIN32)
/* Win32 directory scan helper: collect names where want_dir matches. */
static int win_scan(const char *dir, char out_names[][64], size_t out_cap, size_t *out_count, bool want_dir)
{
    char pattern[PATH_MAX_LEN];
    if (path_join(pattern, sizeof(pattern), dir, "*") != 0) {
        return -1;
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -2;
    }
    size_t n = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }
        bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (is_dir != want_dir) {
            continue;
        }
        if (n < out_cap) {
            size_t l = strlen(fd.cFileName);
            if (l > 63) {
                l = 63;
            }
            memcpy(out_names[n], fd.cFileName, l);
            out_names[n][l] = '\0';
        }
        ++n;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    *out_count = n < out_cap ? n : out_cap;
    return 0;
}
#endif /* _WIN32 win_scan */

/* Directory containing the current executable. */
int path_exe_dir(char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return -1;
    }
    out[0] = '\0';
#if defined(_WIN32)
    char buf[PATH_MAX_LEN];
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
    if (n == 0 || (size_t)n >= sizeof(buf)) {
        return -2;
    }
    /* Strip the executable name, keep the directory. */
    int cut = -1;
    for (int i = (int)n - 1; i >= 0; --i) {
        if (buf[i] == '\\' || buf[i] == '/') {
            cut = i;
            break;
        }
    }
    if (cut <= 0) {
        return -3;
    }
    buf[cut] = '\0';
    if (strlen(buf) + 1 > out_cap) {
        return -4;
    }
    memcpy(out, buf, strlen(buf) + 1);
    return 0;
#elif defined(__APPLE__)
    char buf[PATH_MAX_LEN];
    uint32_t size = (uint32_t)sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        return -2;
    }
    char *sep = strrchr(buf, '/');
    if (sep == NULL || sep == buf) {
        return -3;
    }
    *sep = '\0';
    if (strlen(buf) + 1 > out_cap) {
        return -4;
    }
    memcpy(out, buf, strlen(buf) + 1);
    return 0;
#else
    char buf[PATH_MAX_LEN];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0 || (size_t)n >= sizeof(buf) - 1) {
        return -2;
    }
    buf[n] = '\0';
    char *sep = strrchr(buf, '/');
    if (sep == NULL || sep == buf) {
        return -3;
    }
    *sep = '\0';
    if (strlen(buf) + 1 > out_cap) {
        return -4;
    }
    memcpy(out, buf, strlen(buf) + 1);
    return 0;
#endif
}

/* Resolve an mcassets subdirectory (exe-relative preferred). */
int path_mcassets_dir(char *out, size_t out_cap, const char *sub)
{
    if (out == NULL || out_cap == 0 || sub == NULL || sub[0] == '\0') {
        return -1;
    }
    char exe[PATH_MAX_LEN];
    if (path_exe_dir(exe, sizeof(exe)) == 0) {
        char root[PATH_MAX_LEN];
        if (path_join(root, sizeof(root), exe, "mcassets") == 0 &&
            path_join(out, out_cap, root, sub) == 0) {
            return 0;
        }
    }
    if (path_join(out, out_cap, "mcassets", sub) == 0) {
        return 0;
    }
    return -2;
}

/* List subdirectories. */
int path_list_dirs(const char *dir, char out_names[][64], size_t out_cap, size_t *out_count)
{
    if (dir == NULL || out_names == NULL || out_cap == 0 || out_count == NULL) {
        return -1;
    }
    *out_count = 0;
#if defined(_WIN32)
    return win_scan(dir, out_names, out_cap, out_count, true);
#else
    DIR *d = opendir(dir);
    if (d == NULL) {
        return -2;
    }
    size_t n = 0;
    struct dirent *e;
    char full[PATH_MAX_LEN];
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (path_join(full, sizeof(full), dir, e->d_name) != 0) {
            continue;
        }
        if (!path_is_dir(full)) {
            continue;
        }
        if (n < out_cap) {
            size_t l = strlen(e->d_name);
            if (l > 63) {
                l = 63;
            }
            memcpy(out_names[n], e->d_name, l);
            out_names[n][l] = '\0';
        }
        ++n;
    }
    closedir(d);
    *out_count = n < out_cap ? n : out_cap;
    return 0;
#endif
}

/* List regular files. */
int path_list_files(const char *dir, char out_names[][64], size_t out_cap, size_t *out_count)
{
    if (dir == NULL || out_names == NULL || out_cap == 0 || out_count == NULL) {
        return -1;
    }
    *out_count = 0;
#if defined(_WIN32)
    return win_scan(dir, out_names, out_cap, out_count, false);
#else
    DIR *d = opendir(dir);
    if (d == NULL) {
        return -2;
    }
    size_t n = 0;
    struct dirent *e;
    char full[PATH_MAX_LEN];
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (path_join(full, sizeof(full), dir, e->d_name) != 0) {
            continue;
        }
        if (!path_is_file(full)) {
            continue;
        }
        if (n < out_cap) {
            size_t l = strlen(e->d_name);
            if (l > 63) {
                l = 63;
            }
            memcpy(out_names[n], e->d_name, l);
            out_names[n][l] = '\0';
        }
        ++n;
    }
    closedir(d);
    *out_count = n < out_cap ? n : out_cap;
    return 0;
#endif
}
