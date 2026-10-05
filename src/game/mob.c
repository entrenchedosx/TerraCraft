#include "game/mob.h"
#include "core/log.h"
#include "game/entity.h"
#include "game/item.h"
#include "game/pathfind.h"
#include "game/survival.h"
#include "world/biome.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"
#include "world/world_gen.h"

#include <math.h>
#include <stddef.h>

/* Inert fallback definition (unknown types behave as harmless statues). */
static const MobDefinition MOB_INVALID = {
    ENTITY_NONE, "none", 1.0f, 0.0f, 0.6f, 1.0f, 0.9f, false, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    {{ITEM_NONE, 0, 0, 0.0f}, {ITEM_NONE, 0, 0, 0.0f}, {ITEM_NONE, 0, 0, 0.0f}}, 0, 0, false, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};

/* Static mob registry (original TerraCraft tuning, documented in DECISIONS). */
static const MobDefinition MOB_TABLE[] = {
    {ENTITY_COW, "Cow", 10.0f, 2.0f, 0.9f, 1.4f, 1.3f, false, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, {{ITEM_RAW_BEEF, 1, 3, 1.0f}, {ITEM_LEATHER, 0, 2, 1.0f}, {ITEM_NONE, 0, 0, 0.0f}},
     2, 0, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {ENTITY_GLOOMSTALKER, "Gloomstalker", 20.0f, 3.2f, 0.6f, 1.7f, 1.5f, true, 3.0f, 2.2f, 1.2f,
     0.3f, 12.0f, 18.0f, {{ITEM_COAL, 1, 2, 1.0f}, {ITEM_NONE, 0, 0, 0.0f}, {ITEM_NONE, 0, 0, 0.0f}},
     1, 1, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {ENTITY_SKELETON, "Skeleton", 20.0f, 2.8f, 0.6f, 1.9f, 1.7f, true, 2.0f, 2.2f, 1.5f, 0.2f,
     14.0f, 20.0f, {{ITEM_ARROW, 0, 2, 1.0f}, {ITEM_BONE, 0, 2, 1.0f}, {ITEM_NONE, 0, 0, 0.0f}},
     2, 2, true, 6.0f, 14.0f, 2.2f, 0.8f, 28.0f, 4.0f, 0.05f},
};
#define MOB_TABLE_COUNT (sizeof(MOB_TABLE) / sizeof(MOB_TABLE[0]))

/* xorshift32 RNG (pool-local state, no global rand()). */
static uint32_t mob_rand(MobPool *pool)
{
    uint32_t x = pool->rng;
    if (x == 0) {
        x = 0x9E3779B9u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    pool->rng = x;
    return x;
}

/* Slot index of a handle (-1 unless a plausible live-slot handle). */
int entity_id_index(EntityId id)
{
    if (id == ENTITY_ID_NULL || id == ENTITY_PLAYER_ID) {
        return -1;
    }
    int idx = (int)(id & 0xFFu);
    return (idx >= 0 && idx < MOB_MAX) ? idx : -1;
}

/* Generation of a handle. */
uint32_t entity_id_gen(EntityId id)
{
    if (id == ENTITY_ID_NULL) {
        return 0;
    }
    return id >> 8;
}

/* Build a handle (generation 0 folds to 1 so slots never issue null). */
EntityId entity_id_make(int index, uint32_t gen)
{
    if (index < 0 || index >= MOB_MAX) {
        return ENTITY_ID_NULL;
    }
    if (gen == 0) {
        gen = 1;
    }
    return ((gen << 8) | (uint32_t)index);
}

/* Initialise a pool. */
void mob_pool_init(MobPool *pool, uint32_t seed)
{
    if (pool == NULL) {
        return;
    }
    for (int i = 0; i < MOB_MAX; ++i) {
        pool->mobs[i].active = false;
        pool->mobs[i].gen = 0;
        pool->mobs[i].type = ENTITY_NONE;
    }
    pool->rng = seed != 0 ? seed : 0x85EBCA6Bu;
    pool->spawn_t = 0.0f;
    pool->ai_thinks = 0;
    pool->path_reqs = 0;
}

/* Count active mobs. */
int mob_active_count(const MobPool *pool)
{
    if (pool == NULL) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < MOB_MAX; ++i) {
        if (pool->mobs[i].active) {
            ++n;
        }
    }
    return n;
}

/* Count active mobs of one type. */
int mob_count_type(const MobPool *pool, EntityType type)
{
    if (pool == NULL) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < MOB_MAX; ++i) {
        if (pool->mobs[i].active && pool->mobs[i].type == type) {
            ++n;
        }
    }
    return n;
}

/* Definition lookup (inert fallback, never NULL). */
const MobDefinition *mob_definition(EntityType type)
{
    for (size_t i = 0; i < MOB_TABLE_COUNT; ++i) {
        if (MOB_TABLE[i].type == type) {
            return &MOB_TABLE[i];
        }
    }
    return &MOB_INVALID;
}

/* Spawn a mob. */
EntityId mob_spawn(MobPool *pool, EntityType type, Vec3 pos, float yaw)
{
    if (pool == NULL) {
        return ENTITY_ID_NULL;
    }
    const MobDefinition *def = mob_definition(type);
    if (def->type == ENTITY_NONE) {
        return ENTITY_ID_NULL;
    }
    for (int i = 0; i < MOB_MAX; ++i) {
        Mob *m = &pool->mobs[i];
        if (m->active) {
            continue;
        }
        uint32_t gen = m->gen + 1;
        if (gen == 0) {
            gen = 1;
        }
        m->gen = gen;
        m->active = true;
        m->type = type;
        m->pos = pos;
        m->render_pos = pos;
        m->prev_pos = pos;
        m->vel = mmath_vec3(0.0f, 0.0f, 0.0f);
        m->yaw = yaw;
        m->width = def->width;
        m->height = def->height;
        m->health = def->max_health;
        m->max_health = def->max_health;
        m->hurt_t = 0.0f;
        m->attack_cd = 0.0f;
        m->dead = false;
        m->dead_t = 0.0f;
        m->grounded = false;
        m->state = MOB_STATE_IDLE;
        m->state_t = 0.0f;
        m->ai_t = 0.0f;
        m->target = ENTITY_ID_NULL;
        m->wish_dir = mmath_vec3(0.0f, 0.0f, 0.0f);
        m->wish_speed = 0.0f;
        m->wander_yaw = yaw;
        m->wander_t = 0.0f;
        m->path_len = 0;
        m->path_i = 0;
        m->repath_t = 0.0f;
        m->stuck_pos = pos;
        m->stuck_t = 0.0f;
        m->walk_phase = 0.0f;
        m->fall_peak = -1.0f;
        m->last_fall = -1.0f;
        m->hurt_from = mmath_vec3(0.0f, 0.0f, 0.0f);
        m->last_source = DAMAGE_MELEE;
        return entity_id_make(i, gen);
    }
    return ENTITY_ID_NULL;
}

/* Resolve a handle (generation + active checked). */
Mob *mob_resolve(MobPool *pool, EntityId id)
{
    int idx = entity_id_index(id);
    if (pool == NULL || idx < 0) {
        return NULL;
    }
    Mob *m = &pool->mobs[idx];
    if (!m->active || m->gen != entity_id_gen(id)) {
        return NULL;
    }
    return m;
}

/* Remove a mob (stale handles go safe automatically). */
void mob_remove(MobPool *pool, EntityId id)
{
    Mob *m = mob_resolve(pool, id);
    if (m == NULL) {
        return;
    }
    m->active = false;
    m->type = ENTITY_NONE;
    uint32_t gen = m->gen + 1;
    m->gen = (gen == 0) ? 1 : gen;
}

/* AABB solidity test for mobs (any solid overlap blocks movement). */
static bool mob_box_solid(const World *w, Vec3 mn, Vec3 mx)
{
    int x0 = (int)floorf(mn.x);
    int x1 = (int)floorf(mx.x);
    int y0 = (int)floorf(mn.y);
    int y1 = (int)floorf(mx.y);
    int z0 = (int)floorf(mn.z);
    int z1 = (int)floorf(mx.z);
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

/* Mob body box (feet origin). */
static void mob_box(const Mob *m, Vec3 *out_mn, Vec3 *out_mx)
{
    float hw = m->width * 0.5f;
    *out_mn = mmath_vec3(m->pos.x - hw, m->pos.y, m->pos.z - hw);
    *out_mx = mmath_vec3(m->pos.x + hw, m->pos.y + m->height, m->pos.z + hw);
}

/* Resolve one axis after moving (snap out, zero velocity, land on Y-down).
 * Mirrors the player scheme (axis-separated move-and-clamp) without
 * importing player input concepts (no sprint/sneak/flight here).
 */
static void mob_resolve_axis(Mob *m, const World *w, int axis, float delta, bool is_y_down){
    Vec3 mn, mx;
    mob_box(m, &mn, &mx);
    if (!mob_box_solid(w, mn, mx)) {
        return;
    }
    float hw = m->width * 0.5f;
    if (axis == 0) {
        if (delta > 0.0f) {
            m->pos.x = floorf(mx.x) - hw - 0.001f;
        } else if (delta < 0.0f) {
            m->pos.x = floorf(mn.x) + 1.0f + hw + 0.001f;
        }
        m->vel.x = 0.0f;
    } else if (axis == 2) {
        if (delta > 0.0f) {
            m->pos.z = floorf(mx.z) - hw - 0.001f;
        } else if (delta < 0.0f) {
            m->pos.z = floorf(mn.z) + 1.0f + hw + 0.001f;
        }
        m->vel.z = 0.0f;
    } else {
        if (is_y_down) {
            m->pos.y = floorf(mn.y) + 1.0f;
            m->grounded = true;
        } else {
            m->pos.y = floorf(mx.y) - m->height - 0.001f;
        }
        m->vel.y = 0.0f;
    }
}

/* Try stepping up one block when horizontal movement collides (only when
 * grounded; requires headroom at the raised level).
 */
static bool mob_try_step_up(Mob *m, const World *w, int axis, float delta)
{
    if (delta == 0.0f || !m->grounded) {
        return false;
    }
    Vec3 mn, mx;
    mob_box(m, &mn, &mx);
    mn.y += (float)MOB_STEP_HEIGHT;
    mx.y += (float)MOB_STEP_HEIGHT;
    if (mob_box_solid(w, mn, mx)) {
        return false; /* No headroom: wall stays a wall. */
    }
    m->pos.y += (float)MOB_STEP_HEIGHT;
    if (axis == 0) {
        m->pos.x += delta;
    } else {
        m->pos.z += delta;
    }
    mob_box(m, &mn, &mx);
    if (mob_box_solid(w, mn, mx)) {
        m->pos.y -= (float)MOB_STEP_HEIGHT;
        if (axis == 0) {
            m->pos.x -= delta;
        } else {
            m->pos.z -= delta;
        }
        return false;
    }
    m->vel.y = 0.0f;
    return true;
}

/* Advance one mob's physics. */
void mob_physics_step(Mob *m, World *w, float dt)
{
    if (m == NULL || w == NULL || !(dt > 0.0f)) {
        return;
    }
    if (dt > 0.25f) {
        dt = 0.25f;
    }
    bool was_grounded = m->grounded;
    /* Horizontal velocity follows AI intent exactly (no momentum model;
     * M8 mobs stop crisply, which reads better than sliding). */
    m->vel.x = m->wish_dir.x * m->wish_speed;
    m->vel.z = m->wish_dir.z * m->wish_speed;
    /* Vertical: gravity integrate, terminal clamp. */
    m->vel.y -= MOB_GRAVITY * dt;
    if (m->vel.y < MOB_TERMINAL_VEL) {
        m->vel.y = MOB_TERMINAL_VEL;
    }
    /* Axis-separated move + collide: X (step-up on hit), then Z, then Y.
     * Step-up reads last frame's grounded (we are still "walking" until
     * the Y move re-derives it below). */
    m->grounded = was_grounded;
    float dx = m->vel.x * dt;
    float dz = m->vel.z * dt;
    float dy = m->vel.y * dt;
    Vec3 mn, mx;
    m->pos.x += dx;
    mob_box(m, &mn, &mx);
    if (mob_box_solid(w, mn, mx)) {
        m->pos.x -= dx;
        if (!mob_try_step_up(m, w, 0, dx)) {
            m->pos.x += dx;
            mob_resolve_axis(m, w, 0, dx, false);
        }
    }
    m->pos.z += dz;
    mob_box(m, &mn, &mx);
    if (mob_box_solid(w, mn, mx)) {
        m->pos.z -= dz;
        if (!mob_try_step_up(m, w, 2, dz)) {
            m->pos.z += dz;
            mob_resolve_axis(m, w, 2, dz, false);
        }
    }
    m->grounded = false;
    m->pos.y += dy;
    mob_resolve_axis(m, w, 1, dy, dy <= 0.0f);
    /* Fall tracking: peak while airborne, distance sampled on landing. */
    if (m->grounded) {
        if (m->fall_peak >= 0.0f) {
            m->last_fall = m->fall_peak - m->pos.y;
            if (m->last_fall < 0.0f) {
                m->last_fall = 0.0f;
            }
        }
        m->fall_peak = -1.0f;
    } else if (m->fall_peak < m->pos.y) {
        m->fall_peak = m->pos.y;
    }
}

/* Daytime check matching the sun math (sunrise 0.25, noon 0.5, sunset
 * 0.75 in day_progress units; see time_get_sun_dir).
 */
static bool mob_is_day(float day_progress)
{
    return day_progress > 0.25f && day_progress < 0.75f;
}

/* Face a world-space direction (yaw convention shared with the player:
 * forward = (-sin yaw, -cos yaw)).
 */
static void mob_face_toward(Mob *m, float dx, float dz)
{
    if (dx * dx + dz * dz < 1e-8f) {
        return;
    }
    m->yaw = atan2f(-dx, -dz);
}

/* Wish velocity from the current yaw. */
static void mob_steer_yaw(Mob *m, float speed)
{
    m->wish_dir = mmath_vec3(-sinf(m->yaw), 0.0f, -cosf(m->yaw));
    m->wish_speed = speed;
}

/* Solid-only line of sight between two eye points (leaves block sight;
 * plants/water/air never do — consistent with solidity everywhere).
 */
static bool mob_has_los(const World *w, Vec3 eye, Vec3 target)
{
    Vec3 d = mmath_vec3_sub(target, eye);
    float dist = mmath_vec3_length(d);
    if (dist < 0.001f) {
        return true;
    }
    if (dist > 32.0f) {
        return false;
    }
    Vec3 step = mmath_vec3_scale(d, 1.0f / dist);
    for (float t = 0.5f; t < dist; t += 0.5f) {
        Vec3 p = mmath_vec3_add(eye, mmath_vec3_scale(step, t));
        if (block_is_solid(world_get_block(w, (int)floorf(p.x), (int)floorf(p.y), (int)floorf(p.z)))) {
            return false;
        }
    }
    return true;
}

/* Mob eye position. */
static Vec3 mob_eye(const Mob *m)
{
    return mmath_vec3(m->pos.x, m->pos.y + mob_definition(m->type)->eye_height, m->pos.z);
}

/* Ground probe ahead: solid ground within 4 below the feet-ahead cell. */
static bool mob_ground_ahead(const World *w, Vec3 pos, float yaw)
{
    float ax = pos.x - sinf(yaw) * 1.2f;
    float az = pos.z - cosf(yaw) * 1.2f;
    int bx = (int)floorf(ax);
    int bz = (int)floorf(az);
    int by = (int)floorf(pos.y);
    for (int dy = 0; dy <= 4; ++dy) {
        if (block_is_solid(world_get_block(w, bx, by - dy, bz))) {
            return true;
        }
    }
    return false;
}

/* Pick a fresh wander heading (deterministic via pool RNG). */
static void mob_new_wander(MobPool *pool, Mob *m, float min_dur, float max_dur)
{
    float r1 = (float)(mob_rand(pool) % 1000) / 1000.0f;
    float r2 = (float)(mob_rand(pool) % 1000) / 1000.0f;
    m->wander_yaw = m->yaw + (r1 * 2.0f - 1.0f) * 3.14159265f;
    m->wander_t = min_dur + r2 * (max_dur - min_dur);
    m->state_t = 0.0f;
}

/* Request a ground path to a target cell (cooldown set either way, so
 * failures retry later instead of spinning every think).
 */
static void mob_request_path(MobPool *pool, Mob *m, World *w, int tx, int ty, int tz)
{
    pool->path_reqs++;
    int sx = (int)floorf(m->pos.x);
    int sy = (int)floorf(m->pos.y);
    int sz = (int)floorf(m->pos.z);
    int out[MOB_PATH_MAX_NODES][3];
    int cap = MOB_PATH_MAX_NODES < PATHFIND_MAX_LEN ? MOB_PATH_MAX_NODES : PATHFIND_MAX_LEN;
    int n = pathfind_ground(w, sx, sy, sz, tx, ty, tz, out, cap);
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            m->path_xyz[i][0] = out[i][0];
            m->path_xyz[i][1] = out[i][1];
            m->path_xyz[i][2] = out[i][2];
        }
        m->path_len = n;
        m->path_i = 0;
    } else {
        m->path_len = 0;
        m->path_i = 0;
    }
    m->repath_t = MOB_REPATH_COOLDOWN;
}

