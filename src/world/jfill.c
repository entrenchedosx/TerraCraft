#include "world/jfill.h"

#include "world/jrandom.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

#define WAY_BELOW_MIN_Y (-32512) /* DimensionType.WAY_BELOW_MIN_Y */

typedef struct FluidStatus {
    int level;
    uint8_t type; /* JB_AIR / JB_WATER / JB_LAVA */
} FluidStatus;

typedef struct SurfEntry {
    int qx, qz, value;
    bool used;
} SurfEntry;

#define SURF_CAP 2048

typedef struct Aquifer {
    JChunk *chunk;
    JrPositional rand;
    int sea_level;
    int min_gx, min_gy, min_gz, size_x, size_y, size_z;
    int *loc;            /* x,y,z per grid cell. */
    unsigned char *loc_have;
    FluidStatus *status;
    unsigned char *status_have;
    int skip_above_y;
    SurfEntry surf[SURF_CAP];
} Aquifer;

static const int SURFACE_OFFSETS[13][2] = {{0, 0},  {-2, -1}, {-1, -1}, {0, -1}, {1, -1}, {-3, 0}, {-2, 0},
                                           {-1, 0}, {1, 0},   {-2, 1},  {-1, 1}, {0, 1},  {1, 1}};

static int floor_div(int a, int b)
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        q--;
    }
    return q;
}

static int mth_floor(double v)
{
    int i = (int)v;
    return v < (double)i ? i - 1 : i;
}

