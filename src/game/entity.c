#include "game/entity.h"
#include "core/log.h"
#include "core/noise.h"
#include "game/inventory.h"
#include "game/player.h"
#include "world/block.h"
#include "world/world.h"

#include <math.h>
#include <stddef.h>

/* Clear the pool. */
void entity_pool_clear(EntityPool *pool)
{
    if (pool == NULL) {
        return;
    }
    for (int i = 0; i < ENTITY_MAX; ++i) {
        pool->items[i].active = false;
        stack_clear(&pool->items[i].stack);
        pool->items[i].pos = mmath_vec3(0.0f, 0.0f, 0.0f);
        pool->items[i].vel = mmath_vec3(0.0f, 0.0f, 0.0f);
        pool->items[i].age = 0.0f;
        pool->items[i].pickup_t = 0.0f;
    }
}

/* Spawn a drop in the first free slot. */
int entity_spawn(EntityPool *pool, Vec3 pos, const ItemStack *stack)
{
    if (pool == NULL || stack == NULL || stack_is_empty(stack) || !item_is_valid(stack->item)) {
        return -1;
    }
    for (int i = 0; i < ENTITY_MAX; ++i) {
        if (pool->items[i].active) {
            continue;
        }
        ItemEntity *e = &pool->items[i];
        e->active = true;
        e->pos = pos;
        /* Deterministic pop: hash the spawn cell so drops scatter stably. */
        int cx = (int)floorf(pos.x);
        int cy = (int)floorf(pos.y);
        int cz = (int)floorf(pos.z);
        float hx = noise_hash_to_unit(noise_hash3(cx, cy, cz, 0xD2000001u)) * 2.0f - 1.0f;
        float hz = noise_hash_to_unit(noise_hash3(cx, cy, cz, 0xD2000002u)) * 2.0f - 1.0f;
        e->vel = mmath_vec3(hx * 1.5f, 4.0f, hz * 1.5f);
        e->stack = *stack;
        e->age = 0.0f;
        e->pickup_t = ENTITY_PICKUP_DELAY;
        return i;
    }
    return -1;
}

/* Count active entities. */
int entity_active_count(const EntityPool *pool)
{
    if (pool == NULL) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < ENTITY_MAX; ++i) {
        if (pool->items[i].active) {
            ++n;
        }
    }
    return n;
}

