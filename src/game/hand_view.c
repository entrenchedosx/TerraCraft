#include "game/hand_view.h"

#include <math.h>
#include <string.h>

/* ---- Mth: the 65536-entry sine table used by Java Edition ---------------
 * Mth.sin(d) = SIN[(long)(d * 10430.378350470453) & 65535]; cos adds 16384.
 * Reproduced so first-person angles quantise exactly like the reference. */
static float g_sin_table[65536];
static bool g_sin_ready = false;

static void mth_init(void)
{
    if (g_sin_ready) {
        return;
    }
    for (int i = 0; i < 65536; ++i) {
        g_sin_table[i] = (float)sin((double)i * 3.14159265358979323846 * 2.0 / 65536.0);
    }
    g_sin_ready = true;
}

static float mth_sin(float x)
{
    mth_init();
    long long i = (long long)((double)x * 10430.378350470453);
    return g_sin_table[(int)(i & 65535)];
}

static float mth_cos(float x)
{
    mth_init();
    long long i = (long long)((double)x * 10430.378350470453 + 16384.0);
    return g_sin_table[(int)(i & 65535)];
}

static float mth_sqrt(float x)
{
    return (float)sqrt((double)x);
}

static float mth_lerp(float t, float a, float b)
{
    return a + t * (b - a);
}

static float mth_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

#define HAND_PI 3.1415927f /* (float)Math.PI as it appears in the reference. */

/* ---- Matrix stack primitives (PoseStack semantics) ---------------------- */

Mat4 hand_mat_translate(Mat4 m, float x, float y, float z)
{
    Mat4 t = mmath_mat4_identity();
    t.m[12] = x;
    t.m[13] = y;
    t.m[14] = z;
    return mmath_mat4_mul(m, t);
}

Mat4 hand_mat_rotate_deg(Mat4 m, char axis, float degrees)
{
    /* Axis.rotationDegrees: radians = degrees * 0.017453292f (float). */
    double a = (double)(degrees * 0.017453292f);
    float c = (float)cos(a);
    float s = (float)sin(a);
    Mat4 r = mmath_mat4_identity();
    if (axis == 'x' || axis == 'X') {
        r.m[5] = c;  r.m[9] = -s;
        r.m[6] = s;  r.m[10] = c;
    } else if (axis == 'y' || axis == 'Y') {
        r.m[0] = c;  r.m[8] = s;
        r.m[2] = -s; r.m[10] = c;
    } else {
        r.m[0] = c;  r.m[4] = -s;
        r.m[1] = s;  r.m[5] = c;
    }
    return mmath_mat4_mul(m, r);
}

Mat4 hand_mat_scale(Mat4 m, float x, float y, float z)
{
    Mat4 s = mmath_mat4_identity();
    s.m[0] = x;
    s.m[5] = y;
    s.m[10] = z;
    return mmath_mat4_mul(m, s);
}

Vec3 hand_mat_point(const Mat4 *m, Vec3 p)
{
    return mmath_vec3(m->m[0] * p.x + m->m[4] * p.y + m->m[8] * p.z + m->m[12],
                      m->m[1] * p.x + m->m[5] * p.y + m->m[9] * p.z + m->m[13],
                      m->m[2] * p.x + m->m[6] * p.y + m->m[10] * p.z + m->m[14]);
}

Vec3 hand_mat_dir(const Mat4 *m, Vec3 d)
{
    return mmath_vec3(m->m[0] * d.x + m->m[4] * d.y + m->m[8] * d.z,
                      m->m[1] * d.x + m->m[5] * d.y + m->m[9] * d.z,
                      m->m[2] * d.x + m->m[6] * d.y + m->m[10] * d.z);
}

/* ---- Tick state --------------------------------------------------------- */

void hand_view_init(HandView *hv)
{
    if (hv == NULL) {
        return;
    }
    memset(hv, 0, sizeof(*hv));
    hv->shown_key = -1;
}

static float wrap_deg_180(float d)
{
    d = fmodf(d, 360.0f);
    if (d > 180.0f) {
        d -= 360.0f;
    } else if (d < -180.0f) {
        d += 360.0f;
    }
    return d;
}

