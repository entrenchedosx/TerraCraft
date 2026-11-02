#include "game/vphys.h"

#include "world/block.h"
#include "world/world.h"

#include <math.h>
#include <string.h>

#if defined(_MSC_VER)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

/* ---- Mth sine table (shared recipe with hand_view.c) --------------------- */
static float g_sin[65536];
static bool g_sin_ready;

static void sin_init(void)
{
    if (g_sin_ready) {
        return;
    }
    for (int i = 0; i < 65536; ++i) {
        g_sin[i] = (float)sin((double)i * 3.14159265358979323846 * 2.0 / 65536.0);
    }
    g_sin_ready = true;
}

float vphys_mth_sin(float radians)
{
    sin_init();
    long long i = (long long)((double)radians * 10430.378350470453);
    return g_sin[(int)(i & 65535)];
}

float vphys_mth_cos(float radians)
{
    sin_init();
    long long i = (long long)((double)radians * 10430.378350470453 + 16384.0);
    return g_sin[(int)(i & 65535)];
}

/* ---- geometry ---------------------------------------------------------------- */

typedef struct Box {
    double x0, y0, z0, x1, y1, z1;
} Box;

typedef struct V3 {
    double x, y, z;
} V3;

static Box box_at(const VPhys *s, double x, double y, double z, double w, double h)
{
    (void)s;
    Box b = {x - w * 0.5, y, z - w * 0.5, x + w * 0.5, y + h, z + w * 0.5};
    return b;
}

static Box box_move(Box b, double dx, double dy, double dz)
{
    Box r = {b.x0 + dx, b.y0 + dy, b.z0 + dz, b.x1 + dx, b.y1 + dy, b.z1 + dz};
    return r;
}

static Box box_expand_towards(Box b, double dx, double dy, double dz)
{
    if (dx < 0.0) {
        b.x0 += dx;
    } else {
        b.x1 += dx;
    }
    if (dy < 0.0) {
        b.y0 += dy;
    } else {
        b.y1 += dy;
    }
    if (dz < 0.0) {
        b.z0 += dz;
    } else {
        b.z1 += dz;
    }
    return b;
}

static bool cell_solid(const World *w, int x, int y, int z)
{
    return w != NULL && block_is_solid(world_get_block(w, x, y, z));
}

#define EPS 1.0e-7

/* VoxelShape.collide for a unit cube along one axis (0=X,1=Y,2=Z). */
static double axis_collide(const World *w, int axis, Box b, Box region, double dist)
{
    if (fabs(dist) < EPS) {
        return 0.0;
    }
    int xs = (int)floor(region.x0 - EPS) - 1, xe = (int)ceil(region.x1 + EPS);
    int ys = (int)floor(region.y0 - EPS) - 1, ye = (int)ceil(region.y1 + EPS);
    int zs = (int)floor(region.z0 - EPS) - 1, ze = (int)ceil(region.z1 + EPS);
    for (int cy = ys; cy <= ye; ++cy) {
        for (int cz = zs; cz <= ze; ++cz) {
            for (int cx = xs; cx <= xe; ++cx) {
                if (!cell_solid(w, cx, cy, cz)) {
                    continue;
                }
                double cmin[3] = {(double)cx, (double)cy, (double)cz};
                double bmin[3] = {b.x0, b.y0, b.z0};
                double bmax[3] = {b.x1, b.y1, b.z1};
                bool overlap = true;
                for (int a = 0; a < 3; ++a) {
                    if (a == axis) {
                        continue;
                    }
                    if (!(bmin[a] + EPS < cmin[a] + 1.0 && bmax[a] - EPS > cmin[a])) {
                        overlap = false;
                    }
                }
                if (!overlap) {
                    continue;
                }
                if (dist > 0.0) {
                    double gap = cmin[axis] - bmax[axis];
                    if (gap >= -EPS && gap < dist) {
                        dist = gap;
                    }
                } else {
                    double gap = cmin[axis] + 1.0 - bmin[axis];
                    if (gap <= EPS && gap > dist) {
                        dist = gap;
                    }
                }
                if (fabs(dist) < EPS) {
                    return 0.0;
                }
            }
        }
    }
    return dist;
}