/* Solidity test for the drop's small box (epsilon-shrunk like the player). */
static bool entity_box_solid(const World *w, Vec3 mn, Vec3 mx)
{
    const float eps = 0.001f;
    int x0 = (int)floorf(mn.x);
    int y0 = (int)floorf(mn.y);
    int z0 = (int)floorf(mn.z);
    int x1 = (int)floorf(mx.x - eps);
    int y1 = (int)floorf(mx.y - eps);
    int z1 = (int)floorf(mx.z - eps);
    for (int by = y0; by <= y1; ++by) {
        for (int bz = z0; bz <= z1; ++bz) {
            for (int bx = x0; bx <= x1; ++bx) {
                if (block_is_solid(world_get_block(w, bx, by, bz))) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* Fraction of the item's collision AABB occupied by water. Flowing states
 * use their real bottom-up surface height. */
static float entity_water_contact(const ItemEntity *e, const World *w)
{
    if (e == NULL || w == NULL) {
        return 0.0f;
    }
    const float eps = 0.001f;
    float width = ENTITY_HALF * 2.0f;
    float min_x = e->pos.x - ENTITY_HALF;
    float max_x = e->pos.x + ENTITY_HALF;
    float min_y = e->pos.y;
    float max_y = e->pos.y + width;
    float min_z = e->pos.z - ENTITY_HALF;
    float max_z = e->pos.z + ENTITY_HALF;
    int x0 = (int)floorf(min_x + eps);
    int x1 = (int)floorf(max_x - eps);
    int y0 = (int)floorf(min_y + eps);
    int y1 = (int)floorf(max_y - eps);
    int z0 = (int)floorf(min_z + eps);
    int z1 = (int)floorf(max_z - eps);
    float volume = width * width * width;
    if (!(volume > 0.0f)) {
        return 0.0f;
    }

    float contact = 0.0f;
    for (int y = y0; y <= y1; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                uint16_t id = world_get_block(w, x, y, z);
                if (!block_is_water(id)) {
                    continue;
                }
                float fluid_top = (float)y + block_water_height(id);
                float overlap_x = fminf(max_x, (float)x + 1.0f) - fmaxf(min_x, (float)x);
                float overlap_y = fminf(max_y, fluid_top) - fmaxf(min_y, (float)y);
                float overlap_z = fminf(max_z, (float)z + 1.0f) - fmaxf(min_z, (float)z);
                if (overlap_x > eps && overlap_y > eps && overlap_z > eps) {
                    contact += overlap_x * overlap_y * overlap_z / volume;
                    if (contact >= 1.0f) {
                        return 1.0f;
                    }
                }
            }
        }
    }
    return contact;
}

static float entity_clamp_velocity(float value, float min_value, float max_value)
{
    if (!isfinite(value)) {
        return 0.0f;
    }
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

/* Move one axis with snap-out (mirrors player resolution, lighter). */
static void entity_resolve(ItemEntity *e, World *w, int axis, float delta, bool down)
{
    Vec3 mn = mmath_vec3(e->pos.x - ENTITY_HALF, e->pos.y, e->pos.z - ENTITY_HALF);
    Vec3 mx = mmath_vec3(e->pos.x + ENTITY_HALF, e->pos.y + ENTITY_HALF * 2.0f, e->pos.z + ENTITY_HALF);
    if (!entity_box_solid(w, mn, mx)) {
        return;
    }
    if (axis == 0) {
        if (delta > 0.0f) {
            e->pos.x = floorf(mx.x) - ENTITY_HALF - 0.001f;
        } else if (delta < 0.0f) {
            e->pos.x = floorf(mn.x) + 1.0f + ENTITY_HALF + 0.001f;
        }
        e->vel.x = 0.0f;
    } else if (axis == 2) {
        if (delta > 0.0f) {
            e->pos.z = floorf(mx.z) - ENTITY_HALF - 0.001f;
        } else if (delta < 0.0f) {
            e->pos.z = floorf(mn.z) + 1.0f + ENTITY_HALF + 0.001f;
        }
        e->vel.z = 0.0f;
    } else {
        if (down) {
            e->pos.y = floorf(mn.y) + 1.0f;
        } else {
            e->pos.y = floorf(mx.y) - ENTITY_HALF * 2.0f - 0.001f;
        }
        e->vel.y = 0.0f;
    }
}

/* One physics substep for a single entity. */
static void entity_step(ItemEntity *e, World *w, float dt)
{
    if (!isfinite(e->pos.x) || !isfinite(e->pos.y) || !isfinite(e->pos.z)) {
        e->pos = mmath_vec3(0.0f, 0.0f, 0.0f);
    }
    e->vel.x = entity_clamp_velocity(e->vel.x, -30.0f, 30.0f);
    e->vel.y = entity_clamp_velocity(e->vel.y, ENTITY_TERMINAL_VEL, 8.0f);
    e->vel.z = entity_clamp_velocity(e->vel.z, -30.0f, 30.0f);
    float water_contact = entity_water_contact(e, w);
    float gravity = ENTITY_GRAVITY * (1.0f - 1.15f * water_contact);
    e->vel.y -= gravity * dt;
    if (e->vel.y < ENTITY_TERMINAL_VEL) {
        e->vel.y = ENTITY_TERMINAL_VEL;
    } else if (e->vel.y > 8.0f) {
        e->vel.y = 8.0f;
    }
    /* Mild air drag so pops settle instead of sliding forever. */
    e->vel.x *= 0.98f;
    e->vel.z *= 0.98f;
    if (water_contact > 0.0f) {
        float horizontal_drag = expf(-4.0f * water_contact * dt);
        float vertical_drag = expf(-5.0f * water_contact * dt);
        e->vel.x *= horizontal_drag;
        e->vel.y *= vertical_drag;
        e->vel.z *= horizontal_drag;
    }
    e->vel.x = entity_clamp_velocity(e->vel.x, -30.0f, 30.0f);
    e->vel.y = entity_clamp_velocity(e->vel.y, ENTITY_TERMINAL_VEL, 8.0f);
    e->vel.z = entity_clamp_velocity(e->vel.z, -30.0f, 30.0f);
    if (w == NULL) {
        e->pos = mmath_vec3_add(e->pos, mmath_vec3_scale(e->vel, dt));
        return;
    }
    float dx = e->vel.x * dt;
    float dz = e->vel.z * dt;
    float dy = e->vel.y * dt;
    e->pos.x += dx;
    entity_resolve(e, w, 0, dx, false);
    e->pos.z += dz;
    entity_resolve(e, w, 2, dz, false);
    e->pos.y += dy;
    entity_resolve(e, w, 1, dy, dy <= 0.0f);
}

/* Simulate the pool (substepped against tunneling). */
void entity_update(EntityPool *pool, World *w, float dt)
{
    if (pool == NULL || dt <= 0.0f) {
        return;
    }
    if (dt > 0.25f) {
        dt = 0.25f;
    }
    /* Slice into <= 1/30 s steps: worst fall (30 m/s) moves <= 1 block. */
    int steps = 1;
    while ((dt / (float)steps) > (1.0f / 30.0f) && steps < 8) {
        ++steps;
    }
    float h = dt / (float)steps;
    for (int i = 0; i < ENTITY_MAX; ++i) {
        ItemEntity *e = &pool->items[i];
        if (!e->active) {
            continue;
        }
        e->age += dt;
        if (e->age >= ENTITY_LIFETIME) {
            LOG_DEBUG("entity %d despawned (age %.0fs, item %u)", i, (double)e->age, (unsigned)e->stack.item);
            e->active = false;
            stack_clear(&e->stack);
            continue;
        }
        if (e->pickup_t > 0.0f) {
            e->pickup_t -= dt;
        }
        for (int s = 0; s < steps; ++s) {
            entity_step(e, w, h);
        }
    }
}

/* Try pickup of every eligible entity in range. */
int entity_try_pickup(EntityPool *pool, Player *p, float radius, ItemId *out_last)
{
    if (pool == NULL || p == NULL || !(radius > 0.0f)) {
        return 0;
    }
    Vec3 center = mmath_vec3(p->pos.x, p->pos.y + p->height * 0.5f, p->pos.z);
    int collected = 0;
    ItemId last = ITEM_NONE;
    for (int i = 0; i < ENTITY_MAX; ++i) {
        ItemEntity *e = &pool->items[i];
        if (!e->active || e->pickup_t > 0.0f || stack_is_empty(&e->stack)) {
            continue;
        }
        Vec3 d = mmath_vec3_sub(e->pos, center);
        if (mmath_vec3_length(d) > radius) {
            continue;
        }
        ItemStack moving = e->stack;
        uint16_t left = inv_insert(&p->inv, &moving);
        if (left == 0) {
            last = e->stack.item;
            e->active = false;
            stack_clear(&e->stack);
            ++collected;
        } else {
            /* Partial: entity keeps the remainder, still valid. */
            e->stack = moving;
        }
    }
    if (out_last != NULL) {
        *out_last = last;
    }
    return collected;
}
