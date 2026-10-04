#pragma once

/* 2D HUD geometry builder (M6): crosshair + 9-slot hotbar with selection
 * highlight, item-color icons, health/hunger bars. Pure CPU (y-down pixel
 * space); the renderer uploads and draws the produced vertices.
 * Headless-testable.
 *
 * Vertex format: x, y, r, g, b, a (6 floats, non-indexed triangles).
 */

#include <stddef.h>
#include <stdint.h>

/* Forward declaration (full type in game/inventory.h). */
typedef struct ItemStack ItemStack;

#define HUD_FLOATS_PER_VERT 6
#define HUD_MAX_VERTS 512
#define HUD_HOTBAR_SLOTS 9
#define HUD_SLOT_PX 40
#define HUD_SLOT_GAP 4
#define HUD_BOTTOM_MARGIN 12

/* One frame of HUD vertices (stack-allocatable, ~12 KiB). */
typedef struct HudFrame {
    float verts[HUD_MAX_VERTS * HUD_FLOATS_PER_VERT];
    size_t count; /* Vertices stored (always a multiple of 3). */
} HudFrame;

/* Build crosshair + hotbar geometry for a viewport. Icons use item colors;
 * empty slots render only the background. hotbar_sel is clamped.
 * Frame count is 0 on bad args (NULL frame/stocks, non-positive viewport).
 *
 * Args:
 *   f: frame to fill (must not be NULL).
 *   width, height: viewport in pixels (> 0).
 *   hotbar: 9 ItemStacks, slots 0..8 (must not be NULL).
 *   hotbar_sel: selected slot index.
 */
void hud_build(HudFrame *f, int width, int height, const ItemStack *hotbar, int hotbar_sel);

/* Append health + hunger bars above the hotbar (survival only; callers
 * skip this in creative). Each bar: dark backing + fill proportional to
 * value/max (clamped). No-op on bad args; silently drops on overflow.
 *
 * Args:
 *   f: frame to append to (must not be NULL).
 *   width, height: viewport in pixels (> 0).
 *   health, max_health: hearts value (bar full at max).
 *   hunger, max_hunger: food value.
 */
void hud_build_vitals(HudFrame *f, int width, int height, float health, float max_health, float hunger,
                      float max_hunger);

/* Top-left corner of a hotbar slot cell (for count labels/tooltips).
 * Matches hud_build layout exactly. Outputs zeroed on bad slot.
 *
 * Args:
 *   width, height: viewport in pixels.
 *   slot: 0..8.
 *   out_x/out_y: receivers (each may be NULL).
 */
void hud_hotbar_slot(int width, int height, int slot, float *out_x, float *out_y);

/* Textured item-icon batch (M7.1): atlas-tile quads for hotbar, inventory
 * grids, and cursor stacks. CPU-side geometry (y-down pixel space), drawn
 * by renderer_draw_item_icons. Vertex format per vert: x, y, u, v
 * (4 floats, non-indexed triangles, 6 verts per quad).
 */
#define ICON_FLOATS_PER_VERT 4
#define ICON_MAX_QUADS 64

/* One batch of icon quads (stack-allocatable, ~6 KiB). */
typedef struct IconBatch {
    float verts[ICON_MAX_QUADS * 6 * ICON_FLOATS_PER_VERT];
    int quads; /* Quads stored (0..ICON_MAX_QUADS). */
} IconBatch;

/* Zero a batch (call before pushing).
 *
 * Args:
 *   b: batch (must not be NULL).
 */
void icons_clear(IconBatch *b);

/* Append one item icon quad (top-left origin, y-down). The tile's top
 * texel row maps to the quad's top edge (v1 at y, v0 at y+size).
 * Silently dropped on bad args (NULL batch, non-positive size,
 * negative coords are allowed) or when full.
 *
 * Args:
 *   b: batch (must not be NULL).
 *   x, y: top-left corner in pixels.
 *   size: edge length in pixels (> 0).
 *   tile: atlas tile index (clamped by the UV helper).
 */
void icons_push(IconBatch *b, float x, float y, float size, int tile);
/* Append tool-durability bars under damageable hotbar icons (M7). Each bar
 * is 24 px wide, filled proportionally to remaining uses (green to red).
 * Non-damageable stacks draw nothing. Silently drops on overflow.
 *
 * Args:
 *   f: frame to append to (must not be NULL).
 *   width, height: viewport in pixels (> 0).
 *   hotbar: 9 ItemStacks, slots 0..8 (must not be NULL).
 */
void hud_build_durability(HudFrame *f, int width, int height, const ItemStack *hotbar);

/* Append an eating progress bar above the hotbar (M7): thin bar, fills
 * 0..1 while eating. frac < 0 (or >= 1) draws nothing. No-op on bad args.
 *
 * Args:
 *   f: frame to append to (must not be NULL).
 *   width, height: viewport in pixels (> 0).
 *   frac: eat progress 0..1 (< 0 hides).
 */
void hud_build_eat(HudFrame *f, int width, int height, float frac);

/* Append a bow-draw charge bar under the crosshair (M9): thin amber bar,
 * fills 0..1 while drawing, pale at full draw. frac < 0 draws nothing.
 * No-op on bad args.
 *
 * Args:
 *   f: frame to append to (must not be NULL).
 *   width, height: viewport in pixels (> 0).
 *   frac: draw charge 0..1 (< 0 hides).
 */
void hud_build_bow(HudFrame *f, int width, int height, float frac);

/* Append a fullscreen damage flash (M7): translucent red, alpha scaled by
 * amount (0 draws nothing, 1 is full-strength). No-op on bad args.
 *
 * Args:
 *   f: frame to append to (must not be NULL).
 *   width, height: viewport in pixels (> 0).
 *   amount: flash strength 0..1 (clamped).
 */
void hud_build_flash(HudFrame *f, int width, int height, float amount);