static bool finite_or_zero(float v, float *out)
{
    if (isfinite(v)) {
        *out = v;
        return true;
    }
    *out = 0.0f;
    return false;
}

bool hand_view_swing(HandView *hv)
{
    if (hv == NULL) {
        return false;
    }
    if (!hv->swinging || hv->swing_time >= HAND_SWING_DURATION_TICKS / 2 || hv->swing_time < 0) {
        hv->swing_time = -1;
        hv->swinging = true;
        return true;
    }
    return false;
}

void hand_view_reset_attack_strength(HandView *hv)
{
    if (hv != NULL) {
        hv->attack_ticker = 0.0f;
    }
}

float hand_view_partial(const HandView *hv)
{
    if (hv == NULL) {
        return 0.0f;
    }
    return mth_clamp(hv->accum / HAND_TICK_SECONDS, 0.0f, 1.0f);
}

float hand_view_partial_ahead(const HandView *hv, float extra_seconds)
{
    if (hv == NULL) {
        return 0.0f;
    }
    if (!isfinite(extra_seconds) || extra_seconds < 0.0f) {
        extra_seconds = 0.0f;
    }
    return mth_clamp((hv->accum + extra_seconds) / HAND_TICK_SECONDS, 0.0f, 1.0f);
}

float hand_view_swing_progress(const HandView *hv, float partial)
{
    if (hv == NULL) {
        return 0.0f;
    }
    float f = hv->attack_anim - hv->o_attack_anim;
    if (f < 0.0f) {
        f += 1.0f;
    }
    return hv->o_attack_anim + f * partial;
}

static float current_delay(const HandView *hv)
{
    float delay = hv->have_sample ? hv->last.attack_delay_ticks : 5.0f;
    return delay >= 1.0f ? delay : 1.0f;
}

float hand_view_attack_strength(const HandView *hv, float partial)
{
    if (hv == NULL) {
        return 1.0f;
    }
    return mth_clamp((hv->attack_ticker + partial) / current_delay(hv), 0.0f, 1.0f);
}

float hand_view_swap_scale(const HandView *hv, float partial)
{
    if (hv == NULL) {
        return 1.0f;
    }
    return mth_clamp((hv->swap_ticker + partial) / current_delay(hv), 0.0f, 1.0f);
}

void hand_view_reset_swap(HandView *hv)
{
    if (hv != NULL) {
        hv->attack_ticker = 0.0f;
        hv->swap_ticker = 0.0f;
    }
}

/* One vanilla game tick (20 Hz). Minecraft.tick runs gameRenderer.tick()
 * (ItemInHandRenderer.tick) before the level ticks entities, so the equip
 * step reads itemSwapTicker before this tick's Player.tick increments it; the entity updates (swing, walkDist, bob) follow. */
