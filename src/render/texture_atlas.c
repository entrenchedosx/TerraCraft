#include "render/texture_atlas.h"
#include "core/bmp.h"
#include "core/log.h"
#include "core/noise.h"
#include "core/path.h"
#include "platform/gl_ctx.h"
#include "world/block.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Write one pixel (tile coords: col/row with row 0 at the texture bottom,
 * pixel x/y within the tile with y=0 at the tile bottom). */
static void put_px(unsigned char *px, int col, int row, int x, int y, unsigned char r, unsigned char g,
                   unsigned char b, unsigned char a)
{
    size_t gx = (size_t)(col * ATLAS_TILE_PX + x);
    size_t gy = (size_t)(row * ATLAS_TILE_PX + y);
    size_t o = (gy * ATLAS_SIZE + gx) * ATLAS_BYTES;
    px[o + 0] = r;
    px[o + 1] = g;
    px[o + 2] = b;
    px[o + 3] = a;
}

/* Per-pixel deterministic random in [0,1) from global pixel + tile salt. */
static float px_rand(int col, int row, int x, int y, uint32_t salt)
{
    int gx = col * ATLAS_TILE_PX + x;
    int gy = row * ATLAS_TILE_PX + y;
    return noise_hash_to_unit(noise_hash3(gx, gy, (int)salt, 0xA71A5u));
}

/* Shade helper: multiply rgb by f (clamped to 255 via float math). */
static unsigned char shade(unsigned char c, float f)
{
    float v = (float)c * f;
    if (v > 255.0f) {
        v = 255.0f;
    }
    return (unsigned char)v;
}

/* Fill a tile with a flat base color. */
static void tile_fill(unsigned char *px, int tile, unsigned char r, unsigned char g, unsigned char b,
                      unsigned char a)
{
    int col = tile % ATLAS_TILES;
    int row = tile / ATLAS_TILES;
    for (int y = 0; y < ATLAS_TILE_PX; ++y) {
        for (int x = 0; x < ATLAS_TILE_PX; ++x) {
            put_px(px, col, row, x, y, r, g, b, a);
        }
    }
}

/* Speckle pass: darken/lighten random pixels of a tile. */
static void tile_speckle(unsigned char *px, int tile, uint32_t salt, float dark_frac, float light_frac)
{
    int col = tile % ATLAS_TILES;
    int row = tile / ATLAS_TILES;
    for (int y = 0; y < ATLAS_TILE_PX; ++y) {
        for (int x = 0; x < ATLAS_TILE_PX; ++x) {
            float r = px_rand(col, row, x, y, salt);
            size_t gx = (size_t)(col * ATLAS_TILE_PX + x);
            size_t gy = (size_t)(row * ATLAS_TILE_PX + y);
            size_t o = (gy * ATLAS_SIZE + gx) * ATLAS_BYTES;
            if (r < dark_frac) {
                px[o + 0] = shade(px[o + 0], 0.72f);
                px[o + 1] = shade(px[o + 1], 0.72f);
                px[o + 2] = shade(px[o + 2], 0.72f);
            } else if (r < dark_frac + light_frac) {
                px[o + 0] = shade(px[o + 0], 1.22f);
                px[o + 1] = shade(px[o + 1], 1.22f);
                px[o + 2] = shade(px[o + 2], 1.22f);
            }
        }
    }
}

/* Map a block + face to an atlas tile.
 *
 * Args:
 *   block: block ID.
 *   face: AtlasFace 0..5.
 *
 * Returns: tile index.
 */
int block_tile_for_face(uint16_t block, int face)
{
    if (block_is_water(block)) {
        return TILE_WATER;
    }
    switch (block) {
    case BLOCK_GRASS:
        if (face == ATLAS_FACE_POS_Y) {
            return TILE_GRASS_TOP;
        }
        if (face == ATLAS_FACE_NEG_Y) {
            return TILE_GRASS_BOTTOM;
        }
        return TILE_GRASS_SIDE;
    case BLOCK_DIRT:
        return TILE_DIRT;
    case BLOCK_STONE:
        return TILE_STONE;
    case BLOCK_SAND:
        return TILE_SAND;
    case BLOCK_WOOD:
        if (face == ATLAS_FACE_POS_Y || face == ATLAS_FACE_NEG_Y) {
            return TILE_WOOD_TOP;
        }
        return TILE_WOOD;
    case BLOCK_LEAVES:
        return TILE_LEAVES;
    case BLOCK_GLASS:
        return TILE_GLASS;
    case BLOCK_WATER:
        return TILE_WATER;
    case BLOCK_BEDROCK:
        return TILE_BEDROCK;
    case BLOCK_COAL_ORE:
        return TILE_COAL_ORE;
    case BLOCK_IRON_ORE:
        return TILE_IRON_ORE;
    case BLOCK_GOLD_ORE:
        return TILE_GOLD_ORE;
    case BLOCK_DIAMOND_ORE:
        return TILE_DIAMOND_ORE;
    case BLOCK_SNOW:
        return TILE_SNOW;
    case BLOCK_GRASS_PLANT:
        return TILE_PLANT;
    case BLOCK_FLOWER:
        return TILE_FLOWER;
    case BLOCK_TORCH:
        return TILE_TORCH;
    case BLOCK_WORKBENCH:
        if (face == ATLAS_FACE_POS_Y) {
            return TILE_WORKBENCH_TOP;
        }
        if (face == ATLAS_FACE_NEG_Y) {
            return TILE_PLANKS;
        }
        return TILE_WORKBENCH_SIDE;
    case BLOCK_PLANKS:
        return TILE_PLANKS;
    case BLOCK_AIR:
    default:
        break;
    }
    return TILE_STONE;
}

/* Overridable tile file names (index = tile; keep in sync with AtlasTile).
 * Player tiles load Steve-sourced crops (converter) when present and keep
 * the procedural art as fallback (e.g. CI without mcassets). */
static const char *TILE_FILES[] = {
    "grass_top", "grass_side", "dirt", "stone", "sand", "wood", "leaves", "glass", "water", "bedrock",
    "coal_ore", "iron_ore", "gold_ore", "diamond_ore", "snow", "grass_bottom", "plant", "flower", "torch",
    "tool_pickaxe", "tool_axe", "tool_shovel", "coal", "crack0", "crack1", "crack2", "crack3", "crack4",
    "workbench", "planks", "apple", "stick", "wood_top", "workbench_top", "workbench_side", "wood_pickaxe",
    "stone_pickaxe", "wood_axe", "stone_axe", "wood_shovel", "stone_shovel", "bow", "arrow",
"bone", "beef", "leather", "player_skin", "player_sleeve", "flesh", "wood_sword", "stone_sword",
    "iron_ingot", "diamond", "iron_pickaxe", "iron_axe", "iron_shovel", "diamond_pickaxe", "diamond_axe",
    "diamond_shovel", "iron_sword", "diamond_sword",
};
/* Derived from the array so adding a tile can never desync the bound. */
#define TILE_FILE_COUNT (sizeof(TILE_FILES) / sizeof(TILE_FILES[0]))

/* Tile file name for overrides (NULL when out of range).
 *
 * Args:
 *   tile: tile index.
 *
 * Returns: file stem or NULL.
 */
const char *texture_atlas_tile_file(int tile)
{
    if (tile < 0 || tile >= TILE_FILE_COUNT) {
        return NULL;
    }
    return TILE_FILES[tile];
}

