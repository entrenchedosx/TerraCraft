#include "game/player.h"
#include "core/log.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <math.h>
#include <stddef.h>

/* Snapping epsilon for collision clamps (keeps flush faces non-overlapping). */
#define PLAYER_EPS 0.001f
/* Terminal fall speed (m/s, downward clamp; MC: -3.92 blocks/tick). */
#define PLAYER_TERMINAL_VEL -78.4f

/* Pitch clamp (+/-89 deg, matches camera). */
#define PLAYER_PITCH_LIMIT 1.55334306f

/* Horizontal wish direction from yaw (unit, y=0). */
static Vec3 player_wish_dir(float yaw, float fwd, float strafe)
{
    Vec3 f = mmath_vec3(-sinf(yaw), 0.0f, -cosf(yaw));
    Vec3 r = mmath_vec3(-f.z, 0.0f, f.x); /* right = forward rotated -90 deg. */
    Vec3 w = mmath_vec3_add(mmath_vec3_scale(f, fwd), mmath_vec3_scale(r, strafe));
    float len = mmath_vec3_length(w);
    if (len > 1.0f) {
        w = mmath_vec3_scale(w, 1.0f / len);
    }
    return w;
}

/* Player AABB corners from feet position. */
static void player_box(const Player *p, Vec3 *out_mn, Vec3 *out_mx)
{
    float hw = p->width * 0.5f;
    out_mn->x = p->pos.x - hw;
    out_mn->y = p->pos.y;
    out_mn->z = p->pos.z - hw;
    out_mx->x = p->pos.x + hw;
    out_mx->y = p->pos.y + p->height;
    out_mx->z = p->pos.z + hw;
}

/* Initialise a player with M3 defaults.
 *
 * Args:
 *   p: player to init.
 */
void player_init(Player *p)
{
    if (p == NULL) {
        return;
    }
    p->pos = mmath_vec3(8.5f, 80.0f, 8.5f);
    p->render_pos = p->pos;
    p->vel = mmath_vec3(0.0f, 0.0f, 0.0f);
    p->acc = mmath_vec3(0.0f, 0.0f, 0.0f);
    p->yaw = 0.0f;
    p->pitch = -0.1f;
    p->width = 0.6f;
    p->height = 1.8f;
    p->eye_height = 1.62f;
    p->grounded = false;
    p->flying = false;
    p->sprinting = false;
    p->sneaking = false;
    p->walk_speed = 4.317f;
    p->sprint_speed = 5.612f;
    p->fly_speed = 10.9f;
    p->jump_vel = 8.8f;
    p->gravity = 32.0f;
    p->drag_air = 0.91f;
    p->drag_ground = 0.6f;
    p->step_accum = 0.0f;
    p->mode = 0; /* WORLD_MODE_SURVIVAL (int to avoid header weight). */
    inv_init(&p->inv);
    p->hotbar_sel = 0;
    p->health = 20.0f;
    p->max_health = 20.0f;
    p->hunger = 20.0f;
    p->max_hunger = 20.0f;
    p->dead = false;
    p->hunger_t = 0.0f;
    p->regen_t = 0.0f;
    p->exhaustion = 0.0f;
    p->hurt_t = 0.0f;
    p->attack_cd = 0.0f;
    p->eat_active = false;
    p->eat_slot = 0;
    p->eat_t = 0.0f;
    p->bow_drawing = false;
    p->bow_slot = 0;
    p->bow_t = 0.0f;
    p->bow_full = false;
    p->fall_peak = -1.0f;
    p->last_fall = -1.0f;
    p->mine_active = false;
    p->mine_bx = 0;
    p->mine_by = 0;
    p->mine_bz = 0;
    p->mine_block = 0;
    p->mine_held = ITEM_NONE;
    p->mine_progress = 0.0f;
    p->mine_need = 0.0f;
}

/* Find a safe spawn column top.
 *
 * Args:
 *   w: world.
 *   sx, sz: desired column.
 *   out: receiver.
 *
 * Returns: true when found.
 */
