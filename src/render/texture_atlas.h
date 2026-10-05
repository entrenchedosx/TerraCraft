#pragma once

/* Procedural texture atlas (M2): 256x256 RGBA8, 16x16 grid of 16px tiles.
 * Pixels are generated deterministically at runtime (no image files).
 * NEAREST filtering (retro crisp look), CLAMP_TO_EDGE wrapping.
 *
 * CPU-side helpers (fill/uv/tile mapping) are GL-free and unit-tested;
 * only texture_atlas_generate/delete touch GL (minec_gl_*).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATLAS_SIZE 256
#define ATLAS_TILES 16
#define ATLAS_TILE_PX 16
#define ATLAS_BYTES 4

/* Tile indices (row-major: tile = row*16+col; we use the first 41). */
typedef enum AtlasTile {
    TILE_GRASS_TOP = 0,
    TILE_GRASS_SIDE = 1,
    TILE_DIRT = 2,
    TILE_STONE = 3,
    TILE_SAND = 4,
    TILE_WOOD = 5,
    TILE_LEAVES = 6,
    TILE_GLASS = 7,
    TILE_WATER = 8,
    TILE_BEDROCK = 9,
    TILE_COAL_ORE = 10,
    TILE_IRON_ORE = 11,
    TILE_GOLD_ORE = 12,
    TILE_DIAMOND_ORE = 13,
    TILE_SNOW = 14,
    TILE_GRASS_BOTTOM = 15,
    TILE_PLANT = 16,
    TILE_FLOWER = 17,
    TILE_TORCH = 18,
    TILE_TOOL_PICKAXE = 19,
    TILE_TOOL_AXE = 20,
    TILE_TOOL_SHOVEL = 21,
    TILE_COAL = 22,
    TILE_CRACK0 = 23,
    TILE_CRACK1 = 24,
    TILE_CRACK2 = 25,
    TILE_CRACK3 = 26,
    TILE_CRACK4 = 27,
    TILE_WORKBENCH = 28,
    TILE_PLANKS = 29,
    TILE_APPLE = 30,
    TILE_STICK = 31,
    TILE_WOOD_TOP = 32,
    TILE_WORKBENCH_TOP = 33,
    TILE_WORKBENCH_SIDE = 34,
    TILE_WOOD_PICKAXE = 35,
    TILE_STONE_PICKAXE = 36,
    TILE_WOOD_AXE = 37,
    TILE_STONE_AXE = 38,
    TILE_WOOD_SHOVEL = 39,
    TILE_STONE_SHOVEL = 40,
    TILE_BOW = 41,
    TILE_ARROW = 42,
    TILE_BONE = 43,
    TILE_BEEF = 44,
    TILE_LEATHER = 45,
    TILE_PLAYER_SKIN = 46,
    TILE_PLAYER_SLEEVE = 47,
    TILE_FLESH = 48
} AtlasTile;

/* Resource-pack tile file names, indexed by tile (NULL entry = no override).
 * Files live at resourcepacks/<pack>/tiles/<name>.bmp (16x16, 24/32-bit).
 * Only tiles with a name can be overridden; the rest stay procedural.
 */
const char *texture_atlas_tile_file(int tile);

/* Number of overridable tiles (entries in the filename table). */
int texture_atlas_tile_file_count(void);

/* Build <packs_dir>/<pack>/tiles without aliasing output and input buffers.
 * Exposed so pack path resolution remains headless-testable.
 */
int texture_atlas_pack_tiles_dir(char *out, size_t out_cap, const char *packs_dir, const char *pack);

/* List installed packs beneath a caller-supplied root (used by the app with
 * "resourcepacks" and by headless tests with an isolated directory).
 */
size_t texture_atlas_list_packs_in(const char *packs_dir, char out_packs[][64], size_t out_cap);

/* Block face direction, matching the mesher FACES order (-X,+X,-Y,+Y,-Z,+Z).
 * Used to pick per-face tiles (e.g. grass top vs side).
 */