/* Passive think: idle/wander cycle with cliff awareness + flee-on-hurt. */
static void mob_think_passive(MobPool *pool, Mob *m, World *w, const MobPlayerInfo *pi)
{
    (void)pi;
    const MobDefinition *def = mob_definition(m->type);
    if (m->state == MOB_STATE_HURT) {
        /* Flee from the attacker while the hurt window lasts. */
        float fx = -m->hurt_from.x;
        float fz = -m->hurt_from.z;
        if (fx * fx + fz * fz > 1e-6f) {
            float len = sqrtf(fx * fx + fz * fz);
            m->wish_dir = mmath_vec3(fx / len, 0.0f, fz / len);
            m->wish_speed = def->speed * 1.5f;
            mob_face_toward(m, m->wish_dir.x, m->wish_dir.z);
        }
        if (m->state_t >= MOB_HURT_WINDOW) {
            m->state = MOB_STATE_IDLE;
            m->state_t = 0.0f;
            m->wish_speed = 0.0f;
        }
        return;
    }
    if (m->state == MOB_STATE_IDLE) {
        m->wish_speed = 0.0f;
        if (m->state_t >= m->wander_t) {
            mob_new_wander(pool, m, 2.0f, 4.0f);
            m->state = MOB_STATE_WANDER;
        }
        return;
    }
    /* WANDER: follow the heading, turn at cliffs, stop at leg end. */
    m->yaw = m->wander_yaw;
    mob_steer_yaw(m, def->speed);
    if (!mob_ground_ahead(w, m->pos, m->wander_yaw)) {
        mob_new_wander(pool, m, 2.0f, 4.0f);
        m->state = MOB_STATE_WANDER;
        return;
    }
    if (m->state_t >= m->wander_t) {
        m->state = MOB_STATE_IDLE;
        m->state_t = 0.0f;
        m->wander_t = 1.0f + ((float)(mob_rand(pool) % 1000) / 1000.0f) * 2.0f;
        m->wish_speed = 0.0f;
    }
}

