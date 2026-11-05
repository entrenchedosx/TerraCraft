#include "game/player.h"
#include "game/vphys.h"
#include "core/log.h"
#include "world/block.h"
#include "world/world_meta.h"
#include "world/chunk.h"
#include "world/world.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

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

/* Exponential approach factor for a fixed step (1 = snap, smaller = slower). */
static float player_smoothing(float dt, float tau)
{
    if (!(tau > 1e-5f) || !isfinite(tau)) {
        return 1.0f;
    }
    return 1.0f - expf(-dt / tau);
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
    p->drag_air = 0.969f;
    p->drag_ground = 0.817f;
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
    p->walk_phase = 0.0f;
    p->bob_amp = 0.0f;
    p->water_contact = 0.0f;
    p->anim_time = 0.0f;
    p->limb_pos = p->limb_pos_prev = 0.0f;
    p->limb_speed = p->limb_speed_prev = 0.0f;
    p->body_yaw = p->body_yaw_prev = p->yaw;
    p->yaw_prev = p->yaw;
    p->swing_active = false;
    p->interp_alpha = 1.0f;
    p->mine_active = false;
    p->mine_bx = 0;
    p->mine_by = 0;
    p->mine_bz = 0;
    p->mine_block = 0;
    p->mine_held = ITEM_NONE;
    p->mine_progress = 0.0f;
    p->mine_need = 0.0f;
    memset(&p->ph, 0, sizeof(p->ph));
    p->ph_init = false;
    p->ph_accum = 0.0f;
    p->ph_pos_mirror = p->pos;
    p->ph_vel_mirror = p->vel;
    p->eye_prev = p->eye_height;
    p->eye_cur = p->eye_height;
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
        if (block_is_solid(foot) && !block_is_solid(head1) && !block_is_solid(head2) &&
            !block_is_water(head1) && !block_is_water(head2) && head1 != BLOCK_LAVA && head2 != BLOCK_LAVA) {
            *out = mmath_vec3((float)sx + 0.5f, (float)(y + 1), (float)sz + 0.5f);
            return true;
        }
    }
    return false;
}

/* Apply a capped knockback impulse (mirrors the mob caps). */
void player_apply_knockback(Player *p, float kx, float ky, float kz)
{
    if (p == NULL) {
        return;
    }
    p->vel.x += kx;
    p->vel.y += ky;
    p->vel.z += kz;
    float hs = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
    if (hs > 12.0f) {
        float s = 12.0f / hs;
        p->vel.x *= s;
        p->vel.z *= s;
    }
    if (p->vel.y > 6.0f) {
        p->vel.y = 6.0f;
    }
}

/* Eye position. *
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

/* Fractional water contact for the player's complete AABB. Flow states
 * occupy only block_water_height() from the bottom of their cell; divide
 * actual overlap volume by the full player volume so shallow immersion has
 * a proportionally smaller effect than full submersion. */
float player_water_contact(const World *w, const Player *p)
{
    if (w == NULL || p == NULL || !(p->width > 0.0f) || !(p->height > 0.0f)) {
        return 0.0f;
    }
    float half = p->width * 0.5f;
    float min_x = p->pos.x - half;
    float max_x = p->pos.x + half;
    float min_y = p->pos.y;
    float max_y = p->pos.y + p->height;
    float min_z = p->pos.z - half;
    float max_z = p->pos.z + half;
    int x0 = (int)floorf(min_x + PLAYER_EPS);
    int x1 = (int)floorf(max_x - PLAYER_EPS);
    int y0 = (int)floorf(min_y + PLAYER_EPS);
    int y1 = (int)floorf(max_y - PLAYER_EPS);
    int z0 = (int)floorf(min_z + PLAYER_EPS);
    int z1 = (int)floorf(max_z - PLAYER_EPS);
    float footprint = p->width * p->width;
    float body_volume = footprint * p->height;
    float contact = 0.0f;
    if (!(body_volume > 0.0f)) {
        return 0.0f;
    }
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
                if (overlap_x > PLAYER_EPS && overlap_y > PLAYER_EPS && overlap_z > PLAYER_EPS) {
                    contact += overlap_x * overlap_y * overlap_z / body_volume;
                    if (contact >= 1.0f) {
                        return 1.0f;
                    }
                }
            }
        }
    }
    return contact;
}

bool player_eye_in_water(const World *w, const Player *p)
{
    if (w == NULL || p == NULL || !isfinite(p->pos.x) || !isfinite(p->pos.y) ||
        !isfinite(p->pos.z) || !(p->eye_height > 0.0f)) {
        return false;
    }
    float ey = p->pos.y + p->eye_height;
    uint16_t id = world_get_block(w, (int)floorf(p->pos.x), (int)floorf(ey), (int)floorf(p->pos.z));
    return block_is_water(id);
}