/* Number of overridable tiles. */
int texture_atlas_tile_file_count(void)
{
    return TILE_FILE_COUNT;
}

/* UV rectangle for a tile (half-texel inset, v=0 at bottom).
 *
 * Args:
 *   tile: tile index.
 *   out_*: receivers.
 */
void texture_atlas_tile_uv(int tile, float *out_u0, float *out_v0, float *out_u1, float *out_v1)
{
    if (tile < 0) {
        tile = 0;
    }
    if (tile > 255) {
        tile = 255;
    }
    int col = tile % ATLAS_TILES;
    int row = tile / ATLAS_TILES;
    float u0 = ((float)(col * ATLAS_TILE_PX) + 0.5f) / (float)ATLAS_SIZE;
    float u1 = ((float)((col + 1) * ATLAS_TILE_PX) - 0.5f) / (float)ATLAS_SIZE;
    float v0 = ((float)(row * ATLAS_TILE_PX) + 0.5f) / (float)ATLAS_SIZE;
    float v1 = ((float)((row + 1) * ATLAS_TILE_PX) - 0.5f) / (float)ATLAS_SIZE;
    if (out_u0 != NULL) {
        *out_u0 = u0;
    }
    if (out_v0 != NULL) {
        *out_v0 = v0;
    }
    if (out_u1 != NULL) {
        *out_u1 = u1;
    }
    if (out_v1 != NULL) {
        *out_v1 = v1;
    }
}

/* Fill the whole atlas (bottom-up rows).
 *
 * Args:
 *   out_px: 256*256*4 destination.
 */