static double mth_clamp(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static double mth_inverse_lerp(double v, double a, double b)
{
    return (v - a) / (b - a);
}

static double mth_lerp(double d, double s, double e)
{
    return s + d * (e - s);
}

static double mth_clamped_lerp(double d, double s, double e)
{
    return d < 0.0 ? s : (d > 1.0 ? e : mth_lerp(d, s, e));
}

static double mth_clamped_map(double v, double a1, double a2, double b1, double b2)
{
    return mth_clamped_lerp(mth_inverse_lerp(v, a1, a2), b1, b2);
}

static double mth_map(double v, double a1, double a2, double b1, double b2)
{
    return mth_lerp(mth_inverse_lerp(v, a1, a2), b1, b2);
}

static double router(const Aquifer *a, const char *key, int x, int y, int z)
{
    return jchunk_eval_router(a->chunk, key, x, y, z, false);
}

/* NoiseChunk.preliminarySurfaceLevel(x, z): quart-snapped, floored. */
static int pre_surface(Aquifer *a, int x, int z)
{
    int qx = (x >> 2) << 2;
    int qz = (z >> 2) << 2;
    unsigned h = ((unsigned)qx * 73856093u) ^ ((unsigned)qz * 19349663u);
    for (unsigned i = 0; i < SURF_CAP; ++i) {
        SurfEntry *e = &a->surf[(h + i) % SURF_CAP];
        if (!e->used) {
            e->used = true;
            e->qx = qx;
            e->qz = qz;
            e->value = mth_floor(router(a, "preliminary_surface_level", qx, 0, qz));
            return e->value;
        }
        if (e->qx == qx && e->qz == qz) {
            return e->value;
        }
    }
    return mth_floor(router(a, "preliminary_surface_level", qx, 0, qz));
}

static FluidStatus global_fluid(const Aquifer *a, int y)
{
    int lim = a->sea_level < -54 ? a->sea_level : -54;
    FluidStatus s;
    if (y < lim) {
        s.level = -54;
        s.type = JB_LAVA;
    } else {
        s.level = a->sea_level;
        s.type = JB_WATER;
    }
    return s;
}

static uint8_t fs_at(FluidStatus f, int y)
{
    return y < f.level ? f.type : (uint8_t)JB_AIR;
}

static bool fs_eq(FluidStatus a, FluidStatus b)
{
    return a.level == b.level && a.type == b.type;
}

static int adjust_surface(int s)
{
    return s + 8;
}

static int randomized_fluid_surface(Aquifer *a, int x, int y, int z, int min_surface)
{
    int k = floor_div(x, 16), l = floor_div(y, 40), m = floor_div(z, 16);
    int n = l * 40 + 20;
    double d = router(a, "fluid_level_spread", k, l, m) * 10.0;
    int p = mth_floor(d / 3.0) * 3; /* Mth.quantize(d, 3) */
    int q = n + p;
    return min_surface < q ? min_surface : q;
}

static int compute_surface_level(Aquifer *a, int x, int y, int z, FluidStatus fs, int i, bool bl)
{
    double d, e;
    if (router(a, "erosion", x, y, z) < -0.22499999403953552 && router(a, "depth", x, y, z) > 0.8999999761581421) {
        d = -1.0;
        e = -1.0;
    } else {
        int j = i + 8 - y;
        double f = bl ? mth_clamped_map((double)j, 0.0, 64.0, 1.0, 0.0) : 0.0;
        double g = mth_clamp(router(a, "fluid_level_floodedness", x, y, z), -1.0, 1.0);
        double h = mth_map(f, 1.0, 0.0, -0.3, 0.8);
        double l = mth_map(f, 1.0, 0.0, -0.8, 0.4);
        d = g - l;
        e = g - h;
    }
    if (e > 0.0) {
        return fs.level;
    }
    if (d > 0.0) {
        return randomized_fluid_surface(a, x, y, z, i);
    }
    return WAY_BELOW_MIN_Y;
}

static uint8_t compute_fluid_type(Aquifer *a, int x, int y, int z, FluidStatus fs, int level)
{
    uint8_t bs = fs.type;
    if (level <= -10 && level != WAY_BELOW_MIN_Y && fs.type != JB_LAVA) {
        int k = floor_div(x, 64), l = floor_div(y, 40), m = floor_div(z, 64);
        double d = router(a, "lava", k, l, m);
        if (fabs(d) > 0.3) {
            bs = JB_LAVA;
        }
    }
    (void)y;
    return bs;
}

static FluidStatus compute_fluid(Aquifer *a, int x, int y, int z)
{
    FluidStatus fs = global_fluid(a, y);
    int i = 2147483647;
    int j = y + 12;
    int k = y - 12;
    bool bl = false;
    for (int t = 0; t < 13; ++t) {
        int l = x + (SURFACE_OFFSETS[t][0] << 4);
        int m = z + (SURFACE_OFFSETS[t][1] << 4);
        int n = pre_surface(a, l, m);
        int o = adjust_surface(n);
        bool bl2 = SURFACE_OFFSETS[t][0] == 0 && SURFACE_OFFSETS[t][1] == 0;
        if (bl2 && k > o) {
            return fs;
        }
        bool bl3 = j > o;
        if (bl3 || bl2) {
            FluidStatus fs2 = global_fluid(a, o);
            if (fs_at(fs2, o) != JB_AIR) {
                if (bl2) {
                    bl = true;
                }
                if (bl3) {
                    return fs2;
                }
            }
        }
        if (n < i) {
            i = n;
        }
    }
    int p = compute_surface_level(a, x, y, z, fs, i, bl);
    FluidStatus out;
    out.level = p;
    out.type = compute_fluid_type(a, x, y, z, fs, p);
    return out;
}

static int grid_index(const Aquifer *a, int gx, int gy, int gz)
{
    return ((gy - a->min_gy) * a->size_z + (gz - a->min_gz)) * a->size_x + (gx - a->min_gx);
}

static FluidStatus aquifer_status(Aquifer *a, int idx)
{
    if (!a->status_have[idx]) {
        a->status[idx] = compute_fluid(a, a->loc[idx * 3], a->loc[idx * 3 + 1], a->loc[idx * 3 + 2]);
        a->status_have[idx] = 1;
    }
    return a->status[idx];
}

static double similarity(int a, int b)
{
    return 1.0 - (double)(b - a) / 25.0;
}

static double pressure(Aquifer *a, double *barrier, int x, int y, int z, FluidStatus s1, FluidStatus s2)
{
    uint8_t b1 = fs_at(s1, y), b2 = fs_at(s2, y);
    if ((b1 == JB_LAVA && b2 == JB_WATER) || (b1 == JB_WATER && b2 == JB_LAVA)) {
        return 2.0;
    }
    int j = abs(s1.level - s2.level);
    if (j == 0) {
        return 0.0;
    }
    double d = 0.5 * (double)(s1.level + s2.level);
    double e = (double)y + 0.5 - d;
    double f = (double)j / 2.0;
    double n = f - fabs(e);
    double p;
    if (e > 0.0) {
        double q = 0.0 + n;
        p = q > 0.0 ? q / 1.5 : q / 2.5;
    } else {
        double q = 3.0 + n;
        p = q > 0.0 ? q / 3.0 : q / 10.0;
    }
    double s;
    if (p < -2.0 || p > 2.0) {
        s = 0.0;
    } else {
        if (isnan(*barrier)) {
            *barrier = router(a, "barrier", x, y, z);
        }
        s = *barrier;
    }
    return 2.0 * (s + p);
}

static Aquifer *aquifer_create(JChunk *c)
{
    Aquifer *a = (Aquifer *)calloc(1, sizeof(Aquifer));
    if (a == NULL) {
        return NULL;
    }
    const JRandomState *rs = jchunk_state(c);
    a->chunk = c;
    a->rand = *jrs_aquifer_random(rs);
    a->sea_level = jrs_sea_level(rs);
    int minx = jchunk_min_x(c), minz = jchunk_min_z(c);
    int maxx = minx + 15, maxz = minz + 15;
    a->min_gx = ((minx - 5) >> 4) + 0;
    int max_gx = ((maxx - 5) >> 4) + 1;
    a->size_x = max_gx - a->min_gx + 1;
    a->min_gy = floor_div(JFILL_MIN_Y + 1, 12) - 1;
    int max_gy = floor_div(JFILL_MIN_Y + JFILL_HEIGHT + 1, 12) + 1;
    a->size_y = max_gy - a->min_gy + 1;
    a->min_gz = ((minz - 5) >> 4) + 0;
    int max_gz = ((maxz - 5) >> 4) + 1;
    a->size_z = max_gz - a->min_gz + 1;
    int total = a->size_x * a->size_y * a->size_z;
    a->loc = (int *)calloc((size_t)total * 3, sizeof(int));
    a->loc_have = (unsigned char *)calloc((size_t)total, 1);
    a->status = (FluidStatus *)calloc((size_t)total, sizeof(FluidStatus));
    a->status_have = (unsigned char *)calloc((size_t)total, 1);
    /* skipSamplingAboveY from the maximum preliminary surface over the grid. */
    int ix0 = (a->min_gx << 4) + 0, iz0 = (a->min_gz << 4) + 0;
    int ix1 = (max_gx << 4) + 9, iz1 = (max_gz << 4) + 9;
    int best = (int)0x80000000;
    for (int z = iz0; z <= iz1; z += 4) {
        for (int x = ix0; x <= ix1; x += 4) {
            int v = pre_surface(a, x, z);
            if (v > best) {
                best = v;
            }
        }
    }
    int j = adjust_surface(best);
    int g = floor_div(j + 12, 12) + 1;
    a->skip_above_y = g * 12 + 11 - 1;
    return a;
}

static void aquifer_free(Aquifer *a)
{
    if (a != NULL) {
        free(a->loc);
        free(a->loc_have);
        free(a->status);
        free(a->status_have);
        free(a);
    }
}

/* Aquifer.NoiseBasedAquifer.computeSubstance. Returns JB_* or -1 for "null". */
static int aquifer_substance(Aquifer *a, int x, int y, int z, double density)
{
    if (density > 0.0) {
        return -1;
    }
    FluidStatus fluid = global_fluid(a, y);
    if (y > a->skip_above_y) {
        return fs_at(fluid, y);
    }
    if (fs_at(fluid, y) == JB_LAVA) {
        return JB_LAVA;
    }
    int gx0 = (x - 5) >> 4;
    int gy0 = floor_div(y + 1, 12);
    int gz0 = (z - 5) >> 4;
    int d1 = 2147483647, d2 = 2147483647, d3 = 2147483647, d4 = 2147483647;
    int i1 = 0, i2 = 0, i3 = 0, i4 = 0;
    for (int dx = 0; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = 0; dz <= 1; ++dz) {
                int gx = gx0 + dx, gy = gy0 + dy, gz = gz0 + dz;
                int idx = grid_index(a, gx, gy, gz);
                if (!a->loc_have[idx]) {
                    JrRandom rs = jr_pos_at(&a->rand, gx, gy, gz);
                    a->loc[idx * 3] = (gx << 4) + jr_next_int_bound(&rs, 10);
                    a->loc[idx * 3 + 1] = gy * 12 + jr_next_int_bound(&rs, 9);
                    a->loc[idx * 3 + 2] = (gz << 4) + jr_next_int_bound(&rs, 10);
                    a->loc_have[idx] = 1;
                }
                int ex = a->loc[idx * 3] - x, ey = a->loc[idx * 3 + 1] - y, ez = a->loc[idx * 3 + 2] - z;
                int dist = ex * ex + ey * ey + ez * ez;
                if (d1 >= dist) {
                    i4 = i3; i3 = i2; i2 = i1; i1 = idx;
                    d4 = d3; d3 = d2; d2 = d1; d1 = dist;
                } else if (d2 >= dist) {
                    i4 = i3; i3 = i2; i2 = idx;
                    d4 = d3; d3 = d2; d2 = dist;
                } else if (d3 >= dist) {
                    i4 = i3; i3 = idx;
                    d4 = d3; d3 = dist;
                } else if (d4 >= dist) {
                    i4 = idx;
                    d4 = dist;
                }
            }
        }
    }
    FluidStatus f1 = aquifer_status(a, i1);
    double s12 = similarity(d1, d2);
    uint8_t bs = fs_at(f1, y);
    if (s12 <= 0.0) {
        return bs;
    }
    if (bs == JB_WATER) {
        FluidStatus below = global_fluid(a, y - 1);
        if (fs_at(below, y - 1) == JB_LAVA) {
            return bs;
        }
    }
    double barrier = NAN;
    FluidStatus f2 = aquifer_status(a, i2);
    double e = s12 * pressure(a, &barrier, x, y, z, f1, f2);
    if (density + e > 0.0) {
        return -1;
    }
    FluidStatus f3 = aquifer_status(a, i3);
    double s13 = similarity(d1, d3);
    if (s13 > 0.0) {
        double f = s12 * s13 * pressure(a, &barrier, x, y, z, f1, f3);
        if (density + f > 0.0) {
            return -1;
        }
    }
    double s23 = similarity(d2, d3);
    if (s23 > 0.0) {
        double g = s12 * s23 * pressure(a, &barrier, x, y, z, f2, f3);
        if (density + g > 0.0) {
            return -1;
        }
    }
    (void)i4;
    (void)d4;
    (void)fs_eq;
    return bs;
}