static void hand_view_tick(HandView *hv)
{
    const HandViewSample *s = &hv->last;

    hv->o_attack_anim = hv->attack_anim;
    hv->walk_dist_o = hv->walk_dist;
    hv->o_bob = hv->bob;
    hv->x_bob_o = hv->x_bob;
    hv->y_bob_o = hv->y_bob;
    hv->o_main_hand_height = hv->main_hand_height;

    /* ItemInHandRenderer.tick. */
    if (hv->shown_key < 0) {
        hv->shown_key = s->held_key;
    }
    if (s->hands_busy) {
        hv->main_hand_height = mth_clamp(hv->main_hand_height - 0.4f, 0.0f, 1.0f);
    } else {
        float f = hand_view_swap_scale(hv, 1.0f);
        float f1 = (hv->shown_key != s->held_key) ? 0.0f : f * f * f;
        hv->main_hand_height += mth_clamp(f1 - hv->main_hand_height, -0.4f, 0.4f);
    }
    if (hv->main_hand_height < 0.1f) {
        hv->shown_key = s->held_key;
    }

    /* LivingEntity.updateSwingTime. */
    if (hv->swinging) {
        hv->swing_time++;
        if (hv->swing_time >= HAND_SWING_DURATION_TICKS) {
            hv->swing_time = 0;
            hv->swinging = false;
        }
    } else {
        hv->swing_time = 0;
    }
    hv->attack_anim = (float)hv->swing_time / (float)HAND_SWING_DURATION_TICKS;

    /* Entity.move: walkDist += horizontal movement * 0.6. */
    float dist = hv->pending_dist;
    hv->pending_dist = 0.0f;
    hv->walk_dist += dist * 0.6f;

    /* Player.aiStep: bob follows ground speed only while walking on ground. */
    float target = 0.0f;
    if (s->on_ground && !s->dead && !s->swimming) {
        target = dist < 0.1f ? dist : 0.1f;
    }
    hv->bob += (target - hv->bob) * 0.4f;

    /* LocalPlayer.aiStep: sway chases the view rotation by half each tick. */
    float yaw = hv->yaw_unwrapped;
    hv->x_bob += (s->pitch_deg - hv->x_bob) * 0.5f;
    hv->y_bob += (yaw - hv->y_bob) * 0.5f;

    /* Player.tick: both tickers advance, then a changed main-hand item type
     * restarts them (resetAttackStrengthTicker). */
    hv->attack_ticker += 1.0f;
    hv->swap_ticker += 1.0f;
    if (hv->have_last_held && hv->last_held_key != s->held_key) {
        hv->attack_ticker = 0.0f;
        hv->swap_ticker = 0.0f;
    }
    hv->last_held_key = s->held_key;
    hv->have_last_held = true;

}

void hand_view_advance(HandView *hv, float dt, const HandViewSample *sample)
{
    if (hv == NULL) {
        return;
    }
    if (!isfinite(dt) || dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.25f) {
        dt = 0.25f;
    }
    if (sample != NULL) {
        HandViewSample s = *sample;
        float tmp;
        finite_or_zero(s.yaw_deg, &tmp);
        s.yaw_deg = tmp;
        finite_or_zero(s.pitch_deg, &tmp);
        s.pitch_deg = tmp;
        finite_or_zero(s.step_dist, &tmp);
        s.step_dist = tmp < 0.0f ? 0.0f : tmp;
        if (!hv->have_rot) {
            hv->yaw_unwrapped = s.yaw_deg;
            hv->x_bob = hv->x_bob_o = s.pitch_deg;
            hv->y_bob = hv->y_bob_o = s.yaw_deg;
            hv->have_rot = true;
        } else {
            hv->yaw_unwrapped += wrap_deg_180(s.yaw_deg - hv->yaw_unwrapped);
        }
        hv->last = s;
        hv->have_sample = true;
        hv->pending_dist += s.step_dist;
    }
    if (!hv->have_sample) {
        return;
    }
    if (hv->shown_key < 0) {
        hv->shown_key = hv->last.held_key;
    }
    hv->accum += dt;
    while (hv->accum >= HAND_TICK_SECONDS) {
        hv->accum -= HAND_TICK_SECONDS;
        hand_view_tick(hv);
    }
}

/* ---- Presentation ------------------------------------------------------- */

Mat4 hand_view_bob_matrix(const HandView *hv, float partial, bool enabled)
{
    Mat4 m = mmath_mat4_identity();
    if (hv == NULL || !enabled) {
        return m;
    }
    float f = hv->walk_dist - hv->walk_dist_o;
    float f1 = -(hv->walk_dist + f * partial);
    float f2 = mth_lerp(partial, hv->o_bob, hv->bob);
    float s = mth_sin(f1 * HAND_PI);
    float c = (float)fabs((double)(mth_cos(f1 * HAND_PI) * f2));
    m = hand_mat_translate(m, s * f2 * 0.5f, -c, 0.0f);
    m = hand_mat_rotate_deg(m, 'z', s * f2 * 3.0f);
    float c2 = mth_cos(f1 * HAND_PI - 0.2f);
    m = hand_mat_rotate_deg(m, 'x', (float)fabs((double)(c2 * f2)) * 5.0f);
    return m;
}

/* ItemInHandRenderer.applyItemArmTransform (right arm). */
static Mat4 apply_item_arm_transform(Mat4 m, float equipped)
{
    return hand_mat_translate(m, 0.56f, -0.52f + equipped * -0.6f, -0.72f);
}

