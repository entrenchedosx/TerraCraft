#include "game/pvp.h"

#include <math.h>

bool pvp_ray_hits_player(Vec3 eye, Vec3 dir, float max_dist, Vec3 target_feet, float target_width,
                         float target_height, float *out_dist)
{
    if (out_dist != NULL) {
        *out_dist = 0.0f;
    }
    if (!(max_dist > 0.0f) || !(target_width > 0.0f) || !(target_height > 0.0f)) {
        return false;
    }
    if (!(isfinite(eye.x) && isfinite(eye.y) && isfinite(eye.z) && isfinite(dir.x) && isfinite(dir.y) &&
          isfinite(dir.z) && isfinite(target_feet.x) && isfinite(target_feet.y) &&
          isfinite(target_feet.z))) {
        return false;
    }
    float len2 = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
    if (!(len2 > 1e-12f)) {
        return false;
    }
    float inv_len = 1.0f / sqrtf(len2);
    Vec3 d = mmath_vec3(dir.x * inv_len, dir.y * inv_len, dir.z * inv_len);

    float hw = target_width * 0.5f;
    Vec3 mn = mmath_vec3(target_feet.x - hw, target_feet.y, target_feet.z - hw);
    Vec3 mx = mmath_vec3(target_feet.x + hw, target_feet.y + target_height, target_feet.z + hw);

    /* Inside-start reads as an instant hit (matches mob_raycast). */
    if (eye.x >= mn.x && eye.x <= mx.x && eye.y >= mn.y && eye.y <= mx.y && eye.z >= mn.z &&
        eye.z <= mx.z) {
        if (out_dist != NULL) {
            *out_dist = 0.0f;
        }
        return true;
    }

    float tmin = 0.0f;
    float tmax = max_dist;
    const float o[3] = {eye.x, eye.y, eye.z};
    const float dd[3] = {d.x, d.y, d.z};
    const float lo[3] = {mn.x, mn.y, mn.z};
    const float hi[3] = {mx.x, mx.y, mx.z};
    for (int i = 0; i < 3; ++i) {
        if (fabsf(dd[i]) < 1e-9f) {
            if (o[i] < lo[i] || o[i] > hi[i]) {
                return false;
            }
        } else {
            float inv = 1.0f / dd[i];
            float t0 = (lo[i] - o[i]) * inv;
            float t1 = (hi[i] - o[i]) * inv;
            if (t0 > t1) {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            if (t0 > tmin) {
                tmin = t0;
            }
            if (t1 < tmax) {
                tmax = t1;
            }
            if (tmin > tmax) {
                return false;
            }
        }
    }
    if (tmin < 0.0f || tmin > max_dist) {
        return false;
    }
    if (out_dist != NULL) {
        *out_dist = tmin;
    }
    return true;
}

float pvp_clamp_damage(float damage)
{
    if (!(damage > 0.0f)) {
        return 0.0f;
    }
    if (damage > 20.0f) {
        return 20.0f;
    }
    return damage;
}

float pvp_clamp_arrow_damage(float damage)
{
    /* Bow damage range mirrors SURVIVAL_BOW_DMG_MAX (game/survival.h):
     * kept literal here so game/pvp.h stays light (no item dependency). */
    if (!(damage > 0.0f)) {
        return 0.0f;
    }
    if (damage > 6.0f) {
        return 6.0f;
    }
    return damage;
}

float pvp_lerp_angle(float from, float to, float t)
{
    if (!(t > 0.0f)) {
        return from;
    }
    if (!(t < 1.0f)) {
        return to;
    }
    float d = to - from;
    const float pi = 3.14159265358979323846f;
    while (d > pi) {
        d -= 2.0f * pi;
    }
    while (d < -pi) {
        d += 2.0f * pi;
    }
    float out = from + d * t;
    while (out > pi) {
        out -= 2.0f * pi;
    }
    while (out < -pi) {
        out += 2.0f * pi;
    }
    return out;
}

bool pvp_seq_accept(uint32_t seq, uint32_t *last_seq)
{
    if (last_seq == NULL) {
        return seq != 0;
    }
    /* Sequence 0 is never sent (the counter skips it on wrap): it marks
     * the initial empty state, so only seq >= 1 is acceptable, strictly
     * increasing (retransmits/duplicates rejected). Epoch rollover past
     * UINT32_MAX is owned by the caller (reset to 0). */
    if (seq == 0 || seq <= *last_seq) {
        return false;
    }
    *last_seq = seq;
    return true;
}

bool pvp_validate_strike(bool pvp_enabled, bool attacker_alive, bool victim_alive, uint32_t attacker_id,
                         uint32_t victim_id, float damage, float distance, uint32_t seq,
                         uint32_t *last_seq)
{
    if (!pvp_enabled) {
        return false;
    }
    if (!attacker_alive || !victim_alive) {
        return false;
    }
    if (attacker_id == victim_id) {
        return false;
    }
    if (!(damage > 0.0f) || !(distance >= 0.0f) || !(distance <= PVP_REACH + 0.5f)) {
        return false;
    }
    return pvp_seq_accept(seq, last_seq);
}
