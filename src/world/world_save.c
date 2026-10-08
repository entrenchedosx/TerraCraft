#include "world/world_save.h"
#include "core/log.h"
#include "core/path.h"
#include "game/inventory.h"
#include "game/player.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/entity_save.h"
#include "world/world.h"
#include "world/world_meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Chunk payload: 16*256*16 u16 entries. */
#define CHUNK_FILE_BLOCKS 65536u
#define CHUNK_FILE_SIZE (16u + CHUNK_FILE_BLOCKS * 2u)

/* Build "<dir>/chunks/c_<cx>_<cz>.bin" (always terminates, truncates).
 * Negative coords use a literal '-' (e.g. c_-1_4.bin) — parseable below.
 */
static int chunk_path(char *out, size_t cap, const char *dir, int cx, int cz)
{
    char chunks[PATH_MAX_LEN];
    char leaf[64];
    if (path_join(chunks, sizeof(chunks), dir, "chunks") != 0) {
        return -1;
    }
    int n = snprintf(leaf, sizeof(leaf), "c_%d_%d.bin", cx, cz);
    if (n <= 0 || (size_t)n >= sizeof(leaf)) {
        return -1;
    }
    return path_join(out, cap, chunks, leaf);
}

/* Parse "c_<cx>_<cz>.bin" back into coords (strict: full match required). */
static bool chunk_name_parse(const char *name, int *out_cx, int *out_cz)
{
    if (name == NULL || out_cx == NULL || out_cz == NULL) {
        return false;
    }
    size_t n = strlen(name);
    if (n < 8 || n > 30) {
        return false;
    }
    if (name[0] != 'c' || name[1] != '_') {
        return false;
    }
    if (strcmp(name + n - 4, ".bin") != 0) {
        return false;
    }
    /* Middle must be "<int>_<int>" with nothing else. */
    char mid[24];
    size_t ml = n - 2 - 4;
    if (ml >= sizeof(mid)) {
        return false;
    }
    memcpy(mid, name + 2, ml);
    mid[ml] = '\0';
    char *sep = strchr(mid, '_');
    if (sep == NULL || strchr(sep + 1, '_') != NULL) {
        return false;
    }
    *sep = '\0';
    char *e1 = NULL;
    char *e2 = NULL;
    long cx = strtol(mid, &e1, 10);
    long cz = strtol(sep + 1, &e2, 10);
    if (e1 == mid || *e1 != '\0' || e2 == sep + 1 || *e2 != '\0') {
        return false;
    }
    if (cx < -1000000L || cx > 1000000L || cz < -1000000L || cz > 1000000L) {
        return false;
    }
    *out_cx = (int)cx;
    *out_cz = (int)cz;
    return true;
}

/* Little-endian writers/readers (explicit, no struct padding). */
static void put_u16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void put_i32(unsigned char *p, int32_t v)
{
    uint32_t u = 0;
    memcpy(&u, &v, sizeof(u));
    p[0] = (unsigned char)(u & 0xFFu);
    p[1] = (unsigned char)((u >> 8) & 0xFFu);
    p[2] = (unsigned char)((u >> 16) & 0xFFu);
    p[3] = (unsigned char)((u >> 24) & 0xFFu);
}

static uint16_t get_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int32_t get_i32(const unsigned char *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    int32_t v = 0;
    memcpy(&v, &u, sizeof(v));
    return v;
}

