#include "world/worldgen_v4.h"
#include "core/noise.h"
#include "world/biome.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"
#include "world/world_gen.h"
#include <math.h>
#include <float.h>
#include <stdlib.h>

/* Every sample is anchored in world coordinates. Interpolate individual
 * signed noise fields BEFORE abs/min, otherwise thin intersecting tunnels
 * disappear between lattice nodes. Grid spacing is part of profile 4. */
#define GRID 4
#define NY (CHUNK_Y / GRID + 1)
typedef struct Fields {
    float shape, cheese, sa, sb, na, nb, gate, vein, va, vb, rich;
} Fields;
typedef struct AquiferColumn {
    int level;
    bool wet, lava;
    float boundary;
    int other_level;
    bool other_wet, other_lava;
} AquiferColumn;
static float clamp(float v, float a, float b)
{
    return fminf(b, fmaxf(a, v));
}
static float smooth(float a, float b, float v)
{
    float t = clamp((v - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
static float mix(float a, float b, float t)
{
    return a + (b - a) * t;
}
static int divfloor(int v, int d)
{
    int q = v / d;
    return q - ((v % d) < 0);
}
static float n2(long seed, int x, int z, float scale, uint32_t salt, int oct)
{
    return noise_fbm2((float)x * scale, (float)z * scale, oct, 2.0f, 0.5f, (uint32_t)seed ^ salt);
}

GenClimate worldgen_climate(long seed, int x, int z)
{
    GenClimate c;
    c.continentalness = n2(seed, x, z, .00075f, 0xC001u, 3);
    c.erosion = n2(seed, x, z, .0018f, 0xE001u, 3);
    c.temperature = n2(seed, x, z, .0011f, 0x7101u, 3);
    c.humidity = n2(seed, x, z, .0013f, 0x8101u, 3);
    c.weirdness = n2(seed, x, z, .0025f, 0x9101u, 3);
    /* Broad shelf -> coast -> inland spline. Smooth cubic segments keep
     * the shoreline continuous without flattening the spawn neighborhood. */
    float k = c.continentalness;
    float base;
    if (k < -.35f)
        base = mix(24, 44, smooth(-.8f, -.35f, k));
    else if (k < -.10f)
        base = mix(44, 66, smooth(-.35f, -.10f, k));
    else
        base = mix(66, 104, smooth(-.10f, .65f, k));
    c.mountain = smooth(-.06f, .35f, k) * (1 - smooth(-.35f, .35f, c.erosion));
    float ridge = 1 - fabsf(c.weirdness);
    float peaks = smooth(.35f, .95f, ridge);
    float regional = n2(seed, x, z, .006f, 0xB001u, 3);
    float local = n2(seed, x, z, .035f, 0xB002u, 2);
    c.height = base + c.mountain * (18 + 91 * peaks) + regional * (5 + 10 * c.mountain) + local * 1.8f;
    /* Valleys follow a continuous independent field, not chunk lotteries. */
    int vx = x + (int)(n2(seed, x, z, .0008f, 0xA002u, 2) * 120);
    int vz = z + (int)(n2(seed, x, z, .0008f, 0xA003u, 2) * 120);
    float valley = fabsf(n2(seed, vx, vz, .0019f, 0xA001u, 2));
    c.river = (1 - smooth(.015f, .12f, valley)) * smooth(-.10f, .05f, k) * (1 - c.mountain * .95f);
    c.height = mix(c.height, 60 + local * .6f, c.river);
    c.height = clamp(c.height, 8, 225);
    if (k < -.12f && c.height < 64)
        c.biome = BIOME_OCEAN;
    else if (c.height < 67 && c.height > 60 && k < .05f)
        c.biome = BIOME_BEACH;
    else if (c.mountain > .42f && c.height > 112)
        c.biome = BIOME_MOUNTAINS;
    else if (c.temperature < -.25f)
        c.biome = BIOME_SNOW;
    else if (c.temperature > .20f && c.humidity < -.12f)
        c.biome = BIOME_DESERT;
    else if (c.humidity > .08f)
        c.biome = BIOME_FOREST;
    else
        c.biome = BIOME_PLAINS;
    return c;
}

static Fields fields(long seed, int x, int y, int z)
{
    Fields f;
    uint32_t s = (uint32_t)seed;
#define N(SX, SY, SALT) noise_value3((float)x *(SX), (float)y *(SY), (float)z *(SX), s ^ (SALT))
    f.shape = N(.018f, .025f, 0xD001u);
    f.cheese = N(.022f, .032f, 0xD002u);
    f.sa = N(.035f, .045f, 0xD003u);
    f.sb = N(.035f, .045f, 0xD004u);
    f.na = N(.072f, .085f, 0xD005u);
    f.nb = N(.072f, .085f, 0xD006u);
    f.gate = N(.009f, .016f, 0xD007u);
    f.vein = N(.012f, .018f, 0xD008u);
    f.va = N(.040f, .045f, 0xD009u);
    f.vb = N(.040f, .045f, 0xD00Au);
    f.rich = N(.080f, .080f, 0xD00Bu);
#undef N
    return f;
}
static Fields interpolate(const Fields *f, int stride_x, int stride_z, int stride_y, float tx, float ty, float tz)
{
    Fields out;
    /* Explicit members avoid treating a struct as an array (undefined C). */
#define LERP(M)                                                                                                        \
    out.M = mix(mix(mix(f[0].M, f[stride_x].M, tx), mix(f[stride_z].M, f[stride_z + stride_x].M, tx), tz),             \
                mix(mix(f[stride_y].M, f[stride_y + stride_x].M, tx),                                                  \
                    mix(f[stride_y + stride_z].M, f[stride_y + stride_z + stride_x].M, tx), tz),                       \
                ty)
    LERP(shape);
    LERP(cheese);
    LERP(sa);
    LERP(sb);
    LERP(na);
    LERP(nb);
    LERP(gate);
    LERP(vein);
    LERP(va);
    LERP(vb);
    LERP(rich);
#undef LERP
    return out;
}
static Fields sample(long seed, int x, int y, int z)
{
    int ax = divfloor(x, GRID) * GRID, ay = divfloor(y, GRID) * GRID, az = divfloor(z, GRID) * GRID;
    Fields f[8];
    for (int iy = 0; iy < 2; ++iy)
        for (int iz = 0; iz < 2; ++iz)
            for (int ix = 0; ix < 2; ++ix)
                f[iy * 4 + iz * 2 + ix] = fields(seed, ax + ix * GRID, ay + iy * GRID, az + iz * GRID);
    return interpolate(f, 1, 2, 4, (float)(x - ax) / GRID, (float)(y - ay) / GRID, (float)(z - az) / GRID);
}
static GenDensity combine(GenClimate c, Fields f, int y)
{
    GenDensity d;
    float depth = c.height - (float)y;
    d.terrain = depth / 12 + f.shape * (.32f + c.mountain * 1.1f) * smooth(-12, 20, depth);
    /* Chamber size grows with depth; near-surface cheese is suppressed,
     * but spaghetti intersections may cut natural entrances. */
    float opening = smooth(0, 28, depth);
    d.cheese = (.66f - .18f * opening) - f.cheese;
    d.spaghetti = fmaxf(fabsf(f.sa), fabsf(f.sb)) - (.085f + .045f * smooth(-.4f, .4f, f.gate));
    d.noodle = fmaxf(fabsf(f.na), fabsf(f.nb)) - .065f;
    float cave = fminf(d.cheese, fminf(d.spaghetti, d.noodle));
    if (f.gate < -.35f)
        cave = fminf(d.cheese, d.spaghetti); /* Sparse noodle regions. */
    /* Bottom fade retains a solid foundation. Top gate permits entrances
     * while leaving above-ground air controlled by terrain density. */
    cave = fmaxf(cave, (3.0f - y) * .15f);
    d.final_density = fminf(d.terrain, cave * 12);
    d.vein = f.vein;
    d.vein_ridge = fmaxf(fabsf(f.va), fabsf(f.vb));
    d.richness = clamp(fabsf(f.vein), .4f, .6f) - .3f;
    d.filler_gap = -.3f - f.rich;
    return d;
}
GenDensity worldgen_density(long seed, int x, int y, int z)
{
    return combine(worldgen_climate(seed, x, z), sample(seed, x, y, z), y);
}

/* Nearest jittered local aquifer centers. Different levels/dry cells are
 * separated by pressure walls; no globally connected underground sea. */
static AquiferColumn aquifer_column(long seed, int x, int z)
{
    int cx = divfloor(x, 48), cz = divfloor(z, 48);
    float best = FLT_MAX, next = FLT_MAX;
    int bx = 0, bz = 0, nx = 0, nz = 0;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            int gx = cx + dx, gz = cz + dz;
            uint32_t h = noise_hash2(gx, gz, (uint32_t)seed ^ 0xAF01u);
            int px = gx * 48 + 8 + (int)(h % 32), pz = gz * 48 + 8 + (int)((h >> 8) % 32);
            float dist = (float)((x - px) * (x - px) + (z - pz) * (z - pz));
            if (dist < best) {
                next = best;
                nx = bx;
                nz = bz;
                best = dist;
                bx = px;
                bz = pz;
            } else if (dist < next) {
                next = dist;
                nx = px;
                nz = pz;
            }
        }
    AquiferColumn a;
    a.level = 18 + (int)(smooth(-.6f, .6f, n2(seed, bx, bz, .011f, 0xAF02u, 2)) * 32);
    a.other_level = 18 + (int)(smooth(-.6f, .6f, n2(seed, nx, nz, .011f, 0xAF02u, 2)) * 32);
    a.wet = n2(seed, bx, bz, .008f, 0xAF03u, 2) > .02f;
    a.other_wet = n2(seed, nx, nz, .008f, 0xAF03u, 2) > .02f;
    a.lava = n2(seed, bx, bz, .015f, 0xAF04u, 1) > .35f;
    a.other_lava = n2(seed, nx, nz, .015f, 0xAF04u, 1) > .35f;
    a.boundary = sqrtf(next) - sqrtf(best);
    return a;
}
static GenAquifer aquifer_at(AquiferColumn a, int y, GenClimate c)
{
    GenAquifer out;
    out.level = a.level;
    out.wet = a.wet && y < c.height - 12;
    out.lava = y <= 7 && a.lava;
    bool different = a.wet != a.other_wet || a.level != a.other_level || a.lava != a.other_lava;
    out.barrier =
        different && a.boundary < 2.5f && y <= fmaxf((float)a.level, (float)a.other_level) + 1 && y < c.height - 10;
    return out;
}
GenAquifer worldgen_aquifer(long seed, int x, int y, int z)
{
    return aquifer_at(aquifer_column(seed, x, z), y, worldgen_climate(seed, x, z));
}
static uint16_t base_block(GenClimate c, GenDensity d, GenAquifer a, int y)
{
    if (y == 0)
        return BLOCK_BEDROCK;
    if (d.final_density > 0 || (d.terrain > 0 && a.barrier))
        return y < 32 ? BLOCK_DEEPSLATE : BLOCK_STONE;
    if (d.terrain <= 0 && y <= WORLD_SEA_LEVEL && (c.biome == BIOME_OCEAN || c.height < 64 || c.river > .6f))
        return BLOCK_WATER;
    if (d.terrain > 0) {
        if (a.lava)
            return BLOCK_LAVA;
        if (a.wet && y <= a.level)
            return BLOCK_WATER;
    }
    return BLOCK_AIR;
}
uint16_t worldgen_base_block(long seed, int x, int y, int z)
{
    if (y < 0 || y >= CHUNK_Y)
        return BLOCK_AIR;
    GenClimate c = worldgen_climate(seed, x, z);
    return base_block(c, combine(c, sample(seed, x, y, z), y), aquifer_at(aquifer_column(seed, x, z), y, c), y);
}
int worldgen_height(long seed, int x, int z)
{
    GenClimate c = worldgen_climate(seed, x, z);
    AquiferColumn aq = aquifer_column(seed, x, z);
    int ax = divfloor(x, GRID) * GRID, az = divfloor(z, GRID) * GRID, last = -1;
    Fields f[8];
    for (int y = (int)ceilf(c.height) + 18; y > 0; --y) {
        int ay = divfloor(y, GRID) * GRID;
        if (ay != last) {
            for (int iy = 0; iy < 2; ++iy)
                for (int iz = 0; iz < 2; ++iz)
                    for (int ix = 0; ix < 2; ++ix)
                        f[iy * 4 + iz * 2 + ix] = fields(seed, ax + ix * GRID, ay + iy * GRID, az + iz * GRID);
            last = ay;
        }
        Fields v = interpolate(f, 1, 2, 4, (float)(x - ax) / GRID, (float)(y - ay) / GRID, (float)(z - az) / GRID);
        if (block_is_solid(base_block(c, combine(c, v, y), aquifer_at(aq, y, c), y)))
            return y;
    }
    return 0;
}
uint16_t worldgen_surface(long seed, int x, int z, int h)
{
    GenClimate c = worldgen_climate(seed, x, z);
    float slope = fmaxf(fabsf(c.height - worldgen_climate(seed, x + 2, z).height),
                        fabsf(c.height - worldgen_climate(seed, x, z + 2).height)) *
                  .5f;
    if (h < 60)
        return c.humidity > .0f ? BLOCK_GRAVEL : BLOCK_SAND;
    if (c.biome == BIOME_DESERT || c.biome == BIOME_BEACH || h <= 65)
        return BLOCK_SAND;
    if (c.temperature - (float)(h - 64) * .004f < -.30f)
        return BLOCK_SNOW;
    if (slope > 1.8f || (c.biome == BIOME_MOUNTAINS && h > 145))
        return BLOCK_STONE;
    return BLOCK_GRASS;
}

/* Ores: deterministic feature anchors, height providers, branched swept
 * paths with a bounded block budget and exposed-air rejection. Anchors include a neighbor halo,
 * so clusters crossing chunk boundaries are independent of load order. */
typedef struct OreRule {
    uint16_t block;
    int low, high, count, size;
    bool triangle;
    float discard;
    int biome;
    int rarity; /* 0 = always; otherwise one feature per N anchor regions. */
} OreRule;
static const OreRule ores[] = {{BLOCK_COAL_ORE, 0, 192, 20, 17, true, .5f, -1},
                               {BLOCK_COAL_ORE, 136, 320, 30, 17, false, 0, -1},
                               {BLOCK_IRON_ORE, -24, 56, 10, 9, true, 0, -1},
                               {BLOCK_IRON_ORE, 80, 384, 90, 9, true, 0, -1},
                               {BLOCK_IRON_ORE, -64, 72, 10, 4, false, 0, -1},
                               {BLOCK_COPPER_ORE, -16, 112, 16, 10, true, 0, -1},
                               {BLOCK_GOLD_ORE, -64, 32, 4, 9, true, .5f, -1},
                               {BLOCK_GOLD_ORE, -64, -48, -1, 9, false, .5f, -1},
                               {BLOCK_LAPIS_ORE, -32, 32, 2, 7, true, 0, -1},
                               {BLOCK_LAPIS_ORE, -64, 64, 4, 7, false, 1, -1},
                               {BLOCK_REDSTONE_ORE, -64, 15, 4, 8, false, 0, -1},
                               {BLOCK_REDSTONE_ORE, -96, -32, 8, 8, true, 0, -1},
                               {BLOCK_DIAMOND_ORE, -144, 16, 7, 4, true, .5f, -1},
                               {BLOCK_DIAMOND_ORE, -64, -4, 2, 8, false, .5f, -1},
                               {BLOCK_DIAMOND_ORE, -144, 16, 4, 8, true, 1, -1},
                               {BLOCK_EMERALD_ORE, -16, 480, 100, 3, true, 0, BIOME_MOUNTAINS},
                               {BLOCK_DIAMOND_ORE, -144, 16, 1, 12, true, .7f, -1, 9}};
static float random_unit(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return noise_hash_to_unit(*state);
}
static int java_to_y(float y)
{
    return (int)floorf(y < 63 ? 64 + (y - 63) * .5f : y + 1);
}
static bool rock(uint16_t id)
{
    return id == BLOCK_STONE || id == BLOCK_DEEPSLATE;
}
static void stamp_ores(Chunk *c, long seed)
{
    for (size_t r = 0; r < sizeof(ores) / sizeof(ores[0]); ++r) {
        OreRule rule = ores[r];
        for (int az = c->cz - 1; az <= c->cz + 1; ++az)
            for (int ax = c->cx - 1; ax <= c->cx + 1; ++ax) {
                uint32_t rng = noise_hash3(ax, (int)r, az, (uint32_t)seed ^ 0x0AE1u);
                if (rule.rarity > 0 && rng % (uint32_t)rule.rarity != 0)
                    continue;
                int attempts = rule.count < 0 ? (int)(rng & 1u) : rule.count;
                for (int attempt = 0; attempt < attempts; ++attempt) {
                    float px = ax * 16 + random_unit(&rng) * 16, pz = az * 16 + random_unit(&rng) * 16;
                    float u = random_unit(&rng);
                    if (rule.triangle)
                        u = (u + random_unit(&rng)) * .5f;
                    int y = java_to_y(mix((float)rule.low, (float)rule.high, u));
                    float dx = random_unit(&rng) * 2 - 1, dz = random_unit(&rng) * 2 - 1;
                    float dy = (random_unit(&rng) * 2 - 1) * .4f;
                    uint32_t path_seed = rng;
                    if (y < 2 || y >= CHUNK_Y - 2)
                        continue;
                    if (rule.biome >= 0 && worldgen_climate(seed, (int)px, (int)pz).biome != rule.biome)
                        continue;
                    /* A fixed path seed per anchor avoids chunk-local RNG consumption. */
                    int root_x = (int)floorf(px), root_z = (int)floorf(pz);
                    int cursor_x = root_x, cursor_y = y, cursor_z = root_z;
                    for (int step = 0; step < rule.size; ++step) {
                        int wx = cursor_x, wz = cursor_z, wy = cursor_y;
                        /* One deposition per path step: feature size is a hard
                         * upper bound, not a radius multiplier that produces
                         * dozens of diamonds from a four-block feature. */
                        uint32_t branch = noise_hash3(step, attempt, (int)r, path_seed);
                        int ox = 0, oz = 0, oy = 0;
                        {
                            int lx = wx + ox - c->cx * 16, lz = wz + oz - c->cz * 16, by = wy + oy;
                            bool place = chunk_in_bounds(lx, by, lz) && rock(chunk_get_block(c, lx, by, lz));
                            uint32_t h = noise_hash3(wx + ox, by, wz + oz, path_seed);
                            if (place && noise_hash_to_unit(h) < rule.discard) {
                                static const int dirs[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                                               {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
                                bool air = false;
                                for (int i = 0; i < 6; ++i) {
                                    int nx = lx + dirs[i][0], ny = by + dirs[i][1], nz = lz + dirs[i][2];
                                    uint16_t id =
                                        chunk_in_bounds(nx, ny, nz)
                                            ? chunk_get_block(c, nx, ny, nz)
                                            : worldgen_base_block(seed, wx + ox + dirs[i][0], ny, wz + oz + dirs[i][2]);
                                    if (id == BLOCK_AIR)
                                        air = true;
                                }
                                if (air)
                                    place = false;
                            }
                            if (place)
                                chunk_set_block(c, lx, by, lz, rule.block);
                        }
                        /* Branches return to the already deposited root.
                         * Each branch advances by one face, keeping a small
                         * buried cluster connected rather than scattering
                         * unrelated voxels inside a bounding box. */
                        if (step % 4 == 3) {
                            cursor_x = root_x;
                            cursor_y = y;
                            cursor_z = root_z;
                        } else
                            switch (branch % 4) {
                            case 0:
                                cursor_x += dx >= 0 ? 1 : -1;
                                break;
                            case 1:
                                cursor_z += dz >= 0 ? 1 : -1;
                                break;
                            case 2:
                                cursor_y += dy >= 0 ? 1 : -1;
                                break;
                            default:
                                cursor_x += dx >= 0 ? -1 : 1;
                                break;
                            }
                    }
                }
            }
    }
}
void worldgen_fill(Chunk *c, long seed)
{
    Fields lattice[NY * 5 * 5];
    for (int iy = 0; iy < NY; ++iy)
        for (int iz = 0; iz < 5; ++iz)
            for (int ix = 0; ix < 5; ++ix)
                lattice[iy * 25 + iz * 5 + ix] =
                    fields(seed, c->cx * 16 + ix * GRID, iy * GRID, c->cz * 16 + iz * GRID);
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            int wx = c->cx * 16 + x, wz = c->cz * 16 + z;
            GenClimate climate = worldgen_climate(seed, wx, wz);
            AquiferColumn aq = aquifer_column(seed, wx, wz);
            int top = -1;
            for (int y = 0; y < CHUNK_Y; ++y) {
                Fields f = interpolate(&lattice[(y / GRID) * 25 + (z / GRID) * 5 + x / GRID], 1, 5, 25,
                                       (float)(x % GRID) / GRID, (float)(y % GRID) / GRID, (float)(z % GRID) / GRID);
                GenDensity d = combine(climate, f, y);
                uint16_t id = base_block(climate, d, aquifer_at(aq, y, climate), y);
                /* Richness and filler gaps make veins sparse mineral networks,
                 * not a solid tube of ore. Deep iron / shallow copper bands. */
                float java_y = y < 64 ? (float)((y - 64) * 2 + 63) : (float)(y - 1);
                bool iron = d.vein < 0;
                float vein_low = iron ? -60.0f : 0.0f, vein_high = iron ? -8.0f : 50.0f;
                float edge = clamp(fminf(java_y - vein_low, vein_high - java_y), 0, 20);
                if (rock(id) && d.vein_ridge < .08f && fabsf(d.vein) >= .6f - edge * .01f && java_y >= vein_low &&
                    java_y < vein_high && noise_hash_to_unit(noise_hash3(wx, y, wz, (uint32_t)seed ^ 0xAE02u)) < .7f) {
                    id = iron ? BLOCK_TUFF : BLOCK_GRANITE;
                    if (d.filler_gap <= 0 &&
                        noise_hash_to_unit(noise_hash3(wx, y, wz, (uint32_t)seed ^ 0xAE03u)) < d.richness) {
                        id = iron ? BLOCK_IRON_ORE : BLOCK_COPPER_ORE;
                        if (noise_hash_to_unit(noise_hash3(wx, y, wz, (uint32_t)seed ^ 0xAE04u)) < .02f)
                            id = iron ? BLOCK_RAW_IRON : BLOCK_RAW_COPPER;
                    }
                }
                chunk_set_block(c, x, y, z, id);
                if (block_is_solid(id))
                    top = y;
            }
            if (top > 0) {
                uint16_t surface = worldgen_surface(seed, wx, wz, top);
                /* Only sky-facing terrain receives soil. No dirt shell inside caves. */
                chunk_set_block(c, x, top, z, surface);
                for (int depth = 1; depth <= 4 && top - depth > 0; ++depth) {
                    if (!rock(chunk_get_block(c, x, top - depth, z)))
                        break;
                    uint16_t sub = surface == BLOCK_SAND ? (depth < 3 ? BLOCK_SAND : BLOCK_SANDSTONE)
                                   : surface == BLOCK_GRASS || surface == BLOCK_SNOW ? BLOCK_DIRT
                                                                                     : surface;
                    chunk_set_block(c, x, top - depth, z, sub);
                }
            }
        }
    stamp_ores(c, seed);
}

static bool footing(uint16_t id)
{
    return id == BLOCK_GRASS || id == BLOCK_DIRT || id == BLOCK_STONE || id == BLOCK_SNOW || id == BLOCK_SANDSTONE;
}
bool worldgen_spawn_valid(const World *w, int x, int z, Vec3 *out, float *score)
{
    if (!w || !world_get_chunk(w, divfloor(x, 16), divfloor(z, 16)))
        return false;
    int top = -1;
    for (int y = CHUNK_Y - 1; y >= 0; --y) {
        uint16_t id = world_get_block(w, x, y, z);
        if (id == BLOCK_AIR || id == BLOCK_GRASS_PLANT || id == BLOCK_FLOWER)
            continue;
        if (!footing(id) || y < 66 || y > 190)
            return false;
        top = y;
        break;
    }
    if (top < 0)
        return false;
    int min = top, max = top;
    /* Full 5x5 surface neighborhood, dry three-block standing volume and
     * sky visibility. This exceeds the player's .6 x 1.8 collision box. */
    for (int dz = -2; dz <= 2; ++dz)
        for (int dx = -2; dx <= 2; ++dx) {
            if (!world_get_chunk(w, divfloor(x + dx, 16), divfloor(z + dz, 16)))
                return false;
            int h = -1;
            for (int y = top + 3; y >= top - 2; --y) {
                uint16_t id = world_get_block(w, x + dx, y, z + dz);
                if (id == BLOCK_WATER || id == BLOCK_LAVA || id == BLOCK_LEAVES || id == BLOCK_WOOD)
                    return false;
                if (block_is_solid(id)) {
                    h = y;
                    break;
                }
            }
            if (h < top - 1 || h > top + 1 || !footing(world_get_block(w, x + dx, h, z + dz)))
                return false;
            if (h < min)
                min = h;
            if (h > max)
                max = h;
            for (int y = h + 1; y < CHUNK_Y; ++y)
                if (block_is_solid(world_get_block(w, x + dx, y, z + dz)) ||
                    block_is_water(world_get_block(w, x + dx, y, z + dz)) ||
                    world_get_block(w, x + dx, y, z + dz) == BLOCK_LAVA)
                    return false;
            /* Thin floors over a cavern are unsafe at spawn. */
            if (!block_is_solid(world_get_block(w, x + dx, h - 1, z + dz)) ||
                !block_is_solid(world_get_block(w, x + dx, h - 2, z + dz)))
                return false;
        }
    if (out)
        *out = mmath_vec3((float)x + .5f, (float)top + 1, (float)z + .5f);
    if (score)
        *score = 100 - (max - min) * 12 - fabsf((float)top - 80) * .3f;
    return true;
}
int worldgen_find_spawn_with_budget(World *w, Vec3 *out, int rings)
{
    if (!w || !out)
        return -1;
    /* Seed-dependent origin, then deterministic rings. Screening estimates
     * avoids generating entire oceans. Acceptance always uses real blocks. */
    uint32_t h = noise_hash2(0, 0, (uint32_t)w->seed ^ 0x5FA1u);
    int origin_x = (int)(h % 17) - 8, origin_z = (int)((h >> 8) % 17) - 8;
    if (rings < 0)
        rings = 0;
    if (rings > 128)
        rings = 128;
    for (int ring = 0; ring <= rings; ++ring) {
        for (int dz = -ring; dz <= ring; ++dz)
            for (int dx = -ring; dx <= ring; ++dx) {
                if (abs(dx) != ring && abs(dz) != ring)
                    continue;
                int cx = origin_x + dx * 4, cz = origin_z + dz * 4, x = cx * 16 + 8, z = cz * 16 + 8;
                GenClimate climate = worldgen_climate(w->seed, x, z);
                if (climate.height < 68 || climate.height > 145 || climate.mountain > .6f ||
                    climate.biome == BIOME_DESERT)
                    continue;
                int added_x[9], added_z[9], count = 0;
                bool ready = true;
                for (int oz = -1; oz <= 1; ++oz)
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (!world_get_chunk(w, cx + ox, cz + oz)) {
                            if (world_generate_chunk(w, cx + ox, cz + oz) != 0)
                                ready = false;
                            else {
                                added_x[count] = cx + ox;
                                added_z[count++] = cz + oz;
                            }
                        }
                    }
                if (!ready) {
                    for (int i = 0; i < count; ++i)
                        world_remove_chunk(w, added_x[i], added_z[i]);
                    return -2;
                }
                Vec3 best = mmath_vec3(0, 0, 0);
                float best_score = -FLT_MAX;
                if (ready)
                    for (int pz = 2; pz <= 13; pz += 2)
                        for (int px = 2; px <= 13; px += 2) {
                            Vec3 pos;
                            float score;
                            if (worldgen_spawn_valid(w, cx * 16 + px, cz * 16 + pz, &pos, &score) &&
                                score > best_score) {
                                best = pos;
                                best_score = score;
                            }
                        }
                if (best_score > -FLT_MAX) {
                    *out = best;
                    return 0;
                }
                for (int i = 0; i < count; ++i)
                    world_remove_chunk(w, added_x[i], added_z[i]);
            }
    }
    /* Bounded last resort, deliberately separate from normal generation.
     * A persisted rescue clearing prevents a failed search from placing a
     * player underwater or in midair. Return 1 makes its use measurable. */
    int x = origin_x * 16 + 8, z = origin_z * 16 + 8;
    int cx = divfloor(x, 16), cz = divfloor(z, 16);
    if (!world_get_chunk(w, cx, cz) && world_generate_chunk(w, cx, cz) != 0)
        return -2;
    int y = worldgen_height(w->seed, x, z);
    if (y < 70)
        y = 70;
    if (y > 190)
        y = 190;
    for (int dz = -3; dz <= 3; ++dz)
        for (int dx = -3; dx <= 3; ++dx) {
            for (int by = 1; by < CHUNK_Y; ++by)
                world_set_block(w, x + dx, by, z + dz, by > y ? BLOCK_AIR : by == y ? BLOCK_GRASS : BLOCK_STONE);
        }
    *out = mmath_vec3((float)x + .5f, (float)y + 1, (float)z + .5f);
    return 1;
}
int worldgen_find_spawn(World *w, Vec3 *out)
{
    return worldgen_find_spawn_with_budget(w, out, 128);
}
