#include "world/entity_save.h"
#include "core/log.h"
#include "core/path.h"
#include "game/entity.h"
#include "game/item.h"
#include "game/mob.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Record size: 2+2+12+12+2+4+4 = 38 bytes. Header: 4+2+4 = 10 bytes. */
#define ENTITY_REC_BYTES 38u
/* Mob payload: 1+1+2+12+4+4+4 = 28 bytes (kind byte separate). */
#define ENTITY_MOB_REC_BYTES 28u
#define ENTITY_HDR_BYTES 10u
#define ENTITY_POS_BOUND 1000000.0f
#define ENTITY_VEL_BOUND 100.0f

/* Little-endian codecs (no alignment or padding assumptions). */
static void put_u16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static void put_f32(unsigned char *p, float v)
{
    uint32_t u = 0;
    memcpy(&u, &v, sizeof(u));
    put_u32(p, u);
}

static uint16_t get_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float get_f32(const unsigned char *p)
{
    uint32_t u = get_u32(p);
    float v = 0.0f;
    memcpy(&v, &u, sizeof(v));
    return v;
}

/* Write all live entities (items, then alive mobs). */
int entity_save_write(const char *dir, const EntityPool *pool, const MobPool *mobs)
{
    if (dir == NULL) {
        return -1;
    }
    if (path_mkdir_p(dir) != 0) {
        return -2;
    }
    char path[PATH_MAX_LEN];
    char tmp[PATH_MAX_LEN + 8];
    if (path_join(path, sizeof(path), dir, ENTITY_SAVE_FILE) != 0) {
        return -2;
    }
    size_t pl = strlen(path);
    if (pl + 5 >= sizeof(tmp)) {
        return -2;
    }
    memcpy(tmp, path, pl + 1);
    memcpy(tmp + pl, ".tmp", 5);

    /* Collect live entities first (bounded; counts can only shrink). */
    const ItemEntity *live[ENTITY_MAX];
    uint32_t nitems = 0;
    if (pool != NULL) {
        for (int i = 0; i < ENTITY_MAX; ++i) {
            const ItemEntity *e = &pool->items[i];
            if (!e->active || stack_is_empty(&e->stack)) {
                continue;
            }
            live[nitems++] = e;
        }
    }
    const Mob *livemobs[MOB_MAX];
    uint32_t nmobs = 0;
    if (mobs != NULL) {
        for (int i = 0; i < MOB_MAX; ++i) {
            const Mob *m = &mobs->mobs[i];
            if (!m->active || m->dead) {
                continue;
            }
            livemobs[nmobs++] = m;
        }
    }

    FILE *f = fopen(tmp, "wb");
    bool direct = false;
    if (f == NULL) {
        f = fopen(path, "wb");
        direct = true;
        if (f == NULL) {
            return -3;
        }
    }
    unsigned char hdr[ENTITY_HDR_BYTES];
    hdr[0] = 'M';
    hdr[1] = 'N';
    hdr[2] = 'C';
    hdr[3] = 'E';
    put_u16(hdr + 4, (uint16_t)ENTITY_SAVE_VERSION);
    put_u32(hdr + 6, nitems + nmobs);
    if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f);
        remove(tmp);
        return -4;
    }
    for (uint32_t i = 0; i < nitems; ++i) {
        const ItemEntity *e = live[i];
        unsigned char rec[1 + ENTITY_REC_BYTES];
        rec[0] = (unsigned char)ENTITY_ITEM_DROP;
        put_u16(rec + 1 + 0, e->stack.item);
        put_u16(rec + 1 + 2, e->stack.count);
        put_f32(rec + 1 + 4, e->pos.x);
        put_f32(rec + 1 + 8, e->pos.y);
        put_f32(rec + 1 + 12, e->pos.z);
        put_f32(rec + 1 + 16, e->vel.x);
        put_f32(rec + 1 + 20, e->vel.y);
        put_f32(rec + 1 + 24, e->vel.z);
        put_u16(rec + 1 + 28, e->stack.durability);
        put_f32(rec + 1 + 30, e->age);
        put_f32(rec + 1 + 34, e->pickup_t);
        if (fwrite(rec, 1, sizeof(rec), f) != sizeof(rec)) {
            fclose(f);
            remove(tmp);
            return -4;
        }
    }
    for (uint32_t i = 0; i < nmobs; ++i) {
        const Mob *m = livemobs[i];
        /* Full record: kind byte + 28-byte payload (type repeated: the
         * kind dispatches, the payload type validates). */
        unsigned char rec[1 + ENTITY_MOB_REC_BYTES];
        rec[0] = (unsigned char)m->type;
        rec[1] = (unsigned char)m->type;
        rec[2] = (unsigned char)m->state;
        rec[3] = 0;
        rec[4] = 0;
        put_f32(rec + 5, m->pos.x);
        put_f32(rec + 9, m->pos.y);
        put_f32(rec + 13, m->pos.z);
        put_f32(rec + 17, m->yaw);
        put_f32(rec + 21, m->health);
        put_f32(rec + 25, m->state_t);
        if (fwrite(rec, 1, sizeof(rec), f) != sizeof(rec)) {
            fclose(f);
            remove(tmp);
            return -4;
        }
    }
    if (fclose(f) != 0) {
        remove(tmp);
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

/* Validate one decoded record (finite numbers, sane ranges, real item,
 * plausible wear). Out-of-range values mean corruption or tampering:
 * reject the file (whole-file reject, same as any other bad field).
 */
static bool entity_rec_valid(uint16_t item, uint16_t count, uint16_t durability, const float f[8])
{
    if (!item_is_valid(item)) {
        return false;
    }
    const ItemInfo *info = item_get_info(item);
    if (count == 0 || count > info->max_stack) {
        return false;
    }
    if (info->max_durability == 0) {
        if (durability != 0) {
            return false;
        }
    } else if (durability > info->max_durability) {
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        if (!isfinite(f[i])) {
            return false;
        }
    }
    if (!(f[0] >= -ENTITY_POS_BOUND && f[0] <= ENTITY_POS_BOUND) ||
        !(f[1] >= -ENTITY_POS_BOUND && f[1] <= ENTITY_POS_BOUND) ||
        !(f[2] >= -ENTITY_POS_BOUND && f[2] <= ENTITY_POS_BOUND)) {
        return false;
    }
    if (!(f[3] >= -ENTITY_VEL_BOUND && f[3] <= ENTITY_VEL_BOUND) ||
        !(f[4] >= -ENTITY_VEL_BOUND && f[4] <= ENTITY_VEL_BOUND) ||
        !(f[5] >= -ENTITY_VEL_BOUND && f[5] <= ENTITY_VEL_BOUND)) {
        return false;
    }
    if (!(f[6] >= 0.0f && f[6] <= ENTITY_LIFETIME)) {
        return false;
    }
    if (!(f[7] >= 0.0f && f[7] <= ENTITY_PICKUP_DELAY)) {
        return false;
    }
    return true;
}

/* Validate one decoded mob record (finite numbers, known type/state,
 * plausible health). Anything else means corruption: reject the file.
 */
static bool entity_mob_valid(uint8_t type, uint8_t state, const unsigned char *reserved,
                             const float f[5])
{
    if (type != (uint8_t)ENTITY_COW && type != (uint8_t)ENTITY_GLOOMSTALKER &&
        type != (uint8_t)ENTITY_SKELETON) {
        return false;
    }
    if (state > (uint8_t)MOB_STATE_AIM || state == (uint8_t)MOB_STATE_DEAD) {
        return false; /* Dead mobs never persist. */
    }
    if (reserved[0] != 0 || reserved[1] != 0) {
        return false;
    }
    for (int i = 0; i < 5; ++i) {
        if (!isfinite(f[i])) {
            return false;
        }
    }
    if (!(f[0] >= -ENTITY_POS_BOUND && f[0] <= ENTITY_POS_BOUND) ||
        !(f[1] >= -ENTITY_POS_BOUND && f[1] <= ENTITY_POS_BOUND) ||
        !(f[2] >= -ENTITY_POS_BOUND && f[2] <= ENTITY_POS_BOUND)) {
        return false;
    }
    const MobDefinition *def = mob_definition((EntityType)type);
    if (def->type == ENTITY_NONE) {
        return false;
    }
    if (!(f[3] > 0.0f && f[3] <= def->max_health)) {
        return false;
    }
    if (!(f[4] >= 0.0f && f[4] <= 3600.0f)) {
        return false;
    }
    return true;
}

/* Read one v1 item record body (38 bytes, no kind byte). Shared with the
 * v1 legacy path below. Returns 0 on success (pool slot filled).
 */
static int entity_read_item_v1(EntityPool *pool, const unsigned char *rec, const char *path,
                               uint32_t idx)
{
    uint16_t item = get_u16(rec + 0);
    uint16_t count = get_u16(rec + 2);
    uint16_t durability = get_u16(rec + 28);
    float fv[8];
    for (int k = 0; k < 3; ++k) {
        fv[k] = get_f32(rec + 4 + (size_t)k * 4);
    }
    for (int k = 3; k < 6; ++k) {
        fv[k] = get_f32(rec + 16 + (size_t)(k - 3) * 4);
    }
    fv[6] = get_f32(rec + 30);
    fv[7] = get_f32(rec + 34);
    if (!entity_rec_valid(item, count, durability, fv)) {
        LOG_ERROR("entity_save: record %u invalid (%s)", (unsigned)idx, path);
        return -1;
    }
    int slot = entity_spawn(pool, mmath_vec3(fv[0], fv[1], fv[2]),
                            &(ItemStack){item, count, durability});
    if (slot < 0) {
        LOG_ERROR("entity_save: pool exhausted at record %u (%s)", (unsigned)idx, path);
        return -2;
    }
    pool->items[slot].vel = mmath_vec3(fv[3], fv[4], fv[5]);
    pool->items[slot].age = fv[6];
    pool->items[slot].pickup_t = fv[7];
    return 0;
}

/* Read one v2 mob record body (28 bytes). Returns 0 on success. */
static int entity_read_mob_v2(MobPool *mobs, const unsigned char *rec, const char *path, uint32_t idx)
{
    uint8_t type = rec[0];
    uint8_t state = rec[1];
    float f[5];
    f[0] = get_f32(rec + 4);
    f[1] = get_f32(rec + 8);
    f[2] = get_f32(rec + 12);
    float yaw = get_f32(rec + 16);
    f[3] = get_f32(rec + 20);
    f[4] = get_f32(rec + 24);
    if (!entity_mob_valid(type, state, rec + 2, f)) {
        LOG_ERROR("entity_save: mob record %u invalid (%s)", (unsigned)idx, path);
        return -1;
    }
    EntityId id = mob_spawn(mobs, (EntityType)type, mmath_vec3(f[0], f[1], f[2]), yaw);
    if (id == ENTITY_ID_NULL) {
        LOG_ERROR("entity_save: mob pool exhausted at record %u (%s)", (unsigned)idx, path);
        return -2;
    }
    Mob *m = mob_resolve(mobs, id);
    if (m == NULL) {
        return -2;
    }
    m->health = f[3];
    m->state = (int)state;
    m->state_t = f[4];
    if (m->state != MOB_STATE_IDLE) {
        m->ai_t = 0.0f; /* Re-think promptly after load. */
    }
    return 0;
}

/* Read entities (missing file = clean zero-entity success). */
int entity_save_read(const char *dir, EntityPool *pool, MobPool *mobs)
{
    if (dir == NULL || pool == NULL) {
        return -1;
    }
    entity_pool_clear(pool);
    if (mobs != NULL) {
        mob_pool_init(mobs, 0);
    }
    char path[PATH_MAX_LEN];
    if (path_join(path, sizeof(path), dir, ENTITY_SAVE_FILE) != 0) {
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0; /* M5/M6/M7 worlds predate entity saves: no entities. */
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -2;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > (long)(ENTITY_HDR_BYTES + (size_t)ENTITY_SAVE_MAX_RECORDS * (1 + 38))) {
        fclose(f);
        LOG_ERROR("entity_save: bad size %ld (%s)", sz, path);
        return -3;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -2;
    }
    size_t total = (size_t)sz;
    unsigned char *buf = (unsigned char *)malloc(total > 0 ? total : 1);
    if (buf == NULL) {
        fclose(f);
        return -4;
    }
    size_t got = total > 0 ? fread(buf, 1, total, f) : 0;
    fclose(f);
    if (got != total) {
        free(buf);
        LOG_ERROR("entity_save: short read (%s)", path);
        return -5;
    }
    int rc = -6;
    if (total >= ENTITY_HDR_BYTES && buf[0] == 'M' && buf[1] == 'N' && buf[2] == 'C' && buf[3] == 'E') {
        uint16_t ver = get_u16(buf + 4);
        uint32_t n = get_u32(buf + 6);
        if (ver == 1 && n <= (uint32_t)ENTITY_MAX &&
            total == ENTITY_HDR_BYTES + (size_t)n * ENTITY_REC_BYTES) {
            /* Legacy v1 (M6.1/M7 items only). */
            rc = 0;
            for (uint32_t i = 0; i < n && rc == 0; ++i) {
                const unsigned char *rec = buf + ENTITY_HDR_BYTES + (size_t)i * ENTITY_REC_BYTES;
                if (entity_read_item_v1(pool, rec, path, i) != 0) {
                    rc = -7;
                }
            }
        } else if (ver == (uint16_t)ENTITY_SAVE_VERSION && n <= (uint32_t)ENTITY_SAVE_MAX_RECORDS) {
            rc = 0;
            size_t off = ENTITY_HDR_BYTES;
            for (uint32_t i = 0; i < n && rc == 0; ++i) {
                if (off + 1 > total) {
                    LOG_ERROR("entity_save: truncated record %u (%s)", (unsigned)i, path);
                    rc = -7;
                    break;
                }
                uint8_t kind = buf[off];
                if (kind == (uint8_t)ENTITY_ITEM_DROP) {
                    if (off + 1 + ENTITY_REC_BYTES > total) {
                        LOG_ERROR("entity_save: truncated item %u (%s)", (unsigned)i, path);
                        rc = -7;
                        break;
                    }
                    if (entity_read_item_v1(pool, buf + off + 1, path, i) != 0) {
                        rc = -7;
                        break;
                    }
                    off += 1 + ENTITY_REC_BYTES;
                } else if (kind == (uint8_t)ENTITY_COW || kind == (uint8_t)ENTITY_GLOOMSTALKER ||
                           kind == (uint8_t)ENTITY_SKELETON) {
                    if (mobs == NULL) {
                        LOG_ERROR("entity_save: mob record %u with no mob pool (%s)", (unsigned)i,
                                  path);
                        rc = -7;
                        break;
                    }
                    if (off + 1 + ENTITY_MOB_REC_BYTES > total) {
                        LOG_ERROR("entity_save: truncated mob %u (%s)", (unsigned)i, path);
                        rc = -7;
                        break;
                    }
                    if (entity_read_mob_v2(mobs, buf + off + 1, path, i) != 0) {
                        rc = -7;
                        break;
                    }
                    off += 1 + ENTITY_MOB_REC_BYTES;
                } else {
                    LOG_ERROR("entity_save: unknown kind %u at record %u (%s)", (unsigned)kind,
                              (unsigned)i, path);
                    rc = -7;
                    break;
                }
            }
            if (rc == 0 && off != total) {
                LOG_ERROR("entity_save: trailing bytes (%s)", path);
                rc = -7;
            }
        } else {
            LOG_ERROR("entity_save: bad header (ver %u, count %u, size %zu) (%s)", (unsigned)ver,
                      (unsigned)n, total, path);
        }
    } else {
        LOG_ERROR("entity_save: bad magic (%s)", path);
    }
    if (rc != 0) {
        entity_pool_clear(pool);
        if (mobs != NULL) {
            mob_pool_init(mobs, 0);
        }
    }
    free(buf);
    return rc;
}