/* OreVeinifier.create(...) rule. Returns JB_* or -1 for "null". */
static int vein_rule(JChunk *c, const JrPositional *ore, int x, int y, int z)
{
    double d = jchunk_eval_router(c, "vein_toggle", x, y, z, true);
    bool copper = d > 0.0;
    int max_y = copper ? 50 : -8;
    int min_y = copper ? 0 : -60;
    double e = fabs(d);
    int i = max_y - y;
    int j = y - min_y;
    if (j < 0 || i < 0) {
        return -1;
    }
    int k = i < j ? i : j;
    double f = mth_clamped_map((double)k, 0.0, 20.0, -0.2, 0.0);
    if (e + f < 0.4000000059604645) {
        return -1;
    }
    JrRandom rs = jr_pos_at(ore, x, y, z);
    if (jr_next_float(&rs) > 0.7f) {
        return -1;
    }
    if (jchunk_eval_router(c, "vein_ridged", x, y, z, true) >= 0.0) {
        return -1;
    }
    double g = mth_clamped_map(e, 0.4000000059604645, 0.6000000238418579, 0.10000000149011612,
                               0.30000001192092896);
    if ((double)jr_next_float(&rs) < g && jchunk_eval_router(c, "vein_gap", x, y, z, true) > -0.30000001192092896) {
        if (jr_next_float(&rs) < 0.02f) {
            return copper ? JB_RAW_COPPER_BLOCK : JB_RAW_IRON_BLOCK;
        }
        return copper ? JB_COPPER_ORE : JB_DEEPSLATE_IRON_ORE;
    }
    return copper ? JB_GRANITE : JB_TUFF;
}

