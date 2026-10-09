#include "core/settings.h"
#include "core/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parse "key=value" lines: split at first '=', trim trailing CR/LF/space. */
static void split_kv(char *line, char **out_key, char **out_val)
{
    *out_key = line;
    *out_val = NULL;
    char *eq = strchr(line, '=');
    if (eq == NULL) {
        return;
    }
    *eq = '\0';
    *out_val = eq + 1;
    /* Trim trailing whitespace from key. */
    size_t kl = strlen(*out_key);
    while (kl > 0 && ((*out_key)[kl - 1] == ' ' || (*out_key)[kl - 1] == '\t')) {
        (*out_key)[--kl] = '\0';
    }
    /* Trim trailing CR/LF/space from value. */
    size_t vl = strlen(*out_val);
    while (vl > 0 && ((*out_val)[vl - 1] == '\n' || (*out_val)[vl - 1] == '\r' ||
                      (*out_val)[vl - 1] == ' ' || (*out_val)[vl - 1] == '\t')) {
        (*out_val)[--vl] = '\0';
    }
}

/* Fill defaults. */
void settings_defaults(Settings *s)
{
    if (s == NULL) {
        return;
    }
    s->render_distance = 4;
    s->sensitivity = 0.0025f;
    s->fov = 70.0f;
    s->vsync = true;
    s->auto_jump = false;
    s->view_bobbing = true;
    s->volume = 80;
    s->sfx_volume = 80;
    memset(s->pack, 0, sizeof(s->pack));
    memcpy(s->pack, "Default", 8);
}

/* Clamp every field. */
void settings_clamp(Settings *s)
{
    if (s == NULL) {
        return;
    }
    if (s->render_distance < 2) {
        s->render_distance = 2;
    }
    if (s->render_distance > 8) {
        s->render_distance = 8;
    }
    if (!(s->sensitivity >= 0.0005f)) {
        s->sensitivity = 0.0005f;
    }
    if (!(s->sensitivity <= 0.010f)) {
        s->sensitivity = 0.010f;
    }
    if (!(s->fov >= 60.0f)) {
        s->fov = 60.0f;
    }
    if (!(s->fov <= 110.0f)) {
        s->fov = 110.0f;
    }
    if (s->volume < 0) {
        s->volume = 0;
    }
    if (s->volume > 100) {
        s->volume = 100;
    }
    if (s->sfx_volume < 0) {
        s->sfx_volume = 0;
    }
    if (s->sfx_volume > 100) {
        s->sfx_volume = 100;
    }
    s->pack[SETTINGS_PACK_LEN - 1] = '\0';
    if (s->pack[0] == '\0') {
        memcpy(s->pack, "Default", 8);
    }
}

/* Load overrides over defaults. */
int settings_load(Settings *s, const char *path)
{
    if (s == NULL || path == NULL) {
        return -1;
    }
    settings_defaults(s);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return -2;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r' || line[0] == '\0') {
            continue;
        }
        char *key = NULL;
        char *val = NULL;
        split_kv(line, &key, &val);
        if (val == NULL || val[0] == '\0') {
            continue;
        }
        if (strcmp(key, "render_distance") == 0) {
            s->render_distance = (int)strtol(val, NULL, 10);
        } else if (strcmp(key, "sensitivity") == 0) {
            s->sensitivity = strtof(val, NULL);
        } else if (strcmp(key, "fov") == 0) {
            s->fov = strtof(val, NULL);
        } else if (strcmp(key, "vsync") == 0) {
            s->vsync = !(strcmp(val, "0") == 0 || strcmp(val, "false") == 0 || strcmp(val, "off") == 0);
        } else if (strcmp(key, "auto_jump") == 0) {
            s->auto_jump = !(strcmp(val, "0") == 0 || strcmp(val, "false") == 0 || strcmp(val, "off") == 0);
        } else if (strcmp(key, "view_bobbing") == 0) {
            s->view_bobbing = !(strcmp(val, "0") == 0 || strcmp(val, "false") == 0 || strcmp(val, "off") == 0);
        } else if (strcmp(key, "volume") == 0) {
            s->volume = (int)strtol(val, NULL, 10);
        } else if (strcmp(key, "sfx_volume") == 0) {
            s->sfx_volume = (int)strtol(val, NULL, 10);
        } else if (strcmp(key, "pack") == 0) {
            size_t vl = strlen(val);
            if (vl >= SETTINGS_PACK_LEN) {
                vl = SETTINGS_PACK_LEN - 1;
            }
            memcpy(s->pack, val, vl);
            s->pack[vl] = '\0';
        }
        /* Unknown keys ignored for forward compatibility. */
    }
    fclose(f);
    settings_clamp(s);
    return 0;
}

/* Save all keys (temp file + rename, fallback direct). */
int settings_save(const Settings *s, const char *path)
{
    if (s == NULL || path == NULL) {
        return -1;
    }
    /* Ensure the parent directory exists ("config"). */
    const char *sep = strrchr(path, '/');
    const char *sep2 = strrchr(path, '\\');
    if (sep2 != NULL && (sep == NULL || sep2 > sep)) {
        sep = sep2;
    }
    if (sep != NULL) {
        char dir[PATH_MAX_LEN];
        size_t dl = (size_t)(sep - path);
        if (dl >= sizeof(dir)) {
            return -2;
        }
        memcpy(dir, path, dl);
        dir[dl] = '\0';
        if (path_mkdir_p(dir) != 0) {
            return -3;
        }
    }
    char tmp[PATH_MAX_LEN];
    if (path_join(tmp, sizeof(tmp), NULL, path) != 0) {
        return -2;
    }
    size_t tl = strlen(tmp);
    if (tl + 5 >= sizeof(tmp)) {
        return -2;
    }
    memcpy(tmp + tl, ".tmp", 5);
    FILE *f = fopen(tmp, "w");
    if (f == NULL) {
        f = fopen(path, "w");
        if (f == NULL) {
            return -4;
        }
        tmp[0] = '\0'; /* Mark direct-write mode (no rename). */
    }
    fprintf(f, "# TerraCraft settings (M5). Unknown keys are ignored on load.\n");
    fprintf(f, "render_distance=%d\n", s->render_distance);
    fprintf(f, "sensitivity=%.6f\n", (double)s->sensitivity);
    fprintf(f, "fov=%.2f\n", (double)s->fov);
    fprintf(f, "vsync=%d\n", s->vsync ? 1 : 0);
    fprintf(f, "auto_jump=%d\n", s->auto_jump ? 1 : 0);
    fprintf(f, "view_bobbing=%d\n", s->view_bobbing ? 1 : 0);
    fprintf(f, "volume=%d\n", s->volume);
    fprintf(f, "sfx_volume=%d\n", s->sfx_volume);
    fprintf(f, "pack=%s\n", s->pack);
    if (fclose(f) != 0) {
        return -5;
    }
    if (tmp[0] != '\0') {
        /* Best-effort atomic replace. Windows rename refuses to overwrite,
         * so clear the way first; tolerate odd-FS failures. */
        remove(path);
        if (rename(tmp, path) != 0) {
            remove(tmp);
            return -6;
        }
    }
    return 0;
}
