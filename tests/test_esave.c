#include "test_main.h"

#include "core/path.h"
#include "game/entity.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/player.h"
#include "world/block.h"
#include "world/entity_save.h"

#include <stdio.h>
#include <string.h>

/* Scratch dir for entity-save tests. */
#define ESAVE_TMP "test_tmp_es"

/* Remove dir/entities.bin + dirs (best effort). */
static void esave_cleanup(const char *dir)
{
    char full[PATH_MAX_LEN];
    if (path_join(full, sizeof(full), dir, ENTITY_SAVE_FILE) == 0) {
        path_remove_file(full);
    }
    path_remove_dir(dir);
    path_remove_dir(ESAVE_TMP);
}

/* Write raw bytes as dir/entities.bin (for corruption tests). */
static int esave_write_raw(const char *dir, const unsigned char *buf, size_t n)
{
    char full[PATH_MAX_LEN];
    if (path_join(full, sizeof(full), dir, ENTITY_SAVE_FILE) != 0) {
        return -1;
    }
    if (path_mkdir_p(dir) != 0) {
        return -1;
    }
    FILE *f = fopen(full, "wb");
    if (f == NULL) {
        return -1;
    }
    size_t wrote = n > 0 ? fwrite(buf, 1, n, f) : 0;
    fclose(f);
    return wrote == n ? 0 : -1;
}

/* Little-endian append helpers for hand-crafted files. */
static void raw_u16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void raw_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static void raw_f32(unsigned char *p, float v)
{
    uint32_t u = 0;
    memcpy(&u, &v, sizeof(u));
    raw_u32(p, u);
}

/* Find the single active entity; NULL unless exactly one is active. */
static const ItemEntity *esave_only_active(const EntityPool *pool)
{
    const ItemEntity *found = NULL;
    for (int i = 0; i < ENTITY_MAX; ++i) {
        if (pool->items[i].active) {
            if (found != NULL) {
                return NULL;
            }
            found = &pool->items[i];
        }
    }
    return found;
}

/* Total items of one ID across all ACTIVE pool entities. */
static uint32_t esave_count_item(const EntityPool *pool, ItemId item)
{
    uint32_t total = 0;
    for (int i = 0; i < ENTITY_MAX; ++i) {
        if (pool->items[i].active && pool->items[i].stack.item == item) {
            total += pool->items[i].stack.count;
        }
    }
    return total;
}

/* Test: single entity round trip preserves item/count/pos/vel/age/delay.
 *
 * Returns: failure count.
 */
int test_esave_single(void)
{
    int failures = 0;
    const char *dir = ESAVE_TMP "/single";
    EntityPool pool;
    entity_pool_clear(&pool);
    ItemStack drop = {(ItemId)BLOCK_STONE, 5};
    int idx = entity_spawn(&pool, mmath_vec3(1.5f, 65.25f, -3.5f), &drop);
    TEST_ASSERT(idx >= 0);
    pool.items[idx].vel = mmath_vec3(0.5f, -2.0f, 0.25f);
    pool.items[idx].age = 12.5f;
    pool.items[idx].pickup_t = 0.0f;

    TEST_ASSERT(entity_save_write(dir, &pool, NULL) == 0);
    EntityPool back;
    TEST_ASSERT(entity_save_read(dir, &back, NULL) == 0);
    const ItemEntity *e = esave_only_active(&back);
    TEST_ASSERT(e != NULL);
    if (e != NULL) {
        TEST_ASSERT(e->stack.item == (ItemId)BLOCK_STONE && e->stack.count == 5);
        TEST_ASSERT_FLOAT_EQ(e->pos.x, 1.5f, 1e-5f);
        TEST_ASSERT_FLOAT_EQ(e->pos.y, 65.25f, 1e-5f);
        TEST_ASSERT_FLOAT_EQ(e->pos.z, -3.5f, 1e-5f);
        TEST_ASSERT_FLOAT_EQ(e->vel.x, 0.5f, 1e-5f);
        TEST_ASSERT_FLOAT_EQ(e->vel.y, -2.0f, 1e-5f);
        TEST_ASSERT_FLOAT_EQ(e->age, 12.5f, 1e-5f);
        TEST_ASSERT_FLOAT_EQ(e->pickup_t, 0.0f, 1e-5f);
    }
    esave_cleanup(dir);
    return failures;
}

/* Test: multiple entities (negative coords, partial + maximum stacks).
 *
 * Returns: failure count.
 */