/* ItemInHandRenderer.applyItemArmAttackTransform (right arm). */
static Mat4 apply_item_arm_attack_transform(Mat4 m, float swing)
{
    float f = mth_sin(swing * swing * HAND_PI);
    m = hand_mat_rotate_deg(m, 'y', 45.0f + f * -20.0f);
    float f1 = mth_sin(mth_sqrt(swing) * HAND_PI);
    m = hand_mat_rotate_deg(m, 'z', f1 * -20.0f);
    m = hand_mat_rotate_deg(m, 'x', f1 * -80.0f);
    m = hand_mat_rotate_deg(m, 'y', -45.0f);
    return m;
}

/* ItemInHandRenderer.applyEatTransform (right arm). */
static Mat4 apply_eat_transform(Mat4 m, float partial, const HandViewUse *use)
{
    float f = use->remaining_ticks - partial + 1.0f;
    float dur = use->duration_ticks > 1.0f ? use->duration_ticks : 1.0f;
    float f1 = f / dur;
    if (f1 < 0.8f) {
        float f2 = (float)fabs((double)(mth_cos(f / 4.0f * HAND_PI) * 0.1f));
        m = hand_mat_translate(m, 0.0f, f2, 0.0f);
    }
    float f3 = 1.0f - (float)pow((double)f1, 27.0);
    m = hand_mat_translate(m, f3 * 0.6f, f3 * -0.5f, f3 * 0.0f);
    m = hand_mat_rotate_deg(m, 'y', f3 * 90.0f);
    m = hand_mat_rotate_deg(m, 'x', f3 * 10.0f);
    m = hand_mat_rotate_deg(m, 'z', f3 * 30.0f);
    return m;
}

/* The bow-draw branch of submitArmWithItem (right arm, j = 1). */
static Mat4 apply_bow_transform(Mat4 m, float partial, const HandViewUse *use, float equipped)
{
    m = apply_item_arm_transform(m, equipped);
    m = hand_mat_translate(m, -0.2785682f, 0.18344387f, 0.15731531f);
    m = hand_mat_rotate_deg(m, 'x', -13.935f);
    m = hand_mat_rotate_deg(m, 'y', 35.3f);
    m = hand_mat_rotate_deg(m, 'z', -9.785f);
    float f12 = use->duration_ticks - (use->remaining_ticks - partial + 1.0f);
    float f16 = f12 / 20.0f;
    f16 = (f16 * f16 + f16 * 2.0f) / 3.0f;
    if (f16 > 1.0f) {
        f16 = 1.0f;
    }
    if (f16 > 0.1f) {
        float f19 = mth_sin((f12 - 0.1f) * 1.3f);
        float f22 = f16 - 0.1f;
        float f24 = f19 * f22;
        m = hand_mat_translate(m, f24 * 0.0f, f24 * 0.004f, f24 * 0.0f);
    }
    m = hand_mat_translate(m, f16 * 0.0f, f16 * 0.0f, f16 * 0.04f);
    m = hand_mat_scale(m, 1.0f, 1.0f, 1.0f + f16 * 0.2f);
    m = hand_mat_rotate_deg(m, 'y', -45.0f);
    return m;
}