/* Shared pursuit steering: path when the way is blocked or far, direct
 * steering when the line is clear and close (no fake wall-phasing).
 * Used by melee chase and ranged approach/reposition alike.
 */
static void mob_pursue(MobPool *pool, Mob *m, World *w, const MobPlayerInfo *pi, float dist)
{
    const MobDefinition *def = mob_definition(m->type);
    m->state = MOB_STATE_CHASE;
    bool los = mob_has_los(w, mob_eye(m),
                           mmath_vec3(pi->pos.x, pi->pos.y + pi->eye_height, pi->pos.z));
    bool following = false;
    if ((!los || dist > 8.0f)) {
        if (m->repath_t <= 0.0f) {
            bool stale = true;
            if (m->path_i < m->path_len) {
                int ex = m->path_xyz[m->path_len - 1][0];
                int ey = m->path_xyz[m->path_len - 1][1];
                int ez = m->path_xyz[m->path_len - 1][2];
                float ddx = (float)ex + 0.5f - pi->pos.x;
                float ddy = (float)ey - pi->pos.y;
                float ddz = (float)ez + 0.5f - pi->pos.z;
                stale = (ddx * ddx + ddy * ddy + ddz * ddz) > 9.0f;
            }
            if (m->path_i >= m->path_len || stale) {
                mob_request_path(pool, m, w, (int)floorf(pi->pos.x), (int)floorf(pi->pos.y),
                                 (int)floorf(pi->pos.z));
            } else {
                m->repath_t = MOB_REPATH_COOLDOWN;
            }
        }
        if (m->path_i < m->path_len) {
            int wx = m->path_xyz[m->path_i][0];
            int wy = m->path_xyz[m->path_i][1];
            int wz = m->path_xyz[m->path_i][2];
            float wxd = (float)wx + 0.5f - m->pos.x;
            float wzd = (float)wz + 0.5f - m->pos.z;
            float wd = sqrtf(wxd * wxd + wzd * wzd);
            if (wd < 0.6f) {
                m->path_i++;
            } else {
                mob_face_toward(m, wxd, wzd);
                mob_steer_yaw(m, def->speed);
                following = true;
            }
            if (wy > (int)floorf(m->pos.y) + 2) {
                /* Unreachable height: drop the path, retry later. */
                m->path_len = 0;
                m->repath_t = 0.0f;
            }
        }
    }
    if (!following) {
        mob_steer_yaw(m, def->speed);
    }
}