void texture_atlas_fill_rgba(unsigned char *out_px)
{
    if (out_px == NULL) {
        return;
    }
    /* Base fills. */
    tile_fill(out_px, TILE_GRASS_TOP, 106, 176, 66, 255);
    tile_fill(out_px, TILE_GRASS_SIDE, 134, 96, 67, 255);
    tile_fill(out_px, TILE_DIRT, 134, 96, 67, 255);
    tile_fill(out_px, TILE_STONE, 128, 128, 128, 255);
    tile_fill(out_px, TILE_SAND, 216, 200, 150, 255);
    tile_fill(out_px, TILE_WOOD, 104, 78, 47, 255);
    tile_fill(out_px, TILE_LEAVES, 45, 125, 45, 255);
    tile_fill(out_px, TILE_GLASS, 190, 220, 235, 90);
    tile_fill(out_px, TILE_WATER, 52, 110, 220, 180);
    tile_fill(out_px, TILE_BEDROCK, 40, 40, 40, 255);
    tile_fill(out_px, TILE_COAL_ORE, 128, 128, 128, 255);
    tile_fill(out_px, TILE_IRON_ORE, 128, 128, 128, 255);
    tile_fill(out_px, TILE_GOLD_ORE, 128, 128, 128, 255);
    tile_fill(out_px, TILE_DIAMOND_ORE, 128, 128, 128, 255);
    tile_fill(out_px, TILE_SNOW, 240, 244, 248, 255);
    tile_fill(out_px, TILE_GRASS_BOTTOM, 134, 96, 67, 255);
    tile_fill(out_px, TILE_PLANT, 0, 0, 0, 0);
    tile_fill(out_px, TILE_FLOWER, 0, 0, 0, 0);
    tile_fill(out_px, TILE_TORCH, 0, 0, 0, 0);
    tile_fill(out_px, TILE_TOOL_PICKAXE, 0, 0, 0, 0);
    tile_fill(out_px, TILE_TOOL_AXE, 0, 0, 0, 0);
    tile_fill(out_px, TILE_TOOL_SHOVEL, 0, 0, 0, 0);
    tile_fill(out_px, TILE_COAL, 0, 0, 0, 0);
    tile_fill(out_px, TILE_CRACK0, 0, 0, 0, 0);
    tile_fill(out_px, TILE_CRACK1, 0, 0, 0, 0);
    tile_fill(out_px, TILE_CRACK2, 0, 0, 0, 0);
    tile_fill(out_px, TILE_CRACK3, 0, 0, 0, 0);
    tile_fill(out_px, TILE_CRACK4, 0, 0, 0, 0);
    tile_fill(out_px, TILE_PLAYER_SKIN, 198, 146, 112, 255);
    tile_fill(out_px, TILE_PLAYER_SLEEVE, 55, 112, 190, 255);

    /* Grass top: darker + lighter speckles. */
    tile_speckle(out_px, TILE_GRASS_TOP, 11u, 0.20f, 0.10f);

    /* Grass side: green top strip (4 rows) with ragged edge over dirt. */
    {
        int col = TILE_GRASS_SIDE % ATLAS_TILES;
        int row = TILE_GRASS_SIDE / ATLAS_TILES;
        for (int y = ATLAS_TILE_PX - 4; y < ATLAS_TILE_PX; ++y) {
            for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                put_px(out_px, col, row, x, y, 106, 176, 66, 255);
            }
        }
        for (int x = 0; x < ATLAS_TILE_PX; ++x) {
            /* Ragged 1-2px fringe below the strip. */
            int depth = 1 + (int)(px_rand(col, row, x, 0, 77u) * 2.0f);
            for (int k = 0; k < depth; ++k) {
                int y = ATLAS_TILE_PX - 5 - k;
                if (y >= 0) {
                    put_px(out_px, col, row, x, y, 106, 176, 66, 255);
                }
            }
        }
    }
    tile_speckle(out_px, TILE_GRASS_SIDE, 12u, 0.12f, 0.05f);

    /* Dirt variants. */
    tile_speckle(out_px, TILE_DIRT, 13u, 0.22f, 0.08f);
    tile_speckle(out_px, TILE_GRASS_BOTTOM, 14u, 0.22f, 0.08f);

    /* Stone + sand. */
    tile_speckle(out_px, TILE_STONE, 15u, 0.15f, 0.08f);
    tile_speckle(out_px, TILE_SAND, 16u, 0.10f, 0.06f);

    /* Wood: vertical darker stripes every 4px + speckle. */
    {
        int col = TILE_WOOD % ATLAS_TILES;
        int row = TILE_WOOD / ATLAS_TILES;
        for (int y = 0; y < ATLAS_TILE_PX; ++y) {
            for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                if (x % 4 == 0) {
                    size_t gx = (size_t)(col * ATLAS_TILE_PX + x);
                    size_t gy = (size_t)(row * ATLAS_TILE_PX + y);
                    size_t o = (gy * ATLAS_SIZE + gx) * ATLAS_BYTES;
                    out_px[o + 0] = shade(out_px[o + 0], 0.7f);
                    out_px[o + 1] = shade(out_px[o + 1], 0.7f);
                    out_px[o + 2] = shade(out_px[o + 2], 0.7f);
                }
            }
        }
    }
    tile_speckle(out_px, TILE_WOOD, 17u, 0.10f, 0.05f);

    /* Leaves: transparent holes + dark clusters. */
    {
        int col = TILE_LEAVES % ATLAS_TILES;
        int row = TILE_LEAVES / ATLAS_TILES;
        for (int y = 0; y < ATLAS_TILE_PX; ++y) {
            for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                float r = px_rand(col, row, x, y, 18u);
                size_t gx = (size_t)(col * ATLAS_TILE_PX + x);
                size_t gy = (size_t)(row * ATLAS_TILE_PX + y);
                size_t o = (gy * ATLAS_SIZE + gx) * ATLAS_BYTES;
                if (r < 0.22f) {
                    out_px[o + 3] = 0; /* Hole (discarded by shader). */
                } else if (r < 0.42f) {
                    out_px[o + 0] = shade(out_px[o + 0], 0.6f);
                    out_px[o + 1] = shade(out_px[o + 1], 0.6f);
                    out_px[o + 2] = shade(out_px[o + 2], 0.6f);
                }
            }
        }
    }

    /* Glass: opaque border frame + faint diagonal streak. */
    {
        int col = TILE_GLASS % ATLAS_TILES;
        int row = TILE_GLASS / ATLAS_TILES;
        for (int y = 0; y < ATLAS_TILE_PX; ++y) {
            for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                if (x == 0 || y == 0 || x == ATLAS_TILE_PX - 1 || y == ATLAS_TILE_PX - 1) {
                    put_px(out_px, col, row, x, y, 215, 235, 245, 255);
                } else if ((x + y) % 7 == 0) {
                    put_px(out_px, col, row, x, y, 235, 245, 250, 160);
                }
            }
        }
    }

    /* Water: lighter wave bands every 5 rows. */
    {
        int col = TILE_WATER % ATLAS_TILES;
        int row = TILE_WATER / ATLAS_TILES;
        for (int y = 0; y < ATLAS_TILE_PX; ++y) {
            if (y % 5 == 0) {
                for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                    put_px(out_px, col, row, x, y, 80, 140, 235, 180);
                }
            }
        }
    }
    tile_speckle(out_px, TILE_WATER, 19u, 0.08f, 0.05f);

    /* Bedrock: light blobs on dark base. */
    tile_speckle(out_px, TILE_BEDROCK, 20u, 0.05f, 0.18f);

    /* Ores: stone base + colored 4x4-cell blobs. */
    {
        static const struct {
            int tile;
            unsigned char r, g, b;
        } ores[4] = {
            {TILE_COAL_ORE, 25, 25, 25},
            {TILE_IRON_ORE, 210, 150, 100},
            {TILE_GOLD_ORE, 252, 238, 75},
            {TILE_DIAMOND_ORE, 93, 236, 241},
        };
        for (size_t oi = 0; oi < 4; ++oi) {
            int tile = ores[oi].tile;
            int col = tile % ATLAS_TILES;
            int row = tile / ATLAS_TILES;
            tile_speckle(out_px, tile, 21u + (uint32_t)oi, 0.15f, 0.08f);
            for (int y = 0; y < ATLAS_TILE_PX; ++y) {
                for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                    float rc = px_rand(col / 4, row / 4, x / 4, y / 4, 30u + (uint32_t)oi);
                    if (rc < 0.28f) {
                        put_px(out_px, col, row, x, y, ores[oi].r, ores[oi].g, ores[oi].b, 255);
                    }
                }
            }
        }
    }

    /* Snow: subtle cool speckle. */
    tile_speckle(out_px, TILE_SNOW, 40u, 0.04f, 0.03f);

    /* Grass plant: vertical blades on transparency (cutout in shader). */
    {
        int col = TILE_PLANT % ATLAS_TILES;
        int row = TILE_PLANT / ATLAS_TILES;
        for (int y = 0; y < ATLAS_TILE_PX; ++y) {
            for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                bool blade = (x % 4 == 1 && y > 3) || (x % 4 == 3 && y > 6) || (x % 2 == 0 && y > 9);
                if (!blade) {
                    continue;
                }
                float r = px_rand(col, row, x, y, 41u);
                unsigned char g = (unsigned char)(120.0f + r * 80.0f);
                put_px(out_px, col, row, x, y, 60, g, 45, 255);
            }
        }
    }

    /* Flower: green stem + red crown on transparency. */
    {
        int col = TILE_FLOWER % ATLAS_TILES;
        int row = TILE_FLOWER / ATLAS_TILES;
        for (int y = 4; y < ATLAS_TILE_PX; ++y) {
            put_px(out_px, col, row, 7, y, 55, 130, 45, 255);
            put_px(out_px, col, row, 8, y, 55, 130, 45, 255);
        }
        for (int y = 1; y <= 5; ++y) {
            for (int x = 5; x <= 10; ++x) {
                int dx = x - 7;
                int dy = y - 3;
                if (dx * dx + dy * dy > 7 && !(dx == 0 && dy == 0)) {
                    continue;
                }
                if (dx == 0 && dy == 0) {
                    put_px(out_px, col, row, x, y, 250, 230, 90, 255);
                } else {
                    put_px(out_px, col, row, x, y, 205, 60, 70, 255);
                }
            }
        }
    }

    /* Torch placeholder: stick + glowing head on transparency. Cutout
     * full-cube stand-in until a proper small-model renderer exists. */
    {
        int col = TILE_TORCH % ATLAS_TILES;
        int row = TILE_TORCH / ATLAS_TILES;
        for (int y = 0; y < 11; ++y) {
            put_px(out_px, col, row, 7, y, 122, 88, 52, 255);
            put_px(out_px, col, row, 8, y, 122, 88, 52, 255);
        }
        for (int y = 11; y < ATLAS_TILE_PX; ++y) {
            for (int x = 5; x <= 10; ++x) {
                put_px(out_px, col, row, x, y, 245, 200, 80, 255);
            }
        }
        put_px(out_px, col, row, 7, 13, 255, 240, 170, 255);
        put_px(out_px, col, row, 8, 12, 255, 240, 170, 255);
    }

    /* Tool icons: brown handle + gray head shapes on transparency.
     * Used for hotbar icons and dropped-item cubes (tier shown by shade). */
    {
        static const struct {
            int tile;
            unsigned char hr, hg, hb;
        } tools[3] = {
            {TILE_TOOL_PICKAXE, 150, 150, 155},
            {TILE_TOOL_AXE, 150, 150, 155},
            {TILE_TOOL_SHOVEL, 150, 150, 155},
        };
        for (size_t ti = 0; ti < 3; ++ti) {
            int col = tools[ti].tile % ATLAS_TILES;
            int row = tools[ti].tile / ATLAS_TILES;
            /* Handle: vertical 2px bar. */
            for (int y = 0; y < 12; ++y) {
                put_px(out_px, col, row, 7, y, 122, 88, 52, 255);
                put_px(out_px, col, row, 8, y, 110, 78, 46, 255);
            }
            if (ti == 0) {
                /* Pickaxe: horizontal head with pointed ends. */
                for (int x = 2; x <= 13; ++x) {
                    put_px(out_px, col, row, x, 12, tools[ti].hr, tools[ti].hg, tools[ti].hb, 255);
                    put_px(out_px, col, row, x, 13, tools[ti].hr, tools[ti].hg, tools[ti].hb, 255);
                }
                put_px(out_px, col, row, 1, 12, tools[ti].hr, tools[ti].hg, tools[ti].hb, 255);
                put_px(out_px, col, row, 14, 12, tools[ti].hr, tools[ti].hg, tools[ti].hb, 255);
            } else if (ti == 1) {
                /* Axe: blocky head on the right. */
                for (int y = 9; y <= 13; ++y) {
                    for (int x = 8; x <= 12; ++x) {
                        put_px(out_px, col, row, x, y, tools[ti].hr, tools[ti].hg, tools[ti].hb, 255);
                    }
                }
            } else {
                /* Shovel: spade head on top. */
                for (int y = 11; y <= 14; ++y) {
                    for (int x = 6; x <= 9; ++x) {
                        put_px(out_px, col, row, x, y, tools[ti].hr, tools[ti].hg, tools[ti].hb, 255);
                    }
                }
                put_px(out_px, col, row, 7, 15, tools[ti].hr, tools[ti].hg, tools[ti].hb, 255);
                put_px(out_px, col, row, 8, 15, tools[ti].hr, tools[ti].hg, tools[ti].hb, 255);
            }
        }
    }

    /* Coal lump: dark rounded blob on transparency. */
    {
        int col = TILE_COAL % ATLAS_TILES;
        int row = TILE_COAL / ATLAS_TILES;
        for (int y = 4; y <= 11; ++y) {
            for (int x = 4; x <= 11; ++x) {
                int dx = x - 7;
                int dy = (y - 7) * 2;
                if (dx * dx + dy * dy > 16) {
                    continue;
                }
                float r = px_rand(col, row, x, y, 44u);
                unsigned char c = (unsigned char)(18.0f + r * 30.0f);
                put_px(out_px, col, row, x, y, c, c, c, 255);
            }
        }
    }

    /* Smelted materials: flat ingot bar (iron) and faceted gem (diamond).
     * Both are transparency-backed item sprites, drawn from the same noise
     * field so their speckle reads like the coal lump above. */
    {
        static const struct {
            int tile;
            unsigned char r, g, b; /* Body colour. */
            unsigned char hr, hg, hb; /* Highlight. */
            uint32_t salt;
        } materials[2] = {
            {TILE_IRON_INGOT, 196, 196, 202, 236, 236, 244, 61u},
            {TILE_DIAMOND, 74, 216, 196, 168, 246, 236, 62u},
        };
        for (size_t mi = 0; mi < 2; ++mi) {
            int col = materials[mi].tile % ATLAS_TILES;
            int row = materials[mi].tile / ATLAS_TILES;
            tile_fill(out_px, materials[mi].tile, 0, 0, 0, 0);
            /* Ingot trapezoid: narrower on top, flat base. */
            if (mi == 0) {
                for (int y = 5; y <= 10; ++y) {
                    int inset = (y == 5 || y == 10) ? 1 : 0;
                    for (int x = 3 + inset; x <= 12 - inset; ++x) {
                        float r = px_rand(col, row, x, y, materials[mi].salt);
                        put_px(out_px, col, row, x, y,
                               (unsigned char)((float)materials[mi].r - r * 26.0f),
                               (unsigned char)((float)materials[mi].g - r * 26.0f),
                               (unsigned char)((float)materials[mi].b - r * 22.0f), 255);
                    }
                }
                put_px(out_px, col, row, 4, 6, materials[mi].hr, materials[mi].hg, materials[mi].hb, 255);
                put_px(out_px, col, row, 5, 6, materials[mi].hr, materials[mi].hg, materials[mi].hb, 255);
                put_px(out_px, col, row, 6, 6, materials[mi].hr, materials[mi].hg, materials[mi].hb, 255);
            } else {
                /* Gem: octagonal outline with a bright corner facet. */
                for (int y = 3; y <= 12; ++y) {
                    int dy = y - 7;
                    int half = (dy == 0 || dy == 1) ? 2 : (dy < 0 ? 4 : 3);
                    for (int x = 8 - half; x <= 7 + half; ++x) {
                        float r = px_rand(col, row, x, y, materials[mi].salt);
                        put_px(out_px, col, row, x, y,
                               (unsigned char)((float)materials[mi].r + r * 24.0f),
                               (unsigned char)((float)materials[mi].g - r * 18.0f),
                               (unsigned char)((float)materials[mi].b - r * 14.0f), 255);
                    }
                }
                put_px(out_px, col, row, 6, 6, materials[mi].hr, materials[mi].hg, materials[mi].hb, 255);
                put_px(out_px, col, row, 6, 7, materials[mi].hr, materials[mi].hg, materials[mi].hb, 255);
                put_px(out_px, col, row, 7, 6, materials[mi].hr, materials[mi].hg, materials[mi].hb, 255);
            }
        }
    }

    /* Workbench: plank boards with a dark frame + center grid hint. */
    {
        int col = TILE_WORKBENCH % ATLAS_TILES;
        int row = TILE_WORKBENCH / ATLAS_TILES;
        for (int y = 0; y < ATLAS_TILE_PX; ++y) {
            for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                float r = px_rand(col, row, x, y, 77u);
                unsigned char g = (unsigned char)(140.0f + r * 24.0f);
                put_px(out_px, col, row, x, y, g, (unsigned char)(g * 0.72f), (unsigned char)(g * 0.42f),
                       255);
            }
        }
        for (int i = 0; i < ATLAS_TILE_PX; ++i) {
            put_px(out_px, col, row, i, 0, 70, 48, 26, 255);
            put_px(out_px, col, row, i, 15, 70, 48, 26, 255);
            put_px(out_px, col, row, 0, i, 70, 48, 26, 255);
            put_px(out_px, col, row, 15, i, 70, 48, 26, 255);
            put_px(out_px, col, row, 7, i, 70, 48, 26, 255);
            put_px(out_px, col, row, 8, i, 70, 48, 26, 255);
            put_px(out_px, col, row, i, 7, 70, 48, 26, 255);
            put_px(out_px, col, row, i, 8, 70, 48, 26, 255);
        }
    }

    /* Planks: horizontal boards with seam lines every 4 px. */
    {
        int col = TILE_PLANKS % ATLAS_TILES;
        int row = TILE_PLANKS / ATLAS_TILES;
        for (int y = 0; y < ATLAS_TILE_PX; ++y) {
            for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                if (y % 4 == 3) {
                    put_px(out_px, col, row, x, y, 74, 52, 28, 255);
                    continue;
                }
                float r = px_rand(col, row, x, y, 78u);
                unsigned char g = (unsigned char)(150.0f + r * 22.0f);
                put_px(out_px, col, row, x, y, g, (unsigned char)(g * 0.74f), (unsigned char)(g * 0.44f),
                       255);
            }
        }
    }

    /* Apple: red round blob with stem + leaf, on transparency. */
    {
        int col = TILE_APPLE % ATLAS_TILES;
        int row = TILE_APPLE / ATLAS_TILES;
        for (int y = 4; y <= 13; ++y) {
            for (int x = 3; x <= 12; ++x) {
                int dx = (x - 7) * 2;
                int dy = (y - 8) * 2 + (x < 7 ? -1 : 1);
                if (dx * dx + dy * dy > 64) {
                    continue;
                }
                float r = px_rand(col, row, x, y, 79u);
                unsigned char c = (unsigned char)(200.0f + r * 40.0f);
                put_px(out_px, col, row, x, y, c, (unsigned char)(30.0f + r * 20.0f),
                       (unsigned char)(30.0f + r * 20.0f), 255);
            }
        }
        put_px(out_px, col, row, 7, 2, 110, 72, 40, 255);
        put_px(out_px, col, row, 7, 3, 110, 72, 40, 255);
        put_px(out_px, col, row, 8, 1, 60, 150, 60, 255);
        put_px(out_px, col, row, 9, 1, 60, 150, 60, 255);
        put_px(out_px, col, row, 9, 2, 50, 130, 50, 255);
    }

    /* Stick: diagonal brown bar, on transparency. */
    {
        int col = TILE_STICK % ATLAS_TILES;
        int row = TILE_STICK / ATLAS_TILES;
        for (int i = 0; i < 12; ++i) {
            int x = 2 + i;
            int y = 13 - i;
            put_px(out_px, col, row, x, y, 140, 100, 58, 255);
            put_px(out_px, col, row, x + 1, y, 122, 86, 50, 255);
        }
    }

    /* Bow: curved wooden limb with a taut string, on transparency. */
    {
        int col = TILE_BOW % ATLAS_TILES;
        int row = TILE_BOW / ATLAS_TILES;
        for (int y = 1; y <= 14; ++y) {
            int bend = y <= 7 ? (7 - y) / 2 : (y - 8) / 2;
            int x = 3 + bend;
            put_px(out_px, col, row, x, y, 140, 100, 58, 255);
            put_px(out_px, col, row, x + 1, y, 122, 86, 50, 255);
            put_px(out_px, col, row, 11, y, 220, 220, 220, 255);
        }
        put_px(out_px, col, row, 4, 0, 110, 78, 44, 255);
        put_px(out_px, col, row, 4, 15, 110, 78, 44, 255);
        put_px(out_px, col, row, 11, 0, 220, 220, 220, 255);
        put_px(out_px, col, row, 11, 15, 220, 220, 220, 255);
    }

    /* Arrow: diagonal shaft with head + fletching, on transparency. */
    {
        int col = TILE_ARROW % ATLAS_TILES;
        int row = TILE_ARROW / ATLAS_TILES;
        for (int i = 0; i < 10; ++i) {
            int x = 3 + i;
            int y = 12 - i;
            put_px(out_px, col, row, x, y, 150, 112, 66, 255);
        }
        put_px(out_px, col, row, 12, 2, 200, 200, 205, 255);
        put_px(out_px, col, row, 13, 1, 200, 200, 205, 255);
        put_px(out_px, col, row, 13, 2, 180, 180, 185, 255);
        put_px(out_px, col, row, 2, 13, 220, 220, 220, 255);
        put_px(out_px, col, row, 1, 13, 200, 60, 60, 255);
        put_px(out_px, col, row, 2, 14, 200, 60, 60, 255);
    }

    /* Bone: diagonal off-white shaft with knobbed ends, on transparency. */
    {
        int col = TILE_BONE % ATLAS_TILES;
        int row = TILE_BONE / ATLAS_TILES;
        for (int i = 0; i < 9; ++i) {
            int x = 4 + i;
            int y = 11 - i;
            put_px(out_px, col, row, x, y, 216, 212, 196, 255);
            put_px(out_px, col, row, x + 1, y, 200, 196, 180, 255);
        }
        put_px(out_px, col, row, 3, 11, 226, 222, 206, 255);
        put_px(out_px, col, row, 4, 12, 226, 222, 206, 255);
        put_px(out_px, col, row, 12, 3, 226, 222, 206, 255);
        put_px(out_px, col, row, 13, 4, 226, 222, 206, 255);
    }

    /* Raw beef: dark red slab with lighter fat edge, on transparency. */
    {
        int col = TILE_BEEF % ATLAS_TILES;
        int row = TILE_BEEF / ATLAS_TILES;
        for (int y = 5; y <= 11; ++y) {
            for (int x = 3; x <= 12; ++x) {
                put_px(out_px, col, row, x, y, 150, 60, 50, 255);
            }
        }
        for (int x = 3; x <= 12; ++x) {
            put_px(out_px, col, row, x, 5, 200, 140, 130, 255);
            put_px(out_px, col, row, x, 11, 120, 45, 40, 255);
        }
    }

    /* Leather: tan rounded hide with darker border, on transparency. */
    {
        int col = TILE_LEATHER % ATLAS_TILES;
        int row = TILE_LEATHER / ATLAS_TILES;
        for (int y = 4; y <= 12; ++y) {
            for (int x = 4; x <= 11; ++x) {
                put_px(out_px, col, row, x, y, 165, 108, 64, 255);
            }
        }
        for (int x = 4; x <= 11; ++x) {
            put_px(out_px, col, row, x, 4, 140, 88, 50, 255);
            put_px(out_px, col, row, x, 12, 140, 88, 50, 255);
        }
        for (int y = 4; y <= 12; ++y) {
            put_px(out_px, col, row, 4, y, 140, 88, 50, 255);
            put_px(out_px, col, row, 11, y, 140, 88, 50, 255);
        }
    }

    /* Rotten flesh: sickly green-brown slab with dark mottling. */
    {
        int col = TILE_FLESH % ATLAS_TILES;
        int row = TILE_FLESH / ATLAS_TILES;
        for (int y = 5; y <= 11; ++y) {
            for (int x = 3; x <= 12; ++x) {
                float r = px_rand(col, row, x, y, 84u);
                unsigned char g = (unsigned char)(110.0f + r * 30.0f);
                put_px(out_px, col, row, x, y, (unsigned char)(g * 0.85f), g,
                        (unsigned char)(g * 0.45f), 255);
            }
        }
        for (int x = 3; x <= 12; ++x) {
            put_px(out_px, col, row, x, 5, 90, 110, 55, 255);
            put_px(out_px, col, row, x, 11, 70, 85, 42, 255);
        }
    }

    /* First-person player palette (procedural fallback under the Steve-
     * sourced generated tiles): pixel-shaded skin and a blue sleeve
     * with a small light cuff. */
    {
        int col = TILE_PLAYER_SKIN % ATLAS_TILES;
        int row = TILE_PLAYER_SKIN / ATLAS_TILES;
        for (int y = 1; y < ATLAS_TILE_PX - 1; ++y) {
            for (int x = 1; x < ATLAS_TILE_PX - 1; ++x) {
                unsigned char red = (unsigned char)(190 + ((x + y) % 3) * 4);
                unsigned char green = (unsigned char)(137 + ((x * 2 + y) % 3) * 4);
                unsigned char blue = (unsigned char)(103 + ((x + y * 2) % 3) * 4);
                put_px(out_px, col, row, x, y, red, green, blue, 255);
            }
        }
        for (int x = 0; x < ATLAS_TILE_PX; ++x) {
            put_px(out_px, col, row, x, 0, 136, 91, 69, 255);
            put_px(out_px, col, row, x, 15, 225, 177, 143, 255);
        }
    }
    {
        int col = TILE_PLAYER_SLEEVE % ATLAS_TILES;
        int row = TILE_PLAYER_SLEEVE / ATLAS_TILES;
        tile_speckle(out_px, TILE_PLAYER_SLEEVE, 23u, 0.10f, 0.06f);
        /* Wrist cuff at the painter-bottom rows (= face bottoms = the
         * wrist end of the sleeve; a cuff at the top reads upside-down). */
        for (int x = 0; x < ATLAS_TILE_PX; ++x) {
            put_px(out_px, col, row, x, 2, 35, 70, 130, 255);
            put_px(out_px, col, row, x, 1, 216, 219, 221, 255);
        }
    }

    /* Log top: growth rings on transparency-safe full tile. */
    {
        int col = TILE_WOOD_TOP % ATLAS_TILES;
        int row = TILE_WOOD_TOP / ATLAS_TILES;
        for (int y = 0; y < ATLAS_TILE_PX; ++y) {
            for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                int dx = x - 7;
                int dy = y - 7;
                int ring = (dx * dx + dy * dy) % 5 == 0;
                float r = px_rand(col, row, x, y, 80u);
                unsigned char g = (unsigned char)(168.0f + r * 20.0f);
                if (ring) {
                    put_px(out_px, col, row, x, y, 110, 78, 44, 255);
                } else {
                    put_px(out_px, col, row, x, y, g, (unsigned char)(g * 0.72f),
                           (unsigned char)(g * 0.44f), 255);
                }
            }
        }
    }

    /* Workbench top/side (fallback grids; mcassets carry the real art). */
    {
        int tops[2] = {TILE_WORKBENCH_TOP, TILE_WORKBENCH_SIDE};
        for (int t = 0; t < 2; ++t) {
            int col = tops[t] % ATLAS_TILES;
            int row = tops[t] / ATLAS_TILES;
            for (int y = 0; y < ATLAS_TILE_PX; ++y) {
                for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                    float r = px_rand(col, row, x, y, (uint32_t)(81 + t));
                    unsigned char g = (unsigned char)(148.0f + r * 22.0f);
                    put_px(out_px, col, row, x, y, g, (unsigned char)(g * 0.72f),
                           (unsigned char)(g * 0.42f), 255);
                }
            }
        }
    }

    /* Per-material tool icons (fallback bars; mcassets carry the real art). */
    {
        static const struct {
            int tile;
            unsigned char hr, hg, hb;
        } mtools[12] = {
            {TILE_WOOD_PICKAXE, 150, 110, 70},   {TILE_STONE_PICKAXE, 150, 150, 155},
            {TILE_WOOD_AXE, 150, 110, 70},       {TILE_STONE_AXE, 150, 150, 155},
            {TILE_WOOD_SHOVEL, 150, 110, 70},    {TILE_STONE_SHOVEL, 150, 150, 155},
            {TILE_IRON_PICKAXE, 214, 214, 219},  {TILE_IRON_AXE, 214, 214, 219},
            {TILE_IRON_SHOVEL, 214, 214, 219},   {TILE_DIAMOND_PICKAXE, 108, 232, 214},
            {TILE_DIAMOND_AXE, 108, 232, 214},   {TILE_DIAMOND_SHOVEL, 108, 232, 214},
        };
        for (size_t ti = 0; ti < 12; ++ti) {
            int col = mtools[ti].tile % ATLAS_TILES;
            int row = mtools[ti].tile / ATLAS_TILES;
            for (int y = 2; y < 12; ++y) {
                put_px(out_px, col, row, 7, y, 122, 88, 52, 255);
                put_px(out_px, col, row, 8, y, 110, 78, 46, 255);
            }
            for (int x = 3; x <= 12; ++x) {
                put_px(out_px, col, row, x, 3, mtools[ti].hr, mtools[ti].hg, mtools[ti].hb, 255);
                put_px(out_px, col, row, x, 4, mtools[ti].hr, mtools[ti].hg, mtools[ti].hb, 255);
            }
        }
    }

    /* Small transparent swords drawn on a crisp diagonal. */
    {
        static const struct {
            int tile;
            unsigned char blade[3];
            unsigned char edge[3];
} swords[4] = {
            {TILE_WOOD_SWORD, {156, 112, 62}, {208, 164, 100}},
            {TILE_STONE_SWORD, {132, 139, 145}, {205, 210, 214}},
            {TILE_IRON_SWORD, {214, 214, 219}, {246, 246, 250}},
            {TILE_DIAMOND_SWORD, {108, 232, 214}, {198, 250, 242}},
        };
        for (size_t si = 0; si < 4; ++si) {
            int col = swords[si].tile % ATLAS_TILES;
            int row = swords[si].tile / ATLAS_TILES;
            for (int i = 0; i < 8; ++i) {
                int x = 5 + i;
                int y = 10 - i;
                put_px(out_px, col, row, x, y, swords[si].blade[0], swords[si].blade[1], swords[si].blade[2], 255);
                if (i < 6) {
                    put_px(out_px, col, row, x, y + 1, swords[si].edge[0], swords[si].edge[1], swords[si].edge[2], 255);
                }
            }
            /* Guard and grip. */
            put_px(out_px, col, row, 4, 9, 91, 66, 42, 255);
            put_px(out_px, col, row, 5, 10, 91, 66, 42, 255);
            put_px(out_px, col, row, 6, 11, 91, 66, 42, 255);
            put_px(out_px, col, row, 6, 9, 91, 66, 42, 255);
            put_px(out_px, col, row, 5, 11, 91, 66, 42, 255);
            put_px(out_px, col, row, 4, 13, 117, 78, 44, 255);
            put_px(out_px, col, row, 3, 14, 117, 78, 44, 255);
            put_px(out_px, col, row, 2, 15, 74, 49, 30, 255);
        }
    }

    /* Mining cracks: transparent tiles with darkening fracture specks.
     * Stage density grows so progress reads clearly on any block. */
    {
        static const int crack_tiles[5] = {TILE_CRACK0, TILE_CRACK1, TILE_CRACK2, TILE_CRACK3, TILE_CRACK4};
        for (int s = 0; s < 5; ++s) {
            int col = crack_tiles[s] % ATLAS_TILES;
            int row = crack_tiles[s] / ATLAS_TILES;
            float density = 0.05f + 0.11f * (float)s; /* 5% .. 49%. */
            for (int y = 0; y < ATLAS_TILE_PX; ++y) {
                for (int x = 0; x < ATLAS_TILE_PX; ++x) {
                    float r = px_rand(col, row, x, y, 50u + (uint32_t)s);
                    /* Bias cracks along two diagonals for a fractured look. */
                    int diag = (x + y) % 5 == 0 || (x - y + 16) % 7 == 0;
                    if (r < density && (diag || r < density * 0.4f)) {
                        put_px(out_px, col, row, x, y, 12, 10, 10, 235);
                    }
                }
            }
        }
    }
}

