#include "world/world_gen.h"
#include "core/log.h"
#include "core/noise.h"
#include "world/biome.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/vgen.h"
#include "world/world.h"
#include "world/world_save.h"
#include "world/worldgen_v4.h"
#include "world/worldgen_v5.h"

#include <math.h>
#include <stdlib.h>

/* Floor division for negative-friendly cell math. */
static int floor_div(int v, int d)
{
    if (d <= 0) {
        return 0;
    }
    if (v >= 0) {
        return v / d;
    }
    return -((-v + d - 1) / d);
}

static float gen_clamp01(float v)
{
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

static float gen_smoothstep(float edge0, float edge1, float x)
{
    float t = gen_clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

/* Version 1 column height (M5): continental base + mountain lift + hills + detail.
 * Base 70 keeps most land above the
 * sea (64) with real ocean basins where the continent field dips; mountains spike via the squared mask. Typical range
 * roughly [25..160]; clamped to [4..200] (test-pinned).
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *
 * Returns: clamped column height.
 */
static int world_gen_height_v1(long seed, int wx, int wz)
{
    uint32_t s = (uint32_t)seed;
    float fx = (float)wx;
    float fz = (float)wz;
    float cont = noise_fbm2(fx * 0.0035f, fz * 0.0035f, 4, 2.0f, 0.5f, s);
    float m = biome_mountains(seed, wx, wz);
    float hills = noise_fbm2(fx * 0.020f + 300.0f, fz * 0.020f - 300.0f, 3, 2.0f, 0.5f, s ^ 0x33aa55ccu);
    float detail = noise_value2(fx * 0.080f, fz * 0.080f, s ^ 0x51ab3f29u);
    float h = 70.0f + cont * 24.0f + m * m * 70.0f + hills * 7.0f + detail * 2.0f;
    int hi = (int)h;
    if (hi < 4) {
        hi = 4;
    }
    if (hi > 200) {
        hi = 200;
    }
    return hi;
}

/* Version 2 terrain: warp the continent field before sampling it, build
 * broad connected highlands around sharper
 * ridges, then carve meandering
 * lowlands. This remains a deterministic 2D surface model; it does not
 * claim to
 * reproduce Minecraft's full 3D density router. */
static int world_gen_height_v2(long seed, int wx, int wz)
{
    uint32_t s = (uint32_t)seed;
    float fx = (float)wx;
    float fz = (float)wz;
    float warp_x = noise_fbm2(fx * 0.0014f, fz * 0.0014f, 3, 2.0f, 0.5f, s ^ 0x57415250u) * 104.0f;
    float warp_z = noise_fbm2(fx * 0.0014f + 731.0f, fz * 0.0014f - 419.0f, 3, 2.0f, 0.5f, s ^ 0x57415251u) * 104.0f;
    float x = fx + warp_x;
    float z = fz + warp_z;
    float continent = noise_fbm2(x * 0.0017f, z * 0.0017f, 5, 2.0f, 0.5f, s ^ 0x434F4E54u);
    float broad_hills = noise_fbm2(x * 0.0065f, z * 0.0065f, 4, 2.0f, 0.5f, s ^ 0x48494C4Cu) * 8.0f;
    float rolling = noise_fbm2(x * 0.018f + 93.0f, z * 0.018f - 157.0f, 3, 2.0f, 0.5f, s ^ 0x524F4C4Cu) * 4.0f;
    float ridge_noise = noise_fbm2(x * 0.0044f - 127.0f, z * 0.0044f + 311.0f, 4, 2.0f, 0.5f, s ^ 0x52494447u);
    float ridge = 1.0f - fabsf(ridge_noise);
    float mountain_mask = gen_smoothstep(0.58f, 0.82f, ridge);
    float peak_noise = noise_fbm2(x * 0.012f + 17.0f, z * 0.012f - 83.0f, 3, 2.0f, 0.5f, s ^ 0x5045414Bu);
    float sharp_peaks = 1.0f - fabsf(peak_noise);
    float base = 66.0f + continent * 31.0f + broad_hills + rolling + mountain_mask * (16.0f + sharp_peaks * 43.0f);

    /* A warped zero-contour becomes a broad river network. Carve more
     * strongly through raised ground and taper
     * out in deep ocean basins. */
    float river_field = noise_fbm2(x * 0.00145f + 811.0f, z * 0.00145f + 227.0f, 3, 2.0f, 0.5f, s ^ 0x52495652u);
    float river = 1.0f - gen_smoothstep(0.018f, 0.105f, fabsf(river_field));
    float inland = gen_smoothstep(58.0f, 74.0f, base);
    float river_depth = 7.0f + gen_clamp01((base - 66.0f) / 55.0f) * 11.0f;
    float h = base - river * inland * river_depth;
    h += noise_value2(x * 0.055f, z * 0.055f, s ^ 0x44455441u) * 1.4f;
    int hi = (int)floorf(h);
    if (hi < 4) {
        hi = 4;
    }
    if (hi > 200) {
        hi = 200;
    }
    return hi;
}

/* Version 3 terrain borrows the large-scale structure of modern voxel
 * terrain: broad continents, low-erosion
 * mountain belts, sharper ridges,
 * and river valleys that actually reach the world water table. The block
 * world is
 * still stored as 16x256x16 chunks, so this profile keeps a surface
 * height field and uses the existing 3D cave
 * carvers below it. */
static int world_gen_height_v3(long seed, int wx, int wz)
{
    uint32_t s = (uint32_t)seed;
    float fx = (float)wx;
    float fz = (float)wz;

    /* Low-frequency domain warp keeps continents and ranges irregular over
     * many chunks without introducing
     * per-chunk seams. */
    float warp_x = noise_fbm2(fx * 0.0011f, fz * 0.0011f, 4, 2.0f, 0.5f, s ^ 0x33574158u) * 156.0f;
    float warp_z = noise_fbm2(fx * 0.0011f + 617.0f, fz * 0.0011f - 283.0f, 4, 2.0f, 0.5f, s ^ 0x3357415au) * 156.0f;
    float x = fx + warp_x;
    float z = fz + warp_z;

    float continents = noise_fbm2(x * 0.00105f, z * 0.00105f, 5, 2.0f, 0.5f, s ^ 0x33434f4eu);
    float hills = noise_fbm2(x * 0.0048f, z * 0.0048f, 4, 2.0f, 0.5f, s ^ 0x3348494cu);
    float erosion = noise_fbm2(x * 0.010f + 281.0f, z * 0.010f - 97.0f, 3, 2.0f, 0.5f, s ^ 0x3345524fu);
    float base = 71.0f + continents * 29.0f + hills * 12.0f + erosion * 5.0f;

    /* Broad mountain systems are masked separately from the ridged detail:
     * this yields connected ranges with
     * quieter foothills between them. */
    float range_field = noise_fbm2(x * 0.00165f - 433.0f, z * 0.00165f + 191.0f, 4, 2.0f, 0.5f, s ^ 0x3352414eu);
    float range_mask = gen_smoothstep(0.22f, 0.76f, range_field);
    float ridge_noise = noise_fbm2(x * 0.0037f + 61.0f, z * 0.0037f - 347.0f, 4, 2.0f, 0.5f, s ^ 0x33524944u);
    float ridge = 1.0f - fabsf(ridge_noise);
    float sharp = 1.0f - fabsf(noise_fbm2(x * 0.010f - 19.0f, z * 0.010f + 89.0f, 3, 2.0f, 0.5f, s ^ 0x33504541u));
    float ruggedness = 1.0f - gen_smoothstep(0.15f, 0.82f, erosion);
    float mountains = range_mask * ruggedness * (19.0f + gen_smoothstep(0.42f, 0.90f, ridge) * 57.0f + sharp * 18.0f);
    float h = base + mountains;

    /* The warped channel network is cut down to the water level near its
     * centerline. This produces actual
     * connected lowland water when the
     * normal sea fill runs, instead of valleys that always remain dry. */
    float river_noise = noise_fbm2(x * 0.00155f + 811.0f, z * 0.00155f + 227.0f, 4, 2.0f, 0.5f, s ^ 0x33524956u);
    float river_distance = fabsf(river_noise);
    float channel = 1.0f - gen_smoothstep(0.012f, 0.095f, river_distance);
    float inland = gen_smoothstep(59.0f, 75.0f, base);
    float channel_floor = (float)WORLD_SEA_LEVEL - 1.0f + fmaxf(0.0f, river_distance - 0.006f) * 240.0f;
    float river_cut = h - channel_floor;
    if (river_cut < 0.0f) {
        river_cut = 0.0f;
    }
    h -= channel * inland * river_cut;

    h += noise_value2(x * 0.070f, z * 0.070f, s ^ 0x33444554u) * 1.15f;
    int hi = (int)floorf(h);
    if (hi < 4) {
        hi = 4;
    }
    if (hi > 224) {
        hi = 224;
    }
    return hi;
}

int world_gen_height_version(long seed, int wx, int wz, int terrain_version)
{
    if (terrain_version >= 5)
        return worldgen5_height(seed, wx, wz);
    if (terrain_version >= 4)
        return worldgen_height(seed, wx, wz);
    if (terrain_version >= 3) {
        return world_gen_height_v3(seed, wx, wz);
    }
    return terrain_version >= 2 ? world_gen_height_v2(seed, wx, wz) : world_gen_height_v1(seed, wx, wz);
}

int world_gen_height(long seed, int wx, int wz)
{
    return world_gen_height_version(seed, wx, wz, 1);
}

/* Surface block for a column (biome-driven).
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *   h: column height.
 *
 * Returns: surface block ID.
 */
uint16_t world_gen_surface_version(long seed, int wx, int wz, int h, int terrain_version)
{
    if (terrain_version >= 5)
        return worldgen5_surface(seed, wx, wz, h);
    if (terrain_version >= 4)
        return worldgen_surface(seed, wx, wz, h);
    int b = biome_at(seed, wx, wz, h);
    switch (b) {
    case BIOME_OCEAN:
        return (h >= WORLD_SEA_LEVEL - 4) ? BLOCK_SAND : BLOCK_DIRT;
    case BIOME_BEACH:
    case BIOME_DESERT:
        return BLOCK_SAND;
    case BIOME_SNOW:
        return BLOCK_SNOW;
    case BIOME_MOUNTAINS:
        if (terrain_version >= 3) {
            return (h >= 164) ? BLOCK_SNOW : BLOCK_STONE;
        }
        return (h >= 95) ? BLOCK_SNOW : BLOCK_STONE;
    case BIOME_FOREST:
    case BIOME_PLAINS:
    default:
        break;
    }
    /* Slope estimate via forward differences; steep faces expose stone. */
    int hx = world_gen_height_version(seed, wx + 1, wz, terrain_version);
    int hz = world_gen_height_version(seed, wx, wz + 1, terrain_version);
    int slope = abs(hx - h) + abs(hz - h);
    if (slope >= 6) {
        return BLOCK_STONE;
    }
    return BLOCK_GRASS;
}

uint16_t world_gen_surface(long seed, int wx, int wz, int h)
{
    return world_gen_surface_version(seed, wx, wz, h, 1);
}

/* Cave carve test (pure function of world coords): fBm chambers plus
 * thin worm tunnels. Only evaluated inside [3, h-4] by the caller.
 */
static bool gen_carved(long seed, int wx, int y, int wz)
{
    uint32_t s = (uint32_t)seed;
    float chambers =
        noise_fbm3((float)wx * 0.030f, (float)y * 0.045f, (float)wz * 0.030f, 2, 2.0f, 0.5f, s ^ 0xCA4E0001u);
    if (chambers > 0.72f) {
        return true;
    }
    float worm = noise_fbm3((float)wx * 0.011f + 7.3f, (float)y * 0.025f + 1.7f, (float)wz * 0.011f - 3.1f, 2, 2.0f,
                            0.5f, s ^ 0xCA4E0002u);
    if (worm > -0.03f && worm < 0.03f) {
        return true;
    }
    return false;
}

/* Clustered ore: 3x3x3 cells decide vein presence/type, sub-hash fills.
 * Only replaces stone; depth caps per type preserved from M2.
 */
static uint16_t gen_ore(long seed, int wx, int y, int wz)
{
    if (y > 32) {
        return BLOCK_STONE;
    }
    uint32_t s = (uint32_t)seed;
    int qx = floor_div(wx, 3);
    int qy = y / 3;
    int qz = floor_div(wz, 3);
    float vein = noise_hash_to_unit(noise_hash3(qx, qy, qz, s ^ 0x0E0001u));
    if (vein >= 0.06f) {
        return BLOCK_STONE;
    }
    float pick = noise_hash_to_unit(noise_hash3(qx, qy, qz, s ^ 0x0E0002u));
    uint16_t type = BLOCK_COAL_ORE;
    if (y <= 12 && pick < 0.12f) {
        type = BLOCK_DIAMOND_ORE;
    } else if (y <= 16 && pick < 0.30f) {
        type = BLOCK_GOLD_ORE;
    } else if (y <= 24 && pick < 0.60f) {
        type = BLOCK_IRON_ORE;
    }
    float fill = noise_hash_to_unit(noise_hash3(wx, y, wz, s ^ 0x0E0003u));
    return fill < 0.55f ? type : BLOCK_STONE;
}

/* Tree decision for a trunk column (pure function, order-independent).
 * Trunk height 4..6 written to out_trunk on success.
 */
bool world_gen_tree_version(long seed, int tx, int tz, int terrain_version, int *out_trunk)
{
    if (terrain_version >= 5) return worldgen5_tree(seed, tx, tz, out_trunk);
    /* Reject most profile-4 columns before an expensive density column
     * query. This is the same lottery used
     * below, and .022 is the maximum
     * existing biome tree density, so the output remains identical. */
    if (terrain_version >= 4 && noise_hash_to_unit(noise_hash2(tx, tz, (uint32_t)seed ^ 0x7EE50001u)) >= .022f)
        return false;
    int h = world_gen_height_version(seed, tx, tz, terrain_version);
    if (h <= WORLD_SEA_LEVEL + 1 || h >= 120) {
        return false;
    }
    int b = terrain_version >= 4 ? worldgen_climate(seed, tx, tz).biome : biome_at(seed, tx, tz, h);
    if (terrain_version >= 4 && worldgen_surface(seed, tx, tz, h) != BLOCK_GRASS &&
        worldgen_surface(seed, tx, tz, h) != BLOCK_SNOW)
        return false;
    float density = biome_tree_density(b);
    if (density <= 0.0f) {
        return false;
    }
    uint32_t s = (uint32_t)seed;
    float r = noise_hash_to_unit(noise_hash2(tx, tz, s ^ 0x7EE50001u));
    if (r >= density) {
        return false;
    }
    /* Footing is the surface cell itself, which the carve band [3, h-4]
     * never touches — solidity is structural, no chunk lookup needed. */
    if (out_trunk != NULL) {
        float rh = noise_hash_to_unit(noise_hash2(tx, tz, s ^ 0x7EE50002u));
        *out_trunk = 4 + (int)(rh * 3.0f); /* 4..6. */
    }
    return true;
}

bool world_gen_tree(long seed, int tx, int tz, int *out_trunk)
{
    return world_gen_tree_version(seed, tx, tz, 1, out_trunk);
}

/* Vegetation for a surface column: plant/flower or AIR (pure function). */
uint16_t world_gen_vegetation_version(long seed, int wx, int wz, int h, int terrain_version)
{
    if (terrain_version >= 5) return worldgen5_vegetation(seed, wx, wz, h);
    if (h <= WORLD_SEA_LEVEL + 1) {
        return BLOCK_AIR;
    }
    int b = terrain_version >= 4 ? worldgen_climate(seed, wx, wz).biome : biome_at(seed, wx, wz, h);
    if (!biome_has_vegetation(b)) {
        return BLOCK_AIR;
    }
    uint32_t s = (uint32_t)seed;
    float r = noise_hash_to_unit(noise_hash2(wx, wz, s ^ 0x9E6E7A01u));
    if (b == BIOME_SNOW) {
        return r < 0.015f ? BLOCK_GRASS_PLANT : BLOCK_AIR;
    }
    if (r < 0.050f) {
        return BLOCK_GRASS_PLANT;
    }
    if (r < 0.062f) {
        return BLOCK_FLOWER;
    }
    return BLOCK_AIR;
}

uint16_t world_gen_vegetation(long seed, int wx, int wz, int h)
{
    return world_gen_vegetation_version(seed, wx, wz, h, 1);
}

/* Write one cell of a tree stamp when it falls inside chunk c.
 * Trunks overwrite air/vegetation/leaves; leaves only fill air/vegetation.
 */
static void stamp_cell(Chunk *c, int wx, int wy, int wz, uint16_t id, bool is_trunk)
{
    int lx = wx - c->cx * CHUNK_X;
    int lz = wz - c->cz * CHUNK_Z;
    if (!chunk_in_bounds(lx, wy, lz)) {
        return;
    }
    uint16_t cur = chunk_get_block(c, lx, wy, lz);
    if (is_trunk) {
        if (!block_is_solid(cur) || cur == BLOCK_LEAVES) {
            chunk_set_block(c, lx, wy, lz, id);
        }
        return;
    }
    if (cur == BLOCK_AIR || cur == BLOCK_GRASS_PLANT || cur == BLOCK_FLOWER) {
        chunk_set_block(c, lx, wy, lz, id);
    }
}

/* Stamp all trees whose 7x7 footprint touches chunk c. Trunk columns range
 * over the chunk expanded by 2 (leaf radius); everything is a pure function
 * of world coords, so generation order across chunks cannot matter.
 */
static void stamp_trees(Chunk *c, long seed, int terrain_version)
{
    int x0 = c->cx * CHUNK_X - 2;
    int x1 = c->cx * CHUNK_X + CHUNK_X + 1;
    int z0 = c->cz * CHUNK_Z - 2;
    int z1 = c->cz * CHUNK_Z + CHUNK_Z + 1;
    for (int tx = x0; tx <= x1; ++tx) {
        for (int tz = z0; tz <= z1; ++tz) {
            int th = 0;
            if (!world_gen_tree_version(seed, tx, tz, terrain_version, &th)) {
                continue;
            }
            int h = world_gen_height_version(seed, tx, tz, terrain_version);
            /* Trunk. */
            for (int y = h + 1; y <= h + th; ++y) {
                stamp_cell(c, tx, y, tz, BLOCK_WOOD, true);
            }
            /* Lower canopy: 5x5 minus corners at th-2..th-1. */
            for (int y = h + th - 2; y <= h + th - 1; ++y) {
                for (int dx = -2; dx <= 2; ++dx) {
                    for (int dz = -2; dz <= 2; ++dz) {
                        if ((dx == -2 || dx == 2) && (dz == -2 || dz == 2)) {
                            continue;
                        }
                        if (dx == 0 && dz == 0 && y <= h + th) {
                            continue; /* Trunk column stays wood. */
                        }
                        stamp_cell(c, tx + dx, y, tz + dz, BLOCK_LEAVES, false);
                    }
                }
            }
            /* Upper canopy: 3x3 at th, plus-cap at th+1. */
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) {
                        continue;
                    }
                    stamp_cell(c, tx + dx, h + th, tz + dz, BLOCK_LEAVES, false);
                }
            }
            stamp_cell(c, tx, h + th + 1, tz, BLOCK_LEAVES, false);
            stamp_cell(c, tx + 1, h + th + 1, tz, BLOCK_LEAVES, false);
            stamp_cell(c, tx - 1, h + th + 1, tz, BLOCK_LEAVES, false);
            stamp_cell(c, tx, h + th + 1, tz + 1, BLOCK_LEAVES, false);
            stamp_cell(c, tx, h + th + 1, tz - 1, BLOCK_LEAVES, false);
        }
    }
}