/* Hostile think: acquire/chase/attack the player with path fallback. */
static void mob_think_hostile(MobPool *pool, Mob *m, World *w, const MobPlayerInfo *pi,
                              MobFrameEvents *ev)
{
    const MobDefinition *def = mob_definition(m->type);
    float dx = pi->pos.x - m->pos.x;
    float dz = pi->pos.z - m->pos.z;
    float dist = sqrtf(dx * dx + dz * dz);
    bool valid_target = pi->alive && !pi->creative;
    if (!valid_target && m->target != ENTITY_ID_NULL) {
        m->target = ENTITY_ID_NULL;
        m->state = MOB_STATE_IDLE;
        m->state_t = 0.0f;
        m->path_len = 0;
        m->wish_speed = 0.0f;
    }
    if (valid_target) {
        if (m->target != ENTITY_PLAYER_ID) {
            if (dist <= def->detect_range &&
                mob_has_los(w, mob_eye(m),
                            mmath_vec3(pi->pos.x, pi->pos.y + pi->eye_height, pi->pos.z))) {
                m->target = ENTITY_PLAYER_ID;
                m->state = MOB_STATE_CHASE;
                m->state_t = 0.0f;
                m->path_len = 0;
                m->repath_t = 0.0f;
            }
        } else if (dist > def->lose_range) {
            m->target = ENTITY_ID_NULL;
            m->state = MOB_STATE_IDLE;
            m->state_t = 0.0f;
            m->path_len = 0;
            m->wish_speed = 0.0f;
        }
    }
    if (m->state == MOB_STATE_HURT) {
        /* Brief stagger, then back on the target (or idle without one). */
        m->wish_speed = 0.0f;
        if (m->state_t >= 0.3f) {
            m->state = (m->target == ENTITY_PLAYER_ID) ? MOB_STATE_CHASE : MOB_STATE_IDLE;
            m->state_t = 0.0f;
        }
        return;
    }
    if (m->target != ENTITY_PLAYER_ID) {
        /* No target: lazy wander (shared shape with passive, no flee). */
        if (m->state != MOB_STATE_WANDER) {
            m->state = MOB_STATE_WANDER;
            mob_new_wander(pool, m, 2.0f, 5.0f);
        }
        m->yaw = m->wander_yaw;
        mob_steer_yaw(m, def->speed * 0.6f);
        if (!mob_ground_ahead(w, m->pos, m->wander_yaw)) {
            mob_new_wander(pool, m, 2.0f, 5.0f);
        }
        if (m->state_t >= m->wander_t) {
            m->state = MOB_STATE_IDLE;
            m->state_t = 0.0f;
            m->wander_t = 2.0f;
            m->wish_speed = 0.0f;
        }
        return;
    }
    /* CHASE / ATTACK. */
    mob_face_toward(m, dx, dz);
    if (dist <= def->attack_range) {
        m->state = MOB_STATE_ATTACK;
        m->wish_speed = 0.0f;
        if (m->attack_cd <= 0.0f && dist <= def->attack_range * 1.2f) {
            ev->player_hits++;
            ev->player_damage += def->damage;
            float len = dist > 1e-4f ? dist : 1e-4f;
            ev->player_knock.x += (dx / len) * 6.0f;
            ev->player_knock.z += (dz / len) * 6.0f;
            ev->player_knock.y += 2.0f;
            m->attack_cd = def->attack_cooldown;
        }
        return;
    }
    /* CHASE: shared pursuit steering (path when blocked/far). */
    mob_pursue(pool, m, w, pi, dist);
}