/* Check that all texture GL symbols are loaded.
 *
 * Args: none.
 *
 * Returns: true when usable.
 */
static bool atlas_gl_ready(void)
{
    return minec_glGenTextures != NULL && minec_glBindTexture != NULL && minec_glTexImage2D != NULL &&
           minec_glTexParameteri != NULL && minec_glDeleteTextures != NULL && minec_glActiveTexture != NULL;
}

/* Upload filled pixels as the atlas texture (caller frees px after).
 *
 * Args:
 *   px: 256*256*4 bottom-up RGBA (must not be NULL).
 *
 * Returns: GL texture id, or 0 on failure.
 */
static unsigned int atlas_upload(const unsigned char *px)
{
    unsigned int tex = 0;
    minec_glGenTextures(1, &tex);
    if (tex == 0) {
        LOG_ERROR("texture_atlas: glGenTextures returned 0");
        return 0;
    }
    minec_glBindTexture((MinecGLenum)MINEC_GL_TEXTURE_2D, tex);
    minec_glTexImage2D((MinecGLenum)MINEC_GL_TEXTURE_2D, 0, (MinecGLint)MINEC_GL_RGBA, (MinecGLsizei)ATLAS_SIZE,
                       (MinecGLsizei)ATLAS_SIZE, 0, (MinecGLenum)MINEC_GL_RGBA,
                       (MinecGLenum)MINEC_GL_UNSIGNED_BYTE, px);
    minec_glTexParameteri((MinecGLenum)MINEC_GL_TEXTURE_2D, (MinecGLenum)MINEC_GL_TEXTURE_MIN_FILTER,
                          (MinecGLint)MINEC_GL_NEAREST);
    minec_glTexParameteri((MinecGLenum)MINEC_GL_TEXTURE_2D, (MinecGLenum)MINEC_GL_TEXTURE_MAG_FILTER,
                          (MinecGLint)MINEC_GL_NEAREST);
    minec_glTexParameteri((MinecGLenum)MINEC_GL_TEXTURE_2D, (MinecGLenum)MINEC_GL_TEXTURE_WRAP_S,
                          (MinecGLint)MINEC_GL_CLAMP_TO_EDGE);
    minec_glTexParameteri((MinecGLenum)MINEC_GL_TEXTURE_2D, (MinecGLenum)MINEC_GL_TEXTURE_WRAP_T,
                          (MinecGLint)MINEC_GL_CLAMP_TO_EDGE);
    minec_glBindTexture((MinecGLenum)MINEC_GL_TEXTURE_2D, 0);
    return tex;
}

