#include "core/bmp.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Hard cap: 4 MiB input (a 16x16 tile is ~1 KiB; anything bigger is junk). */
#define BMP_MAX_BYTES (4u * 1024u * 1024u)

/* Little-endian readers (BMP is LE; explicit to stay endian-clean). */
static uint16_t bmp_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t bmp_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t bmp_i32(const unsigned char *p)
{
    uint32_t u = bmp_u32(p);
    int32_t v = 0;
    memcpy(&v, &u, sizeof(v));
    return v;
}

/* Release pixels. */
void bmp_free(BmpImage *img)
{
    if (img == NULL) {
        return;
    }
    free(img->px);
    img->px = NULL;
    img->width = 0;
    img->height = 0;
}

/* Parse BMP bytes. */
int bmp_parse(const unsigned char *data, size_t len, BmpImage *out)
{
    if (out == NULL) {
        return -1;
    }
    out->px = NULL;
    out->width = 0;
    out->height = 0;
    if (data == NULL || len < 54) {
        return -2;
    }
    if (data[0] != 'B' || data[1] != 'M') {
        return -3;
    }
    uint32_t data_off = bmp_u32(data + 10);
    uint32_t dib = bmp_u32(data + 14);
    if (dib != 40) {
        return -4; /* Only BITMAPINFOHEADER. */
    }
    int32_t w = bmp_i32(data + 18);
    int32_t h = bmp_i32(data + 22);
    uint16_t planes = bmp_u16(data + 26);
    uint16_t bpp = bmp_u16(data + 28);
    uint32_t comp = bmp_u32(data + 30);
    if (planes != 1 || comp != 0 || (bpp != 24 && bpp != 32)) {
        return -5;
    }
    bool top_down = false;
    if (h < 0) {
        top_down = true;
        h = -h;
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        return -6;
    }
    size_t row_stride = ((size_t)w * (bpp / 8u) + 3u) & ~(size_t)3u;
    size_t need = (size_t)data_off + row_stride * (size_t)h;
    if (need > len || need > BMP_MAX_BYTES) {
        return -7;
    }
    size_t px_count = (size_t)w * (size_t)h;
    if (px_count > BMP_MAX_BYTES / 4) {
        return -7;
    }
    unsigned char *px = (unsigned char *)malloc(px_count * 4);
    if (px == NULL) {
        LOG_ERROR("bmp_parse: out of memory (%dx%d)", w, h);
        return -8;
    }
    size_t bpp_bytes = (size_t)bpp / 8u;
    for (int y = 0; y < h; ++y) {
        int src_row = top_down ? y : (h - 1 - y);
        const unsigned char *src = data + data_off + (size_t)src_row * row_stride;
        unsigned char *dst = px + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; ++x) {
            dst[x * 4 + 0] = src[x * bpp_bytes + 2];
            dst[x * 4 + 1] = src[x * bpp_bytes + 1];
            dst[x * 4 + 2] = src[x * bpp_bytes + 0];
            dst[x * 4 + 3] = (bpp_bytes == 4) ? src[x * 4 + 3] : 255;
        }
    }
    out->px = px;
    out->width = w;
    out->height = h;
    return 0;
}

/* Load a BMP file (size-capped). */
int bmp_load_file(const char *path, BmpImage *out)
{
    if (out == NULL) {
        return -1;
    }
    out->px = NULL;
    out->width = 0;
    out->height = 0;
    if (path == NULL) {
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -2;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -3;
    }
    long sz = ftell(f);
    if (sz <= 0 || (unsigned long)sz > BMP_MAX_BYTES) {
        fclose(f);
        return -4;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -3;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (buf == NULL) {
        fclose(f);
        LOG_ERROR("bmp_load_file: out of memory (%ld bytes)", sz);
        return -5;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return -6;
    }
    int rc = bmp_parse(buf, (size_t)sz, out);
    free(buf);
    return rc;
}

/* 16x16 tile check. */
bool bmp_is_tile(const BmpImage *img)
{
    return img != NULL && img->px != NULL && img->width == 16 && img->height == 16;
}