bool player_find_spawn(const World *w, int sx, int sz, Vec3 *out)
{
    if (w == NULL || out == NULL) {
        return false;
    }
    for (int y = CHUNK_Y - 3; y >= 1; --y) {
        uint16_t foot = world_get_block(w, sx, y, sz);
        uint16_t head1 = world_get_block(w, sx, y + 1, sz);
        uint16_t head2 = world_get_block(w, sx, y + 2, sz);
        if (block_is_solid(foot) && !block_is_solid(head1) && !block_is_solid(head2)) {
            *out = mmath_vec3((float)sx + 0.5f, (float)(y + 1), (float)sz + 0.5f);
            return true;
        }
    }
    return false;
}

/* Eye position.
 *
 * Args:
 *   p: player.
 *
 * Returns: eye position.
 */
Vec3 player_eye_pos(const Player *p)
{
    if (p == NULL) {
        return mmath_vec3(0.0f, 0.0f, 0.0f);
    }
    /* Render eye: interpolated feet + eye height (smoothed at 60 Hz). */
    return mmath_vec3(p->render_pos.x, p->render_pos.y + p->eye_height, p->render_pos.z);
}

/* Apply mouse look (clamped pitch).
 *
 * Args:
 *   p: player.
 *   dx, dy: pixels.
 *   sensitivity: rad/px.
 */
void player_add_look(Player *p, float dx, float dy, float sensitivity)
{
    if (p == NULL) {
        return;
    }
    p->yaw -= dx * sensitivity;
    p->pitch -= dy * sensitivity;
    if (p->pitch > PLAYER_PITCH_LIMIT) {
        p->pitch = PLAYER_PITCH_LIMIT;
    }
    if (p->pitch < -PLAYER_PITCH_LIMIT) {
        p->pitch = -PLAYER_PITCH_LIMIT;
    }
}

/* AABB solidity test (epsilon-shrunk max side so flush faces don't count).
 *
 * Args:
 *   w: world.
 *   mn, mx: box corners.
 *
 * Returns: true on solid overlap.
 */