/* Owner-supplied asset root (converted mcassets live here; local data,
 * never committed — see .gitignore). Resolved exe-relative first, CWD
 * fallback second (see path_mcassets_dir).
 */

/* Forward: 16x16 tile blit (defined below with the pack machinery). */
static void atlas_blit_tile(unsigned char *px, int tile, const BmpImage *img);

/* Overlay owner-supplied converted tiles over the procedural base (one
 * summary line; missing files simply keep procedural pixels, e.g. on CI
 * where mcassets/ does not exist). Called by both GL texture builders
 * so the layer applies with or without a user pack.
 *
 * Args:
 *   px: 256*256*4 bottom-up RGBA atlas buffer (must not be NULL).
 *   dir: tile directory holding <stem>.bmp files (must not be NULL).
 */
void texture_atlas_apply_dir(unsigned char *px, const char *dir)
{
    if (px == NULL || dir == NULL) {
        return;
    }
    int loaded = 0;
    for (int t = 0; t < TILE_FILE_COUNT; ++t) {
        const char *stem = texture_atlas_tile_file(t);
        char leaf[96];
        int n = snprintf(leaf, sizeof(leaf), "%s.bmp", stem != NULL ? stem : "unknown");
        if (n <= 0 || (size_t)n >= sizeof(leaf)) {
            continue;
        }
        char file[PATH_MAX_LEN];
        if (path_join(file, sizeof(file), dir, leaf) != 0) {
            continue;
        }
        BmpImage img;
        img.px = NULL;
        if (bmp_load_file(file, &img) != 0 || !bmp_is_tile(&img)) {
            bmp_free(&img);
            continue;
        }
        atlas_blit_tile(px, t, &img);
        bmp_free(&img);
        ++loaded;
    }
    LOG_INFO("texture_atlas: dir '%s' tiles %d/%d (rest procedural)", dir, loaded, TILE_FILE_COUNT);
}