bool player_can_stand(const Player *p, const World *w)
{
    if (p == NULL) {
        return false;
    }
    if (w == NULL) {
        return true;
    }
    if (!isfinite(p->pos.x) || !isfinite(p->pos.y) || !isfinite(p->pos.z) || !(p->width > 0.0f)) {
        return false;
    }
    float hw = p->width * 0.5f;
    Vec3 mn = mmath_vec3(p->pos.x - hw, p->pos.y, p->pos.z - hw);
    Vec3 mx = mmath_vec3(p->pos.x + hw, p->pos.y + PLAYER_STAND_HEIGHT, p->pos.z + hw);
    return !player_aabb_solid(w, mn, mx);
}

void player_view_bob_offset(const Player *p, float *out_lateral, float *out_vertical)
{
    float lat = 0.0f;
    float vert = 0.0f;
    if (p != NULL && isfinite(p->walk_phase) && isfinite(p->bob_amp) && p->bob_amp > 0.0f) {
        float amp = p->bob_amp;
        if (amp < 0.0f) {
            amp = 0.0f;
        }
        lat = sinf(p->walk_phase) * amp * 0.5f;
        float c = cosf(p->walk_phase) * amp;
        vert = -(c < 0.0f ? -c : c);
    }
    if (out_lateral != NULL) {
        *out_lateral = lat;
    }
    if (out_vertical != NULL) {
        *out_vertical = vert;
    }
}

/* ---- vanilla 20 TPS walking path -------------------------------------------- */

#define RAD_TO_DEG 57.29577951308232f

static bool vec3_differs(Vec3 a, Vec3 b, float eps)
{
    return fabsf(a.x - b.x) > eps || fabsf(a.y - b.y) > eps || fabsf(a.z - b.z) > eps;
}

/* Pull externally changed position/velocity (spawn, respawn, knockback) into
 * the double-precision physics state. */
static void player_phys_import(Player *p)
{
    if (!p->ph_init) {
        vphys_init(&p->ph, (double)p->pos.x, (double)p->pos.y, (double)p->pos.z);
        p->ph.vx = (double)p->vel.x / 20.0;
        p->ph.vy = (double)p->vel.y / 20.0;
        p->ph.vz = (double)p->vel.z / 20.0;
        p->ph_init = true;
        p->ph_pos_mirror = p->pos;
        p->ph_vel_mirror = p->vel;
        p->ph_accum = 0.0f;
        p->eye_prev = p->eye_height;
        p->eye_cur = p->eye_height;
        return;
    }
    if (vec3_differs(p->pos, p->ph_pos_mirror, 1e-4f)) {
        p->ph.x = p->ph.px = (double)p->pos.x;
        p->ph.y = p->ph.py = (double)p->pos.y;
        p->ph.z = p->ph.pz = (double)p->pos.z;
        p->ph.fall_distance = 0.0;
    }
    if (vec3_differs(p->vel, p->ph_vel_mirror, 1e-4f)) {
        p->ph.vx = (double)p->vel.x / 20.0;
        p->ph.vy = (double)p->vel.y / 20.0;
        p->ph.vz = (double)p->vel.z / 20.0;
    }
}

/* Wrap an angle difference into -pi..pi. */
static float wrap_pi(float a)
{
    const float two_pi = 6.28318530717958647692f;
    a = fmodf(a, two_pi);
    if (a > 3.14159265358979f) {
        a -= two_pi;
    } else if (a < -3.14159265358979f) {
        a += two_pi;
    }
    return a;
}

/* LivingEntity.tick body animation, once per 20 Hz tick: the walk animation speed/position
 * (calculateEntityAnimation + WalkAnimationState.update with factor 0.4) and the body rotation
 * (yBodyRot follows the movement direction, tickHeadTurn lets the head turn 50 degrees from it). */
static void player_tick_body_animation(Player *p)
{
    const float rad_to_deg = 57.29577951308232f;
    float dx = (float)(p->ph.x - p->ph.px);
    float dz = (float)(p->ph.z - p->ph.pz);
    float moved = sqrtf(dx * dx + dz * dz);
    float target = moved * 4.0f < 1.0f ? moved * 4.0f : 1.0f;
    p->limb_pos_prev = p->limb_pos;
    p->limb_speed_prev = p->limb_speed;
    p->limb_speed += (target - p->limb_speed) * 0.4f;
    p->limb_pos += p->limb_speed;

    p->body_yaw_prev = p->body_yaw;
    p->yaw_prev = p->yaw;
    float want = p->body_yaw;
    if (dx * dx + dz * dz > 0.0025000002f) {
        /* Heading in this engine's yaw convention (0 faces -Z, forward = (-sin, -cos)). */
        float heading = atan2f(-dx, -dz);
        float off = fabsf(wrap_pi(p->yaw - heading)) * rad_to_deg;
        want = (off > 95.0f && off < 265.0f) ? heading + 3.14159265358979f : heading;
    }
    if (p->swing_active) {
        want = p->yaw;
    }
    p->body_yaw += wrap_pi(want - p->body_yaw) * 0.3f;
    float rel = wrap_pi(p->yaw - p->body_yaw);
    const float max_rel = 50.0f / rad_to_deg;
    if (fabsf(rel) > max_rel) {
        p->body_yaw += rel - (rel < 0.0f ? -max_rel : max_rel);
    }
}