/* Ranged think (skeleton): acquire like other hostiles, then manage a
 * preferred firing band — approach when too far, retreat when too
 * close, hold + aim when placed with line of sight. ATTACK is the
 * release tick (shot queued, cooldown set); AIM accumulates draw time.
 */
static void mob_think_ranged(MobPool *pool, Mob *m, World *w, const MobPlayerInfo *pi,
                             MobFrameEvents *ev)
{
    const MobDefinition *def = mob_definition(m->type);
    float dx = pi->pos.x - m->pos.x;
    float dz = pi->pos.z - m->pos.z;
    float dist = sqrtf(dx * dx + dz * dz);
    bool valid_target = pi->alive && !pi->creative;
    if (!valid_target && m->target != ENTITY_ID_NULL) {
        m->target = ENTITY_ID_NULL;
        m->state = MOB_STATE_IDLE;
        m->state_t = 0.0f;
        m->path_len = 0;
        m->wish_speed = 0.0f;
    }
    if (valid_target) {
        if (m->target != ENTITY_PLAYER_ID) {
            if (dist <= def->detect_range &&
                mob_has_los(w, mob_eye(m),
                            mmath_vec3(pi->pos.x, pi->pos.y + pi->eye_height, pi->pos.z))) {
                m->target = ENTITY_PLAYER_ID;
                m->state = MOB_STATE_CHASE;
                m->state_t = 0.0f;
                m->path_len = 0;
                m->repath_t = 0.0f;
            }
        } else if (dist > def->lose_range) {
            m->target = ENTITY_ID_NULL;
            m->state = MOB_STATE_IDLE;
            m->state_t = 0.0f;
            m->path_len = 0;
            m->wish_speed = 0.0f;
        }
    }
    if (m->state == MOB_STATE_HURT) {
        /* Brief stagger, then re-evaluate range from scratch. */
        m->wish_speed = 0.0f;
        if (m->state_t >= 0.3f) {
            m->state = (m->target == ENTITY_PLAYER_ID) ? MOB_STATE_CHASE : MOB_STATE_IDLE;
            m->state_t = 0.0f;
        }
        return;
    }
    if (m->target != ENTITY_PLAYER_ID) {
        /* No target: lazy wander (same shape as other hostiles). */
        if (m->state != MOB_STATE_WANDER) {
            m->state = MOB_STATE_WANDER;
            mob_new_wander(pool, m, 2.0f, 5.0f);
        }
        m->yaw = m->wander_yaw;
        mob_steer_yaw(m, def->speed * 0.6f);
        if (!mob_ground_ahead(w, m->pos, m->wander_yaw)) {
            mob_new_wander(pool, m, 2.0f, 5.0f);
        }
        if (m->state_t >= m->wander_t) {
            m->state = MOB_STATE_IDLE;
            m->state_t = 0.0f;
            m->wander_t = 2.0f;
            m->wish_speed = 0.0f;
        }
        return;
    }
    mob_face_toward(m, dx, dz);
    /* Melee fallback at contact range (same ev shape as melee mobs). */
    if (dist <= def->attack_range) {
        m->state = MOB_STATE_ATTACK;
        m->wish_speed = 0.0f;
        m->path_len = 0;
        if (m->attack_cd <= 0.0f && dist <= def->attack_range * 1.2f) {
            ev->player_hits++;
            ev->player_damage += def->damage;
            float len = dist > 1e-4f ? dist : 1e-4f;
            ev->player_knock.x += (dx / len) * 5.0f;
            ev->player_knock.z += (dz / len) * 5.0f;
            ev->player_knock.y += 1.5f;
            m->attack_cd = def->attack_cooldown;
        }
        return;
    }
    bool los = mob_has_los(w, mob_eye(m),
                           mmath_vec3(pi->pos.x, pi->pos.y + pi->eye_height, pi->pos.z));
    if (dist > def->prefer_max || !los) {
        /* Too far or blind: approach/reposition with ordinary
         * navigation (pursuit steering, no teleporting). */
        mob_pursue(pool, m, w, pi, dist);
        return;
    }
    if (dist < def->prefer_min) {
        /* Too close: back away along ordinary physics (direct steer;
         * cliffs still respected — never walk off willingly). */
        m->state = MOB_STATE_CHASE;
        float len = dist > 1e-4f ? dist : 1e-4f;
        float bx = -dx / len;
        float bz = -dz / len;
        mob_face_toward(m, -bx, -bz);
        float back_yaw = atan2f(-bx, -bz);
        if (mob_ground_ahead(w, m->pos, back_yaw)) {
            m->wish_dir = mmath_vec3(bx, 0.0f, bz);
            m->wish_speed = def->speed;
        } else {
            /* Nowhere to back to: hold and fire anyway when lined up. */
            m->wish_speed = 0.0f;
            if (los && m->attack_cd <= 0.0f) {
                m->state = MOB_STATE_AIM;
                m->state_t = 0.0f;
            }
        }
        return;
    }
    /* In the band with sight: hold position and draw. */
    if (m->state != MOB_STATE_AIM && m->state != MOB_STATE_ATTACK) {
        m->state = MOB_STATE_AIM;
        m->state_t = 0.0f;
    }
    m->wish_speed = 0.0f;
    m->path_len = 0;
    if (m->state == MOB_STATE_ATTACK) {
        /* Release tick is one think long, then back to the draw. */
        if (m->state_t >= 0.2f) {
            m->state = MOB_STATE_AIM;
            m->state_t = 0.0f;
        }
        return;
    }
    if (m->attack_cd > 0.0f || m->state_t < def->aim_time) {
        return; /* Still drawing / cooling down. */
    }
    /* Fire: aim at the player's center with distance-scaled jitter
     * (dodgeable, never an aimbot). LOS rechecked at the release. */
    if (!los) {
        return;
    }
    Vec3 eye = mob_eye(m);
    Vec3 aim_at =
        mmath_vec3(pi->pos.x, pi->pos.y + pi->eye_height * 0.55f, pi->pos.z);
    Vec3 aim = mmath_vec3_sub(aim_at, eye);
    float alen = mmath_vec3_length(aim);
    if (!(alen > 1e-4f)) {
        return;
    }
    aim = mmath_vec3_scale(aim, 1.0f / alen);
    float jitter = def->inaccuracy * (1.0f + alen * 0.06f);
    float jx = ((float)(mob_rand(pool) % 1000) / 1000.0f - 0.5f) * 2.0f * jitter;
    float jy = ((float)(mob_rand(pool) % 1000) / 1000.0f - 0.5f) * 2.0f * jitter;
    aim.x += jx;
    aim.y += jy;
    float nlen = mmath_vec3_length(aim);
    if (nlen > 1e-6f) {
        aim = mmath_vec3_scale(aim, 1.0f / nlen);
    }
    if (ev->fire_requests < MOB_MAX_SHOTS) {
        ProjectileShot *s = &ev->shots[ev->fire_requests++];
        s->origin = mmath_vec3_add(eye, mmath_vec3_scale(aim, 0.7f));
        s->dir = aim;
        s->speed = def->arrow_speed;
        s->damage = def->arrow_damage;
        s->knock_power = 5.0f;
        s->owner = entity_id_make((int)(m - pool->mobs), m->gen);
    } else {
        ev->shots_dropped++;
    }
    m->attack_cd = def->fire_cooldown;
    m->state = MOB_STATE_ATTACK;
    m->state_t = 0.0f;
}