/* Generate + adopt one chunk (terrain, caves, ores, water, trees, plants).
 * When the world has a save directory, a valid saved chunk overrides fresh
 * generation (player edits win); missing/corrupt files fall back to
 * deterministic terrain. Corrupt files are logged and regenerated.
 *
 * Args:
 *   w: world (seed source).
 *   cx, cz: chunk coords.
 *
 * Returns: 0 on success.
 */
int world_generate_chunk(World *w, int cx, int cz)
{
    if (w == NULL) {
        return -1;
    }
    if (world_get_chunk(w, cx, cz) != NULL) {
        return -2;
    }
    if (w->terrain_version >= 6) {
        struct VGen *g = world_vgen(w);
        if (g == NULL) {
            return -5;
        }
        Chunk *vc = chunk_create(cx, cz);
        if (vc == NULL) {
            return -3;
        }
        if (!vgen_build_chunk(g, vc, cx, cz)) {
            chunk_destroy(vc);
            return -6;
        }
        return world_adopt_vanilla_chunk(w, vc);
    }
    Chunk *c = chunk_create(cx, cz);
    if (c == NULL) {
        LOG_ERROR("world_generate_chunk: OOM chunk (%d,%d)", cx, cz);
        return -3;
    }
    if (w->save_dir[0] != '\0' && world_save_read_chunk(w->save_dir, c) == 0) {
        LOG_DEBUG("world_generate_chunk: loaded saved chunk (%d,%d)", cx, cz);
        if (world_add_chunk(w, c) != 0) {
            chunk_destroy(c);
            return -4;
        }
        return 0;
    }
    long seed = w->seed;
    if (w->terrain_version >= 4) {
        worldgen_fill_profile(c, seed, w->terrain_version);
        int tops[16][16];
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                int h = CHUNK_Y - 1;
                while (h > 0 && !block_is_solid(chunk_get_block(c, x, h, z)))
                    --h;
                tops[z][x] = h;
            }
        stamp_trees(c, seed, w->terrain_version);
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                int wx = cx * 16 + x, wz = cz * 16 + z;
                int h = tops[z][x];
                if (h + 1 < CHUNK_Y && chunk_get_block(c, x, h, z) == BLOCK_GRASS &&
                    chunk_get_block(c, x, h + 1, z) == BLOCK_AIR)
                    chunk_set_block(c, x, h + 1, z, world_gen_vegetation_version(seed, wx, wz, h, w->terrain_version));
            }
        c->dirty = true;
        c->save_dirty = false;
        if (world_add_chunk(w, c) != 0) {
            chunk_destroy(c);
            return -4;
        }
        return 0;
    }
    int terrain_version = w->terrain_version >= 3 ? 3 : (w->terrain_version >= 2 ? 2 : 1);
    int heights[CHUNK_X][CHUNK_Z];
    int biomes[CHUNK_X][CHUNK_Z];
    for (int lx = 0; lx < CHUNK_X; ++lx) {
        for (int lz = 0; lz < CHUNK_Z; ++lz) {
            int wx = cx * CHUNK_X + lx;
            int wz = cz * CHUNK_Z + lz;
            int h = world_gen_height_version(seed, wx, wz, terrain_version);
            int b = biome_at(seed, wx, wz, h);
            heights[lx][lz] = h;
            biomes[lx][lz] = b;
            uint16_t surface = world_gen_surface_version(seed, wx, wz, h, terrain_version);
            uint16_t subsurface = (b == BIOME_OCEAN || b == BIOME_BEACH || b == BIOME_DESERT) ? BLOCK_SAND : BLOCK_DIRT;
            if (b == BIOME_MOUNTAINS) {
                subsurface = BLOCK_DIRT;
            }
            for (int y = 0; y <= h; ++y) {
                uint16_t id;
                if (y == 0) {
                    id = BLOCK_BEDROCK;
                } else if (y == h) {
                    id = surface;
                } else if (y >= h - 3) {
                    id = subsurface;
                } else if (y >= 3 && y <= h - 4 && gen_carved(seed, wx, y, wz)) {
                    id = BLOCK_AIR; /* Cave chamber/tunnel. */
                } else {
                    id = gen_ore(seed, wx, y, wz);
                }
                chunk_set_block(c, lx, y, lz, id);
            }
            /* Water fill for basins below sea level. */
            if (h < WORLD_SEA_LEVEL) {
                for (int y = h + 1; y <= WORLD_SEA_LEVEL; ++y) {
                    chunk_set_block(c, lx, y, lz, BLOCK_WATER);
                }
            }
        }
    }
    /* Trees (order-independent stamps), then surface vegetation. */
    stamp_trees(c, seed, terrain_version);
    for (int lx = 0; lx < CHUNK_X; ++lx) {
        for (int lz = 0; lz < CHUNK_Z; ++lz) {
            int h = heights[lx][lz];
            uint16_t veg = world_gen_vegetation_version(seed, cx * CHUNK_X + lx, cz * CHUNK_Z + lz, h, terrain_version);
            if (veg != BLOCK_AIR && h + 1 < CHUNK_Y && chunk_get_block(c, lx, h + 1, lz) == BLOCK_AIR) {
                chunk_set_block(c, lx, h + 1, lz, veg);
            }
        }
    }
    (void)biomes;
    c->dirty = true;
    /* Generated terrain regenerates exactly from the seed: not save-dirty.
     * (chunk_set_block flags edits; generation clears once at the end.) */
    c->save_dirty = false;
    if (world_add_chunk(w, c) != 0) {
        LOG_ERROR("world_generate_chunk: world full, dropping (%d,%d)", cx, cz);
        chunk_destroy(c);
        return -4;
    }
    return 0;
}

