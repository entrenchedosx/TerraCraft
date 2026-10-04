#include "game/particle.h"
#include "game/item.h"
#include "render/texture_atlas.h"
#include "world/block.h"

#include <stddef.h>

/* Deterministic spray velocity from the pool sequence: hashed direction
 * with an up bias (debris pops up, then gravity takes over).
 */
static Vec3 particle_spray_dir(uint32_t seq)
{
    uint32_t h = seq * 2654435761u ^ 0x9e3779b9u;
    h ^= h >> 15;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    float ax = (float)(h & 0xFFu) / 255.0f - 0.5f;
    float ay = (float)((h >> 8) & 0xFFu) / 255.0f;
    float az = (float)((h >> 16) & 0xFFu) / 255.0f - 0.5f;
    return mmath_vec3(ax * 3.0f, 2.0f + ay * 3.0f, az * 3.0f);
}

/* Clear the pool. */
void particle_pool_clear(ParticlePool *pool)
{
    if (pool == NULL) {
        return;
    }
    for (int i = 0; i < PARTICLE_MAX; ++i) {
        pool->items[i].active = false;
    }
    pool->seq = 0;
}

/* Count active particles. */
int particle_active_count(const ParticlePool *pool)
{
    if (pool == NULL) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < PARTICLE_MAX; ++i) {
        if (pool->items[i].active) {
            ++n;
        }
    }
    return n;
}

/* Spawn one particle. */
int particle_spawn(ParticlePool *pool, Vec3 pos, Vec3 vel, float life, float size, float r, float g,
                   float b, int tile, float gravity)
{
    if (pool == NULL || !(life > 0.0f) || !(size > 0.0f)) {
        return -1;
    }
    for (int i = 0; i < PARTICLE_MAX; ++i) {
        Particle *p = &pool->items[i];
        if (p->active) {
            continue;
        }
        p->active = true;
        p->pos = pos;
        p->vel = vel;
        p->age = 0.0f;
        p->life = life;
        p->size = size;
        p->r = r;
        p->g = g;
        p->b = b;
        p->tile = tile;
        p->gravity = gravity;
        pool->seq++;
        return i;
    }
    return -1;
}

/* Block burst: tinted cubes from the cell volume. */
int particle_burst_block(ParticlePool *pool, uint16_t block, int cx, int cy, int cz, int n)
{
    if (pool == NULL || n <= 0) {
        return 0;
    }
    const BlockInfo *info = block_get_info(block);
    int tile = block_tile_for_face(block, 3);
    int made = 0;
    for (int i = 0; i < n; ++i) {
        uint32_t s = pool->seq;
        float jx = (float)(s % 5) / 5.0f;
        float jy = (float)((s / 5) % 5) / 5.0f;
        float jz = (float)((s / 25) % 5) / 5.0f;
        Vec3 at = mmath_vec3((float)cx + 0.2f + jx * 0.6f, (float)cy + 0.2f + jy * 0.6f,
                             (float)cz + 0.2f + jz * 0.6f);
        Vec3 vel = particle_spray_dir(s);
        if (particle_spawn(pool, at, vel, 0.65f, 0.11f, info->color_r, info->color_g, info->color_b,
                           tile, 18.0f) >= 0) {
            ++made;
        } else {
            break;
        }
    }
    return made;
}

/* Item burst: small gentle poof around a point (pickup feedback). */
int particle_burst_item(ParticlePool *pool, uint16_t item, Vec3 at, int n)
{
    if (pool == NULL || n <= 0 || !item_is_valid((ItemId)item)) {
        return 0;
    }
    const ItemInfo *info = item_get_info((ItemId)item);
    int made = 0;
    for (int i = 0; i < n; ++i) {
        Vec3 vel = particle_spray_dir(pool->seq);
        vel = mmath_vec3_scale(vel, 0.4f);
        if (particle_spawn(pool, at, vel, 0.35f, 0.07f, info->color_r, info->color_g, info->color_b,
                           info->tile, 4.0f) >= 0) {
            ++made;
        } else {
            break;
        }
    }
    return made;
}

/* Simulate one frame. */
void particle_update(ParticlePool *pool, float dt)
{
    if (pool == NULL || !(dt > 0.0f)) {
        return;
    }
    if (dt > 0.25f) {
        dt = 0.25f;
    }
    for (int i = 0; i < PARTICLE_MAX; ++i) {
        Particle *p = &pool->items[i];
        if (!p->active) {
            continue;
        }
        p->age += dt;
        if (p->age >= p->life) {
            p->active = false;
            continue;
        }
        p->vel.y -= p->gravity * dt;
        p->pos = mmath_vec3_add(p->pos, mmath_vec3_scale(p->vel, dt));
    }
}
