#include "ui/hud.h"
#include "game/inventory.h"
#include "game/item.h"
#include "render/texture_atlas.h"

#include <math.h>
#include <stddef.h>

/* Icon size is local (visual detail); slot geometry comes from hud.h so
 * hud_hotbar_slot() and the builder can never drift apart. */
#define HUD_SLOT HUD_SLOT_PX
#define HUD_GAP HUD_SLOT_GAP
#define HUD_ICON 24

/* Append one quad as two triangles (6 verts). Silently drops on overflow. */
static void hud_quad(HudFrame *f, float x0, float y0, float x1, float y1, float r, float g, float b,
                     float a)
{
    if (f->count + 6 > HUD_MAX_VERTS) {
        return;
    }
    float *v = f->verts + f->count * HUD_FLOATS_PER_VERT;
    /* Triangle 1: (x0,y0) (x1,y0) (x0,y1). */
    v[0] = x0;
    v[1] = y0;
    v[2] = r;
    v[3] = g;
    v[4] = b;
    v[5] = a;
    v[6] = x1;
    v[7] = y0;
    v[8] = r;
    v[9] = g;
    v[10] = b;
    v[11] = a;
    v[12] = x0;
    v[13] = y1;
    v[14] = r;
    v[15] = g;
    v[16] = b;
    v[17] = a;
    /* Triangle 2: (x0,y1) (x1,y0) (x1,y1). */
    v[18] = x0;
    v[19] = y1;
    v[20] = r;
    v[21] = g;
    v[22] = b;
    v[23] = a;
    v[24] = x1;
    v[25] = y0;
    v[26] = r;
    v[27] = g;
    v[28] = b;
    v[29] = a;
    v[30] = x1;
    v[31] = y1;
    v[32] = r;
    v[33] = g;
    v[34] = b;
    v[35] = a;
    f->count += 6;
}

/* Build the HUD frame.
 *
 * Args:
 *   f: frame to fill.
 *   width, height: viewport.
 *   hotbar: 9 ItemStacks (slots 0..8).
 *   hotbar_sel: selected index.
 */
void hud_build(HudFrame *f, int width, int height, const ItemStack *hotbar, int hotbar_sel)
{
    if (f == NULL) {
        return;
    }
    f->count = 0;
    if (width <= 0 || height <= 0 || hotbar == NULL) {
        return;
    }
    if (hotbar_sel < 0) {
        hotbar_sel = 0;
    }
    if (hotbar_sel >= HUD_HOTBAR_SLOTS) {
        hotbar_sel = HUD_HOTBAR_SLOTS - 1;
    }
    (void)hotbar; /* Icons are textured quads now (see icons_push), not HUD verts. */

    float w = (float)width;
    float h = (float)height;

    /* Crosshair: white plus at the center. */
    float cx = w * 0.5f;
    float cy = h * 0.5f;
    hud_quad(f, cx - 8.0f, cy - 1.0f, cx + 8.0f, cy + 1.0f, 1.0f, 1.0f, 1.0f, 0.9f);
    hud_quad(f, cx - 1.0f, cy - 8.0f, cx + 1.0f, cy + 8.0f, 1.0f, 1.0f, 1.0f, 0.9f);

    /* Hotbar strip geometry. */
    float total = (float)(HUD_HOTBAR_SLOTS * HUD_SLOT + (HUD_HOTBAR_SLOTS - 1) * HUD_GAP);
    float x0 = (w - total) * 0.5f;
    float y0 = h - (float)HUD_BOTTOM_MARGIN - (float)HUD_SLOT;

    /* Selected-slot highlight (white backing quad, slightly larger). */
    float sel_x = x0 + (float)hotbar_sel * (float)(HUD_SLOT + HUD_GAP);
    hud_quad(f, sel_x - 2.0f, y0 - 2.0f, sel_x + (float)HUD_SLOT + 2.0f, y0 + (float)HUD_SLOT + 2.0f, 1.0f,
             1.0f, 1.0f, 1.0f);

    for (int i = 0; i < HUD_HOTBAR_SLOTS; ++i) {
        float sx = x0 + (float)i * (float)(HUD_SLOT + HUD_GAP);
        /* Slot background (dark translucent). Item icons are textured
         * atlas quads drawn by a separate icon pass (see icons_push). */
        hud_quad(f, sx, y0, sx + (float)HUD_SLOT, y0 + (float)HUD_SLOT, 0.08f, 0.08f, 0.08f, 0.55f);
    }
}