static const char *const BLOCK_NAMES[JB_COUNT] = {
    "minecraft:air",
    "minecraft:stone",
    "minecraft:water",
    "minecraft:lava",
    "minecraft:copper_ore",
    "minecraft:raw_copper_block",
    "minecraft:granite",
    "minecraft:deepslate_iron_ore",
    "minecraft:raw_iron_block",
    "minecraft:tuff",
    "minecraft:bedrock",
    "minecraft:deepslate",
    "minecraft:calcite",
    "minecraft:coarse_dirt",
    "minecraft:dirt",
    "minecraft:grass_block",
    "minecraft:gravel",
    "minecraft:ice",
    "minecraft:mud",
    "minecraft:mycelium",
    "minecraft:orange_terracotta",
    "minecraft:packed_ice",
    "minecraft:podzol",
    "minecraft:powder_snow",
    "minecraft:red_sand",
    "minecraft:red_sandstone",
    "minecraft:sand",
    "minecraft:sandstone",
    "minecraft:snow_block",
    "minecraft:terracotta",
    "minecraft:white_terracotta",
    "minecraft:yellow_terracotta",
    "minecraft:brown_terracotta",
    "minecraft:red_terracotta",
    "minecraft:light_gray_terracotta",
};

/* Blocks beyond the fixed JFillBlock ids (ores, plants, ...) are registered on demand; ids stay
 * one byte. Only the block name is tracked (no block-state properties). */
