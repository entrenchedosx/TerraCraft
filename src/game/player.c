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
    p->walk_phase = 0.0f;
    p->bob_amp = 0.0f;
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

/* Dry step-up: walking into a single-block ledge while grounded climbs it
 * instead of grinding. The candidate position is up one PLUS the blocked
 * horizontal motion applied: only a real 1-high obstacle ahead accepts the
 * step (a tall wall stays solid at the raised level, so the player cannot
 * elevator up it). The full raised box must be free (headroom included).
 * Sneaking never steps (ledge safety); ascending jumps never step either.
 */
static bool player_try_step_up(Player *p, World *w, float dx, float dz)
{
    if (p->vel.y > 0.0f) {
        return false;
    }
    Vec3 saved = p->pos;
    p->pos.x = saved.x + dx;
    p->pos.z = saved.z + dz;
    p->pos.y = saved.y + 1.0f;
    Vec3 mn, mx;
    player_box(p, &mn, &mx);
    if (player_aabb_solid(w, mn, mx)) {
        p->pos = saved;
        return false;
    }
    p->vel.y = 0.0f;
    p->grounded = true;
    return true;
}

/* Clamber onto a shore while pushing against it from the water (only
 * when partially immersed: full submersion never steps). Requires
 * headroom at the raised level, like the mob step-up.
 */