/* One AI decision tick (10 Hz; called with mob->ai_t drained by update). */
static void mob_think(MobPool *pool, Mob *m, World *w, const MobPlayerInfo *pi, MobFrameEvents *ev)
{
    pool->ai_thinks++;
    const MobDefinition *def = mob_definition(m->type);
    if (m->repath_t > 0.0f) {
        m->repath_t -= MOB_AI_TICK;
        if (m->repath_t < 0.0f) {
            m->repath_t = 0.0f;
        }
    }
    if (def->hostile) {
        if (def->ranged) {
            mob_think_ranged(pool, m, w, pi, ev);
        } else {
            mob_think_hostile(pool, m, w, pi, ev);
        }
    } else {
        mob_think_passive(pool, m, w, pi);
    }
}

/* Damage a mob with an explicit source (hurt gate, knockback with
 * resistance, death + once-only drops). See header for the contract.
 */
bool living_entity_damage_src(MobPool *pool, EntityPool *drops, EntityId id, float amount,
                              Vec3 knock_dir, float knock_power, const Vec3 *from_dir, int source)
{
    Mob *m = mob_resolve(pool, id);
    if (m == NULL || m->dead || !(amount > 0.0f)) {
        return false;
    }
    if (m->hurt_t > 0.0f) {
        return false;
    }
    m->last_source = source;
    const MobDefinition *def = mob_definition(m->type);
    m->health -= amount;
    /* Knockback (resisted, clamped; small upward pop, never a launch). */
    float kx = knock_dir.x;
    float kz = knock_dir.z;
    float kl = sqrtf(kx * kx + kz * kz);
    if (kl > 1e-4f) {
        float k = knock_power * (1.0f - def->knockback_resist);
        m->vel.x += (kx / kl) * k;
        m->vel.z += (kz / kl) * k;
        float hspeed = sqrtf(m->vel.x * m->vel.x + m->vel.z * m->vel.z);
        if (hspeed > 12.0f) {
            m->vel.x *= 12.0f / hspeed;
            m->vel.z *= 12.0f / hspeed;
        }
        m->vel.y += knock_power * 0.25f;
        if (m->vel.y > 6.0f) {
            m->vel.y = 6.0f;
        }
    }
    if (from_dir != NULL) {
        m->hurt_from = mmath_vec3(from_dir->x, 0.0f, from_dir->z);
    }
    m->hurt_t = MOB_HURT_WINDOW;
    m->state = MOB_STATE_HURT;
    m->state_t = 0.0f;
    if (m->health <= 0.0f) {
        m->health = 0.0f;
        m->dead = true;
        m->dead_t = 0.0f;
        m->state = MOB_STATE_DEAD;
        m->state_t = 0.0f;
        m->wish_speed = 0.0f;
        /* Exactly-once drops through the shared item-entity system.
         * min_count 0 is legal (MC 0-2 rolls): a zero roll spawns
         * nothing, so kills sometimes drop nothing from a table. */
        for (int i = 0; i < def->ndrops; ++i) {
            const MobDrop *d = &def->drops[i];
            if (d->item == ITEM_NONE || d->max_count < d->min_count) {
                continue;
            }
            float roll = (float)(mob_rand(pool) % 1000) / 1000.0f;
            if (roll > d->chance) {
                continue;
            }
            uint16_t span = (uint16_t)(d->max_count - d->min_count + 1);
            uint16_t n = (uint16_t)(d->min_count + (mob_rand(pool) % span));
            if (n == 0) {
                continue;
            }
            ItemStack drop = {d->item, n, 0};
            Vec3 at = mmath_vec3(m->pos.x, m->pos.y + 0.5f, m->pos.z);
            if (drops != NULL) {
                entity_spawn(drops, at, &drop);
            }
        }
        return true;
    }
    return false;
}

/* Damage a mob (melee source; see the _src variant for the contract). */
bool living_entity_damage(MobPool *pool, EntityPool *drops, EntityId id, float amount, Vec3 knock_dir,
                           float knock_power, const Vec3 *from_dir)
{
    return living_entity_damage_src(pool, drops, id, amount, knock_dir, knock_power, from_dir,
                                    DAMAGE_MELEE);
}

/* True when a block cell shields a melee swing (solid only). */
bool mob_block_shields(uint16_t block)
{
    return block_is_solid(block);
}