static const char *const HEART_OUTER[9] = {
    ".##..##..", "####.####", "#########", "#########", ".#######.",
    "..#####..", "...###...", "....#....", ".........",
};
static const char *const HEART_INNER[9] = {
    ".........", "..#...#..", ".###.###.", ".#######.", "..#####..",
    "...###...", "....#....", ".........", ".........",
};
static const char *const FOOD_OUTER[9] = {
    "...####..", "..######.", ".########", "########.", "########.",
    ".######..", "..####...", "...###...", "....###..",
};
static const char *const FOOD_INNER[9] = {
    ".........", "...###...", "..#####..", ".######..", ".######..",
    "..####...", "...##....", ".........", ".........",
};

/* Pixel-art colors: outline, empty interior, filled interior, highlight,
 * and the pale bone at the end of the food icon. */
static void hud_vital_color(int code, bool food, float *r, float *g, float *b)
{
    static const float HEART_COLORS[4][3] = {
        {0.10f, 0.035f, 0.035f}, {0.36f, 0.13f, 0.14f}, {0.82f, 0.12f, 0.16f}, {1.00f, 0.48f, 0.48f},
    };
    static const float FOOD_COLORS[5][3] = {
        {0.12f, 0.075f, 0.035f}, {0.34f, 0.25f, 0.13f}, {0.88f, 0.55f, 0.12f},
        {1.00f, 0.78f, 0.32f}, {0.88f, 0.85f, 0.71f},
    };
    int idx = code - 1;
    if (food) {
        if (idx < 0 || idx >= 5) {
            idx = 0;
        }
        *r = FOOD_COLORS[idx][0];
        *g = FOOD_COLORS[idx][1];
        *b = FOOD_COLORS[idx][2];
    } else {
        if (idx < 0 || idx >= 4) {
            idx = 0;
        }
        *r = HEART_COLORS[idx][0];
        *g = HEART_COLORS[idx][1];
        *b = HEART_COLORS[idx][2];
    }
}

/* Draw one nine-pixel HUD icon as horizontal pixel runs. This keeps the
 * classic pixel edges crisp at every viewport size without a GUI texture
 * asset or per-pixel 3D work. */
static void hud_vital_icon(HudFrame *f, float x, float y, float scale,
                           const char *const outer[9], const char *const inner[9],
                           bool food, int fill)
{
    for (int row = 0; row < 9; ++row) {
        int run_code = 0;
        int run_start = 0;
        for (int col = 0; col <= 9; ++col) {
            int code = 0;
            if (col < 9 && outer[row][col] == '#') {
                code = 1; /* Dark silhouette and edge. */
                if (inner[row][col] == '#') {
                    bool is_fill = fill == 2 || (fill == 1 && col < 4);
                    code = is_fill ? 3 : 2;
                    if (is_fill && row <= 2 && col <= 2) {
                        code = 4; /* Small glossy highlight. */
                    }
                }
                if (food && fill > 0 && row >= 7 && col >= 5) {
                    code = 5; /* Bone tip. */
                }
            }
            if (code != run_code) {
                if (run_code != 0) {
                    float cr, cg, cb;
                    hud_vital_color(run_code, food, &cr, &cg, &cb);
                    hud_quad(f, x + (float)run_start * scale, y + (float)row * scale,
                             x + (float)col * scale, y + (float)(row + 1) * scale,
                             cr, cg, cb, 1.0f);
                }
                run_code = code;
                run_start = col;
            }
        }
    }
}