/* Entity.collideWithShapes: Y first, then the smaller horizontal axis first. */
static V3 collide_with_shapes(const World *w, V3 mv, Box b, Box region)
{
    double d = mv.x, e = mv.y, f = mv.z;
    if (e != 0.0) {
        e = axis_collide(w, 1, b, region, e);
        if (e != 0.0) {
            b = box_move(b, 0.0, e, 0.0);
        }
    }
    bool zfirst = fabs(d) < fabs(f);
    if (zfirst && f != 0.0) {
        f = axis_collide(w, 2, b, region, f);
        if (f != 0.0) {
            b = box_move(b, 0.0, 0.0, f);
        }
    }
    if (d != 0.0) {
        d = axis_collide(w, 0, b, region, d);
        if (!zfirst && d != 0.0) {
            b = box_move(b, d, 0.0, 0.0);
        }
    }
    if (!zfirst && f != 0.0) {
        f = axis_collide(w, 2, b, region, f);
    }
    V3 r = {d, e, f};
    return r;
}

static V3 collide_box(const World *w, V3 mv, Box b)
{
    Box region = box_expand_towards(b, mv.x, mv.y, mv.z);
    return collide_with_shapes(w, mv, b, region);
}

static double hdist_sqr(V3 v)
{
    return v.x * v.x + v.z * v.z;
}

/* Entity.collide including the step-up search (maxUpStep 0.6). */
static V3 entity_collide(const VPhys *s, const World *w, V3 mv)
{
    Box b = box_at(s, s->x, s->y, s->z, s->width, s->height);
    V3 vec3 = (mv.x * mv.x + mv.y * mv.y + mv.z * mv.z) == 0.0 ? mv : collide_box(w, mv, b);
    bool bl = mv.x != vec3.x;
    bool bl2 = mv.y != vec3.y;
    bool bl3 = mv.z != vec3.z;
    bool bl4 = s->on_ground || (bl2 && mv.y < 0.0);
    double up = (double)VPHYS_STEP_HEIGHT;
    if (up > 0.0 && bl4 && (bl || bl3)) {
        V3 m1 = {mv.x, up, mv.z};
        V3 vec32 = collide_box(w, m1, b);
        V3 m2 = {0.0, up, 0.0};
        V3 vec33 = collide_box(w, m2, box_expand_towards(b, mv.x, 0.0, mv.z));
        if (vec33.y < up) {
            V3 m3 = {mv.x, 0.0, mv.z};
            V3 t = collide_box(w, m3, box_move(b, vec33.x, vec33.y, vec33.z));
            V3 vec34 = {t.x + vec33.x, t.y + vec33.y, t.z + vec33.z};
            if (hdist_sqr(vec34) > hdist_sqr(vec32)) {
                vec32 = vec34;
            }
        }
        if (hdist_sqr(vec32) > hdist_sqr(vec3)) {
            V3 m4 = {0.0, -vec32.y + mv.y, 0.0};
            V3 t = collide_box(w, m4, box_move(b, vec32.x, vec32.y, vec32.z));
            V3 r = {vec32.x + t.x, vec32.y + t.y, vec32.z + t.z};
            return r;
        }
    }
    return vec3;
}