/* Ray vs living-mob AABBs (nearest alive hit). */
bool mob_raycast(const MobPool *pool, Vec3 eye, Vec3 dir, float max_dist, float *out_dist,
                 EntityId *out_id)
{
    if (out_dist != NULL) {
        *out_dist = 0.0f;
    }
    if (out_id != NULL) {
        *out_id = ENTITY_ID_NULL;
    }
    if (pool == NULL || !(max_dist > 0.0f)) {
        return false;
    }
    float len = mmath_vec3_length(dir);
    if (!(len > 1e-8f)) {
        return false;
    }
    Vec3 d = mmath_vec3_scale(dir, 1.0f / len);
    bool found = false;
    float best = max_dist;
    EntityId best_id = ENTITY_ID_NULL;
    for (int i = 0; i < MOB_MAX; ++i) {
        const Mob *m = &pool->mobs[i];
        if (!m->active || m->dead) {
            continue;
        }
        float hw = m->width * 0.5f;
        Vec3 mn = mmath_vec3(m->pos.x - hw, m->pos.y, m->pos.z - hw);
        Vec3 mx = mmath_vec3(m->pos.x + hw, m->pos.y + m->height, m->pos.z + hw);
        /* Slab test per axis. */
        float tmin = 0.0f;
        float tmax = best;
        bool miss = false;
        const float o[3] = {eye.x, eye.y, eye.z};
        const float dd[3] = {d.x, d.y, d.z};
        const float bmin[3] = {mn.x, mn.y, mn.z};
        const float bmax[3] = {mx.x, mx.y, mx.z};
        for (int a = 0; a < 3 && !miss; ++a) {
            if (fabsf(dd[a]) < 1e-8f) {
                if (o[a] < bmin[a] || o[a] > bmax[a]) {
                    miss = true;
                }
            } else {
                float t1 = (bmin[a] - o[a]) / dd[a];
                float t2 = (bmax[a] - o[a]) / dd[a];
                if (t1 > t2) {
                    float t = t1;
                    t1 = t2;
                    t2 = t;
                }
                if (t1 > tmin) {
                    tmin = t1;
                }
                if (t2 < tmax) {
                    tmax = t2;
                }
                if (tmin > tmax) {
                    miss = true;
                }
            }
        }
        if (!miss && tmin > 0.0f && tmin <= best) {
            best = tmin;
            best_id = entity_id_make(i, m->gen);
            found = true;
        }
    }
    if (found) {
        if (out_dist != NULL) {
            *out_dist = best;
        }
        if (out_id != NULL) {
            *out_id = best_id;
        }
    }
    return found;
}

/* Melee stats for a held item (data-driven from the item registry;
 * bare hands and invalid IDs hit as fists: 1 damage, 0.4 s).
 */
void mob_tool_stats(ItemId held, float *out_damage, float *out_cooldown)
{
    float dmg = 1.0f;
    float cd = 0.4f;
    if (held != ITEM_NONE && item_is_valid(held)) {
        const ItemInfo *info = item_get_info(held);
        if (info->attack_damage > 0) {
            dmg = (float)info->attack_damage;
        }
        if (info->attack_cooldown > 0.0f) {
            cd = info->attack_cooldown;
        }
    }
    if (out_damage != NULL) {
        *out_damage = dmg;
    }
    if (out_cooldown != NULL) {
        *out_cooldown = cd;
    }
}

/* Chunk coords with negatives (same formula as interaction.c). */
static int mob_chunk_of(int v)
{
    return v >= 0 ? v / CHUNK_X : -((-v + CHUNK_X - 1) / CHUNK_X);
}

/* Find surface feet Y in a column: highest solid with two air above and
 * no water in the feet/head cells. Returns -1 when none exists.
 */
static int mob_surface_y(const World *w, int x, int z)
{
    for (int y = CHUNK_Y - 3; y >= 1; --y) {
        uint16_t ground = world_get_block(w, x, y, z);
        if (ground == BLOCK_AIR || ground == BLOCK_WATER || !block_is_solid(ground)) {
            continue;
        }
        uint16_t feet = world_get_block(w, x, y + 1, z);
        uint16_t head = world_get_block(w, x, y + 2, z);
        if (feet == BLOCK_AIR && head == BLOCK_AIR) {
            return y + 1;
        }
    }
    return -1;
}

/* One natural-spawn attempt for a type (4 column tries, first valid wins).
 * Passive wants daylight + grass/dirt ground; hostile wants night.
 */
static void mob_try_spawn(MobPool *pool, World *w, long seed, Vec3 player_pos, bool day,
                          EntityType type)
{
    (void)seed; /* Biome gating is ground-block based (documented M8 rule). */
    for (int attempt = 0; attempt < 4; ++attempt) {
        float ang = (float)(mob_rand(pool) % 360) * 3.14159265f / 180.0f;
        float dist = MOB_SPAWN_MIN_DIST + (float)(mob_rand(pool) % MOB_SPAWN_RING);
        int x = (int)floorf(player_pos.x + cosf(ang) * dist);
        int z = (int)floorf(player_pos.z + sinf(ang) * dist);
        if (world_get_chunk(w, mob_chunk_of(x), mob_chunk_of(z)) == NULL) {
            continue; /* Only spawn into loaded terrain. */
        }
        int gy = mob_surface_y(w, x, z);
        if (gy < 0) {
            continue;
        }
        uint16_t ground = world_get_block(w, x, gy - 1, z);
        if (type == ENTITY_COW) {
            if (!day) {
                return; /* Daylight grazers only (one rule per tick). */
            }
            if (ground != BLOCK_GRASS && ground != BLOCK_DIRT) {
                continue;
            }
            if (mob_count_type(pool, ENTITY_COW) >= MOB_MAX_PASSIVE) {
                return;
            }
        } else if (type == ENTITY_GLOOMSTALKER || type == ENTITY_SKELETON) {
            if (day) {
                return;
            }
            /* Shared hostile cap: skeletons count against it (M9 rule). */
            if (mob_count_type(pool, ENTITY_GLOOMSTALKER) + mob_count_type(pool, ENTITY_SKELETON) >=
                MOB_MAX_HOSTILE) {
                return;
            }
        } else {
            return;
        }
        if (mob_spawn(pool, type, mmath_vec3((float)x + 0.5f, (float)gy, (float)z + 0.5f),
                      (float)(mob_rand(pool) % 360) * 3.14159265f / 180.0f) != ENTITY_ID_NULL) {
            LOG_DEBUG("mob spawned %s at (%d,%d,%d)", mob_definition(type)->name, x, gy, z);
        }
        return;
    }
}