/* Owner-converted tile pass (mcassets layer). Resolves next to the
 * executable first (robust to working directory), CWD fallback second.
 */
static void texture_atlas_apply_mcassets(unsigned char *px)
{
    char dir[PATH_MAX_LEN];
    if (path_mcassets_dir(dir, sizeof(dir), "generated/tiles") != 0) {
        return;
    }
    texture_atlas_apply_dir(px, dir);
}

/* Create the GL texture object (fully procedural).
 *
 * Returns: texture id or 0.
 */
unsigned int texture_atlas_generate(void)
{
    if (!atlas_gl_ready()) {
        LOG_ERROR("texture_atlas_generate: required minec_gl_* texture symbols missing");
        return 0;
    }
    const size_t px_size = (size_t)ATLAS_SIZE * (size_t)ATLAS_SIZE * ATLAS_BYTES;
    unsigned char *px = (unsigned char *)malloc(px_size);
    if (px == NULL) {
        LOG_ERROR("texture_atlas_generate: out of memory (%zu bytes)", px_size);
        return 0;
    }
    texture_atlas_fill_rgba(px);
    texture_atlas_apply_mcassets(px);
    unsigned int tex = atlas_upload(px);
    free(px);
    if (tex != 0) {
        LOG_INFO("texture atlas generated: id %u (%dx%d RGBA8, NEAREST)", tex, ATLAS_SIZE, ATLAS_SIZE);
    }
    return tex;
}