void hand_view_frame(const HandView *hv, float partial, float view_yaw_deg, float view_pitch_deg,
                     HandItemKind item_kind, const HandViewUse *use, HandViewFrame *out)
{
    if (out == NULL) {
        return;
    }
    Mat4 id = mmath_mat4_identity();
    memset(out, 0, sizeof(*out));
    out->arm = id;
    out->item = id;
    out->kind = item_kind;
    if (hv == NULL) {
        return;
    }
    partial = mth_clamp(partial, 0.0f, 1.0f);
    float yaw = view_yaw_deg;
    if (!isfinite(yaw)) {
        yaw = hv->yaw_unwrapped;
    }
    yaw = hv->yaw_unwrapped + wrap_deg_180(yaw - hv->yaw_unwrapped);
    float pitch = isfinite(view_pitch_deg) ? view_pitch_deg : 0.0f;

    Mat4 m = id;
    m = hand_mat_rotate_deg(m, 'x', (pitch - mth_lerp(partial, hv->x_bob_o, hv->x_bob)) * 0.1f);
    m = hand_mat_rotate_deg(m, 'y', (yaw - mth_lerp(partial, hv->y_bob_o, hv->y_bob)) * 0.1f);

    float swing = hand_view_swing_progress(hv, partial);
    float equipped = 1.0f - mth_lerp(partial, hv->o_main_hand_height, hv->main_hand_height);
    out->swing = swing;
    out->equipped = equipped;

    if (item_kind == HAND_ITEM_NONE) {
        /* ItemInHandRenderer.renderPlayerArm, right arm (f = 1). */
        float f1 = mth_sqrt(swing);
        float f2 = -0.3f * mth_sin(f1 * HAND_PI);
        float f3 = 0.4f * mth_sin(f1 * 6.2831855f);
        float f4 = -0.4f * mth_sin(swing * HAND_PI);
        m = hand_mat_translate(m, f2 + 0.64000005f, f3 + -0.6f + equipped * -0.6f, f4 + -0.71999997f);
        m = hand_mat_rotate_deg(m, 'y', 45.0f);
        float f5 = mth_sin(swing * swing * HAND_PI);
        float f6 = mth_sin(f1 * HAND_PI);
        m = hand_mat_rotate_deg(m, 'y', f6 * 70.0f);
        m = hand_mat_rotate_deg(m, 'z', f5 * -20.0f);
        m = hand_mat_translate(m, -1.0f, 3.6f, 3.5f);
        m = hand_mat_rotate_deg(m, 'z', 120.0f);
        m = hand_mat_rotate_deg(m, 'x', 200.0f);
        m = hand_mat_rotate_deg(m, 'y', -135.0f);
        m = hand_mat_translate(m, 5.6f, 0.0f, 0.0f);
        out->arm = m;
        return;
    }

    /* Item held: the arm is not drawn, only the item model. */
    HandUse kind = use != NULL ? use->kind : HAND_USE_NONE;
    if (kind == HAND_USE_EAT && use->remaining_ticks > 0.0f) {
        m = apply_eat_transform(m, partial, use);
        m = apply_item_arm_transform(m, equipped);
    } else if (kind == HAND_USE_BOW && use->remaining_ticks > 0.0f) {
        m = apply_bow_transform(m, partial, use, equipped);
    } else {
        float f5 = -0.4f * mth_sin(mth_sqrt(swing) * HAND_PI);
        float f6 = 0.2f * mth_sin(mth_sqrt(swing) * 6.2831855f);
        float f10 = -0.2f * mth_sin(swing * HAND_PI);
        m = hand_mat_translate(m, f5, f6, f10);
        m = apply_item_arm_transform(m, equipped);
        m = apply_item_arm_attack_transform(m, swing);
    }
    out->item = mmath_mat4_mul(m, hand_item_display_matrix(item_kind));
}

/* ---- Geometry ----------------------------------------------------------- */

typedef struct CubeVert {
    float x, y, z;
} CubeVert;