static int hud_vital_fill(float value, float maximum, int index)
{
    if (!(maximum > 0.0f) || !isfinite(maximum) || !isfinite(value)) {
        return 0;
    }
    float fraction = value / maximum;
    if (!(fraction > 0.0f)) {
        return 0;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    float points = fraction * 10.0f - (float)index;
    if (points >= 1.0f) {
        return 2;
    }
    return points >= 0.5f ? 1 : 0;
}

/* Append ten health hearts and ten hunger icons above the hotbar. A half
 * icon represents one health/food point; values stay continuous in the
 * simulation and are only quantized for display. */
void hud_build_vitals(HudFrame *f, int width, int height, float health, float max_health,
                      float hunger, float max_hunger)
{
    if (f == NULL || width <= 0 || height <= 0) {
        return;
    }
    float scale = fminf(2.0f, ((float)width - 16.0f) / 202.0f);
    if (!(scale > 0.0f) || !isfinite(scale)) {
        return;
    }
    float icon_width = 9.0f * scale;
    float stride = 10.0f * scale;
    float span = 9.0f * stride + icon_width;
    float center = (float)width * 0.5f;
    float gap = 4.0f * scale;
    float hx = center - gap * 0.5f - span;
    float fx = center + gap * 0.5f;
    float y = (float)height - (float)HUD_BOTTOM_MARGIN - (float)HUD_SLOT - 9.0f * scale - 5.0f * scale;
    if (y < 1.0f) {
        y = 1.0f;
    }
    for (int i = 0; i < 10; ++i) {
        hud_vital_icon(f, hx + (float)i * stride, y, scale, HEART_OUTER, HEART_INNER,
                       false, hud_vital_fill(health, max_health, i));
        hud_vital_icon(f, fx + (float)i * stride, y, scale, FOOD_OUTER, FOOD_INNER,
                       true, hud_vital_fill(hunger, max_hunger, i));
    }
}

/* Top-left corner of a hotbar slot cell (matches hud_build layout).
 *
 * Args:
 *   width, height: viewport.
 *   slot: 0..8.
 *   out_x/out_y: receivers.
 */
void hud_hotbar_slot(int width, int height, int slot, float *out_x, float *out_y)
{
    float x = 0.0f;
    float y = 0.0f;
    if (width > 0 && height > 0 && slot >= 0 && slot < HUD_HOTBAR_SLOTS) {
        float total = (float)(HUD_HOTBAR_SLOTS * HUD_SLOT + (HUD_HOTBAR_SLOTS - 1) * HUD_GAP);
        x = ((float)width - total) * 0.5f + (float)slot * (float)(HUD_SLOT + HUD_GAP);
        y = (float)height - (float)HUD_BOTTOM_MARGIN - (float)HUD_SLOT;
    }
    if (out_x != NULL) {
        *out_x = x;
    }
    if (out_y != NULL) {
        *out_y = y;
    }
}

/* Zero an icon batch. */
void icons_clear(IconBatch *b)
{
    if (b == NULL) {
        return;
    }
    b->quads = 0;
}

/* Append one icon quad (two triangles, 6 verts of 4 floats). */
void icons_push(IconBatch *b, float x, float y, float size, int tile)
{
    if (b == NULL || !(size > 0.0f) || b->quads >= ICON_MAX_QUADS) {
        return;
    }
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    texture_atlas_tile_uv(tile, &u0, &v0, &u1, &v1);
    /* Screen top edge samples the tile top (v1); screen bottom samples v0. */
    float *v = b->verts + (size_t)b->quads * 6 * ICON_FLOATS_PER_VERT;
    float x1 = x + size;
    float y1 = y + size;
    /* Triangle 1: top-left, top-right, bottom-left. */
    v[0] = x;
    v[1] = y;
    v[2] = u0;
    v[3] = v1;
    v[4] = x1;
    v[5] = y;
    v[6] = u1;
    v[7] = v1;
    v[8] = x;
    v[9] = y1;
    v[10] = u0;
    v[11] = v0;
    /* Triangle 2: bottom-left, top-right, bottom-right. */
    v[12] = x;
    v[13] = y1;
    v[14] = u0;
    v[15] = v0;
    v[16] = x1;
    v[17] = y;
    v[18] = u1;
    v[19] = v1;
    v[20] = x1;
    v[21] = y1;
    v[22] = u1;
    v[23] = v0;
    b->quads++;
}

/* Hotbar strip origin (shared by the durability/eat bars). */
static void hud_strip(float width, float height, float *out_x0, float *out_y0, float *out_total)
{
    float total = (float)(HUD_HOTBAR_SLOTS * HUD_SLOT + (HUD_HOTBAR_SLOTS - 1) * HUD_GAP);
    float x0 = (width - total) * 0.5f;
    float y0 = height - (float)HUD_BOTTOM_MARGIN - (float)HUD_SLOT;
    if (out_x0 != NULL) {
        *out_x0 = x0;
    }
    if (out_y0 != NULL) {
        *out_y0 = y0;
    }
    if (out_total != NULL) {
        *out_total = total;
    }
}

/* Durability bars under damageable hotbar icons. */
void hud_build_durability(HudFrame *f, int width, int height, const ItemStack *hotbar)
{
    if (f == NULL || width <= 0 || height <= 0 || hotbar == NULL) {
        return;
    }
    float x0 = 0.0f;
    float y0 = 0.0f;
    hud_strip((float)width, (float)height, &x0, &y0, NULL);
    for (int i = 0; i < HUD_HOTBAR_SLOTS; ++i) {
        const ItemStack *s = &hotbar[i];
        if (stack_is_empty(s)) {
            continue;
        }
        const ItemInfo *info = item_get_info(s->item);
        if (info->max_durability == 0) {
            continue;
        }
        float frac = 1.0f;
        if (s->durability < info->max_durability) {
            frac = 1.0f - (float)s->durability / (float)info->max_durability;
        } else {
            frac = 0.0f;
        }
        float sx = x0 + (float)i * (float)(HUD_SLOT + HUD_GAP);
        float bx = sx + ((float)HUD_SLOT - 24.0f) * 0.5f;
        float by = y0 + (float)HUD_SLOT - 5.0f;
        hud_quad(f, bx, by, bx + 24.0f, by + 3.0f, 0.08f, 0.08f, 0.08f, 0.8f);
        float r = 1.0f - frac;
        float g = frac;
        hud_quad(f, bx + 1.0f, by + 1.0f, bx + 1.0f + 22.0f * frac, by + 2.0f, r, g, 0.15f, 1.0f);
    }
}

/* Eating progress bar above the hotbar. */
void hud_build_eat(HudFrame *f, int width, int height, float frac)
{
    if (f == NULL || width <= 0 || height <= 0) {
        return;
    }
    if (!(frac >= 0.0f) || frac >= 1.0f) {
        return;
    }
    if (frac > 1.0f) {
        frac = 1.0f;
    }
    float x0 = 0.0f;
    float y0 = 0.0f;
    float total = 0.0f;
    hud_strip((float)width, (float)height, &x0, &y0, &total);
    float bw = 120.0f;
    float bx = x0 + (total - bw) * 0.5f;
    float by = y0 - 16.0f;
    hud_quad(f, bx, by, bx + bw, by + 5.0f, 0.08f, 0.08f, 0.08f, 0.7f);
    hud_quad(f, bx + 1.0f, by + 1.0f, bx + 1.0f + (bw - 2.0f) * frac, by + 4.0f, 0.35f, 0.75f, 0.35f,
             1.0f);
}

/* Bow draw charge bar under the crosshair (amber, flashes pale at full). */
void hud_build_bow(HudFrame *f, int width, int height, float frac)
{
    if (f == NULL || width <= 0 || height <= 0) {
        return;
    }
    if (!(frac >= 0.0f)) {
        return;
    }
    if (frac > 1.0f) {
        frac = 1.0f;
    }
    float cx = (float)width * 0.5f;
    float cy = (float)height * 0.5f;
    float bw = 96.0f;
    float bx = cx - bw * 0.5f;
    float by = cy + 16.0f;
    hud_quad(f, bx, by, bx + bw, by + 5.0f, 0.08f, 0.08f, 0.08f, 0.7f);
    bool full = frac >= 1.0f;
    float r = full ? 1.0f : 0.85f;
    float g = full ? 1.0f : 0.55f;
    hud_quad(f, bx + 1.0f, by + 1.0f, bx + 1.0f + (bw - 2.0f) * frac, by + 4.0f, r, g, 0.15f, 1.0f);
}

/* Fullscreen damage flash (translucent red). */
void hud_build_flash(HudFrame *f, int width, int height, float amount)
{
    if (f == NULL || width <= 0 || height <= 0) {
        return;
    }
    if (!(amount > 0.0f)) {
        return;
    }
    if (amount > 1.0f) {
        amount = 1.0f;
    }
    hud_quad(f, 0.0f, 0.0f, (float)width, (float)height, 0.75f, 0.05f, 0.05f, 0.35f * amount);
}