static bool player_try_water_step_up(Player *p, World *w)
{
    Vec3 mn, mx;
    player_box(p, &mn, &mx);
    mn.y += 1.0f;
    mx.y += 1.0f;
    if (player_aabb_solid(w, mn, mx)) {
        return false;
    }
    p->pos.y += 1.0f;
    player_box(p, &mn, &mx);
    if (player_aabb_solid(w, mn, mx)) {
        p->pos.y -= 1.0f;
        return false;
    }
    p->vel.y = 0.0f;
    p->grounded = true;
    return true;
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
        } else {
            /* Embedded with no motion (spawn/teleport/fly-toggle inside a
             * block): eject along the smallest penetration instead of
             * sitting stuck with zeroed velocity forever. */
            float out_neg = floorf(mn.x) + 1.0f + hw + PLAYER_EPS - p->pos.x;
            float out_pos = floorf(mx.x) - hw - PLAYER_EPS - p->pos.x;
            p->pos.x += (fabsf(out_neg) < fabsf(out_pos)) ? out_neg : out_pos;
        }
        p->vel.x = 0.0f;
    } else if (axis == 2) {
        if (delta > 0.0f) {
            p->pos.z = floorf(mx.z) - hw - PLAYER_EPS;
        } else if (delta < 0.0f) {
            p->pos.z = floorf(mn.z) + 1.0f + hw + PLAYER_EPS;
        } else {
            float out_neg = floorf(mn.z) + 1.0f + hw + PLAYER_EPS - p->pos.z;
            float out_pos = floorf(mx.z) - hw - PLAYER_EPS - p->pos.z;
            p->pos.z += (fabsf(out_neg) < fabsf(out_pos)) ? out_neg : out_pos;
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

/* Support test for sneak edge protection (MC Entity.moveEntity clip):
 * true when any solid block overlaps the 1-block slab directly under the
 * footprint, so a ledge with no ground beneath reads as unsupported.
 * Standing exactly on a block top counts (epsilon shrink is max-side only).
 */
static bool player_has_support(const Player *p, const World *w)
{
    float hw = p->width * 0.5f;
    Vec3 mn = mmath_vec3(p->pos.x - hw, p->pos.y - 1.0f, p->pos.z - hw);
    Vec3 mx = mmath_vec3(p->pos.x + hw, p->pos.y, p->pos.z + hw);
    return player_aabb_solid(w, mn, mx);
}

/* Support test at an arbitrary feet position (for pre-move corner checks).
 * Uses the player's width; y is the feet Y. */
static bool player_has_support_at(const Player *p, const World *w, float x, float y, float z)
{
    if (p == NULL) {
        return false;
    }
    float hw = p->width * 0.5f;
    Vec3 mn = mmath_vec3(x - hw, y - 1.0f, z - hw);
    Vec3 mx = mmath_vec3(x + hw, y, z + hw);
    return player_aabb_solid(w, mn, mx);
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
    bool sneak_in = in ? in->sneak : false;
    bool sprint = in ? in->sprint : false;
    bool auto_jump = in ? in->auto_jump : false;
    const float dt = PLAYER_STEP_DT;

    /* Pose first (feet stay, head shrinks/grows): sneak forces the 1.5 /
     * 1.27 crouch; releasing sneak stands only with headroom, otherwise
     * the crouch persists (forced). Flying never crouches (sneak is
     * fly-down there). Camera rides eye_height via player_eye_pos. */
    if (p->flying) {
        p->sneaking = false;
        p->height = PLAYER_STAND_HEIGHT;
        p->eye_height = PLAYER_STAND_EYE;
    } else if (sneak_in) {
        p->sneaking = true;
        p->height = PLAYER_SNEAK_HEIGHT;
        p->eye_height = PLAYER_SNEAK_EYE;
    } else if (player_can_stand(p, w)) {
        p->sneaking = false;
        p->height = PLAYER_STAND_HEIGHT;
        p->eye_height = PLAYER_STAND_EYE;
    } else {
        p->sneaking = true;
        p->height = PLAYER_SNEAK_HEIGHT;
        p->eye_height = PLAYER_SNEAK_EYE;
    }
    bool sneak = p->sneaking;

    float water_contact = player_water_contact(w, p);

    p->sprinting = sprint && !sneak;

    if (p->flying) {
        /* Creative no-clip flight: direct velocity, no gravity/collision. */
        Vec3 wish = player_wish_dir(p->yaw, fwd, strafe);
        float vy = 0.0f;
        if (jump) {
            vy += 1.0f;
        }
        if (sneak_in) {
            vy -= 1.0f;
        }
        p->vel = mmath_vec3_add(mmath_vec3_scale(wish, p->fly_speed), mmath_vec3(0.0f, vy * p->fly_speed, 0.0f));
        p->acc = mmath_vec3(0.0f, 0.0f, 0.0f);
        p->pos = mmath_vec3_add(p->pos, mmath_vec3_scale(p->vel, dt));
        p->grounded = false;
        p->fall_peak = -1.0f; /* Flight never counts as falling. */
        p->last_fall = -1.0f;
        p->bob_amp += (0.0f - p->bob_amp) * PLAYER_BOB_LERP;
        return;
    }

    /* Walk mode: horizontal velocity follows input; sneak is 0.3x (MC:
     * 1.31 m/s vs 4.317 walk). */
    float speed = sprint && !sneak ? p->sprint_speed : p->walk_speed;
    if (sneak) {
        speed *= 0.3f;
    }
    if (water_contact > 0.0f) {
        speed *= 1.0f - 0.45f * water_contact;
    }
    Vec3 wish = player_wish_dir(p->yaw, fwd, strafe);
    if (mmath_vec3_length_sq(wish) > 1e-8f) {
        p->vel.x = wish.x * speed;
        p->vel.z = wish.z * speed;
    } else {
        /* No input: decay horizontal velocity (MC-style per-step retention). */
        float dry_drag = p->grounded ? p->drag_ground : p->drag_air;
        float d = dry_drag + (0.80f - dry_drag) * water_contact;
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
    float gravity = p->gravity * (1.0f - 0.88f * water_contact);
    p->acc = mmath_vec3(0.0f, -gravity, 0.0f);
    p->vel.y -= gravity * dt;
    if (p->vel.y < PLAYER_TERMINAL_VEL) {
        p->vel.y = PLAYER_TERMINAL_VEL;
    }
    if (jump && p->grounded) {
        p->vel.y = p->jump_vel;
        p->grounded = false;
    }
    if (water_contact > 0.0f) {
        if (jump) {
            p->vel.y += (2.4f - p->vel.y) * water_contact;
        } else if (sneak) {
            p->vel.y += (-2.4f - p->vel.y) * water_contact;
        } else {
            p->vel.y *= 1.0f - 0.20f * water_contact;
        }
        p->vel.x *= 1.0f - 0.08f * water_contact;
        p->vel.z *= 1.0f - 0.08f * water_contact;
    }

    if (w == NULL) {
        p->pos = mmath_vec3_add(p->pos, mmath_vec3_scale(p->vel, dt));
        return;
    }

    /* Axis-separated move + collide: X, then Z, then Y. */
    bool was_grounded = p->grounded;
    p->grounded = false;
    float dx = p->vel.x * dt;
    float dz = p->vel.z * dt;
    float dy = p->vel.y * dt;
    float pre_x = p->pos.x;
    float pre_z = p->pos.z;
    /* Diagonal corner-cut guard (MC Entity.moveEntity third loop): when
     * both single-axis destinations are supported but the combined
     * diagonal hangs over the void (outer L-corner), cancel both so the
     * corner needs axis-aligned steps. Single-axis slides (narrow ledge
     * along X, sideways Z unsupported) still pass through to the per-axis
     * clips below. Tested at intended positions; walls do not count as
     * support (slab is y-1..y only). */
    if (sneak && was_grounded && dx != 0.0f && dz != 0.0f) {
        bool x_only = player_has_support_at(p, w, pre_x + dx, p->pos.y, pre_z);
        bool z_only = player_has_support_at(p, w, pre_x, p->pos.y, pre_z + dz);
        bool both = player_has_support_at(p, w, pre_x + dx, p->pos.y, pre_z + dz);
        if (x_only && z_only && !both) {
            dx = 0.0f;
            dz = 0.0f;
            p->vel.x = 0.0f;
            p->vel.z = 0.0f;
        }
    }
    p->pos.x += dx;
    player_resolve_axis(p, w, 0, dx, false);
    /* Sneak edge protection (MC Entity.moveEntity clip: grounded +
     * sneaking clips the move where the 1-below slab has no support,
     * per axis). Stairs still descend: the lower step is inside the
     * 1-below slab, so it counts. */
    if (sneak && was_grounded && dx != 0.0f && !player_has_support(p, w)) {
        p->pos.x = pre_x;
        p->vel.x = 0.0f;
    }
    p->pos.z += dz;
    player_resolve_axis(p, w, 2, dz, false);
    if (sneak && was_grounded && dz != 0.0f && !player_has_support(p, w)) {
        p->pos.z = pre_z;
        p->vel.z = 0.0f;
    }
    /* Dry step-up: grounded, pushing a ledge, barely moving -> climb one
     * block instead of grinding. Sneak never steps up (ledge safety).
     * Gated on the autoJump setting (MC default off). */
    if (auto_jump && water_contact <= 0.0f && !sneak && mmath_vec3_length_sq(wish) > 1e-8f) {
        float moved = fabsf(p->pos.x - pre_x) + fabsf(p->pos.z - pre_z);
        float wanted = fabsf(dx) + fabsf(dz);
        if (wanted > 1e-6f && moved < wanted * 0.25f) {
            player_try_step_up(p, w, dx, dz);
        }
    }
    /* Shore clamber: swimming into a bank while partially immersed steps
     * up one block instead of grinding against it. Gated on stalled
     * horizontal motion with live input so open-water swimming never
     * climbs. Also gated on autoJump, like the dry step-up. */
    if (auto_jump && water_contact > 0.15f && water_contact < 0.75f &&
        mmath_vec3_length_sq(wish) > 1e-8f) {
        float hs = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
        if (hs < 0.05f) {
            player_try_water_step_up(p, w);
        }
    }
    p->pos.y += dy;
    player_resolve_axis(p, w, 1, dy, dy <= 0.0f);

    /* Fall tracking: peak while airborne, distance sampled on landing. */
    float hspeed = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
    float bob_target = 0.0f;
    if (p->grounded) {
        if (p->fall_peak >= 0.0f) {
            p->last_fall = p->fall_peak - p->pos.y;
            if (p->last_fall < 0.0f) {
                p->last_fall = 0.0f;
            }
        }
        p->fall_peak = -1.0f;
        /* Stride phase for third-person limb swing (same cadence as mobs:
         * distance-paced, so the swing matches ground speed at any rate). */
        p->walk_phase += hspeed * dt * 4.0f;
        /* View-bob amplitude chases locomotion (Java cameraYaw): full
         * while striding on ground, calmer while sneaking, silent still. */
        if (hspeed > 0.5f) {
            bob_target = sneak ? PLAYER_BOB_AMP_SNEAK : PLAYER_BOB_AMP_WALK;
        }
    } else {
        if (p->fall_peak < p->pos.y) {
            p->fall_peak = p->pos.y;
        }
        /* Swimming strokes pace off the same distance clock so the body
         * animates while stroking through water. */
        if (water_contact > 0.3f && hspeed > 0.5f) {
            p->walk_phase += hspeed * dt * 6.0f;
        }
    }
    p->bob_amp += (bob_target - p->bob_amp) * PLAYER_BOB_LERP;
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
