#pragma once

/* Minimal BMP image reader (M5): uncompressed 24/32-bit BI_RGB bitmaps only.
 * Written from scratch (no third-party code) for resource-pack tile
 * overrides. Rejects everything else safely. Pure CPU, headless-testable.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decoded image (caller-owned pixels, RGBA8, top-down rows).
 * Free with bmp_free().
 */
typedef struct BmpImage {
    unsigned char *px; /* width*height*4 RGBA bytes, top-down. */
    int width;         /* Image width in pixels. */
    int height;        /* Image height in pixels. */
} BmpImage;

/* Parse BMP bytes into an RGBA image.
 * Accepts: 'BM' magic, BITMAPINFOHEADER (40 B), BI_RGB, 24 or 32 bpp.
 * Supports bottom-up (positive height) and top-down (negative height).
 * Rejects files over 4 MiB and truncations/mismatches safely.
 *
 * Args:
 *   data: file bytes (must not be NULL).
 *   len: byte count.
 *   out: receives the image (must not be NULL; px NULL on failure).
 *
 * Returns: 0 on success, non-zero on any rejection (out->px NULL).
 */
int bmp_parse(const unsigned char *data, size_t len, BmpImage *out);

/* Release image pixels (NULL-safe, zeroes the struct).
 *
 * Args:
 *   img: image to free (may be NULL).
 */
void bmp_free(BmpImage *img);

/* Load + parse a BMP file from disk (size-capped).
 *
 * Args:
 *   path: file path (must not be NULL).
 *   out: receives the image (must not be NULL).
 *
 * Returns: 0 on success, non-zero on I/O or parse failure.
 */
int bmp_load_file(const char *path, BmpImage *out);

/* Validate tile suitability: exactly 16x16.
 *
 * Args:
 *   img: parsed image (must not be NULL).
 *
 * Returns: true when 16x16 with pixels present.
 */
bool bmp_is_tile(const BmpImage *img);