void hand_cube_build(HandCube *out, int tex_u, int tex_v, float ox, float oy, float oz, float dx,
                     float dy, float dz, float grow, float tex_w, float tex_h)
{
    if (out == NULL || tex_w <= 0.0f || tex_h <= 0.0f) {
        return;
    }
    float x0 = ox - grow, y0 = oy - grow, z0 = oz - grow;
    float x1 = ox + dx + grow, y1 = oy + dy + grow, z1 = oz + dz + grow;
    /* Vertex numbering matches ModelPart.Cube (v0..v7). */
    CubeVert v[8] = {
        {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
        {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1},
    };
    float f3 = (float)tex_u;
    float f4 = f3 + dz;
    float f5 = f4 + dx;
    float f6 = f5 + dx;
    float f7 = f5 + dz;
    float f8 = f7 + dx;
    float f9 = (float)tex_v;
    float f10 = f9 + dz;
    float f11 = f10 + dy;
    /* Polygon: vertex indices, u0,v0,u1,v1, normal. */
    static const int idx[6][4] = {
        {5, 4, 0, 1}, {2, 3, 7, 6}, {0, 4, 7, 3}, {1, 0, 3, 2}, {5, 1, 2, 6}, {4, 5, 6, 7},
    };
    static const float nrm[6][3] = {
        {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {0, 0, -1}, {1, 0, 0}, {0, 0, 1},
    };
    float uv[6][4] = {
        {f4, f9, f5, f10}, {f5, f10, f6, f9}, {f3, f10, f4, f11},
        {f4, f10, f5, f11}, {f5, f10, f7, f11}, {f7, f10, f8, f11},
    };
    for (int p = 0; p < 6; ++p) {
        float u0 = uv[p][0], vv0 = uv[p][1], u1 = uv[p][2], vv1 = uv[p][3];
        /* Polygon constructor remap order: (u1,v0) (u0,v0) (u0,v1) (u1,v1). */
        float us[4] = {u1, u0, u0, u1};
        float vs[4] = {vv0, vv0, vv1, vv1};
        for (int k = 0; k < 4; ++k) {
            HandVertex *o = &out->v[p][k];
            const CubeVert *c = &v[idx[p][k]];
            o->x = c->x / 16.0f;
            o->y = c->y / 16.0f;
            o->z = c->z / 16.0f;
            o->nx = nrm[p][0];
            o->ny = nrm[p][1];
            o->nz = nrm[p][2];
            o->u = us[k] / tex_w;
            o->v = vs[k] / tex_h;
        }
    }
}

void hand_arm_cube(HandCube *out)
{
    /* PlayerModel/HumanoidModel right_arm: texOffs(40,16), box(-3,-2,-2,4,12,4). */
    hand_cube_build(out, 40, 16, -3.0f, -2.0f, -2.0f, 4.0f, 12.0f, 4.0f, 0.0f, 64.0f, 64.0f);
}

void hand_sleeve_cube(HandCube *out)
{
    /* right_sleeve: texOffs(40,32), same box, CubeDeformation(0.25). */
    hand_cube_build(out, 40, 32, -3.0f, -2.0f, -2.0f, 4.0f, 12.0f, 4.0f, 0.25f, 64.0f, 64.0f);
}

Mat4 hand_arm_part_matrix(void)
{
    Mat4 m = mmath_mat4_identity();
    m = hand_mat_translate(m, -5.0f / 16.0f, 2.0f / 16.0f, 0.0f);
    /* ModelPart.translateAndRotate: rotationZYX(zRot, yRot, xRot) with only
     * zRot = 0.1 rad (set by AvatarRenderer.renderHand). */
    Mat4 r = mmath_mat4_identity();
    float c = (float)cos(0.1);
    float s = (float)sin(0.1);
    r.m[0] = c;  r.m[4] = -s;
    r.m[1] = s;  r.m[5] = c;
    return mmath_mat4_mul(m, r);
}

Mat4 hand_item_display_matrix(HandItemKind kind)
{
    Mat4 m = mmath_mat4_identity();
    float rx = 0.0f, ry = 0.0f, rz = 0.0f, tx = 0.0f, ty = 0.0f, tz = 0.0f, sc = 1.0f;
    if (kind == HAND_ITEM_BLOCK) {
        /* block/block firstperson_righthand. */
        ry = 45.0f;
        sc = 0.4f;
    } else {
        /* item/generated and item/handheld firstperson_righthand. */
        ry = -90.0f;
        rz = 25.0f;
        tx = 1.13f;
        ty = 3.2f;
        tz = 1.13f;
        sc = 0.68f;
    }
    m = hand_mat_translate(m, tx / 16.0f, ty / 16.0f, tz / 16.0f);
    /* Quaternionf.rotationXYZ(x, y, z) == Rx * Ry * Rz. */
    m = hand_mat_rotate_deg(m, 'x', rx);
    m = hand_mat_rotate_deg(m, 'y', ry);
    m = hand_mat_rotate_deg(m, 'z', rz);
    m = hand_mat_scale(m, sc, sc, sc);
    m = hand_mat_translate(m, -0.5f, -0.5f, -0.5f);
    return m;
}
