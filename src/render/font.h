#pragma once

/* Original 8x8 TerraCraft bitmap fallback (ASCII 32..126), baked from the
 * OFL-licensed Monocraft font. MSB of each row byte is the left pixel.
 * Pure CPU, headless-testable. The renderer may use an owner-local Minecraft
 * font sheet when available; this fallback remains for clean checkouts.
 */

#include <stddef.h>
#include <stdint.h>

/* Font metrics. */
#define FONT_W 8
#define FONT_H 8
#define FONT_FIRST 32
#define FONT_COUNT 95

/* Copy the 8 row bytes for a character (out-of-range -> blank space).
 *
 * Args:
 *   c: character code.
 *   out_rows: receives 8 bytes, MSB-left (must not be NULL).
 */
void font_glyph(char c, uint8_t out_rows[8]);

/* Proportional advance in bitmap pixels (includes one pixel of spacing;
 * spaces advance four pixels). Unknown characters use the space advance.
 */
int font_advance(char c);

/* Measure text extents (supports '\n' line breaks; '\t' = 4 spaces).
 *
 * Args:
 *   text: NUL-terminated string (NULL measures as empty).
 *   scale: pixel scale per font pixel (> 0; invalid falls back to 1).
 *   out_w/out_h: receive pixel extents (each may be NULL).
 */
void font_measure(const char *text, float scale, float *out_w, float *out_h);

/* Emit one screen quad per set font pixel (y-down origin at x,y top-left).
 * Unknown/control chars (except '\n', '\t') render blank but advance.
 *
 * Args:
 *   text: string to rasterize (NULL writes nothing).
 *   x, y: top-left origin in pixels.
 *   scale: pixel scale per font pixel.
 *   r, g, b, a: quad color.
 *   out_verts: destination for x,y,r,g,b,a floats (must not be NULL).
 *   cap_verts: capacity in vertices.
 *
 * Returns: vertices written (always a multiple of 6).
 */
size_t font_build_quads(const char *text, float x, float y, float scale, float r, float g, float b, float a,
                        float *out_verts, size_t cap_verts);
