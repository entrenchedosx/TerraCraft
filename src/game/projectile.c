#include "game/projectile.h"
#include "core/log.h"
#include "game/entity.h"
#include "game/mob.h"
#include "world/block.h"
#include "world/world.h"

#include <math.h>
#include <stddef.h>

/* Inert fallback definition (unknown types fly straight and fade). */
static const ProjectileDefinition PROJECTILE_INVALID = {
    PROJECTILE_NONE, "none", 0.0f, 1.0f, 0.1f, 1.0f, 1.0f, false,
};

/* Static projectile registry (original TerraCraft tuning, documented).
 * Arrow: gravity 20 b/s^2 with 0.82/s drag arcs satisfyingly at 10..45
 * m/s launch speeds; 6 s of flight outranges any legitimate shot.
 */
static const ProjectileDefinition PROJECTILE_TABLE[] = {
    {PROJECTILE_ARROW, "Arrow", 20.0f, 0.82f, 0.12f, 6.0f, 12.0f, true},
};
#define PROJECTILE_TABLE_COUNT (sizeof(PROJECTILE_TABLE) / sizeof(PROJECTILE_TABLE[0]))

/* DDA traversal bound per segment (substeps are short; this is a sanity
 * cap, not a range limit — range comes from speed * lifetime).
 */
#define PROJ_DDA_MAX_STEPS 160

/* Definition lookup (inert fallback, never NULL). */
const ProjectileDefinition *projectile_definition(ProjectileType type)
{
    for (size_t i = 0; i < PROJECTILE_TABLE_COUNT; ++i) {
        if (PROJECTILE_TABLE[i].type == type) {
            return &PROJECTILE_TABLE[i];
        }
    }
    return &PROJECTILE_INVALID;
}

/* Deactivate every slot and zero counters. */
void projectile_pool_clear(ProjectilePool *pool)
{
    if (pool == NULL) {
        return;
    }
    for (int i = 0; i < PROJECTILE_MAX; ++i) {
        pool->projs[i].active = false;
    }
    pool->fired = 0;
    pool->impacts = 0;
}

/* Count active projectiles. */
int projectile_active_count(const ProjectilePool *pool)
{
    if (pool == NULL) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < PROJECTILE_MAX; ++i) {
        if (pool->projs[i].active) {
            ++n;
        }
    }
    return n;
}

/* Count embedded projectiles. */
int projectile_embedded_count(const ProjectilePool *pool)
{
    if (pool == NULL) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < PROJECTILE_MAX; ++i) {
        if (pool->projs[i].active && pool->projs[i].state == PROJECTILE_EMBEDDED) {
            ++n;
        }
    }
    return n;
}

/* Fire a projectile (first free slot). */
bool projectile_fire(ProjectilePool *pool, ProjectileType type, Vec3 origin, Vec3 dir, float speed,
                     float damage, float knock_power, ProjectileOwnerId owner)
{
    if (pool == NULL || !(speed > 0.0f) || !(damage > 0.0f) || !(knock_power >= 0.0f)) {
        return false;
    }
    const ProjectileDefinition *def = projectile_definition(type);
    if (def->type == PROJECTILE_NONE) {
        return false;
    }
    float len = mmath_vec3_length(dir);
    if (!(len > 1e-8f)) {
        return false;
    }
    for (int i = 0; i < PROJECTILE_MAX; ++i) {
        Projectile *p = &pool->projs[i];
        if (p->active) {
            continue;
        }
        p->active = true;
        p->type = type;
        p->state = PROJECTILE_FLYING;
        p->pos = origin;
        p->prev_pos = origin;
        p->vel = mmath_vec3_scale(dir, speed / len);
        p->owner = owner;
        p->damage = damage;
        p->knock_power = knock_power;
        p->age = 0.0f;
        p->grace_t = PROJECTILE_OWNER_GRACE;
        p->embed_t = 0.0f;
        p->embed_yaw = 0.0f;
        p->embed_pitch = 0.0f;
        pool->fired++;
        return true;
    }
    return false; /* Pool full: caller consumes nothing (documented). */
}

/* Remove a projectile (silent no-op on bad args). */
void projectile_remove(ProjectilePool *pool, int index)
{
    if (pool == NULL || index < 0 || index >= PROJECTILE_MAX) {
        return;
    }
    pool->projs[index].active = false;
}