int test_esave_multiple(void)
{
    int failures = 0;
    const char *dir = ESAVE_TMP "/multi";
    EntityPool pool;
    entity_pool_clear(&pool);
    ItemStack a = {(ItemId)BLOCK_DIRT, 7}; /* Partial stack. */
    ItemStack b = {(ItemId)BLOCK_STONE, 64}; /* Maximum stack. */
    ItemStack c = {ITEM_COAL, 3};
    TEST_ASSERT(entity_spawn(&pool, mmath_vec3(-100.5f, 70.0f, -200.25f), &a) >= 0);
    TEST_ASSERT(entity_spawn(&pool, mmath_vec3(0.0f, 80.0f, 0.0f), &b) >= 0);
    TEST_ASSERT(entity_spawn(&pool, mmath_vec3(3000.0f, 65.0f, 4000.0f), &c) >= 0);
    TEST_ASSERT(entity_save_write(dir, &pool, NULL) == 0);

    EntityPool back;
    TEST_ASSERT(entity_save_read(dir, &back, NULL) == 0);
    TEST_ASSERT(entity_active_count(&back) == 3);
    TEST_ASSERT(esave_count_item(&back, (ItemId)BLOCK_DIRT) == 7);
    TEST_ASSERT(esave_count_item(&back, (ItemId)BLOCK_STONE) == 64);
    TEST_ASSERT(esave_count_item(&back, ITEM_COAL) == 3);
    esave_cleanup(dir);
    return failures;
}

/* Test: corrupt files are rejected safely (pool cleared, no crash).
 *
 * Returns: failure count.
 */
int test_esave_corrupt(void)
{
    int failures = 0;
    const char *dir = ESAVE_TMP "/corrupt";
    EntityPool pool;

    /* Bad magic. */
    unsigned char bad_magic[10] = {'X', 'X', 'X', 'X', 1, 0, 0, 0, 0, 0};
    TEST_ASSERT(esave_write_raw(dir, bad_magic, sizeof(bad_magic)) == 0);
    TEST_ASSERT(entity_save_read(dir, &pool, NULL) != 0);
    TEST_ASSERT(entity_active_count(&pool) == 0);

    /* Unsupported version. */
    unsigned char bad_ver[10] = {'M', 'N', 'C', 'E', 99, 0, 0, 0, 0, 0};
    TEST_ASSERT(esave_write_raw(dir, bad_ver, sizeof(bad_ver)) == 0);
    TEST_ASSERT(entity_save_read(dir, &pool, NULL) != 0);

    /* Truncated record (header claims 1, body short). */
    unsigned char trunc[10 + 38];
    trunc[0] = 'M';
    trunc[1] = 'N';
    trunc[2] = 'C';
    trunc[3] = 'E';
    raw_u16(trunc + 4, 1);
    raw_u32(trunc + 6, 1);
    memset(trunc + 10, 0, 38);
    TEST_ASSERT(esave_write_raw(dir, trunc, sizeof(trunc) - 5) == 0);
    TEST_ASSERT(entity_save_read(dir, &pool, NULL) != 0);
    TEST_ASSERT(entity_active_count(&pool) == 0);

    /* One valid record builder (38 bytes): stone x1 at y=70. */
    unsigned char good[38];
    raw_u16(good + 0, (uint16_t)BLOCK_STONE);
    raw_u16(good + 2, 1);
    raw_f32(good + 4, 0.0f);
    raw_f32(good + 8, 70.0f);
    raw_f32(good + 12, 0.0f);
    raw_f32(good + 16, 0.0f);
    raw_f32(good + 20, 0.0f);
    raw_f32(good + 24, 0.0f);
    raw_u16(good + 28, 0);
    raw_f32(good + 30, 0.0f);
    raw_f32(good + 34, 0.0f);

    /* Trailing garbage after a valid record. */
    unsigned char trail[10 + 38 + 3];
    trail[0] = 'M';
    trail[1] = 'N';
    trail[2] = 'C';
    trail[3] = 'E';
    raw_u16(trail + 4, 1);
    raw_u32(trail + 6, 1);
    memcpy(trail + 10, good, 38);
    trail[48] = 0xAA;
    trail[49] = 0xBB;
    trail[50] = 0xCC;
    TEST_ASSERT(esave_write_raw(dir, trail, sizeof(trail)) == 0);
    TEST_ASSERT(entity_save_read(dir, &pool, NULL) != 0);
    TEST_ASSERT(entity_active_count(&pool) == 0);

    /* Excessive count (1000 claimed, file capped at header). */
    unsigned char big[10];
    big[0] = 'M';
    big[1] = 'N';
    big[2] = 'C';
    big[3] = 'E';
    raw_u16(big + 4, 1);
    raw_u32(big + 6, 1000);
    TEST_ASSERT(esave_write_raw(dir, big, sizeof(big)) == 0);
    TEST_ASSERT(entity_save_read(dir, &pool, NULL) != 0);

    /* Invalid ItemId (999 is unregistered). */
    unsigned char bad_id[10 + 38];
    bad_id[0] = 'M';
    bad_id[1] = 'N';
    bad_id[2] = 'C';
    bad_id[3] = 'E';
    raw_u16(bad_id + 4, 1);
    raw_u32(bad_id + 6, 1);
    memcpy(bad_id + 10, good, 38);
    raw_u16(bad_id + 10, 999);
    TEST_ASSERT(esave_write_raw(dir, bad_id, sizeof(bad_id)) == 0);
    TEST_ASSERT(entity_save_read(dir, &pool, NULL) != 0);
    TEST_ASSERT(entity_active_count(&pool) == 0);

    /* Invalid count (zero, and over max stack). */
    unsigned char bad_count[10 + 38];
    memcpy(bad_count, bad_id, 10);
    memcpy(bad_count + 10, good, 38);
    raw_u16(bad_count + 12, 0);
    TEST_ASSERT(esave_write_raw(dir, bad_count, sizeof(bad_count)) == 0);
    TEST_ASSERT(entity_save_read(dir, &pool, NULL) != 0);
    raw_u16(bad_count + 12, 65);
    TEST_ASSERT(esave_write_raw(dir, bad_count, sizeof(bad_count)) == 0);
    TEST_ASSERT(entity_save_read(dir, &pool, NULL) != 0);

    /* Invalid wear (non-zero on non-damageable stone; record offset 28
     * = file offset 38). */
    unsigned char bad_wear[10 + 38];
    memcpy(bad_wear, bad_id, 10);
    memcpy(bad_wear + 10, good, 38);
    raw_u16(bad_wear + 38, 7);
    TEST_ASSERT(esave_write_raw(dir, bad_wear, sizeof(bad_wear)) == 0);
    TEST_ASSERT(entity_save_read(dir, &pool, NULL) != 0);
    TEST_ASSERT(entity_active_count(&pool) == 0);

    esave_cleanup(dir);
    return failures;
}