/* Blit one 16x16 tile image into the atlas buffer (BMP is top-down rows;
 * the atlas buffer is bottom-up; flip while copying).
 */
static void atlas_blit_tile(unsigned char *px, int tile, const BmpImage *img)
{
    int col = tile % ATLAS_TILES;
    int row = tile / ATLAS_TILES;
    for (int y = 0; y < ATLAS_TILE_PX; ++y) {
        for (int x = 0; x < ATLAS_TILE_PX; ++x) {
            const unsigned char *src = img->px + ((size_t)y * 16 + (size_t)x) * 4;
            size_t gx = (size_t)(col * ATLAS_TILE_PX + x);
            size_t gy = (size_t)(row * ATLAS_TILE_PX + (ATLAS_TILE_PX - 1 - y));
            size_t o = (gy * ATLAS_SIZE + gx) * ATLAS_BYTES;
            px[o + 0] = src[0];
            px[o + 1] = src[1];
            px[o + 2] = src[2];
            px[o + 3] = src[3];
        }
    }
}

/* Reject suspicious pack names (only the listing feeds this, but validate
 * anyway: plain names, no separators, dots, or drive specs).
 */
static bool pack_name_ok(const char *pack)
{
    if (pack == NULL || pack[0] == '\0') {
        return false;
    }
    size_t n = strlen(pack);
    if (n > 48) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        char c = pack[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                  c == '-' || c == ' ';
        if (!ok) {
            return false;
        }
    }
    return true;
}

int texture_atlas_pack_tiles_dir(char *out, size_t out_cap, const char *packs_dir, const char *pack)
{
    if (out == NULL || out_cap == 0 || packs_dir == NULL || packs_dir[0] == '\0' || !pack_name_ok(pack)) {
        return -1;
    }

    char pack_dir[PATH_MAX_LEN];
    if (path_join(pack_dir, sizeof(pack_dir), packs_dir, pack) != 0 ||
        path_join(out, out_cap, pack_dir, "tiles") != 0) {
        return -1;
    }
    return 0;
}

/* Create the GL texture from a resource pack (procedural + BMP overrides).
 *
 * Args:
 *   pack: pack directory name ("Default"/NULL/empty = procedural).
 *
 * Returns: texture id or 0.
 */
