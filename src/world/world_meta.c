#include "world/world_meta.h"
#include "core/path.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Fill defaults. */
void world_meta_defaults(WorldMeta *m)
{
    if (m == NULL) {
        return;
    }
    m->version = WORLD_META_VERSION;
    memset(m->name, 0, sizeof(m->name));
    memcpy(m->name, "World", 6);
    m->seed = 0;
    m->mode = WORLD_MODE_SURVIVAL;
    m->terrain_version = WORLD_TERRAIN_VERSION_CURRENT;
    m->px = 8.5f;
    m->py = 80.0f;
    m->pz = 8.5f;
    m->yaw = 0.0f;
    m->pitch = 0.0f;
    m->day = 0.35f;
    m->last_played = 0;
    m->has_player = false;
    m->health = 20.0f;
    m->hunger = 20.0f;
    m->spawn_x = 8.5f;
    m->spawn_y = 80.0f;
    m->spawn_z = 8.5f;
    m->has_spawn = false;
    memset(m->inv_items, 0, sizeof(m->inv_items));
    memset(m->inv_counts, 0, sizeof(m->inv_counts));
    memset(m->inv_dur, 0, sizeof(m->inv_dur));
}

/* Trim trailing CR/LF/space/tab in place. */
static void trim_end(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

/* Advance past one comma-separated inv entry (malformed entries are
 * skipped, never aborting the rest of the list). */
static const char *meta_inv_skip(const char *e)
{
    while (*e != '\0' && *e != ',') {
        ++e;
    }
    return (*e == ',') ? e + 1 : e;
}

/* Parse buffer into a record. Shared by file read and direct tests. */
int world_meta_parse(const char *text, WorldMeta *out)
{
    if (text == NULL || out == NULL) {
        return -1;
    }
    WorldMeta m;
    world_meta_defaults(&m);
    /* Pre-versioned saves must keep producing the original shape when an
     * unvisited chunk is generated later; terrain_version is optional in
     * metadata v1, so absence selects the legacy generator. */
    m.terrain_version = 1;
    bool have_version = false;
    bool have_name = false;
    bool have_seed = false;
    /* Position triples stage in temps: has_player/has_spawn are only set
     * when all three axes parsed finite (partial lines never fabricate). */
    float tpx = 0.0f, tpy = 0.0f, tpz = 0.0f;
    int seen_player = 0;
    float tsx = 0.0f, tsy = 0.0f, tsz = 0.0f;
    int seen_spawn = 0;

    const char *p = text;
    /* Longest legal line is a full inventory (~36 x "35:65535:65535," =
     * ~500 chars); 1024 leaves headroom so tail slots never truncate. */
    char line[1024];
    while (*p != '\0') {
        size_t i = 0;
        while (*p != '\0' && *p != '\n' && i + 1 < sizeof(line)) {
            line[i++] = *p++;
        }
        if (*p == '\n') {
            ++p;
        }
        line[i] = '\0';
        trim_end(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue; /* Tolerate junk lines (forward tolerant). */
        }
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;
        if (strcmp(key, "format_version") == 0) {
            char *end = NULL;
            long v = strtol(val, &end, 10);
            if (end != val && *end == '\0') {
                m.version = (int)v;
                have_version = true;
            }
        } else if (strcmp(key, "world_name") == 0) {
            size_t vl = strlen(val);
            if (vl >= sizeof(m.name)) {
                vl = sizeof(m.name) - 1;
            }
            memcpy(m.name, val, vl);
            m.name[vl] = '\0';
            have_name = val[0] != '\0';
        } else if (strcmp(key, "seed") == 0) {
            char *end = NULL;
            long long v = strtoll(val, &end, 10);
            if (end != val && *end == '\0') {
                m.seed = (int64_t)v;
                have_seed = true;
            }
        } else if (strcmp(key, "game_mode") == 0) {
            if (strcmp(val, "creative") == 0) {
                m.mode = WORLD_MODE_CREATIVE;
            } else {
                m.mode = WORLD_MODE_SURVIVAL;
            }
        } else if (strcmp(key, "terrain_version") == 0) {
            char *end = NULL;
            long v = strtol(val, &end, 10);
            if (end != val && *end == '\0' && v >= 1 && v <= WORLD_TERRAIN_VERSION_CURRENT) {
                m.terrain_version = (int)v;
            } else {
                return -4; /* Never silently blend an unknown generator. */
            }
        } else if (strcmp(key, "player_x") == 0) {
            float v = strtof(val, NULL);
            if (isfinite(v)) {
                tpx = v;
                seen_player |= 1;
            }
        } else if (strcmp(key, "player_y") == 0) {
            float v = strtof(val, NULL);
            if (isfinite(v)) {
                tpy = v;
                seen_player |= 2;
            }
        } else if (strcmp(key, "player_z") == 0) {
            float v = strtof(val, NULL);
            if (isfinite(v)) {
                tpz = v;
                seen_player |= 4;
            }
        } else if (strcmp(key, "player_yaw") == 0) {
            float v = strtof(val, NULL);
            if (isfinite(v)) {
                m.yaw = v;
            }
        } else if (strcmp(key, "player_pitch") == 0) {
            float v = strtof(val, NULL);
            if (isfinite(v)) {
                m.pitch = v;
            }
        } else if (strcmp(key, "day") == 0) {
            char *end = NULL;
            float d = strtof(val, &end);
            if (end != val && *end == '\0' && d >= 0.0f && d < 1.0f) {
                m.day = d;
            }
        } else if (strcmp(key, "last_played") == 0) {
            char *end = NULL;
            long long v = strtoll(val, &end, 10);
            if (end != val && *end == '\0' && v >= 0) {
                m.last_played = (int64_t)v;
            }
        } else if (strcmp(key, "health") == 0) {
            char *end = NULL;
            float v = strtof(val, &end);
            if (end != val && *end == '\0' && v >= 0.0f && v <= 100.0f) {
                m.health = v;
            }
        } else if (strcmp(key, "hunger") == 0) {
            char *end = NULL;
            float v = strtof(val, &end);
            if (end != val && *end == '\0' && v >= 0.0f && v <= 100.0f) {
                m.hunger = v;
            }
        } else if (strcmp(key, "spawn_x") == 0) {
            float v = strtof(val, NULL);
            if (isfinite(v)) {
                tsx = v;
                seen_spawn |= 1;
            }
        } else if (strcmp(key, "spawn_y") == 0) {
            float v = strtof(val, NULL);
            if (isfinite(v)) {
                tsy = v;
                seen_spawn |= 2;
            }
        } else if (strcmp(key, "spawn_z") == 0) {
            float v = strtof(val, NULL);
            if (isfinite(v)) {
                tsz = v;
                seen_spawn |= 4;
            }
        } else if (strcmp(key, "inv") == 0) {
            /* Compact list "slot:item:count,..." — malformed entries are
             * skipped individually (advance past the next comma); slot/item/
             * count are validated on apply. */
            const char *e = val;
            while (*e != '\0') {
                char *end = NULL;
                long slot = strtol(e, &end, 10);
                if (end == e || *end != ':') {
                    e = meta_inv_skip(e);
                    continue;
                }
                e = end + 1;
                long item = strtol(e, &end, 10);
                if (end == e || *end != ':') {
                    e = meta_inv_skip(e);
                    continue;
                }
                e = end + 1;
                long count = strtol(e, &end, 10);
                if (end == e || (*end != ',' && *end != '\0')) {
                    e = meta_inv_skip(e);
                    continue;
                }
                if (slot >= 0 && slot < WORLD_META_INV_SLOTS && item >= 0 && item <= 65535 && count >= 0 &&
                    count <= 65535) {
                    m.inv_items[slot] = (uint16_t)item;
                    m.inv_counts[slot] = (uint16_t)count;
                }
                e = (*end == ',') ? end + 1 : end;
            }
        } else if (strcmp(key, "invdur") == 0) {
            /* Compact list "slot:dur,..." — same skip-per-entry tolerance.
             * Wear is applied to the matching slot and clamped by
             * inv_sanitize on session load (never trusted blindly). */
            const char *e = val;
            while (*e != '\0') {
                char *end = NULL;
                long slot = strtol(e, &end, 10);
                if (end == e || *end != ':') {
                    e = meta_inv_skip(e);
                    continue;
                }
                e = end + 1;
                long dur = strtol(e, &end, 10);
                if (end == e || (*end != ',' && *end != '\0')) {
                    e = meta_inv_skip(e);
                    continue;
                }
                if (slot >= 0 && slot < WORLD_META_INV_SLOTS && dur >= 0 && dur <= 65535) {
                    m.inv_dur[slot] = (uint16_t)dur;
                }
                e = (*end == ',') ? end + 1 : end;
            }
        }
        /* Unknown keys ignored (forward tolerant within version). */
    }
    if (!have_version || !have_name || !have_seed) {
        return -2;
    }
    /* Triples only count when complete: partial lines never fabricate a
     * position or spawn out of defaults. */
    if (seen_player == 7) {
        m.px = tpx;
        m.py = tpy;
        m.pz = tpz;
        m.has_player = true;
    }
    if (seen_spawn == 7) {
        m.spawn_x = tsx;
        m.spawn_y = tsy;
        m.spawn_z = tsz;
        m.has_spawn = true;
    }
    if (m.version <= 0 || m.version > WORLD_META_VERSION) {
        return -3; /* Future (or bogus) version: refuse, don't guess. */
    }
    if (m.mode != WORLD_MODE_SURVIVAL && m.mode != WORLD_MODE_CREATIVE) {
        m.mode = WORLD_MODE_SURVIVAL;
    }
    *out = m;
    return 0;
}

/* Write metadata. */
int world_meta_write(const char *dir, const WorldMeta *m)
{
    if (dir == NULL || m == NULL) {
        return -1;
    }
    if (path_mkdir_p(dir) != 0) {
        return -2;
    }
    char path[PATH_MAX_LEN];
    char tmp[PATH_MAX_LEN + 8];
    if (path_join(path, sizeof(path), dir, WORLD_META_FILE) != 0) {
        return -2;
    }
    size_t pl = strlen(path);
    if (pl + 5 >= sizeof(tmp)) {
        return -2;
    }
    memcpy(tmp, path, pl + 1);
    memcpy(tmp + pl, ".tmp", 5);
    FILE *f = fopen(tmp, "wb");
    bool direct = false;
    if (f == NULL) {
        f = fopen(path, "wb");
        direct = true;
        if (f == NULL) {
            return -3;
        }
    }
    WorldMeta stamped = *m;
    stamped.version = WORLD_META_VERSION;
    stamped.last_played = (int64_t)time(NULL);
    fprintf(f, "# TerraCraft world metadata v%d (key=value; unknown keys ignored)\n", WORLD_META_VERSION);
    fprintf(f, "format_version=%d\n", stamped.version);
    fprintf(f, "world_name=%s\n", stamped.name);
    fprintf(f, "seed=%lld\n", (long long)stamped.seed);
    fprintf(f, "game_mode=%s\n", stamped.mode == WORLD_MODE_CREATIVE ? "creative" : "survival");
    fprintf(f, "terrain_version=%d\n", stamped.terrain_version);
    /* Player/spawn triples are optional: omitted when invalid so a fresh
     * world never fabricates a position or spawn on reload (has_player /
     * has_spawn derive from key presence). */
    if (stamped.has_player) {
        fprintf(f, "player_x=%.3f\n", (double)stamped.px);
        fprintf(f, "player_y=%.3f\n", (double)stamped.py);
        fprintf(f, "player_z=%.3f\n", (double)stamped.pz);
        fprintf(f, "player_yaw=%.4f\n", (double)stamped.yaw);
        fprintf(f, "player_pitch=%.4f\n", (double)stamped.pitch);
    }
    fprintf(f, "day=%.6f\n", (double)stamped.day);
    fprintf(f, "last_played=%lld\n", (long long)stamped.last_played);
    fprintf(f, "health=%.1f\n", (double)stamped.health);
    fprintf(f, "hunger=%.1f\n", (double)stamped.hunger);
    if (stamped.has_spawn) {
        fprintf(f, "spawn_x=%.3f\n", (double)stamped.spawn_x);
        fprintf(f, "spawn_y=%.3f\n", (double)stamped.spawn_y);
        fprintf(f, "spawn_z=%.3f\n", (double)stamped.spawn_z);
    }
    /* Compact non-empty inventory: "slot:item:count,...". */
    fprintf(f, "inv=");
    {
        bool first = true;
        for (int i = 0; i < WORLD_META_INV_SLOTS; ++i) {
            if (stamped.inv_items[i] == 0 && stamped.inv_counts[i] == 0) {
                continue;
            }
            fprintf(f, "%s%d:%u:%u", first ? "" : ",", i, (unsigned)stamped.inv_items[i],
                    (unsigned)stamped.inv_counts[i]);
            first = false;
        }
    }
    fprintf(f, "\n");
    /* Compact tool wear: "slot:dur,..." (only nonzero; absent reads as 0,
     * so M6 files without this key load tools at full durability). */
    fprintf(f, "invdur=");
    {
        bool first = true;
        for (int i = 0; i < WORLD_META_INV_SLOTS; ++i) {
            if (stamped.inv_dur[i] == 0) {
                continue;
            }
            fprintf(f, "%s%d:%u", first ? "" : ",", i, (unsigned)stamped.inv_dur[i]);
            first = false;
        }
    }
    fprintf(f, "\n");
    if (fclose(f) != 0) {
        return -4;
    }
    if (!direct) {
        /* Windows rename refuses to overwrite: clear the way first. */
        remove(path);
        if (rename(tmp, path) != 0) {
            remove(tmp);
            return -5;
        }
    }
    return 0;
}

/* Read metadata from disk (size-capped). */
int world_meta_read(const char *dir, WorldMeta *out)
{
    if (dir == NULL || out == NULL) {
        return -1;
    }
    char path[PATH_MAX_LEN];
    if (path_join(path, sizeof(path), dir, WORLD_META_FILE) != 0) {
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -2;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -3;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > 65536) {
        fclose(f);
        return -4;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -3;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(f);
        return -5;
    }
    size_t got = sz > 0 ? fread(buf, 1, (size_t)sz, f) : 0;
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return -6;
    }
    buf[sz] = '\0';
    int rc = world_meta_parse(buf, out);
    free(buf);
    return rc;
}
