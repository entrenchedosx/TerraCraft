#include "test_main.h"

#include "core/path.h"
#include "game/entity.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/player.h"
#include "game/raycast.h"
#include "game/survival.h"
#include "render/mesher.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"
#include "world/world_gen.h"
#include "world/world_meta.h"
#include "world/world_save.h"

#include <stdio.h>
#include <string.h>

/* Test: mining break times follow the MC formula (divisor 30 when
 * harvestable — hardness * 1.5 / speed — vs divisor 100, i.e.
 * hardness * 5, for pickaxe-class blocks without a sufficient pickaxe;
 * hardness 0 breaks instantly) and unbreakable blocks report -1.
 *
 * Returns: failure count.
 */
int test_survival_mine_time(void)
{
    int failures = 0;
    /* Stone hardness 1.5 (pickaxe-class): hand 7.5 s, wood pick 1.125 s,
     * stone pick 0.5625 s. */
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_STONE, ITEM_NONE), 7.5f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_STONE, ITEM_WOOD_PICKAXE), 1.125f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_STONE, ITEM_STONE_PICKAXE), 0.5625f, 1e-4f);
    /* Wrong tool tier/category on pickaxe-class counts as unharvestable. */
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_STONE, ITEM_WOOD_AXE), 7.5f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_STONE, ITEM_WOOD_SHOVEL), 7.5f, 1e-4f);
    /* Dirt (0.5, shovel): hand 0.75 s (harvestable bare-handed),
     * wood shovel 0.375 s, wrong-category tool same as hand. */
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_DIRT, ITEM_NONE), 0.75f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_DIRT, ITEM_WOOD_SHOVEL), 0.375f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_DIRT, ITEM_WOOD_AXE), 0.75f, 1e-4f);
    /* Wood (2.0, axe): hand 3 s, wood axe 1.5 s. */
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_WOOD, ITEM_NONE), 3.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_WOOD, ITEM_WOOD_AXE), 1.5f, 1e-4f);
    /* Leaves (0.2, no tool): hand 0.3 s. */
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_LEAVES, ITEM_NONE), 0.3f, 1e-4f);
    /* Hardness 0 breaks instantly. */
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_TORCH, ITEM_NONE), 0.0f, 1e-4f);
    /* Unbreakable blocks never finish. */
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_BEDROCK, ITEM_STONE_PICKAXE), -1.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_mine_time(BLOCK_BEDROCK, ITEM_NONE), -1.0f, 1e-4f);
    return failures;
}

/* Test: block drops follow the MC harvest rule (pickaxe-class blocks need
 * a sufficient pickaxe; glass/leaves/bedrock drop nothing).
 *
 * Returns: failure count.
 */
int test_survival_drops(void)
{
    int failures = 0;
    ItemStack stone_hand = survival_block_drop(BLOCK_STONE, ITEM_NONE);
    TEST_ASSERT(stack_is_empty(&stone_hand)); /* Punched stone: no drop. */
    ItemStack stone_pick = survival_block_drop(BLOCK_STONE, ITEM_WOOD_PICKAXE);
    TEST_ASSERT(stone_pick.item == (ItemId)BLOCK_STONE && stone_pick.count == 1);
    ItemStack coal_hand = survival_block_drop(BLOCK_COAL_ORE, ITEM_NONE);
    TEST_ASSERT(stack_is_empty(&coal_hand));
    ItemStack coal_pick = survival_block_drop(BLOCK_COAL_ORE, ITEM_WOOD_PICKAXE);
    TEST_ASSERT(coal_pick.item == ITEM_COAL && coal_pick.count == 1);
    /* Iron needs stone tier: wood pick yields nothing. */
    ItemStack iron_wood = survival_block_drop(BLOCK_IRON_ORE, ITEM_WOOD_PICKAXE);
    TEST_ASSERT(stack_is_empty(&iron_wood));
    ItemStack iron_stone = survival_block_drop(BLOCK_IRON_ORE, ITEM_STONE_PICKAXE);
    TEST_ASSERT(iron_stone.item == (ItemId)BLOCK_IRON_ORE && iron_stone.count == 1);
    /* Non-pickaxe blocks drop regardless of tool. */
    ItemStack dirt_hand = survival_block_drop(BLOCK_DIRT, ITEM_NONE);
    TEST_ASSERT(dirt_hand.item == (ItemId)BLOCK_DIRT && dirt_hand.count == 1);
    ItemStack glass_pick = survival_block_drop(BLOCK_GLASS, ITEM_STONE_PICKAXE);
    ItemStack leaves = survival_block_drop(BLOCK_LEAVES, ITEM_NONE);
    ItemStack bedrock = survival_block_drop(BLOCK_BEDROCK, ITEM_STONE_PICKAXE);
    TEST_ASSERT(stack_is_empty(&glass_pick));
    TEST_ASSERT(stack_is_empty(&leaves));
    TEST_ASSERT(stack_is_empty(&bedrock));
    TEST_ASSERT(survival_block_drop(9999, ITEM_NONE).item == ITEM_NONE); /* Bad ID safe. */
    return failures;
}