unsigned int texture_atlas_generate_from_pack(const char *pack)
{
    if (!atlas_gl_ready()) {
        LOG_ERROR("texture_atlas_generate_from_pack: required minec_gl_* texture symbols missing");
        return 0;
    }
    const size_t px_size = (size_t)ATLAS_SIZE * (size_t)ATLAS_SIZE * ATLAS_BYTES;
    unsigned char *px = (unsigned char *)malloc(px_size);
    if (px == NULL) {
        LOG_ERROR("texture_atlas_generate_from_pack: out of memory (%zu bytes)", px_size);
        return 0;
    }
    texture_atlas_fill_rgba(px);
    texture_atlas_apply_mcassets(px);
    int overridden = 0;
    if (pack != NULL && pack[0] != '\0' && strcmp(pack, "Default") != 0) {
        if (!pack_name_ok(pack)) {
            LOG_WARN("texture_atlas: rejecting suspicious pack name '%s'", pack);
        } else {
            char tiles[PATH_MAX_LEN];
            char file[PATH_MAX_LEN];
            if (texture_atlas_pack_tiles_dir(tiles, sizeof(tiles), "resourcepacks", pack) != 0) {
                LOG_WARN("texture_atlas: could not build resource-pack path for '%s'", pack);
            } else {
                for (int t = 0; t < TILE_FILE_COUNT; ++t) {
                    const char *stem = texture_atlas_tile_file(t);
                    char leaf[96];
                    int n = snprintf(leaf, sizeof(leaf), "%s.bmp", stem != NULL ? stem : "unknown");
                    if (n <= 0 || (size_t)n >= sizeof(leaf)) {
                        continue;
                    }
                    if (path_join(file, sizeof(file), tiles, leaf) != 0) {
                        continue;
                    }
                    BmpImage img;
                    img.px = NULL;
                    if (bmp_load_file(file, &img) != 0 || !bmp_is_tile(&img)) {
                        bmp_free(&img); /* Missing/invalid: procedural fallback stays. */
                        continue;
                    }
                    atlas_blit_tile(px, t, &img);
                    bmp_free(&img);
                    ++overridden;
                }
                LOG_INFO("texture_atlas: pack '%s' overrode %d/%d tiles", pack, overridden, TILE_FILE_COUNT);
            }
        }
    }
    unsigned int tex = atlas_upload(px);
    free(px);
    return tex;
}

/* List installed packs ("Default" first, then valid pack dirs).
 *
 * Args:
 *   out_packs, out_cap: name buffer.
 *
 * Returns: total entries available.
 */
size_t texture_atlas_list_packs_in(const char *packs_dir, char out_packs[][64], size_t out_cap)
{
    size_t total = 1; /* "Default" always exists. */
    if (out_cap > 0 && out_packs != NULL) {
        memcpy(out_packs[0], "Default", 8);
    }
    if (packs_dir == NULL || packs_dir[0] == '\0') {
        return total;
    }
    char dirs[64][64];
    size_t ndirs = 0;
    if (path_list_dirs(packs_dir, dirs, 64, &ndirs) != 0) {
        return total;
    }
    for (size_t i = 0; i < ndirs; ++i) {
        if (!pack_name_ok(dirs[i])) {
            continue;
        }
        char tiles[PATH_MAX_LEN];
        if (texture_atlas_pack_tiles_dir(tiles, sizeof(tiles), packs_dir, dirs[i]) != 0) {
            continue;
        }
        if (!path_is_dir(tiles)) {
            continue; /* Not a pack (no tiles/); skip quietly. */
        }
        if (total < out_cap && out_packs != NULL) {
            size_t l = strlen(dirs[i]);
            if (l > 63) {
                l = 63;
            }
            memcpy(out_packs[total], dirs[i], l);
            out_packs[total][l] = '\0';
        }
        ++total;
    }
    return total;
}

size_t texture_atlas_list_packs(char out_packs[][64], size_t out_cap)
{
    return texture_atlas_list_packs_in("resourcepacks", out_packs, out_cap);
}

/* Delete a GL atlas texture.
 *
 * Args:
 *   tex: texture id.
 */
void texture_atlas_delete(unsigned int tex)
{
    if (tex == 0 || minec_glDeleteTextures == NULL) {
        return;
    }
    minec_glDeleteTextures(1, &tex);
}

/* Load an owner-converted mob skin as a GL texture (see header). The
 * BMP parser yields top-down RGBA; GL wants bottom-up, so rows flip on
 * the way into the upload buffer (one malloc, freed before return).
 */
unsigned int mob_skin_load(const char *stem, int *out_w, int *out_h)
{
    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    if (stem == NULL || stem[0] == '\0' || !atlas_gl_ready()) {
        return 0;
    }
    char dir[PATH_MAX_LEN];
    if (path_mcassets_dir(dir, sizeof(dir), "generated/mobs") != 0 || !path_is_dir(dir)) {
        return 0; /* Normal without mcassets (e.g. CI): tile path stays. */
    }
    char leaf[96];
    int n = snprintf(leaf, sizeof(leaf), "%s.bmp", stem);
    if (n <= 0 || (size_t)n >= sizeof(leaf)) {
        return 0;
    }
    char file[PATH_MAX_LEN];
    if (path_join(file, sizeof(file), dir, leaf) != 0) {
        return 0;
    }
    BmpImage img;
    img.px = NULL;
    if (bmp_load_file(file, &img) != 0) {
        bmp_free(&img);
        return 0;
    }
    if (!((img.width == 64 && img.height == 32) || (img.width == 64 && img.height == 64)) ||
        img.px == NULL) {
        bmp_free(&img);
        return 0;
    }
    size_t row_bytes = (size_t)img.width * 4;
    unsigned char *flip = (unsigned char *)malloc(row_bytes * (size_t)img.height);
    if (flip == NULL) {
        bmp_free(&img);
        return 0;
    }
    for (int y = 0; y < img.height; ++y) {
        memcpy(flip + (size_t)y * row_bytes,
               img.px + (size_t)(img.height - 1 - y) * row_bytes, row_bytes);
    }
    unsigned int tex = 0;
    minec_glGenTextures(1, &tex);
    if (tex == 0) {
        free(flip);
        bmp_free(&img);
        return 0;
    }
    minec_glBindTexture((MinecGLenum)MINEC_GL_TEXTURE_2D, tex);
    minec_glTexImage2D((MinecGLenum)MINEC_GL_TEXTURE_2D, 0, (MinecGLint)MINEC_GL_RGBA,
                       (MinecGLsizei)img.width, (MinecGLsizei)img.height, 0,
                       (MinecGLenum)MINEC_GL_RGBA, (MinecGLenum)MINEC_GL_UNSIGNED_BYTE, flip);
    minec_glTexParameteri((MinecGLenum)MINEC_GL_TEXTURE_2D, (MinecGLenum)MINEC_GL_TEXTURE_MIN_FILTER,
                          (MinecGLint)MINEC_GL_NEAREST);
    minec_glTexParameteri((MinecGLenum)MINEC_GL_TEXTURE_2D, (MinecGLenum)MINEC_GL_TEXTURE_MAG_FILTER,
                          (MinecGLint)MINEC_GL_NEAREST);
    minec_glTexParameteri((MinecGLenum)MINEC_GL_TEXTURE_2D, (MinecGLenum)MINEC_GL_TEXTURE_WRAP_S,
                          (MinecGLint)MINEC_GL_CLAMP_TO_EDGE);
    minec_glTexParameteri((MinecGLenum)MINEC_GL_TEXTURE_2D, (MinecGLenum)MINEC_GL_TEXTURE_WRAP_T,
                          (MinecGLint)MINEC_GL_CLAMP_TO_EDGE);
    minec_glBindTexture((MinecGLenum)MINEC_GL_TEXTURE_2D, 0);
    if (out_w != NULL) {
        *out_w = img.width;
    }
    if (out_h != NULL) {
        *out_h = img.height;
    }
    LOG_INFO("mob skin loaded: '%s' (%dx%d, tex %u)", stem, img.width, img.height, tex);
    free(flip);
    bmp_free(&img);
    return tex;
}