static void player_phys_tick(Player *p, const PlayerInput *in, World *w)
{
    VInput vi;
    vi.forward = in != NULL ? in->fwd : 0.0f;
    vi.left = in != NULL ? -in->strafe : 0.0f;
    vi.jump = in != NULL && in->jump;
    vi.shift = in != NULL && in->sneak;
    vi.sprint_key = in != NULL && in->sprint;
    float yaw_deg = 180.0f - p->yaw * RAD_TO_DEG;
    float pitch_deg = -p->pitch * RAD_TO_DEG;
    /* Creative may toggle flight with a double jump tap; vphys ends it on landing. */
    p->ph.may_fly = p->mode == WORLD_MODE_CREATIVE;
    p->ph.flying = p->flying && p->ph.may_fly;
    vphys_tick(&p->ph, &vi, w, yaw_deg, pitch_deg);
    p->flying = p->ph.flying;
    p->anim_time += VPHYS_TICK_SECONDS;
    player_tick_body_animation(p);

    p->pos = mmath_vec3((float)p->ph.x, (float)p->ph.y, (float)p->ph.z);
    p->vel = mmath_vec3((float)(p->ph.vx * 20.0), (float)(p->ph.vy * 20.0), (float)(p->ph.vz * 20.0));
    p->ph_pos_mirror = p->pos;
    p->ph_vel_mirror = p->vel;
    p->grounded = p->ph.on_ground;
    p->sprinting = p->ph.sprinting;
    p->sneaking = p->ph.pose == VPOSE_CROUCHING;
    if (p->flying) {
        p->fall_peak = -1.0f; /* Flight never counts as falling. */
        p->last_fall = -1.0f;
    }
    p->width = (float)p->ph.width;
    p->height = (float)p->ph.height;
    p->acc = mmath_vec3(0.0f, 0.0f, 0.0f);
    p->water_contact = player_water_contact(w, p);
    /* Camera eye height eases toward the pose's eye height (Camera.tick: 0.5/tick). */
    p->eye_prev = p->eye_cur;
    p->eye_cur += ((float)p->ph.eye_height - p->eye_cur) * 0.5f;
    if (p->ph.landed_fall >= 0.0) {
        p->last_fall = (float)p->ph.landed_fall;
    }
    p->fall_peak = -1.0f;

    float hspeed = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
    float bob_target = 0.0f;
    if (p->grounded) {
        p->walk_phase += hspeed * VPHYS_TICK_SECONDS * 4.0f;
        if (hspeed > 0.5f) {
            bob_target = p->sneaking ? PLAYER_BOB_AMP_SNEAK : PLAYER_BOB_AMP_WALK;
        }
    } else if (p->water_contact > 0.3f && hspeed > 0.5f) {
        p->walk_phase += hspeed * VPHYS_TICK_SECONDS * 6.0f;
    }
    p->bob_amp += (bob_target - p->bob_amp) * 0.4f;
}

/* Smooth the render position/eye between the last two physics ticks.
 * extra_seconds is frame time not yet simulated (0 right after a step). */
void player_interpolate(Player *p, float extra_seconds)
{
    if (p == NULL) {
        return;
    }
    if (!p->ph_init) {
        p->render_pos = p->pos;
        return;
    }
    if (!isfinite(extra_seconds) || extra_seconds < 0.0f) {
        extra_seconds = 0.0f;
    }
    float a = (p->ph_accum + extra_seconds) / VPHYS_TICK_SECONDS;
    a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
    p->interp_alpha = a;
    p->render_pos = mmath_vec3((float)(p->ph.px + (p->ph.x - p->ph.px) * (double)a),
                               (float)(p->ph.py + (p->ph.y - p->ph.py) * (double)a),
                               (float)(p->ph.pz + (p->ph.z - p->ph.pz) * (double)a));
    p->eye_height = p->eye_prev + (p->eye_cur - p->eye_prev) * a;
}

/* Advance the player. Walking runs the vanilla 20 TPS movement (vphys);
 * creative flight keeps its direct 60 Hz velocity model.
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
    if (dt < 0.0f || !isfinite(dt)) {
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
    player_phys_import(p);
    p->ph_accum += dt;
    int ticks = 0;
    while (p->ph_accum >= VPHYS_TICK_SECONDS && ticks < PLAYER_MAX_STEPS) {
        player_phys_tick(p, in, w);
        p->ph_accum -= VPHYS_TICK_SECONDS;
        ticks++;
    }
    if (ticks == PLAYER_MAX_STEPS) {
        p->ph_accum = 0.0f; /* Spiral-of-death guard: drop excess time. */
    }
    player_interpolate(p, 0.0f);
}