/* Swept voxel traversal (solid-only policy: plants/torches/water never
 * stop arrows; leaves and glass do — they are solid). Returns the entry
 * param t in [0,1] of the first solid cell, or -1 when the segment is
 * clear. Starting inside solid counts as an immediate hit.
 */
static float proj_segment_block(const World *w, Vec3 a, Vec3 b)
{
    Vec3 d = mmath_vec3_sub(b, a);
    float seg = mmath_vec3_length(d);
    if (!(seg > 1e-8f)) {
        return block_is_solid(world_get_block(w, (int)floorf(a.x), (int)floorf(a.y),
                                              (int)floorf(a.z)))
                   ? 0.0f
                   : -1.0f;
    }
    Vec3 dir = mmath_vec3_scale(d, 1.0f / seg);
    int x = (int)floorf(a.x);
    int y = (int)floorf(a.y);
    int z = (int)floorf(a.z);
    if (block_is_solid(world_get_block(w, x, y, z))) {
        return 0.0f;
    }
    int step_x = dir.x > 0.0f ? 1 : -1;
    int step_y = dir.y > 0.0f ? 1 : -1;
    int step_z = dir.z > 0.0f ? 1 : -1;
    const float inf = 1e30f;
    float tmax_x = (dir.x != 0.0f)
                       ? ((step_x > 0 ? (float)(x + 1) - a.x : a.x - (float)x) / fabsf(dir.x))
                       : inf;
    float tmax_y = (dir.y != 0.0f)
                       ? ((step_y > 0 ? (float)(y + 1) - a.y : a.y - (float)y) / fabsf(dir.y))
                       : inf;
    float tmax_z = (dir.z != 0.0f)
                       ? ((step_z > 0 ? (float)(z + 1) - a.z : a.z - (float)z) / fabsf(dir.z))
                       : inf;
    float tdelta_x = (dir.x != 0.0f) ? fabsf(1.0f / dir.x) : inf;
    float tdelta_y = (dir.y != 0.0f) ? fabsf(1.0f / dir.y) : inf;
    float tdelta_z = (dir.z != 0.0f) ? fabsf(1.0f / dir.z) : inf;
    float t = 0.0f;
    for (int i = 0; i < PROJ_DDA_MAX_STEPS && t <= seg; ++i) {
        if (tmax_x <= tmax_y && tmax_x <= tmax_z) {
            x += step_x;
            t = tmax_x;
            tmax_x += tdelta_x;
        } else if (tmax_y <= tmax_z) {
            y += step_y;
            t = tmax_y;
            tmax_y += tdelta_y;
        } else {
            z += step_z;
            t = tmax_z;
            tmax_z += tdelta_z;
        }
        if (t > seg) {
            break;
        }
        if (block_is_solid(world_get_block(w, x, y, z))) {
            return t / seg;
        }
    }
    return -1.0f;
}

/* Swept segment vs AABB (box expanded by radius): slab test in segment
 * param space. Returns the entry t in [0,1], 0 when the origin starts
 * inside, or -1 on a miss.
 */
static float proj_segment_aabb(Vec3 a, Vec3 d, Vec3 mn, Vec3 mx)
{
    float tmin = 0.0f;
    float tmax = 1.0f;
    const float o[3] = {a.x, a.y, a.z};
    const float dd[3] = {d.x, d.y, d.z};
    const float bmin[3] = {mn.x, mn.y, mn.z};
    const float bmax[3] = {mx.x, mx.y, mx.z};
    for (int i = 0; i < 3; ++i) {
        if (fabsf(dd[i]) < 1e-9f) {
            if (o[i] < bmin[i] || o[i] > bmax[i]) {
                return -1.0f;
            }
        } else {
            float t1 = (bmin[i] - o[i]) / dd[i];
            float t2 = (bmax[i] - o[i]) / dd[i];
            if (t1 > t2) {
                float tmp = t1;
                t1 = t2;
                t2 = tmp;
            }
            if (t1 > tmin) {
                tmin = t1;
            }
            if (t2 < tmax) {
                tmax = t2;
            }
            if (tmin > tmax) {
                return -1.0f;
            }
        }
    }
    if (tmin < 0.0f) {
        tmin = 0.0f; /* Origin inside: immediate contact. */
    }
    return (tmin <= 1.0f && tmin <= tmax) ? tmin : -1.0f;
}

/* Nearest living-mob hit along a segment (dead/inactive skipped, owner
 * skipped while its grace runs). Returns slot or -1 with the entry t.
 */