/* Natural-spawn tick (every MOB_SPAWN_PERIOD seconds). */
static void mob_spawner_update(MobPool *pool, World *w, float day_progress, Vec3 player_pos, float dt)
{
    pool->spawn_t += dt;
    if (pool->spawn_t < MOB_SPAWN_PERIOD) {
        return;
    }
    pool->spawn_t = 0.0f;
    bool day = mob_is_day(day_progress);
    mob_try_spawn(pool, w, w->seed, player_pos, day, ENTITY_COW);
    /* Hostile pick: skeletons join the night rotation (~35%) without
     * their own manager or cap (deterministic via pool RNG). */
    EntityType hostile = (mob_rand(pool) % 100 < 35) ? ENTITY_SKELETON : ENTITY_GLOOMSTALKER;
    mob_try_spawn(pool, w, w->seed, player_pos, day, hostile);
}

/* Simulate every mob (see header for the contract). */
void mob_update_all(MobPool *pool, EntityPool *drops, World *w, const MobPlayerInfo *pi, float dt,
                    MobFrameEvents *ev)
{
    if (pool == NULL || drops == NULL || w == NULL || pi == NULL || ev == NULL) {
        return;
    }
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.25f) {
        dt = 0.25f;
    }
    ev->player_hits = 0;
    ev->player_damage = 0.0f;
    ev->player_knock = mmath_vec3(0.0f, 0.0f, 0.0f);
    ev->mobs_died = 0;
    ev->mobs_hurt = 0;
    ev->fire_requests = 0;
    ev->shots_dropped = 0;
    ev->last_death_pos = mmath_vec3(0.0f, 0.0f, 0.0f);
    ev->last_death_item = ITEM_NONE;
    mob_spawner_update(pool, w, pi->day_progress, pi->pos, dt);
    for (int i = 0; i < MOB_MAX; ++i) {
        Mob *m = &pool->mobs[i];
        if (!m->active) {
            continue;
        }
        if (m->dead) {
            /* Corpse timer, then removal (drops fired exactly once at kill). */
            m->dead_t += dt;
            if (m->dead_t >= MOB_DEATH_TIME) {
                mob_remove(pool, entity_id_make(i, m->gen));
            }
            continue;
        }
        float dx = pi->pos.x - m->pos.x;
        float dz = pi->pos.z - m->pos.z;
        float dist = sqrtf(dx * dx + dz * dz);
        /* Hostile despawn past range comes before the sim freeze below:
         * frozen far mobs must still be collected, or the pool leaks. */
        if (!m->dead && mob_definition(m->type)->hostile && dist > MOB_DESPAWN_RANGE) {
            mob_remove(pool, entity_id_make(i, m->gen));
            continue;
        }
        if (dist > MOB_SIM_RANGE) {
            /* Frozen: persist position, hold still, skip everything. */
            m->prev_pos = m->pos;
            m->render_pos = m->pos;
            m->stuck_pos = m->pos;
            m->wish_speed = 0.0f;
            continue;
        }
        if (m->hurt_t > 0.0f) {
            m->hurt_t -= dt;
            if (m->hurt_t < 0.0f) {
                m->hurt_t = 0.0f;
            }
        }
        if (m->attack_cd > 0.0f) {
            m->attack_cd -= dt;
            if (m->attack_cd < 0.0f) {
                m->attack_cd = 0.0f;
            }
        }
        if (m->repath_t > 0.0f) {
            m->repath_t -= dt;
            if (m->repath_t < 0.0f) {
                m->repath_t = 0.0f;
            }
        }
        m->state_t += dt;
        /* AI decisions at 10 Hz (physics stays per-frame). */
        m->ai_t += dt;
        while (m->ai_t >= MOB_AI_TICK) {
            m->ai_t -= MOB_AI_TICK;
            mob_think(pool, m, w, pi, ev);
        }
        /* Stuck detection: 1 s windows, wish active but no progress. */
        m->stuck_t += dt;
        if (m->stuck_t >= 1.0f) {
            float sx = m->pos.x - m->stuck_pos.x;
            float sz = m->pos.z - m->stuck_pos.z;
            if (m->wish_speed > 0.01f && (sx * sx + sz * sz) < 0.04f) {
                const MobDefinition *def = mob_definition(m->type);
                if (def->hostile) {
                    m->path_len = 0;
                    m->repath_t = 0.0f;
                } else {
                    mob_new_wander(pool, m, 1.0f, 2.0f);
                    m->state = MOB_STATE_WANDER;
                }
            }
            m->stuck_pos = m->pos;
            m->stuck_t = 0.0f;
        }
        /* Physics + walk animation. */
        m->prev_pos = m->pos;
        mob_physics_step(m, w, dt);
        float hspeed = sqrtf(m->vel.x * m->vel.x + m->vel.z * m->vel.z);
        if (hspeed > 0.1f && m->grounded) {
            m->walk_phase += hspeed * dt * 4.0f;
        }
        /* Fall damage (same policy as the player, no knockback). */
        if (m->last_fall >= 0.0f) {
            float dmg = survival_fall_damage(m->last_fall);
            m->last_fall = -1.0f;
            if (dmg > 0.0f) {
                Vec3 zero = mmath_vec3(0.0f, 0.0f, 0.0f);
                float before = m->health;
                if (living_entity_damage(pool, drops, entity_id_make(i, m->gen), dmg, zero, 0.0f,
                                         NULL)) {
                    ev->mobs_died++;
                    ev->last_death_pos = m->pos;
                    const MobDefinition *fdef = mob_definition(m->type);
                    ev->last_death_item =
                        fdef->ndrops > 0 ? fdef->drops[0].item : (ItemId)ITEM_NONE;
                } else if (m->health < before) {
                    /* Gated fall ticks (hurt window) report nothing. */
                    ev->mobs_hurt++;
                }
            }
        }
    }
}