/* ---- vanilla-exact worlds (terrain_version >= 6) --------------------------------------------------- */

struct VGen *world_vgen(World *w)
{
    if (w == NULL || w->terrain_version < 6) {
        return NULL;
    }
    const int64_t seed = w->seed64 != 0 ? w->seed64 : (int64_t)w->seed;
    if (w->vgen != NULL && vgen_seed(w->vgen) != seed) {
        vgen_destroy(w->vgen);
        w->vgen = NULL;
    }
    if (w->vgen == NULL && !w->vgen_failed) {
        char root[1024];
        if (vgen_find_datapack(root, (int)sizeof(root))) {
            w->vgen = vgen_create(root, seed);
        } else {
            LOG_ERROR("world: this world needs the vanilla worldgen data pack, which was not found");
        }
        if (w->vgen == NULL) {
            w->vgen_failed = true;
        }
    }
    return w->vgen;
}

int world_adopt_vanilla_chunk(World *w, Chunk *c)
{
    if (w == NULL || c == NULL) {
        chunk_destroy(c);
        return -1;
    }
    if (world_get_chunk(w, c->cx, c->cz) != NULL) {
        chunk_destroy(c);
        return -2;
    }
    /* A saved chunk holds the player's edits and wins; reading it leaves the biome columns alone. */
    if (w->save_dir[0] != '\0' && world_save_read_chunk(w->save_dir, c) == 0) {
        LOG_DEBUG("world: loaded saved chunk (%d,%d)", c->cx, c->cz);
    }
    c->dirty = true;
    if (world_add_chunk(w, c) != 0) {
        LOG_ERROR("world: world full, dropping chunk (%d,%d)", c->cx, c->cz);
        chunk_destroy(c);
        return -4;
    }
    return 0;
}