/* Level.noCollision for an AABB. */
static bool no_collision(const World *w, Box b)
{
    int xs = (int)floor(b.x0), xe = (int)floor(b.x1 - 1e-12);
    int ys = (int)floor(b.y0), ye = (int)floor(b.y1 - 1e-12);
    int zs = (int)floor(b.z0), ze = (int)floor(b.z1 - 1e-12);
    for (int y = ys; y <= ye; ++y) {
        for (int z = zs; z <= ze; ++z) {
            for (int x = xs; x <= xe; ++x) {
                if (cell_solid(w, x, y, z)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* ---- fluids ------------------------------------------------------------------- */

/* FluidState.getHeight for a water cell, in blocks (0 when not water). */
static double water_cell_height(const World *w, int x, int y, int z)
{
    uint16_t id = world_get_block(w, x, y, z);
    if (!block_is_water(id)) {
        return 0.0;
    }
    if (block_is_water(world_get_block(w, x, y + 1, z))) {
        return 1.0; /* same fluid above */
    }
    int level = block_water_level(id);
    if (level <= 0 || block_water_is_falling(id)) {
        return (double)(8.0f / 9.0f);
    }
    return (double)((float)(8 - level) / 9.0f);
}

/* Entity.updateFluidHeightAndDoFluidPushing(WATER) without the flow push. */
static bool update_fluid_height(VPhys *s, const World *w)
{
    Box b = box_at(s, s->x, s->y, s->z, s->width, s->height);
    b.x0 += 0.001; b.y0 += 0.001; b.z0 += 0.001;
    b.x1 -= 0.001; b.y1 -= 0.001; b.z1 -= 0.001;
    int i = (int)floor(b.x0), j = (int)ceil(b.x1);
    int k = (int)floor(b.y0), l = (int)ceil(b.y1);
    int m = (int)floor(b.z0), n = (int)ceil(b.z1);
    double d = 0.0;
    bool found = false;
    if (w != NULL) {
        for (int x = i; x < j; ++x) {
            for (int y = k; y < l; ++y) {
                for (int z = m; z < n; ++z) {
                    double h = water_cell_height(w, x, y, z);
                    if (h > 0.0) {
                        double e = (double)((float)y + (float)h);
                        if (e >= b.y0) {
                            found = true;
                            if (e - b.y0 > d) {
                                d = e - b.y0;
                            }
                        }
                    }
                }
            }
        }
    }
    s->water_height = d;
    return found;
}

/* Entity.isEyeInFluid(WATER). */
static bool eye_in_water(const VPhys *s, const World *w)
{
    if (w == NULL) {
        return false;
    }
    double eye_y = s->y + s->eye_height - 0.1111111119389534;
    int bx = (int)floor(s->x), by = (int)floor(eye_y), bz = (int)floor(s->z);
    double h = water_cell_height(w, bx, by, bz);
    return h > 0.0 && (double)by + h > eye_y;
}

/* ---- setup ---------------------------------------------------------------------- */

void vphys_pose_dims(VPose pose, double *width, double *height, double *eye)
{
    *width = 0.6;
    switch (pose) {
    case VPOSE_CROUCHING: *height = 1.5; *eye = (double)1.27f; break;
    case VPOSE_SWIMMING: *height = 0.6; *eye = (double)0.4f; break;
    default: *height = 1.8; *eye = (double)1.62f; break;
    }
}

void vphys_init(VPhys *s, double x, double y, double z)
{
    memset(s, 0, sizeof(*s));
    s->x = s->px = x;
    s->y = s->py = y;
    s->z = s->pz = z;
    s->pose = VPOSE_STANDING;
    vphys_pose_dims(s->pose, &s->width, &s->height, &s->eye_height);
    s->landed_fall = -1.0;
}

/* ---- movement ----------------------------------------------------------------------- */

static float player_speed(const VPhys *s)
{
    /* Attribute: base 0.10000000149011612, sprint modifier ADD_MULTIPLIED_TOTAL +0.3. */
    double v = VPHYS_BASE_SPEED;
    if (s->sprinting) {
        v *= 1.0 + 0.3;
    }
    return (float)v;
}

static float flying_speed(const VPhys *s)
{
    return s->sprinting ? 0.025999999f : 0.02f; /* Player.getFlyingSpeed */
}

/* Entity.moveRelative */
static void move_relative(VPhys *s, float speed, V3 in, float yaw_deg)
{
    double d = in.x * in.x + in.y * in.y + in.z * in.z;
    if (d < 1.0E-7) {
        return;
    }
    V3 v = in;
    if (d > 1.0) {
        double len = sqrt(d);
        v.x = in.x / len;
        v.y = in.y / len;
        v.z = in.z / len;
    }
    v.x *= (double)speed;
    v.y *= (double)speed;
    v.z *= (double)speed;
    float f = vphys_mth_sin(yaw_deg * 0.017453292f);
    float g = vphys_mth_cos(yaw_deg * 0.017453292f);
    s->vx += v.x * (double)g - v.z * (double)f;
    s->vy += v.y;
    s->vz += v.z * (double)g + v.x * (double)f;
}

/* Player.maybeBackOffFromEdge (sneaking edge protection). */
static bool can_fall_at_least(const VPhys *s, const World *w, double x, double z, double dist)
{
    Box b = box_at(s, s->x, s->y, s->z, s->width, s->height);
    Box t = {b.x0 + 1.0E-7 + x, b.y0 - dist - 1.0E-7, b.z0 + 1.0E-7 + z,
             b.x1 - 1.0E-7 + x, b.y0, b.z1 - 1.0E-7 + z};
    return no_collision(w, t);
}

static bool is_above_ground(const VPhys *s, const World *w, float f)
{
    return s->on_ground || (s->fall_distance < (double)f && !can_fall_at_least(s, w, 0.0, 0.0, (double)f - s->fall_distance));
}

static V3 back_off_from_edge(const VPhys *s, const World *w, V3 mv)
{
    float f = VPHYS_STEP_HEIGHT;
    if (!(mv.y <= 0.0) || !s->shift_key || !is_above_ground(s, w, f)) {
        return mv;
    }
    double d = mv.x, e = mv.z;
    double g = 0.05;
    double h = (d > 0.0 ? 1.0 : (d < 0.0 ? -1.0 : 0.0)) * g;
    double i = (e > 0.0 ? 1.0 : (e < 0.0 ? -1.0 : 0.0)) * g;
    while (d != 0.0 && can_fall_at_least(s, w, d, 0.0, (double)f)) {
        if (fabs(d) <= 0.05) {
            d = 0.0;
            break;
        }
        d -= h;
    }
    while (e != 0.0 && can_fall_at_least(s, w, 0.0, e, (double)f)) {
        if (fabs(e) <= 0.05) {
            e = 0.0;
            break;
        }
        e -= i;
    }
    while (d != 0.0 && e != 0.0 && can_fall_at_least(s, w, d, e, (double)f)) {
        d = fabs(d) <= 0.05 ? 0.0 : d - h;
        e = fabs(e) <= 0.05 ? 0.0 : e - i;
    }
    V3 r = {d, mv.y, e};
    return r;
}

/* Entity.move(MoverType.SELF, ...) */
static void entity_move(VPhys *s, const World *w, V3 mv)
{
    mv = back_off_from_edge(s, w, mv);
    V3 vec3 = entity_collide(s, w, mv);
    double d = vec3.x * vec3.x + vec3.y * vec3.y + vec3.z * vec3.z;
    double mvsq = mv.x * mv.x + mv.y * mv.y + mv.z * mv.z;
    if (d > 1.0E-7 || mvsq - d < 1.0E-7) {
        s->x += vec3.x;
        s->y += vec3.y;
        s->z += vec3.z;
    }
    bool bl = !(fabs(mv.x - vec3.x) < 1.0E-5);
    bool bl2 = !(fabs(mv.z - vec3.z) < 1.0E-5);
    s->horizontal_collision = bl || bl2;
    /* isLocalInstanceAuthoritative: always for the local player. */
    s->vertical_collision = mv.y != vec3.y;
    s->vertical_collision_below = s->vertical_collision && mv.y < 0.0;
    s->on_ground = s->vertical_collision_below;
    /* LivingEntity.checkFallDamage: refresh the water state first. */
    if (!s->in_water && update_fluid_height(s, w)) {
        s->in_water = true;
        s->fall_distance = 0.0;
    }
    /* Entity.checkFallDamage. */
    if (!s->in_water && vec3.y < 0.0) {
        s->fall_distance -= (double)(float)vec3.y;
    }
    if (s->on_ground) {
        if (s->fall_distance > 0.0) {
            s->landed_fall = s->fall_distance;
        }
        s->fall_distance = 0.0;
    }
    if (s->horizontal_collision) {
        s->vx = bl ? 0.0 : s->vx;
        s->vz = bl2 ? 0.0 : s->vz;
    }
    if (mv.y != vec3.y) {
        s->vy = 0.0; /* Block.updateEntityMovementAfterFallOn */
    }
}

/* Block friction under the feet (Block.getFriction; only the default 0.6). */
static float block_friction_below(const VPhys *s, const World *w)
{
    (void)s;
    (void)w;
    return 0.6f;
}

/* LivingEntity.jumpOutOfFluid */
static void jump_out_of_fluid(VPhys *s, const World *w, double y0)
{
    if (!s->horizontal_collision) {
        return;
    }
    Box b = box_at(s, s->x, s->y, s->z, s->width, s->height);
    Box t = box_move(b, s->vx, s->vy + 0.6000000238418579 - s->y + y0, s->vz);
    bool free_box = no_collision(w, t);
    if (free_box) {
        /* ... and no liquid inside the shifted box (Level.containsAnyLiquid). */
        int xs = (int)floor(t.x0), xe = (int)floor(t.x1 - 1e-12);
        int ys = (int)floor(t.y0), ye = (int)floor(t.y1 - 1e-12);
        int zs = (int)floor(t.z0), ze = (int)floor(t.z1 - 1e-12);
        for (int y = ys; y <= ye && free_box; ++y) {
            for (int z = zs; z <= ze && free_box; ++z) {
                for (int x = xs; x <= xe; ++x) {
                    if (block_is_water(world_get_block(w, x, y, z))) {
                        free_box = false;
                        break;
                    }
                }
            }
        }
    }
    if (free_box) {
        s->vy = 0.30000001192092896;
    }
}

/* LivingEntity.getFluidFallingAdjustedMovement */
static V3 fluid_falling_adjusted(double gravity, bool falling, V3 mv)
{
    if (gravity != 0.0) {
        double e;
        if (falling && fabs(mv.y - 0.005) >= 0.003 && fabs(mv.y - gravity / 16.0) < 0.003) {
            e = -0.003;
        } else {
            e = mv.y - gravity / 16.0;
        }
        V3 r = {mv.x, e, mv.z};
        return r;
    }
    return mv;
}

/* LivingEntity.travelInAir */
static void travel_in_air(VPhys *s, const World *w, V3 in, float yaw_deg)
{
    float friction = s->on_ground ? block_friction_below(s, w) : 1.0f;
    float f = friction * 0.91f;
    /* handleRelativeFrictionAndCalculateMovement */
    float accel = s->on_ground ? player_speed(s) * (0.21600002f / (friction * friction * friction)) : flying_speed(s);
    move_relative(s, accel, in, yaw_deg);
    V3 mv = {s->vx, s->vy, s->vz};
    entity_move(s, w, mv);
    double y = s->vy;
    y -= VPHYS_GRAVITY;
    s->vx = s->vx * (double)f;
    s->vy = y * (double)VPHYS_DRAG_VERTICAL;
    s->vz = s->vz * (double)f;
}

/* LivingEntity.travelInWater */
static void travel_in_water(VPhys *s, const World *w, V3 in, float yaw_deg, double gravity, bool falling, double y0)
{
    float f = s->sprinting ? 0.9f : 0.8f;
    float g = 0.02f;
    /* WATER_MOVEMENT_EFFICIENCY attribute is 0 for the player (no Depth Strider). */
    move_relative(s, g, in, yaw_deg);
    V3 mv = {s->vx, s->vy, s->vz};
    entity_move(s, w, mv);
    V3 d = {s->vx * (double)f, s->vy * 0.800000011920929, s->vz * (double)f};
    d = fluid_falling_adjusted(gravity, falling, d);
    s->vx = d.x;
    s->vy = d.y;
    s->vz = d.z;
    jump_out_of_fluid(s, w, y0);
}

/* Player.travel / LivingEntity.travel */
static void travel(VPhys *s, const World *w, V3 in, float yaw_deg, float pitch_deg)
{
    if (s->swimming) {
        /* Player.travel: look-based vertical acceleration while swimming. */
        double look_y = -(double)vphys_mth_sin(pitch_deg * 0.017453292f);
        double e = look_y < -0.2 ? 0.085 : 0.06;
        bool above_fluid_empty = true;
        if (w != NULL) {
            above_fluid_empty = !block_is_water(world_get_block(w, (int)floor(s->x), (int)floor(s->y + 1.0 - 0.1), (int)floor(s->z)));
        }
        if (look_y <= 0.0 || s->jumping || !above_fluid_empty) {
            s->vy += (look_y - s->vy) * e;
        }
    }
    if (s->in_water) {
        bool falling = s->vy <= 0.0;
        double y0 = s->y;
        travel_in_water(s, w, in, yaw_deg, VPHYS_GRAVITY, falling, y0);
    } else {
        travel_in_air(s, w, in, yaw_deg);
    }
}

/* LocalPlayer.modifyInput. Returns processed (left, forward). */
static void modify_input(const VPhys *s, float left, float forward, float *out_l, float *out_f)
{
    float lsq = left * left + forward * forward;
    if (lsq == 0.0f) {
        *out_l = left;
        *out_f = forward;
        return;
    }
    float x = left * 0.98f, y = forward * 0.98f;
    if (s->pose == VPOSE_CROUCHING || (s->pose == VPOSE_SWIMMING && !s->swimming)) {
        x *= 0.3f; /* SNEAKING_SPEED attribute (isMovingSlowly) */
        y *= 0.3f;
    }
    float len = (float)sqrt((double)(x * x + y * y));
    if (len > 0.0f) {
        float ux = x * (1.0f / len), uy = y * (1.0f / len);
        float ax = fabsf(ux), ay = fabsf(uy);
        float h = ay > ax ? ax / ay : ay / ax;
        float dist = (float)sqrt((double)(1.0f + h * h));
        float m = len * dist < 1.0f ? len * dist : 1.0f;
        x = ux * m;
        y = uy * m;
    }
    *out_l = x;
    *out_f = y;
}

static void update_pose(VPhys *s, const World *w)
{
    VPose want = VPOSE_STANDING;
    if (s->swimming) {
        want = VPOSE_SWIMMING;
    } else if (s->shift_key) {
        want = VPOSE_CROUCHING;
    }
    VPose order[3] = {want, VPOSE_CROUCHING, VPOSE_SWIMMING};
    VPose chosen = VPOSE_SWIMMING;
    for (int i = 0; i < 3; ++i) {
        double ww, hh, ee;
        vphys_pose_dims(order[i], &ww, &hh, &ee);
        Box b = box_at(s, s->x, s->y, s->z, ww, hh);
        b.x0 += 1.0E-7; b.y0 += 1.0E-7; b.z0 += 1.0E-7;
        b.x1 -= 1.0E-7; b.y1 -= 1.0E-7; b.z1 -= 1.0E-7;
        if (w == NULL || no_collision(w, b)) {
            chosen = order[i];
            break;
        }
    }
    s->pose = chosen;
    vphys_pose_dims(chosen, &s->width, &s->height, &s->eye_height);
}

void vphys_tick(VPhys *s, const VInput *in, const World *w, float yaw_deg, float pitch_deg)
{
    if (s == NULL) {
        return;
    }
    VInput none;
    memset(&none, 0, sizeof(none));
    if (in == NULL) {
        in = &none;
    }
    s->px = s->x;
    s->py = s->y;
    s->pz = s->z;
    s->landed_fall = -1.0;
    s->ticks++;

    /* Entity.baseTick: water state, swimming, underwater. */
    s->shift_key = in->shift;
    bool touching = update_fluid_height(s, w);
    if (touching) {
        s->fall_distance = 0.0; /* resetFallDistance on water contact */
    }
    s->in_water = touching;
    s->eye_in_water = eye_in_water(s, w);
    bool under_water = s->eye_in_water && s->in_water;
    if (s->swimming) {
        s->swimming = s->sprinting && s->in_water;
    } else {
        s->swimming = s->sprinting && under_water;
    }

    /* LocalPlayer.aiStep: sprint start/stop. */
    bool crouching = s->pose == VPOSE_CROUCHING;
    bool has_forward = in->forward >= 0.8f;
    if (s->sprinting) {
        bool stop;
        if (s->swimming) {
            stop = !(in->forward > 1.0E-5f) || !s->in_water;
        } else {
            stop = !(in->forward > 1.0E-5f) || crouching || s->horizontal_collision || (s->in_water && !under_water);
        }
        if (stop) {
            s->sprinting = false;
        }
    } else if (in->sprint_key && has_forward && (!crouching || under_water) && (!s->in_water || under_water)) {
        s->sprinting = true;
    }

    if (in->shift && s->in_water) {
        s->vy += -0.03999999910593033; /* LocalPlayer.aiStep: goDownInWater */
    }
    s->jumping = in->jump;

    /* LivingEntity.aiStep. */
    if (s->no_jump_delay > 0) {
        s->no_jump_delay--;
    }
    if (s->vx * s->vx + s->vz * s->vz < 9.0E-6) {
        s->vx = 0.0;
        s->vz = 0.0;
    }
    if (fabs(s->vy) < 0.003) {
        s->vy = 0.0;
    }
    modify_input(s, in->left, in->forward, &s->xxa, &s->zza);
    bool jumping = in->jump;
    if (jumping) {
        double fluid_h = s->water_height;
        bool in_water = s->in_water && fluid_h > 0.0;
        double threshold = s->eye_height < 0.4 ? 0.0 : 0.4;
        if (in_water && (!s->on_ground || fluid_h > threshold)) {
            s->vy += 0.03999999910593033; /* jumpInLiquid */
        } else if ((s->on_ground || (in_water && fluid_h <= threshold)) && s->no_jump_delay == 0) {
            /* jumpFromGround */
            float power = VPHYS_JUMP_POWER;
            s->vy = (double)power > s->vy ? (double)power : s->vy;
            if (s->sprinting) {
                float yr = yaw_deg * 0.017453292f;
                s->vx += (double)(-vphys_mth_sin(yr)) * 0.2;
                s->vz += (double)vphys_mth_cos(yr) * 0.2;
            }
            s->no_jump_delay = 10;
        }
    } else {
        s->no_jump_delay = 0;
    }
    V3 input = {(double)s->xxa, 0.0, (double)s->zza};
    travel(s, w, input, yaw_deg, pitch_deg);

    update_pose(s, w);
}
