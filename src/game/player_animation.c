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
    a->idle_phase = 0.0f;
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
    if (in->use_hold) {
        return PLAYER_ANIM_USE;
    }
    if (in->water > 0.3f) {
        return PLAYER_ANIM_SWIM;
    }
    if (!in->grounded) {
        return PLAYER_ANIM_AIR;
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
    a->idle_phase += step * PLAYER_ANIM_BREATHE_RATE;
    if (a->idle_phase > 6.2831853f) {
        a->idle_phase -= 6.2831853f;
    }
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
    /* Stride advances underfoot (walk/sprint) and through swim strokes. */
    if (a->state == PLAYER_ANIM_WALK || a->state == PLAYER_ANIM_SPRINT ||
        a->state == PLAYER_ANIM_SWIM) {
        float rate = PLAYER_ANIM_STRIDE_WALK;
        if (a->state == PLAYER_ANIM_SPRINT) {
            rate = PLAYER_ANIM_STRIDE_SPRINT;
        } else if (a->state == PLAYER_ANIM_SWIM) {
            rate = PLAYER_ANIM_STRIDE_SWIM;
        }
        a->stride += rate * step;
    }
}

PlayerAnimPose player_anim_pose(const PlayerAnim *a)
{
    PlayerAnimPose p = {0};
    if (a == NULL) {
        return p;
    }
    switch (a->state) {
    case PLAYER_ANIM_WALK:
    case PLAYER_ANIM_SPRINT: {
        float amp = a->state == PLAYER_ANIM_SPRINT ? PLAYER_ANIM_BOB_SPRINT : PLAYER_ANIM_BOB_WALK;
        float joint_amp = a->state == PLAYER_ANIM_SPRINT ? 0.22f : 0.15f;
        if (a->sneak) {
            amp *= 0.5f;
            joint_amp *= 0.5f;
        }
        p.bob_x = amp * sinf(a->stride);
        float bounce = 0.5f - 0.5f * cosf(2.0f * a->stride);
        p.bob_y = amp * 0.6f * bounce;
        /* The arm counter-swings with each step; rotation makes locomotion
         * visible even when the small camera bob is hard to notice. */
        p.arm_roll = joint_amp * sinf(a->stride);
        p.arm_pitch = joint_amp * 0.72f * cosf(a->stride * 2.0f);
        p.arm_yaw = joint_amp * 0.42f * sinf(a->stride * 0.5f);
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
        p.arm_pitch = 0.16f * a->land_mag * sinf(phase * 3.14159265358979323846f);
        p.arm_roll = -0.06f * a->land_mag * sinf(phase * 3.14159265358979323846f);
        break;
    }
    case PLAYER_ANIM_ATTACK: {
        float phase = player_swing_phase(a->t);
        p.punch = player_swing_weight(phase);
        /* Quick diagonal jab: roll in, yaw across, small forward pitch. */
        float wind = sinf(phase * 6.2831853f);
        p.arm_roll = -0.24f * p.punch + 0.05f * wind;
        p.arm_yaw = 0.16f * p.punch;
        p.arm_pitch = -0.08f * p.punch;
        break;
    }
    case PLAYER_ANIM_USE: {
        p.raise = PLAYER_ANIM_USE_RAISE;
        p.arm_pitch = -0.45f + 0.03f * sinf(a->idle_phase * 2.4f);
        p.arm_roll = 0.06f;
        break;
    }
    case PLAYER_ANIM_HURT: {
        float phase = a->t / PLAYER_ANIM_HURT_DURATION;
        if (phase > 1.0f) {
            phase = 1.0f;
        }
        /* Flinch lifts the hand (negative dip = upward jerk). */
        p.dip = -0.03f * sinf(phase * 3.14159265358979323846f);
        p.arm_roll = 0.12f * sinf(phase * 3.14159265358979323846f);
        p.arm_pitch = -0.10f * sinf(phase * 3.14159265358979323846f);
        break;
    }
    case PLAYER_ANIM_SWIM: {
        /* Freestyle stroke: reach forward, sweep down, recover. */
        float stroke = sinf(a->stride);
        float lift = 0.5f - 0.5f * cosf(2.0f * a->stride);
        p.bob_x = 0.014f * stroke;
        p.bob_y = 0.006f * lift;
        p.dip = 0.035f;
        p.arm_pitch = -0.55f + 0.55f * stroke;
        p.arm_roll = 0.20f * stroke;
        p.arm_yaw = 0.10f * cosf(a->stride);
        break;
    }
    case PLAYER_ANIM_AIR:
        /* Airborne hand floats up a touch and drifts with the body. */
        p.bob_y = 0.010f + 0.004f * sinf(a->idle_phase * 0.9f);
        p.arm_pitch = -0.12f;
        p.arm_roll = 0.05f * sinf(a->idle_phase);
        break;
    case PLAYER_ANIM_IDLE:
        /* Slow breathing keeps the hand alive without distracting sway. */
        p.bob_y = PLAYER_ANIM_BREATHE_BOB * (0.5f - 0.5f * cosf(a->idle_phase));
        p.arm_pitch = 0.020f * sinf(a->idle_phase);
        p.arm_roll = 0.012f * sinf(a->idle_phase * 0.8f);
        break;
    default:
        break;
    }
    if (a->sneak && a->state != PLAYER_ANIM_WALK && a->state != PLAYER_ANIM_SPRINT) {
        p.dip += 0.025f;
    }
    return p;
}