/* Feet-level ground that is fine to spawn on: dry, not under a canopy. */
static bool vanilla_footing(uint16_t id)
{
    return id == BLOCK_GRASS || id == BLOCK_DIRT || id == BLOCK_SAND || id == BLOCK_SNOW || id == BLOCK_GRAVEL ||
           id == BLOCK_SANDSTONE;
}

static int vanilla_top(const World *w, int x, int z)
{
    for (int y = CHUNK_Y - 1; y >= 0; --y) {
        if (world_get_block(w, x, y, z) != BLOCK_AIR) {
            return y;
        }
    }
    return -1;
}

int world_find_spawn_vanilla(World *w, Vec3 *out)
{
    if (w == NULL || out == NULL || world_vgen(w) == NULL) {
        return -1;
    }
    const int sea_surface = 127; /* Minecraft y 63 */
    for (int ring = 0; ring <= 24; ++ring) {
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if ((dx < 0 ? -dx : dx) != ring && (dz < 0 ? -dz : dz) != ring) {
                    continue;
                }
                int cx = dx, cz = dz;
                bool added = false;
                if (world_get_chunk(w, cx, cz) == NULL && !vgen_chunk_may_be_land(world_vgen(w), cx, cz)) {
                    continue; /* open water: no need to build the terrain to know it is no place to spawn */
                }
                if (world_get_chunk(w, cx, cz) == NULL) {
                    if (world_generate_chunk(w, cx, cz) != 0) {
                        return -2;
                    }
                    added = true;
                }
                /* Probe a few columns of the chunk; take the first with open, dry, solid ground. */
                for (int pz = 4; pz <= 12; pz += 4) {
                    for (int px = 4; px <= 12; px += 4) {
                        int x = cx * 16 + px, z = cz * 16 + pz;
                        int top = vanilla_top(w, x, z);
                        if (top < sea_surface + 1 || top + 3 >= CHUNK_Y) {
                            continue;
                        }
                        if (!vanilla_footing(world_get_block(w, x, top, z))) {
                            continue;
                        }
                        bool flat = true;
                        for (int oz = -1; oz <= 1 && flat; ++oz) {
                            for (int ox = -1; ox <= 1 && flat; ++ox) {
                                int t = vanilla_top(w, x + ox, z + oz);
                                flat = t >= top - 1 && t <= top + 1 &&
                                       vanilla_footing(world_get_block(w, x + ox, t, z + oz));
                            }
                        }
                        if (flat) {
                            *out = mmath_vec3((float)x + 0.5f, (float)top + 1.0f, (float)z + 0.5f);
                            return 0;
                        }
                    }
                }
                if (added) {
                    world_remove_chunk(w, cx, cz);
                }
            }
        }
    }
    return -3;
}