static int proj_segment_mob(const MobPool *mobs, Vec3 a, Vec3 d, float radius,
                            ProjectileOwnerId owner, bool owner_armed, float *out_t)
{
    int best = -1;
    float best_t = 2.0f;
    for (int i = 0; i < MOB_MAX; ++i) {
        const Mob *m = &mobs->mobs[i];
        if (!m->active || m->dead) {
            continue;
        }
        if (owner_armed && entity_id_make(i, m->gen) == (EntityId)owner) {
            continue;
        }
        float hw = m->width * 0.5f + radius;
        Vec3 mn = mmath_vec3(m->pos.x - hw, m->pos.y - radius, m->pos.z - hw);
        Vec3 mx = mmath_vec3(m->pos.x + hw, m->pos.y + m->height + radius, m->pos.z + hw);
        float t = proj_segment_aabb(a, d, mn, mx);
        if (t >= 0.0f && t < best_t) {
            best_t = t;
            best = i;
        }
    }
    if (out_t != NULL) {
        *out_t = best_t;
    }
    return best;
}

/* Resolve one substep segment: nearest impact wins (block beats entity
 * on exact ties, so shots never leak through walls). Applies damage,
 * embed, or despawn through the shared M8 systems.
 */
static void proj_collide_segment(ProjectilePool *pool, int idx, MobPool *mobs, EntityPool *drops,
                                 World *w, const ProjectilePlayer *player, Vec3 a, Vec3 b,
                                 ProjectileFrameEvents *ev)
{
    Projectile *p = &pool->projs[idx];
    const ProjectileDefinition *def = projectile_definition(p->type);
    Vec3 d = mmath_vec3_sub(b, a);
    float block_t = proj_segment_block(w, a, b);
    bool owner_armed = p->grace_t > 0.0f;
    float mob_t = 2.0f;
    int mob_slot = proj_segment_mob(mobs, a, d, def->radius, p->owner, owner_armed, &mob_t);
    float player_t = -1.0f;
    if (player->alive && !player->creative &&
        !(owner_armed && p->owner == (ProjectileOwnerId)ENTITY_PLAYER_ID)) {
        float hw = player->width * 0.5f + def->radius;
        Vec3 mn = mmath_vec3(player->pos.x - hw, player->pos.y - def->radius, player->pos.z - hw);
        Vec3 mx =
            mmath_vec3(player->pos.x + hw, player->pos.y + player->height + def->radius,
                       player->pos.z + hw);
        player_t = proj_segment_aabb(a, d, mn, mx);
    }
    /* Entity hits strictly inside the block hit win; ties go to the
     * block (documented wall-first policy). */
    bool entity_first = false;
    float entity_t = 2.0f;
    bool entity_is_player = false;
    if (player_t >= 0.0f && (block_t < 0.0f || player_t < block_t)) {
        entity_first = true;
        entity_t = player_t;
        entity_is_player = true;
    }
    if (mob_slot >= 0 && (block_t < 0.0f || mob_t < block_t) &&
        (!entity_first || mob_t < entity_t)) {
        entity_first = true;
        entity_t = mob_t;
        entity_is_player = false;
    }
    if (!entity_first && block_t < 0.0f) {
        return; /* Clear segment: keep flying. */
    }
    pool->impacts++;
    if (!entity_first) {
        /* Terrain impact: embed (kept, oriented, silent physics off) or
         * despawn for non-embedding types. */
        ev->blocks_hit++;
        ev->last_block_pos = mmath_vec3_add(a, mmath_vec3_scale(d, block_t));
        if (def->can_embed) {
            float vl = mmath_vec3_length(p->vel);
            Vec3 vdir = vl > 1e-6f ? mmath_vec3_scale(p->vel, 1.0f / vl) : mmath_vec3(0, -1, 0);
            p->pos = mmath_vec3_sub(ev->last_block_pos, mmath_vec3_scale(vdir, 0.03f));
            p->prev_pos = p->pos;
            p->embed_yaw = atan2f(-vdir.x, -vdir.z);
            float sy = vdir.y > 1.0f ? 1.0f : (vdir.y < -1.0f ? -1.0f : vdir.y);
            p->embed_pitch = asinf(sy);
            p->state = PROJECTILE_EMBEDDED;
            p->embed_t = 0.0f;
            p->vel = mmath_vec3(0.0f, 0.0f, 0.0f);
        } else {
            p->active = false;
        }
        return;
    }
    if (entity_is_player) {
        /* Player hits report (never applied here — the app gates damage
         * through hurt_t exactly like melee strikes). */
        ev->player_hits++;
        ev->player_damage += p->damage;
        float vl = mmath_vec3_length(p->vel);
        if (vl > 1e-4f) {
            float push = p->knock_power * 0.5f;
            ev->player_knock.x += (p->vel.x / vl) * push;
            ev->player_knock.z += (p->vel.z / vl) * push;
            ev->player_knock.y += push * 0.25f;
        }
        p->active = false;
        return;
    }
    /* Living-mob hit through the M8 damage API (hurt window, knockback
     * resist, exactly-once death drops — no direct health writes here). */
    const Mob *target = &mobs->mobs[mob_slot];
    EntityId id = entity_id_make(mob_slot, target->gen);
    Vec3 knock = mmath_vec3(p->vel.x, 0.0f, p->vel.z);
    float vl = mmath_vec3_length(p->vel);
    Vec3 from = vl > 1e-4f ? mmath_vec3(-p->vel.x / vl, 0.0f, -p->vel.z / vl)
                           : mmath_vec3(0.0f, 0.0f, 0.0f);
    if (living_entity_damage_src(mobs, drops, id, p->damage, knock, p->knock_power, &from,
                                 DAMAGE_PROJECTILE)) {
        ev->mobs_died++;
        ev->last_died_type = (int)target->type;
        const Mob *dead = mob_resolve(mobs, id);
        ev->last_death_pos = dead != NULL ? dead->pos : mmath_vec3_add(a, mmath_vec3_scale(d, mob_t));
        const MobDefinition *fdef = mob_definition(target->type);
        ev->last_death_item = fdef->ndrops > 0 ? (ProjectileItemId)fdef->drops[0].item : 0;
    } else {
        ev->mobs_hit++;
        ev->last_hurt_type = (int)target->type;
    }
    p->active = false; /* Arrows never penetrate (M9 rule). */
}

