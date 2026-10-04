#include "world/world_gen.h"
#include "core/log.h"
#include "core/noise.h"
#include "world/biome.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"
#include "world/world_save.h"

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

/* Column height (M5): continental base + mountain lift + hills + detail.
 * Base 70 keeps most land above the sea (64) with real ocean basins where
 * the continent field dips; mountains spike via the squared mask.
 * Typical range roughly [25..160]; clamped to [4..200] (test-pinned).
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *
 * Returns: clamped column height.
 */
int world_gen_height(long seed, int wx, int wz)
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

/* Surface block for a column (biome-driven).
 *
 * Args:
 *   seed: world seed.
 *   wx, wz: world coords.
 *   h: column height.
 *
 * Returns: surface block ID.
 */
uint16_t world_gen_surface(long seed, int wx, int wz, int h)
{
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
        return (h >= 95) ? BLOCK_SNOW : BLOCK_STONE;
    case BIOME_FOREST:
    case BIOME_PLAINS:
    default:
        break;
    }
    /* Slope estimate via forward differences; steep faces expose stone. */
    int hx = world_gen_height(seed, wx + 1, wz);
    int hz = world_gen_height(seed, wx, wz + 1);
    int slope = abs(hx - h) + abs(hz - h);
    if (slope >= 6) {
        return BLOCK_STONE;
    }
    return BLOCK_GRASS;
}

/* Cave carve test (pure function of world coords): fBm chambers plus
 * thin worm tunnels. Only evaluated inside [3, h-4] by the caller.
 */
static bool gen_carved(long seed, int wx, int y, int wz)
{
    uint32_t s = (uint32_t)seed;
    float chambers = noise_fbm3((float)wx * 0.030f, (float)y * 0.045f, (float)wz * 0.030f, 2, 2.0f, 0.5f,
                                s ^ 0xCA4E0001u);
    if (chambers > 0.72f) {
        return true;
    }
    float worm = noise_fbm3((float)wx * 0.011f + 7.3f, (float)y * 0.025f + 1.7f, (float)wz * 0.011f - 3.1f, 2,
                            2.0f, 0.5f, s ^ 0xCA4E0002u);
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
bool world_gen_tree(long seed, int tx, int tz, int *out_trunk)
{
    int h = world_gen_height(seed, tx, tz);
    if (h <= WORLD_SEA_LEVEL + 1 || h >= 120) {
        return false;
    }
    int b = biome_at(seed, tx, tz, h);
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

/* Vegetation for a surface column: plant/flower or AIR (pure function). */
uint16_t world_gen_vegetation(long seed, int wx, int wz, int h)
{
    if (h <= WORLD_SEA_LEVEL + 1) {
        return BLOCK_AIR;
    }
    int b = biome_at(seed, wx, wz, h);
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
static void stamp_trees(Chunk *c, long seed)
{
    int x0 = c->cx * CHUNK_X - 2;
    int x1 = c->cx * CHUNK_X + CHUNK_X + 1;
    int z0 = c->cz * CHUNK_Z - 2;
    int z1 = c->cz * CHUNK_Z + CHUNK_Z + 1;
    for (int tx = x0; tx <= x1; ++tx) {
        for (int tz = z0; tz <= z1; ++tz) {
            int th = 0;
            if (!world_gen_tree(seed, tx, tz, &th)) {
                continue;
            }
            int h = world_gen_height(seed, tx, tz);
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
    int heights[CHUNK_X][CHUNK_Z];
    int biomes[CHUNK_X][CHUNK_Z];
    for (int lx = 0; lx < CHUNK_X; ++lx) {
        for (int lz = 0; lz < CHUNK_Z; ++lz) {
            int wx = cx * CHUNK_X + lx;
            int wz = cz * CHUNK_Z + lz;
            int h = world_gen_height(seed, wx, wz);
            int b = biome_at(seed, wx, wz, h);
            heights[lx][lz] = h;
            biomes[lx][lz] = b;
            uint16_t surface = world_gen_surface(seed, wx, wz, h);
            uint16_t subsurface =
                (b == BIOME_OCEAN || b == BIOME_BEACH || b == BIOME_DESERT) ? BLOCK_SAND : BLOCK_DIRT;
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
    stamp_trees(c, seed);
    for (int lx = 0; lx < CHUNK_X; ++lx) {
        for (int lz = 0; lz < CHUNK_Z; ++lz) {
            int h = heights[lx][lz];
            uint16_t veg = world_gen_vegetation(seed, cx * CHUNK_X + lx, cz * CHUNK_Z + lz, h);
            if (veg != BLOCK_AIR && h + 1 < CHUNK_Y &&
                chunk_get_block(c, lx, h + 1, lz) == BLOCK_AIR) {
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