/* Write one chunk file (temp + rename). */
int world_save_write_chunk(const char *dir, const Chunk *c)
{
    if (dir == NULL || c == NULL) {
        return -1;
    }
    char path[PATH_MAX_LEN];
    if (chunk_path(path, sizeof(path), dir, c->cx, c->cz) != 0) {
        return -2;
    }
    /* Ensure chunks/ exists. */
    char chunks[PATH_MAX_LEN];
    if (path_join(chunks, sizeof(chunks), dir, "chunks") != 0 || path_mkdir_p(chunks) != 0) {
        return -2;
    }
    unsigned char *buf = (unsigned char *)malloc(CHUNK_FILE_SIZE);
    if (buf == NULL) {
        LOG_ERROR("world_save: out of memory writing chunk (%d,%d)", c->cx, c->cz);
        return -3;
    }
    buf[0] = WORLD_CHUNK_MAGIC_0;
    buf[1] = WORLD_CHUNK_MAGIC_1;
    buf[2] = WORLD_CHUNK_MAGIC_2;
    buf[3] = WORLD_CHUNK_MAGIC_3;
    put_u16(buf + 4, (uint16_t)WORLD_CHUNK_VERSION);
    put_i32(buf + 6, (int32_t)c->cx);
    put_i32(buf + 10, (int32_t)c->cz);
    for (uint32_t i = 0; i < CHUNK_FILE_BLOCKS; ++i) {
        put_u16(buf + 16 + i * 2, c->blocks[i]);
    }
    char tmp[PATH_MAX_LEN + 8];
    size_t pl = strlen(path);
    if (pl + 5 >= sizeof(tmp)) {
        free(buf);
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
            free(buf);
            return -4;
        }
    }
    size_t wrote = fwrite(buf, 1, CHUNK_FILE_SIZE, f);
    free(buf);
    if (fclose(f) != 0 || wrote != CHUNK_FILE_SIZE) {
        if (!direct) {
            remove(tmp);
        }
        return -5;
    }
    if (!direct) {
        /* Windows rename refuses to overwrite: clear the way first. */
        remove(path);
        if (rename(tmp, path) != 0) {
            remove(tmp);
            return -6;
        }
    }
    return 0;
}

/* Read + validate one chunk file into c (coords must match c->cx/cz). */
int world_save_read_chunk(const char *dir, Chunk *c)
{
    if (dir == NULL || c == NULL) {
        return -1;
    }
    char path[PATH_MAX_LEN];
    if (chunk_path(path, sizeof(path), dir, c->cx, c->cz) != 0) {
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -2; /* Missing file: caller generates fresh terrain. */
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -3;
    }
    long sz = ftell(f);
    if (sz != (long)CHUNK_FILE_SIZE) {
        fclose(f);
        return -4; /* Truncated/padded: reject. */
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -3;
    }
    unsigned char *buf = (unsigned char *)malloc(CHUNK_FILE_SIZE);
    if (buf == NULL) {
        fclose(f);
        return -5;
    }
    size_t got = fread(buf, 1, CHUNK_FILE_SIZE, f);
    fclose(f);
    if (got != CHUNK_FILE_SIZE) {
        free(buf);
        return -6;
    }
    if (buf[0] != WORLD_CHUNK_MAGIC_0 || buf[1] != WORLD_CHUNK_MAGIC_1 || buf[2] != WORLD_CHUNK_MAGIC_2 ||
        buf[3] != WORLD_CHUNK_MAGIC_3) {
        free(buf);
        return -7;
    }
    uint16_t file_version = get_u16(buf + 4);
    if (file_version != 1 && file_version != (uint16_t)WORLD_CHUNK_VERSION) {
        free(buf);
        return -8; /* Unknown version: refuse, don't guess. */
    }
    if (get_i32(buf + 6) != (int32_t)c->cx || get_i32(buf + 10) != (int32_t)c->cz) {
        free(buf);
        return -9;
    }
    for (uint32_t i = 0; i < CHUNK_FILE_BLOCKS; ++i) {
        uint16_t id = get_u16(buf + 16 + i * 2);
        /* Clamp unknown future IDs to stone (never crash on new blocks);
         * AIR..TORCH range is authoritative, anything else is suspect. */
        if ((file_version == 1 && id > (uint16_t)BLOCK_PLANKS) || id >= (uint16_t)BLOCK_COUNT) {
            id = BLOCK_STONE;
        }
        c->blocks[i] = id;
    }
    free(buf);
    c->dirty = true;      /* Freshly loaded data still needs a mesh. */
    c->save_dirty = false; /* On-disk state matches memory now. */
    return 0;
}

