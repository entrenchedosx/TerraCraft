#include "game/player_animation.h"

#include <math.h>

float player_swing_phase(float elapsed_seconds)
{
    if (!isfinite(elapsed_seconds) || elapsed_seconds <= 0.0f) {
        return 0.0f;
    }
    float phase = elapsed_seconds / PLAYER_SWING_DURATION;
    return phase < 1.0f ? phase : 1.0f;
}

float player_swing_weight(float phase)
{
    if (!isfinite(phase) || phase <= 0.0f || phase >= 1.0f) {
        return 0.0f;
    }
    return sinf(phase * 3.14159265358979323846f);
}

void player_anim_init(PlayerAnim *a)
{
    if (a == NULL) {
        return;
    }
    a->state = PLAYER_ANIM_IDLE;
    a->t = 0.0f;
    a->stride = 0.0f;
    a->land_mag = 0.0f;
    a->sneak = false;
    a->ev_attack = false;
    a->ev_hurt = false;
    a->ev_land = false;
    a->ev_land_dist = 0.0f;
}

void player_anim_notify_attacked(PlayerAnim *a)
{
    if (a == NULL) {
        return;
    }
    a->ev_attack = true;
}

void player_anim_notify_hurt(PlayerAnim *a)
{
    if (a == NULL) {
        return;
    }
    a->ev_hurt = true;
}

void player_anim_notify_landed(PlayerAnim *a, float dist)
{
    if (a == NULL) {
        return;
    }
    a->ev_land = true;
    a->ev_land_dist = isfinite(dist) ? dist : 0.0f;
}

/* Base locomotion state for a steady input (no one-shot running). */
static PlayerAnimState player_anim_base(const PlayerAnimInput *in)
{
    if (!in->grounded) {
        return PLAYER_ANIM_AIR;
    }
    if (in->use_hold) {
        return PLAYER_ANIM_USE;
    }
    if (!in->moving) {
        return PLAYER_ANIM_IDLE;
    }
    return in->sprinting ? PLAYER_ANIM_SPRINT : PLAYER_ANIM_WALK;
}

void player_anim_update(PlayerAnim *a, const PlayerAnimInput *in, float dt)
{
    if (a == NULL || in == NULL) {
        return;
    }
    float step = isfinite(dt) ? dt : 0.0f;
    if (step < 0.0f) {
        step = 0.0f;
    }
    if (step > 0.25f) {
        step = 0.25f;
    }
    a->sneak = in->sneaking;
    /* Priority: HURT > ATTACK > LAND > held/base. A fresh latch
     * restarts its one-shot even mid-pose (interruptible). */
    if (a->ev_hurt) {
        a->state = PLAYER_ANIM_HURT;
        a->t = 0.0f;
    } else if (a->ev_attack) {
        a->state = PLAYER_ANIM_ATTACK;
        a->t = 0.0f;
    } else if (a->ev_land) {
        if (a->ev_land_dist > PLAYER_ANIM_LAND_MIN_DIST) {
            a->state = PLAYER_ANIM_LAND;
            a->t = 0.0f;
            float mag = a->ev_land_dist / PLAYER_ANIM_LAND_FULL_DIST;
            a->land_mag = mag < 1.0f ? mag : 1.0f;
        } else {
            a->state = player_anim_base(in);
            a->t = 0.0f;
        }
    }
    a->ev_attack = false;
    a->ev_hurt = false;
    a->ev_land = false;
    a->ev_land_dist = 0.0f;
    /* Advance one-shot timers; expiry falls through to the base state
     * for the live input (interrupted only by the next latch). */
    bool one_shot = a->state == PLAYER_ANIM_LAND || a->state == PLAYER_ANIM_ATTACK ||
                    a->state == PLAYER_ANIM_HURT;
    if (one_shot) {
        a->t += step;
        float dur = PLAYER_ANIM_LAND_DURATION;
        if (a->state == PLAYER_ANIM_ATTACK) {
            dur = PLAYER_SWING_DURATION;
        } else if (a->state == PLAYER_ANIM_HURT) {
            dur = PLAYER_ANIM_HURT_DURATION;
        }
        if (a->t >= dur) {
            a->state = player_anim_base(in);
            a->t = 0.0f;
        }
    } else {
        PlayerAnimState base = player_anim_base(in);
        if (base != a->state) {
            a->state = base;
            a->t = 0.0f;
        }
    }
    /* Stride advances only underfoot (walk/sprint, calmer while sneak). */
    if (a->state == PLAYER_ANIM_WALK || a->state == PLAYER_ANIM_SPRINT) {
        float rate = a->state == PLAYER_ANIM_SPRINT ? PLAYER_ANIM_STRIDE_SPRINT : PLAYER_ANIM_STRIDE_WALK;
        a->stride += rate * step;
    }
}

PlayerAnimPose player_anim_pose(const PlayerAnim *a)
{
    PlayerAnimPose p = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    if (a == NULL) {
        return p;
    }
    switch (a->state) {
    case PLAYER_ANIM_WALK:
    case PLAYER_ANIM_SPRINT: {
        float amp = a->state == PLAYER_ANIM_SPRINT ? PLAYER_ANIM_BOB_SPRINT : PLAYER_ANIM_BOB_WALK;
        if (a->sneak) {
            amp *= 0.5f;
        }
        p.bob_x = amp * sinf(a->stride);
        float bounce = 0.5f - 0.5f * cosf(2.0f * a->stride);
        p.bob_y = amp * 0.6f * bounce;
        if (a->sneak) {
            p.dip += 0.025f;
        }
        break;
    }
    case PLAYER_ANIM_LAND: {
        float phase = a->t / PLAYER_ANIM_LAND_DURATION;
        if (phase > 1.0f) {
            phase = 1.0f;
        }
        p.dip = PLAYER_ANIM_LAND_MAX_DIP * a->land_mag * sinf(phase * 3.14159265358979323846f);
        break;
    }
    case PLAYER_ANIM_ATTACK:
        p.punch = player_swing_weight(player_swing_phase(a->t));
        break;
    case PLAYER_ANIM_USE:
        p.raise = 0.10f;
        break;
    case PLAYER_ANIM_HURT: {
        float phase = a->t / PLAYER_ANIM_HURT_DURATION;
        if (phase > 1.0f) {
            phase = 1.0f;
        }
        /* Flinch lifts the hand (negative dip = upward jerk). */
        p.dip = -0.03f * sinf(phase * 3.14159265358979323846f);
        break;
    }
    case PLAYER_ANIM_AIR:
    case PLAYER_ANIM_IDLE:
    default:
        break;
    }
    if (a->sneak && a->state != PLAYER_ANIM_WALK && a->state != PLAYER_ANIM_SPRINT) {
        p.dip += 0.025f;
    }
    return p;
}