typedef enum AtlasFace {
    ATLAS_FACE_NEG_X = 0,
    ATLAS_FACE_POS_X = 1,
    ATLAS_FACE_NEG_Y = 2,
    ATLAS_FACE_POS_Y = 3,
    ATLAS_FACE_NEG_Z = 4,
    ATLAS_FACE_POS_Z = 5
} AtlasFace;

/* Map a block + face to an atlas tile index.
 *
 * Args:
 *   block: block ID (BlockType).
 *   face: AtlasFace 0..5 (out-of-range faces map like sides).
 *
 * Returns: tile index 0..255 (blocks without art map to STONE).
 */
int block_tile_for_face(uint16_t block, int face);

/* UV rectangle for a tile, with half-texel inset to prevent NEAREST bleed.
 * v=0 is the texture bottom (matches glTexImage2D upload order).
 *
 * Args:
 *   tile: tile index 0..255 (clamped).
 *   out_u0/out_v0/out_u1/out_v1: receive UV corners (each may be NULL).
 */
void texture_atlas_tile_uv(int tile, float *out_u0, float *out_v0, float *out_u1, float *out_v1);

/* Fill a 256x256x4 RGBA buffer (bottom-up rows, GL upload order).
 *
 * Args:
 *   out_px: destination (must hold ATLAS_SIZE*ATLAS_SIZE*4 bytes).
 */
void texture_atlas_fill_rgba(unsigned char *out_px);

/* Overlay a directory of <stem>.bmp tiles over a filled atlas buffer
 * (owner-converted or user content; missing/invalid files keep whatever
 * pixels are already there). CPU-side and headless-testable.
 *
 * Args:
 *   px: 256*256*4 bottom-up RGBA buffer (must not be NULL).
 *   dir: tile directory (must not be NULL).
 */
void texture_atlas_apply_dir(unsigned char *px, const char *dir);

/* Create the GL texture object from generated pixels (NEAREST, CLAMP).
 * Requires a current GL context and loaded minec_gl_* texture symbols.
 *
 * Returns: GL texture id, or 0 on failure.
 */
unsigned int texture_atlas_generate(void);

/* Create the GL texture from a user resource pack: procedural pixels first,
 * then per-tile BMP overrides from resourcepacks/<pack>/tiles/. NULL,
 * empty, or "Default" pack means fully procedural. Missing/invalid files
 * fall back to procedural pixels with a warning (never fatal).
 * Requires a current GL context.
 *
 * Args:
 *   pack: pack directory name (must be a plain name, see docs/RESOURCE_PACKS.md).
 *
 * Returns: GL texture id, or 0 on failure.
 */
unsigned int texture_atlas_generate_from_pack(const char *pack);

/* List installed resource packs (subdirectories of resourcepacks/ holding
 * a tiles/ directory), plus the implicit "Default" procedural entry first.
 *
 * Args:
 *   out_packs: [out_cap][64] name buffer (must not be NULL).
 *   out_cap: max entries (0 allowed to query nothing).
 *
 * Returns: total entries available (may exceed out_cap; first entry is
 * always "Default" when out_cap > 0).
 */
size_t texture_atlas_list_packs(char out_packs[][64], size_t out_cap);

/* Delete a GL atlas texture. No-op for 0 or when GL symbols are missing.
 *
 * Args:
 *   tex: texture id to delete (0 is ignored).
 */
void texture_atlas_delete(unsigned int tex);

/* Load an owner-converted mob skin as a GL texture (NEAREST, CLAMP).
 * Skins keep native size (64x32 or 64x64); missing/invalid files return
 * 0 and the caller keeps the procedural tile path (e.g. on CI without
 * mcassets/). Requires a current GL context.
 *
 * Args:
 *   stem: generated skin name, e.g. "skeleton" (must not be NULL).
 *   out_w/out_h: receive native size (each may be NULL).
 *
 * Returns: GL texture id, or 0 when unavailable.
 */
unsigned int mob_skin_load(const char *stem, int *out_w, int *out_h);