/* Test: held-button mining accumulates on a stable target, resets on held
 * switch, completes with an event, and refuses unbreakable targets.
 *
 * Returns: failure count.
 */
int test_survival_mining(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    w->seed = 42L;
    TEST_ASSERT(world_generate_chunk(w, 0, 0) == 0);
    Chunk *c = world_get_chunk(w, 0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        world_destroy(w);
        return failures + 1;
    }
    chunk_set_block(c, 2, 40, 2, BLOCK_STONE);
    chunk_set_block(c, 3, 40, 3, BLOCK_BEDROCK);
    TEST_ASSERT(world_get_block(w, 2, 40, 2) == BLOCK_STONE);
    TEST_ASSERT(world_get_block(w, 3, 40, 3) == BLOCK_BEDROCK);

    Player p;
    player_init(&p);
    MineResult out;
    /* First touch latches the target (hand on stone: 7.5 s needed). */
    survival_mine_update(&p, w, 2, 40, 2, ITEM_NONE, 0.0f, &out);
    TEST_ASSERT(!out.finished && p.mine_active);
    TEST_ASSERT_FLOAT_EQ(p.mine_need, 7.5f, 1e-4f);
    /* Partial progress keeps the block alive. */
    survival_mine_update(&p, w, 2, 40, 2, ITEM_NONE, 1.0f, &out);
    TEST_ASSERT(!out.finished && p.mine_active);
    TEST_ASSERT_FLOAT_EQ(p.mine_progress, 1.0f, 1e-4f);
    /* Held-item switch retargets (progress resets, no completion). */
    survival_mine_update(&p, w, 2, 40, 2, ITEM_WOOD_PICKAXE, 5.0f, &out);
    TEST_ASSERT(!out.finished && p.mine_active);
    TEST_ASSERT_FLOAT_EQ(p.mine_progress, 0.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(p.mine_need, 1.125f, 1e-4f);
    /* One wood-pickaxe need finishes with a completion event. */
    survival_mine_update(&p, w, 2, 40, 2, ITEM_WOOD_PICKAXE, 1.125f, &out);
    TEST_ASSERT(out.finished && out.block == BLOCK_STONE);
    TEST_ASSERT(out.bx == 2 && out.by == 40 && out.bz == 2);
    TEST_ASSERT(!p.mine_active);
    /* Unbreakable target: latched off, never finishes. */
    survival_mine_update(&p, w, 3, 40, 3, ITEM_STONE_PICKAXE, 0.0f, &out);
    TEST_ASSERT(!out.finished && !p.mine_active);
    survival_mine_update(&p, w, 3, 40, 3, ITEM_STONE_PICKAXE, 30.0f, &out);
    TEST_ASSERT(!out.finished && !p.mine_active);
    world_destroy(w);
    return failures;
}

/* Test: damage/heal gates (creative immunity, death latch, heal clamp).
 *
 * Returns: failure count.
 */
int test_survival_damage_heal(void)
{
    int failures = 0;
    TEST_ASSERT(!survival_is_creative(NULL));
    Player p;
    player_init(&p);
    TEST_ASSERT(!survival_is_creative(&p));
    p.mode = 1; /* WORLD_MODE_CREATIVE. */
    TEST_ASSERT(survival_is_creative(&p));
    survival_damage_player(&p, 50.0f);
    TEST_ASSERT_FLOAT_EQ(p.health, 20.0f, 1e-4f);
    TEST_ASSERT(!p.dead);

    p.mode = 0;
    survival_damage_player(&p, 5.0f);
    TEST_ASSERT_FLOAT_EQ(p.health, 15.0f, 1e-4f);
    survival_heal_player(&p, 100.0f);
    TEST_ASSERT_FLOAT_EQ(p.health, 20.0f, 1e-4f);
    survival_heal_player(&p, 1.0f);
    TEST_ASSERT_FLOAT_EQ(p.health, 20.0f, 1e-4f);
    survival_damage_player(&p, 25.0f);
    TEST_ASSERT_FLOAT_EQ(p.health, 0.0f, 1e-4f);
    TEST_ASSERT(p.dead);
    /* Dead players ignore damage and healing alike. */
    survival_damage_player(&p, 5.0f);
    TEST_ASSERT_FLOAT_EQ(p.health, 0.0f, 1e-4f);
    survival_heal_player(&p, 5.0f);
    TEST_ASSERT_FLOAT_EQ(p.health, 0.0f, 1e-4f);
    TEST_ASSERT(p.dead);
    return failures;
}

/* Test: fall damage free allowance (3 blocks, MC) then 1 HP per block.
 *
 * Returns: failure count.
 */
int test_survival_fall(void)
{
    int failures = 0;
    TEST_ASSERT_FLOAT_EQ(survival_fall_damage(0.0f), 0.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_fall_damage(2.99f), 0.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_fall_damage(3.0f), 0.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_fall_damage(4.0f), 1.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(survival_fall_damage(10.0f), 7.0f, 1e-4f);
    return failures;
}

/* Test: hunger has no passive drain (MC: standing still never starves);
 * regen heals at high hunger, starving at zero hunger damages, activity
 * burns via exhaustion, creative skips all timers.
 *
 * Returns: failure count.
 */
int test_survival_hunger(void)
{
    int failures = 0;
    Player p;
    player_init(&p);
    /* An hour idle costs nothing. */
    survival_hunger_update(&p, 3600.0f);
    TEST_ASSERT_FLOAT_EQ(p.hunger, 20.0f, 1e-4f);
    /* Regen: hurt but well-fed heals +1 per 4 s. */
    p.health = 10.0f;
    p.hunger = 20.0f;
    p.hunger_t = 0.0f;
    p.regen_t = 0.0f;
    p.exhaustion = 0.0f;
    survival_hunger_update(&p, SURVIVAL_REGEN_PERIOD);
    TEST_ASSERT_FLOAT_EQ(p.health, 11.0f, 1e-4f);
    /* Starve: empty hunger damages instead. */
    p.health = 10.0f;
    p.hunger = 0.0f;
    p.hunger_t = 0.0f;
    p.regen_t = 0.0f;
    p.exhaustion = 0.0f;
    survival_hunger_update(&p, SURVIVAL_STARVE_PERIOD);
    TEST_ASSERT_FLOAT_EQ(p.health, 9.0f, 1e-4f);
    /* Exhaustion burn: 8 strain ticks 0.5 each burn 1 hunger. */
    p.health = 20.0f;
    p.hunger = 10.0f;
    p.hunger_t = 0.0f;
    p.regen_t = 0.0f;
    p.exhaustion = 0.0f;
    survival_add_exhaustion(&p, 2.0f);
    survival_add_exhaustion(&p, 2.0f);
    survival_hunger_update(&p, 0.5f);
    TEST_ASSERT_FLOAT_EQ(p.hunger, 9.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(p.exhaustion, 0.0f, 1e-4f);
    /* Creative: timers frozen. */
    p.mode = 1;
    p.health = 5.0f;
    p.hunger = 5.0f;
    p.hunger_t = 0.0f;
    p.regen_t = 0.0f;
    survival_hunger_update(&p, 120.0f);
    TEST_ASSERT_FLOAT_EQ(p.health, 5.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(p.hunger, 5.0f, 1e-4f);
    return failures;
}

/* Test: respawn restores vitals, clears motion/timers, teleports to spawn.
 *
 * Returns: failure count.
 */
int test_survival_respawn(void)
{
    int failures = 0;
    Player p;
    player_init(&p);
    survival_damage_player(&p, 100.0f);
    TEST_ASSERT(p.dead);
    p.pos = mmath_vec3(50.0f, 10.0f, 50.0f);
    p.vel = mmath_vec3(3.0f, -9.0f, 1.0f);
    p.fall_peak = 60.0f;
    p.hunger = 2.0f;
    Vec3 spawn = mmath_vec3(8.5f, 80.0f, 8.5f);
    survival_respawn(&p, spawn);
    TEST_ASSERT(!p.dead);
    TEST_ASSERT_FLOAT_EQ(p.health, 20.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(p.hunger, 20.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(p.pos.x, 8.5f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(p.pos.y, 80.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(p.pos.z, 8.5f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(p.vel.x, 0.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(p.fall_peak, -1.0f, 1e-4f);
    TEST_ASSERT(!p.mine_active);
    return failures;
}

/* Test: drop pickup honors the 0.5 s delay, stores into the inventory, and
 * never destroys items when the inventory is full.
 *
 * Returns: failure count.
 */
int test_entity_pickup(void)
{
    int failures = 0;
    EntityPool pool;
    entity_pool_clear(&pool);
    Player p;
    player_init(&p);

    ItemStack drop = {(ItemId)BLOCK_STONE, 5, 0};
    int idx = entity_spawn(&pool, p.pos, &drop);
    TEST_ASSERT(idx >= 0);
    TEST_ASSERT(entity_active_count(&pool) == 1);
    /* Fresh drops are not grabbable yet. */
    TEST_ASSERT(entity_try_pickup(&pool, &p, ENTITY_PICKUP_RADIUS, NULL) == 0);
    TEST_ASSERT(entity_active_count(&pool) == 1);
    TEST_ASSERT(inv_count_item(&p.inv, (ItemId)BLOCK_STONE) == 0);
    /* After the delay the stack is collected whole. */
    pool.items[idx].pickup_t = 0.0f;
    ItemId last = ITEM_NONE;
    TEST_ASSERT(entity_try_pickup(&pool, &p, ENTITY_PICKUP_RADIUS, &last) == 1);
    TEST_ASSERT(last == (ItemId)BLOCK_STONE);
    TEST_ASSERT(entity_active_count(&pool) == 0);
    TEST_ASSERT(inv_count_item(&p.inv, (ItemId)BLOCK_STONE) == 5);

    /* Full inventory: the entity survives untouched. */
    const ItemInfo *stone_info = item_get_info((ItemId)BLOCK_STONE);
    for (int i = 0; i < INV_SIZE; ++i) {
        p.inv.slots[i].item = (ItemId)BLOCK_STONE;
        p.inv.slots[i].count = stone_info->max_stack;
    }
    ItemStack dirt = {(ItemId)BLOCK_DIRT, 3, 0};
    int idx2 = entity_spawn(&pool, p.pos, &dirt);
    TEST_ASSERT(idx2 >= 0);
    pool.items[idx2].pickup_t = 0.0f;
    TEST_ASSERT(entity_try_pickup(&pool, &p, ENTITY_PICKUP_RADIUS, NULL) == 0);
    TEST_ASSERT(entity_active_count(&pool) == 1);
    TEST_ASSERT(pool.items[idx2].stack.count == 3);
    entity_pool_clear(&pool);
    TEST_ASSERT(entity_active_count(&pool) == 0);
    return failures;
}

/* Test: entities age in free fall and despawn after their lifetime.
 *
 * Returns: failure count.
 */
int test_entity_lifetime(void)
{
    int failures = 0;
    EntityPool pool;
    entity_pool_clear(&pool);
    ItemStack drop = {(ItemId)BLOCK_DIRT, 1, 0};
    int idx = entity_spawn(&pool, mmath_vec3(0.0f, 100.0f, 0.0f), &drop);
    TEST_ASSERT(idx >= 0);
    /* Updates clamp dt to 0.25 s slices (anti-spiral), so pump frames. */
    for (int i = 0; i < 5; ++i) {
        entity_update(&pool, NULL, 0.2f);
    }
    TEST_ASSERT(entity_active_count(&pool) == 1);
    TEST_ASSERT(pool.items[idx].age > 0.9f);
    for (int i = 0; i < 1205; ++i) {
        entity_update(&pool, NULL, 0.25f);
    }
    TEST_ASSERT(entity_active_count(&pool) == 0);
    return failures;
}

/* Test: M6 session round trip persists vitals, spawn, and inventory.
 *
 * Returns: failure count.
 */
int test_save_m6_roundtrip(void)
{
    int failures = 0;
    const char *dir = "test_tmp_m6/rt";
    Player pl;
    player_init(&pl);
    pl.health = 13.0f;
    pl.hunger = 7.0f;
    pl.pos = mmath_vec3(1.5f, 65.0f, 2.5f);
    pl.yaw = 1.0f;
    pl.pitch = -0.2f;
    pl.inv.slots[0].item = (ItemId)BLOCK_STONE;
    pl.inv.slots[0].count = 10;
    pl.inv.slots[9].item = ITEM_COAL;
    pl.inv.slots[9].count = 3;
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    w->seed = 7L;
    memcpy(w->name, "M6_RT", 6);
    w->mode = 0;
    Vec3 spawn = mmath_vec3(1.5f, 70.0f, 2.5f);
    TEST_ASSERT(world_save_all(dir, w, &pl, spawn, true, 0.25f) == 0);
    world_destroy(w);

    WorldMeta m;
    TEST_ASSERT(world_meta_read(dir, &m) == 0);
    TEST_ASSERT_FLOAT_EQ(m.health, 13.0f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(m.hunger, 7.0f, 1e-3f);
    TEST_ASSERT(m.has_spawn);
    TEST_ASSERT_FLOAT_EQ(m.spawn_x, 1.5f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(m.spawn_y, 70.0f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(m.spawn_z, 2.5f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(m.px, 1.5f, 1e-3f);
    TEST_ASSERT_FLOAT_EQ(m.day, 0.25f, 1e-4f);
    TEST_ASSERT(m.inv_items[0] == (uint16_t)BLOCK_STONE && m.inv_counts[0] == 10);
    TEST_ASSERT(m.inv_items[9] == ITEM_COAL && m.inv_counts[9] == 3);
    TEST_ASSERT(m.inv_items[1] == 0 && m.inv_counts[1] == 0);

    char meta[PATH_MAX_LEN];
    if (path_join(meta, sizeof(meta), dir, WORLD_META_FILE) == 0) {
        path_remove_file(meta);
    }
    path_remove_dir(dir);
    path_remove_dir("test_tmp_m6");
    return failures;
}

/* Test: M5-era metadata (no M6 keys) loads with migration defaults: full
 * vitals, no stored spawn, empty inventory.
 *
 * Returns: failure count.
 */
int test_save_m5_migration(void)
{
    int failures = 0;
    static const char old_meta[] =
        "format_version=1\n"
        "world_name=Old World\n"
        "seed=99\n"
        "game_mode=survival\n"
        "player_x=1.0\n"
        "player_y=65.0\n"
        "player_z=2.0\n"
        "player_yaw=0.5\n"
        "player_pitch=-0.1\n"
        "day=0.3\n"
        "last_played=1700000000\n";
    WorldMeta m;
    TEST_ASSERT(world_meta_parse(old_meta, &m) == 0);
    TEST_ASSERT(m.seed == 99LL);
    TEST_ASSERT_FLOAT_EQ(m.px, 1.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(m.day, 0.3f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(m.health, 20.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(m.hunger, 20.0f, 1e-4f);
    TEST_ASSERT(!m.has_spawn);
    for (int i = 0; i < WORLD_META_INV_SLOTS; ++i) {
        if (m.inv_items[i] != 0 || m.inv_counts[i] != 0) {
            printf("[FAIL] %s:%d: migration inventory not empty at %d\n", __FILE__, __LINE__, i);
            failures++;
            break;
        }
    }
    return failures;
}

/* Test: the ray hits non-solid plants/torches but passes through water.
 *
 * Returns: failure count.
 */
int test_raycast_plants_water(void)
{
    int failures = 0;
    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        world_destroy(w);
        return failures + 1;
    }
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 60; y <= 64; ++y) {
                chunk_set_block(c, x, y, z, BLOCK_STONE);
            }
        }
    }
    /* Plant on the floor at (8,65,8); 3-deep water over (4,64,4). */
    chunk_set_block(c, 8, 65, 8, BLOCK_GRASS_PLANT);
    chunk_set_block(c, 4, 65, 4, BLOCK_WATER);
    chunk_set_block(c, 4, 66, 4, BLOCK_WATER);
    chunk_set_block(c, 4, 67, 4, BLOCK_WATER);
    if (world_add_chunk(w, c) != 0) {
        chunk_destroy(c);
        world_destroy(w);
        return failures + 1;
    }
    /* Straight down onto the plant: the plant itself is the hit. */
    HitResult plant =
        raycast_from_eye(mmath_vec3(8.5f, 70.0f, 8.5f), 0.0f, -MMATH_PI * 0.5f, w, RAYCAST_MAX_DIST);
    TEST_ASSERT(plant.hit == true);
    TEST_ASSERT(plant.block[0] == 8 && plant.block[1] == 65 && plant.block[2] == 8);
    TEST_ASSERT(plant.normal[1] == 1);
    /* Straight down through water: passes through to the stone floor. */
    HitResult wet =
        raycast_from_eye(mmath_vec3(4.5f, 68.5f, 4.5f), 0.0f, -MMATH_PI * 0.5f, w, RAYCAST_MAX_DIST);
    TEST_ASSERT(wet.hit == true);
    TEST_ASSERT(wet.block[0] == 4 && wet.block[1] == 64 && wet.block[2] == 4);
    world_destroy(w);
    return failures;
}

/* Test: save_dirty semantics — init false, set-on-change, untouched by
 * generation, so only edited chunks hit the disk.
 *
 * Returns: failure count.
 */
int test_chunk_save_dirty_semantics(void)
{
    int failures = 0;
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    TEST_ASSERT(c->save_dirty == false);
    chunk_set_block(c, 1, 60, 1, BLOCK_AIR); /* No-op write. */
    TEST_ASSERT(c->save_dirty == false);
    chunk_set_block(c, 1, 60, 1, BLOCK_STONE);
    TEST_ASSERT(c->save_dirty == true);
    c->save_dirty = false; /* Simulate a save. */
    chunk_set_block(c, 1, 60, 1, BLOCK_STONE); /* Same value: no-op. */
    TEST_ASSERT(c->save_dirty == false);
    chunk_destroy(c);

    World *w = world_create();
    TEST_ASSERT(w != NULL);
    if (w == NULL) {
        return failures + 1;
    }
    w->seed = 4242L;
    TEST_ASSERT(world_generate_chunk(w, 0, 0) == 0);
    Chunk *g = world_get_chunk(w, 0, 0);
    TEST_ASSERT(g != NULL);
    if (g != NULL) {
        TEST_ASSERT(g->save_dirty == false);
    }
    world_destroy(w);
    return failures;
}

/* Test: a fresh-world record (no player/spawn keys) round-trips without
 * fabricating a position — the spawn search must still run on first load.
 *
 * Returns: failure count.
 */
int test_save_fresh_world_meta(void)
{
    int failures = 0;
    const char *dir = "test_tmp_m6/fresh";
    WorldMeta m;
    world_meta_defaults(&m);
    memcpy(m.name, "Fresh", 6);
    m.seed = 31337LL;
    m.mode = 0;
    TEST_ASSERT(m.has_player == false);
    TEST_ASSERT(m.has_spawn == false);
    TEST_ASSERT(world_meta_write(dir, &m) == 0);
    WorldMeta back;
    TEST_ASSERT(world_meta_read(dir, &back) == 0);
    TEST_ASSERT(back.has_player == false);
    TEST_ASSERT(back.has_spawn == false);
    TEST_ASSERT(back.seed == 31337LL);
    char meta[PATH_MAX_LEN];
    if (path_join(meta, sizeof(meta), dir, WORLD_META_FILE) == 0) {
        path_remove_file(meta);
    }
    path_remove_dir(dir);
    path_remove_dir("test_tmp_m6");
    return failures;
}

/* Test: partial triples never fabricate positions, and junk scalars are
 * rejected instead of coerced.
 *
 * Returns: failure count.
 */
int test_meta_partial_and_strict(void)
{
    int failures = 0;
    WorldMeta m;
    static const char partial[] =
        "format_version=1\nworld_name=P\nseed=5\ngame_mode=survival\n"
        "player_x=1.0\nplayer_y=65.0\n" /* No player_z: not a position. */
        "spawn_x=9.0\n"                 /* No spawn_y/z: not a spawn. */
        "health=abc\n"                  /* Junk: default kept. */
        "day=2.5\n";                    /* Out of range: default kept. */
    TEST_ASSERT(world_meta_parse(partial, &m) == 0);
    TEST_ASSERT(m.has_player == false);
    TEST_ASSERT(m.has_spawn == false);
    TEST_ASSERT_FLOAT_EQ(m.health, 20.0f, 1e-4f);
    TEST_ASSERT_FLOAT_EQ(m.day, 0.35f, 1e-4f);
    /* Junk version rejected (was silently accepted as 1). */
    TEST_ASSERT(world_meta_parse("format_version=1junk\nworld_name=X\nseed=5\n", &m) != 0);
    /* NaN yaw rejected (was stored and propagated to the camera). */
    static const char nan_yaw[] =
        "format_version=1\nworld_name=X\nseed=5\n"
        "player_x=1.0\nplayer_y=65.0\nplayer_z=2.0\nplayer_yaw=nan\n";
    TEST_ASSERT(world_meta_parse(nan_yaw, &m) == 0);
    TEST_ASSERT_FLOAT_EQ(m.yaw, 0.0f, 1e-6f);
    TEST_ASSERT(m.has_player == true);
    /* One malformed inv entry no longer eats the whole tail. */
    static const char bad_inv[] =
        "format_version=1\nworld_name=X\nseed=5\n"
        "inv=0:1:64,bogus,9:100:3\n";
    TEST_ASSERT(world_meta_parse(bad_inv, &m) == 0);
    TEST_ASSERT(m.inv_items[0] == 1 && m.inv_counts[0] == 64);
    TEST_ASSERT(m.inv_items[9] == ITEM_COAL && m.inv_counts[9] == 3);
    return failures;
}

/* Test: a completely full inventory (worst-case ~540-char line, past the
 * old 256-char truncation) round-trips every slot exactly.
 *
 * Returns: failure count.
 */
int test_meta_full_inventory(void)
{
    int failures = 0;
    const char *dir = "test_tmp_m6/fullinv";
    WorldMeta m;
    world_meta_defaults(&m);
    memcpy(m.name, "Full", 5);
    m.seed = 777LL;
    m.has_player = true;
    for (int i = 0; i < WORLD_META_INV_SLOTS; ++i) {
        m.inv_items[i] = (uint16_t)(60000 + i);
        m.inv_counts[i] = (uint16_t)(60000 + i);
    }
    TEST_ASSERT(world_meta_write(dir, &m) == 0);
    WorldMeta back;
    TEST_ASSERT(world_meta_read(dir, &back) == 0);
    for (int i = 0; i < WORLD_META_INV_SLOTS; ++i) {
        if (back.inv_items[i] != (uint16_t)(60000 + i) || back.inv_counts[i] != (uint16_t)(60000 + i)) {
            printf("[FAIL] %s:%d: slot %d lost (%u:%u)\n", __FILE__, __LINE__, i,
                   (unsigned)back.inv_items[i], (unsigned)back.inv_counts[i]);
            failures++;
        }
    }
    char meta[PATH_MAX_LEN];
    if (path_join(meta, sizeof(meta), dir, WORLD_META_FILE) == 0) {
        path_remove_file(meta);
    }
    path_remove_dir(dir);
    path_remove_dir("test_tmp_m6");
    return failures;
}

/* Test: glass neither shades sunlight nor casts AO (leaves still do).
 *
 * Returns: failure count.
 */
int test_mesher_glass_light(void)
{
    int failures = 0;
    Chunk *c = chunk_create(0, 0);
    TEST_ASSERT(c != NULL);
    if (c == NULL) {
        return failures + 1;
    }
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 60; y <= 64; ++y) {
                chunk_set_block(c, x, y, z, BLOCK_STONE);
            }
        }
    }
    /* Glass in the sun column above (8,64,8) and beside its top face. */
    chunk_set_block(c, 8, 66, 8, BLOCK_GLASS);
    chunk_set_block(c, 7, 65, 8, BLOCK_GLASS);
    MeshData *m = mesher_build_chunk_mesh(c, NULL);
    TEST_ASSERT(m != NULL);
    if (m != NULL) {
        float ao = -1.0f;
        for (size_t i = 0; i < m->vertex_count; ++i) {
            const float *v = m->vertices + i * MESHER_FLOATS_PER_VERTEX;
            if (v[1] == 65.0f && v[4] == 1.0f && v[0] == 8.0f && v[2] == 8.0f) {
                ao = v[8];
                break;
            }
        }
        TEST_ASSERT_FLOAT_EQ(ao, 1.0f, 1e-6f);
        mesher_free(m);
    }
    chunk_destroy(c);
    return failures;
}