#define MAX_BLOCK_IDS 256
static char *g_dyn_names[MAX_BLOCK_IDS];
static int g_block_count = JB_COUNT;

int jfill_block_count(void)
{
    return g_block_count;
}

const char *jfill_block_name(JFillBlock b)
{
    if (b >= 0 && b < JB_COUNT) {
        return BLOCK_NAMES[b];
    }
    if (b >= JB_COUNT && b < g_block_count) {
        return g_dyn_names[b];
    }
    return "minecraft:air";
}

int jfill_block_from_name(const char *name)
{
    for (int i = 0; i < g_block_count; ++i) {
        if (strcmp(jfill_block_name((JFillBlock)i), name) == 0) {
            return i;
        }
    }
    return -1;
}

int jfill_block_register(const char *name)
{
    int id = jfill_block_from_name(name);
    if (id >= 0 || g_block_count >= MAX_BLOCK_IDS) {
        return id;
    }
    size_t n = strlen(name) + 1;
    g_dyn_names[g_block_count] = (char *)malloc(n);
    if (g_dyn_names[g_block_count] == NULL) {
        return -1;
    }
    memcpy(g_dyn_names[g_block_count], name, n);
    return g_block_count++;
}

bool jblock_is_air(int b)
{
    return b == JB_AIR;
}

bool jblock_is_fluid(int b)
{
    return b == JB_WATER || b == JB_LAVA;
}

/* BlockState.blocksMotion: solid collision. Powder snow has no default collision. */
static bool g_no_collision[MAX_BLOCK_IDS];

void jblock_set_no_collision(int id)
{
    if (id >= 0 && id < MAX_BLOCK_IDS) {
        g_no_collision[id] = true;
    }
}

bool jblock_blocks_motion(int b)
{
    if (b >= 0 && b < MAX_BLOCK_IDS && g_no_collision[b]) {
        return false;
    }
    return !(b == JB_AIR || b == JB_WATER || b == JB_LAVA || b == JB_POWDER_SNOW);
}