/* Test: missing file (M5/M6 worlds) reads as zero entities, pool cleared.
 *
 * Returns: failure count.
 */
int test_esave_missing(void)
{
    int failures = 0;
    const char *dir = ESAVE_TMP "/nodir_xyz";
    EntityPool pool;
    entity_pool_clear(&pool);
    ItemStack drop = {(ItemId)BLOCK_STONE, 2};
    TEST_ASSERT(entity_spawn(&pool, mmath_vec3(0.0f, 70.0f, 0.0f), &drop) >= 0);
    TEST_ASSERT(entity_save_read(dir, &pool, NULL) == 0);
    TEST_ASSERT(entity_active_count(&pool) == 0);
    return failures;
}

/* Test: despawned and picked-up entities are not persisted.
 *
 * Returns: failure count.
 */
int test_esave_transient(void)
{
    int failures = 0;
    const char *dir = ESAVE_TMP "/transient";
    EntityPool pool;
    entity_pool_clear(&pool);
    /* One entity aged past its lifetime (despawned on update). */
    ItemStack old = {(ItemId)BLOCK_DIRT, 4};
    TEST_ASSERT(entity_spawn(&pool, mmath_vec3(0.0f, 100.0f, 0.0f), &old) >= 0);
    for (int i = 0; i < 1205; ++i) {
        entity_update(&pool, NULL, 0.25f);
    }
    TEST_ASSERT(entity_active_count(&pool) == 0);
    /* One entity picked up by the player. */
    Player p;
    player_init(&p);
    ItemStack fresh = {(ItemId)BLOCK_STONE, 3};
    int idx = entity_spawn(&pool, p.pos, &fresh);
    TEST_ASSERT(idx >= 0);
    pool.items[idx].pickup_t = 0.0f;
    TEST_ASSERT(entity_try_pickup(&pool, &p, ENTITY_PICKUP_RADIUS, NULL) == 1);
    TEST_ASSERT(entity_save_write(dir, &pool, NULL) == 0);
    EntityPool back;
    TEST_ASSERT(entity_save_read(dir, &back, NULL) == 0);
    TEST_ASSERT(entity_active_count(&back) == 0);
    esave_cleanup(dir);
    return failures;
}