/* Simulate every projectile (see header for the contract). */
void projectile_update(ProjectilePool *pool, MobPool *mobs, EntityPool *drops, World *w,
                       const ProjectilePlayer *player, float dt, ProjectileFrameEvents *ev)
{
    if (ev != NULL) {
        ev->player_hits = 0;
        ev->player_damage = 0.0f;
        ev->player_knock = mmath_vec3(0.0f, 0.0f, 0.0f);
        ev->mobs_hit = 0;
        ev->mobs_died = 0;
        ev->last_died_type = 0;
        ev->last_hurt_type = 0;
        ev->last_death_pos = mmath_vec3(0.0f, 0.0f, 0.0f);
        ev->last_death_item = 0;
        ev->blocks_hit = 0;
        ev->last_block_pos = mmath_vec3(0.0f, 0.0f, 0.0f);
    }
    if (pool == NULL || mobs == NULL || drops == NULL || w == NULL || player == NULL || ev == NULL) {
        return;
    }
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.25f) {
        dt = 0.25f;
    }
    for (int i = 0; i < PROJECTILE_MAX; ++i) {
        Projectile *p = &pool->projs[i];
        if (!p->active) {
            continue;
        }
        const ProjectileDefinition *def = projectile_definition(p->type);
        if (p->state == PROJECTILE_EMBEDDED) {
            p->embed_t += dt;
            if (p->embed_t >= def->life_embed) {
                p->active = false;
            }
            continue;
        }
        int steps = (int)(dt / PROJECTILE_SUBSTEP + 0.5f);
        if (steps < 1) {
            steps = 1;
        }
        if (steps > 16) {
            steps = 16;
        }
        float h = steps > 0 ? dt / (float)steps : 0.0f;
        for (int s = 0; s < steps && p->active; ++s) {
            p->age += h;
            if (p->age >= def->life_fly) {
                p->active = false;
                break;
            }
            if (p->grace_t > 0.0f) {
                p->grace_t -= h;
                if (p->grace_t < 0.0f) {
                    p->grace_t = 0.0f;
                }
            }
            p->vel.y -= def->gravity * h;
            float retention = powf(def->drag, h);
            p->vel.x *= retention;
            p->vel.z *= retention;
            p->vel.y *= retention;
            Vec3 a = p->pos;
            Vec3 b = mmath_vec3_add(a, mmath_vec3_scale(p->vel, h));
            p->prev_pos = a;
            p->pos = b;
            proj_collide_segment(pool, i, mobs, drops, w, player, a, b, ev);
        }
    }
}