bool player_aabb_solid(const World *w, Vec3 mn, Vec3 mx)
{
    if (w == NULL) {
        return false;
    }
    int x0 = (int)floorf(mn.x);
    int y0 = (int)floorf(mn.y);
    int z0 = (int)floorf(mn.z);
    int x1 = (int)floorf(mx.x - PLAYER_EPS);
    int y1 = (int)floorf(mx.y - PLAYER_EPS);
    int z1 = (int)floorf(mx.z - PLAYER_EPS);
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

/* Resolve one axis after moving: snap out and zero velocity on hit.
 * is_y_down selects landing logic (sets grounded).
 */
static void player_resolve_axis(Player *p, World *w, int axis, float delta, bool is_y_down)
{
    Vec3 mn, mx;
    player_box(p, &mn, &mx);
    if (!player_aabb_solid(w, mn, mx)) {
        return;
    }
    float hw = p->width * 0.5f;
    if (axis == 0) {
        if (delta > 0.0f) {
            p->pos.x = floorf(mx.x) - hw - PLAYER_EPS;
        } else if (delta < 0.0f) {
            p->pos.x = floorf(mn.x) + 1.0f + hw + PLAYER_EPS;
        }
        p->vel.x = 0.0f;
    } else if (axis == 2) {
        if (delta > 0.0f) {
            p->pos.z = floorf(mx.z) - hw - PLAYER_EPS;
        } else if (delta < 0.0f) {
            p->pos.z = floorf(mn.z) + 1.0f + hw + PLAYER_EPS;
        }
        p->vel.z = 0.0f;
    } else {
        if (is_y_down) {
            p->pos.y = floorf(mn.y) + 1.0f;
            p->grounded = true;
        } else {
            p->pos.y = floorf(mx.y) - p->height - PLAYER_EPS;
        }
        p->vel.y = 0.0f;
    }
}

/* Single fixed physics step.
 *
 * Args:
 *   p: player.
 *   in: input.
 *   w: world (collision skipped when NULL).
 */
static void player_step(Player *p, const PlayerInput *in, World *w)
{
    float fwd = in ? in->fwd : 0.0f;
    float strafe = in ? in->strafe : 0.0f;
    bool jump = in ? in->jump : false;
    bool sneak = in ? in->sneak : false;
    bool sprint = in ? in->sprint : false;
    const float dt = PLAYER_STEP_DT;

    p->sprinting = sprint && !sneak;
    p->sneaking = sneak && !p->flying;

    if (p->flying) {
        /* Creative no-clip flight: direct velocity, no gravity/collision. */
        Vec3 wish = player_wish_dir(p->yaw, fwd, strafe);
        float vy = 0.0f;
        if (jump) {
            vy += 1.0f;
        }
        if (sneak) {
            vy -= 1.0f;
        }
        p->vel = mmath_vec3_add(mmath_vec3_scale(wish, p->fly_speed), mmath_vec3(0.0f, vy * p->fly_speed, 0.0f));
        p->acc = mmath_vec3(0.0f, 0.0f, 0.0f);
        p->pos = mmath_vec3_add(p->pos, mmath_vec3_scale(p->vel, dt));
        p->grounded = false;
        p->fall_peak = -1.0f; /* Flight never counts as falling. */
        p->last_fall = -1.0f;
        return;
    }

    /* Walk mode: horizontal velocity follows input; sneak is 0.3x (MC:
     * 1.31 m/s vs 4.317 walk). */
    float speed = sprint && !sneak ? p->sprint_speed : p->walk_speed;
    if (sneak) {
        speed *= 0.3f;
    }
    Vec3 wish = player_wish_dir(p->yaw, fwd, strafe);
    if (mmath_vec3_length_sq(wish) > 1e-8f) {
        p->vel.x = wish.x * speed;
        p->vel.z = wish.z * speed;
    } else {
        /* No input: decay horizontal velocity (MC-style per-step retention). */
        float d = p->grounded ? p->drag_ground : p->drag_air;
        p->vel.x *= d;
        p->vel.z *= d;
        if (fabsf(p->vel.x) < 1e-4f) {
            p->vel.x = 0.0f;
        }
        if (fabsf(p->vel.z) < 1e-4f) {
            p->vel.z = 0.0f;
        }
    }

    /* Vertical: gravity integrate, terminal clamp, jump on grounded. */
    p->acc = mmath_vec3(0.0f, -p->gravity, 0.0f);
    p->vel.y -= p->gravity * dt;
    if (p->vel.y < PLAYER_TERMINAL_VEL) {
        p->vel.y = PLAYER_TERMINAL_VEL;
    }
    if (jump && p->grounded) {
        p->vel.y = p->jump_vel;
        p->grounded = false;
    }

    if (w == NULL) {
        p->pos = mmath_vec3_add(p->pos, mmath_vec3_scale(p->vel, dt));
        return;
    }

    /* Axis-separated move + collide: X, then Z, then Y. */
    p->grounded = false;
    float dx = p->vel.x * dt;
    float dz = p->vel.z * dt;
    float dy = p->vel.y * dt;
    p->pos.x += dx;
    player_resolve_axis(p, w, 0, dx, false);
    p->pos.z += dz;
    player_resolve_axis(p, w, 2, dz, false);
    p->pos.y += dy;
    player_resolve_axis(p, w, 1, dy, dy <= 0.0f);

    /* Fall tracking: peak while airborne, distance sampled on landing. */
    if (p->grounded) {
        if (p->fall_peak >= 0.0f) {
            p->last_fall = p->fall_peak - p->pos.y;
            if (p->last_fall < 0.0f) {
                p->last_fall = 0.0f;
            }
        }
        p->fall_peak = -1.0f;
    } else if (p->fall_peak < p->pos.y) {
        p->fall_peak = p->pos.y;
    }
}

/* Advance the player with fixed substeps.
 *
 * Args:
 *   p: player.
 *   in: input.
 *   w: world.
 *   dt: frame time.
 */
void player_update(Player *p, const PlayerInput *in, World *w, float dt)
{
    if (p == NULL) {
        return;
    }
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.25f) {
        dt = 0.25f;
    }
    /* Clamp pitch defensively (mouse code should already clamp). */
    if (p->pitch > PLAYER_PITCH_LIMIT) {
        p->pitch = PLAYER_PITCH_LIMIT;
    }
    if (p->pitch < -PLAYER_PITCH_LIMIT) {
        p->pitch = -PLAYER_PITCH_LIMIT;
    }
    p->step_accum += dt;
    int steps = 0;
    while (p->step_accum >= PLAYER_STEP_DT && steps < PLAYER_MAX_STEPS) {
        player_step(p, in, w);
        p->step_accum -= PLAYER_STEP_DT;
        steps++;
    }
    if (steps == PLAYER_MAX_STEPS) {
        p->step_accum = 0.0f; /* Spiral-of-death guard: drop excess time. */
    }
}