/* Persist metadata + every save-dirty chunk. */
int world_save_all(const char *dir, World *w, const Player *p, Vec3 spawn, bool has_spawn, float day)
{
    if (dir == NULL || w == NULL || p == NULL) {
        return -1;
    }
    /* Falling blocks are transient world objects. Convert them back to their
     * deterministic landing cells before writing chunk snapshots so a save
     * cannot omit or duplicate a moving block. */
    if (!world_gravity_settle_all(w, w->gravity_replicate_changes)) {
        LOG_ERROR("world_save: could not safely settle falling blocks before saving");
        return -4;
    }
    WorldMeta m;
    world_meta_defaults(&m);
    size_t nl = strlen(w->name);
    if (nl >= sizeof(m.name)) {
        nl = sizeof(m.name) - 1;
    }
    memcpy(m.name, w->name, nl);
    m.name[nl] = '\0';
    m.seed = (int64_t)w->seed;
    m.mode = w->mode;
    m.terrain_version = w->terrain_version;
    m.px = p->pos.x;
    m.py = p->pos.y;
    m.pz = p->pos.z;
    m.yaw = p->yaw;
    m.pitch = p->pitch;
    m.day = day;
    m.has_player = true;
    m.health = p->health;
    m.hunger = p->hunger;
    m.spawn_x = spawn.x;
    m.spawn_y = spawn.y;
    m.spawn_z = spawn.z;
    m.has_spawn = has_spawn;
    for (int i = 0; i < WORLD_META_INV_SLOTS && i < INV_SIZE; ++i) {
        m.inv_items[i] = p->inv.slots[i].item;
        m.inv_counts[i] = p->inv.slots[i].count;
        m.inv_dur[i] = p->inv.slots[i].durability;
    }
    if (world_meta_write(dir, &m) != 0) {
        LOG_ERROR("world_save: metadata write failed (%s)", dir);
        return -2;
    }
    int chunk_fails = 0;
    size_t chunk_saved = 0;
    for (size_t i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        Chunk *c = w->chunks[i];
        if (c == NULL || !c->save_dirty) {
            continue;
        }
        if (world_save_write_chunk(dir, c) == 0) {
            c->save_dirty = false;
            ++chunk_saved;
        } else {
            ++chunk_fails;
            LOG_ERROR("world_save: chunk (%d,%d) write failed", c->cx, c->cz);
        }
    }
    LOG_INFO("world_save: meta + %zu chunk(s) saved, %d failure(s) (%s)", chunk_saved, chunk_fails, dir);
    return chunk_fails == 0 ? 0 : -3;
}

/* Delete a world tree: chunk bins, world.meta, then the directories. */
int world_save_delete(const char *dir)
{
    if (dir == NULL || dir[0] == '\0') {
        return -1;
    }
    char chunks[PATH_MAX_LEN];
    char names[256][64];
    size_t count = 0;
    if (path_join(chunks, sizeof(chunks), dir, "chunks") == 0 && path_is_dir(chunks)) {
        if (path_list_files(chunks, names, 256, &count) == 0) {
            for (size_t i = 0; i < count; ++i) {
                int fx = 0, fz = 0;
                /* Only delete files matching our chunk pattern. */
                if (!chunk_name_parse(names[i], &fx, &fz)) {
                    char skip[PATH_MAX_LEN];
                    if (path_join(skip, sizeof(skip), chunks, names[i]) == 0) {
                        LOG_WARN("world_save_delete: refusing foreign file %s", skip);
                    }
                    continue;
                }
                char full[PATH_MAX_LEN];
                if (path_join(full, sizeof(full), chunks, names[i]) == 0) {
                    path_remove_file(full);
                }
            }
        }
        path_remove_dir(chunks);
    }
    {
        char meta[PATH_MAX_LEN];
        if (path_join(meta, sizeof(meta), dir, WORLD_META_FILE) == 0) {
            path_remove_file(meta);
        }
    }
    {
        char ent[PATH_MAX_LEN];
        if (path_join(ent, sizeof(ent), dir, ENTITY_SAVE_FILE) == 0) {
            path_remove_file(ent);
        }
    }
    if (path_remove_dir(dir) != 0) {
        return -2;
    }
    return 0;
}