void jheightmaps_prime(JHeightmaps *hm, const uint8_t *blocks)
{
    for (int lz = 0; lz < 16; ++lz) {
        for (int lx = 0; lx < 16; ++lx) {
            int ws = JFILL_MIN_Y, of = JFILL_MIN_Y;
            for (int y = JFILL_MIN_Y + JFILL_HEIGHT - 1; y >= JFILL_MIN_Y; --y) {
                int b = blocks[JFILL_INDEX(lx, lz, y)];
                if (ws == JFILL_MIN_Y && !jblock_is_air(b)) {
                    ws = y + 1;
                }
                if (of == JFILL_MIN_Y && jblock_blocks_motion(b)) {
                    of = y + 1;
                }
                if (ws != JFILL_MIN_Y && of != JFILL_MIN_Y) {
                    break;
                }
            }
            hm->world_surface[lz * 16 + lx] = ws;
            hm->ocean_floor[lz * 16 + lx] = of;
        }
    }
}

/* Heightmap.update for one type. `matches` is the type's block predicate. */
static void heightmap_update(int *cell, const uint8_t *blocks, int lx, int y, int lz, int state, bool (*matches)(int))
{
    int cur = *cell;
    if (y <= cur - 2) {
        return;
    }
    if (matches(state)) {
        if (y >= cur) {
            *cell = y + 1;
        }
    } else if (cur - 1 == y) {
        for (int yy = y - 1; yy >= JFILL_MIN_Y; --yy) {
            if (matches(blocks[JFILL_INDEX(lx, lz, yy)])) {
                *cell = yy + 1;
                return;
            }
        }
        *cell = JFILL_MIN_Y;
    }
}

static bool not_air(int b)
{
    return !jblock_is_air(b);
}

void jheightmaps_update(JHeightmaps *hm, const uint8_t *blocks, int lx, int y, int lz, int state)
{
    heightmap_update(&hm->ocean_floor[lz * 16 + lx], blocks, lx, y, lz, state, jblock_blocks_motion);
    heightmap_update(&hm->world_surface[lz * 16 + lx], blocks, lx, y, lz, state, not_air);
}

int jheightmaps_world_surface_height(const JHeightmaps *hm, int lx, int lz)
{
    return hm->world_surface[lz * 16 + lx] - 1;
}

int jheightmaps_ocean_floor_height(const JHeightmaps *hm, int lx, int lz)
{
    return hm->ocean_floor[lz * 16 + lx] - 1;
}

JAquifer *jaquifer_create(JChunk *chunk)
{
    return chunk != NULL ? aquifer_create(chunk) : NULL;
}

void jaquifer_free(JAquifer *a)
{
    aquifer_free(a);
}

int jaquifer_substance(JAquifer *a, int x, int y, int z, double density)
{
    return a != NULL ? aquifer_substance(a, x, y, z, density) : -1;
}

bool jchunk_fill(JChunk *c, uint8_t *out)
{
    if (c == NULL || out == NULL) {
        return false;
    }
    Aquifer *aq = aquifer_create(c);
    if (aq == NULL) {
        return false;
    }
    const JrPositional *ore = jrs_ore_random(jchunk_state(c));
    int minx = jchunk_min_x(c), minz = jchunk_min_z(c);
    for (int xx = 0; xx < 16; ++xx) {
        for (int zz = 0; zz < 16; ++zz) {
            for (int y = JFILL_MIN_Y + JFILL_HEIGHT - 1; y >= JFILL_MIN_Y; --y) {
                int x = minx + xx, z = minz + zz;
                double density = jchunk_final_density(c, x, y, z);
                int b = aquifer_substance(aq, x, y, z, density);
                if (b < 0) {
                    b = vein_rule(c, ore, x, y, z);
                }
                if (b < 0) {
                    b = JB_STONE; /* NoiseGeneratorSettings.defaultBlock */
                }
                out[JFILL_INDEX(xx, zz, y)] = (uint8_t)b;
            }
        }
    }
    aquifer_free(aq);
    return true;
}
