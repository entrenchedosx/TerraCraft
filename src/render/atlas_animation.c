#include "render/texture_atlas.h"
#include "core/bmp.h"
#include "core/path.h"
#include "platform/gl_ctx.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct FluidAnimation {
    BmpImage image; /* One 16px frame per game tick, in chronological order. */
    int tile;
    int tick;
} FluidAnimation;

struct AtlasAnimations {
    FluidAnimation fluids[2];
};

AtlasAnimations *texture_atlas_animations_create(const char *pack)
{
    AtlasAnimations *a = (AtlasAnimations *)calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    static const char *names[2] = {"water", "lava"};
    for (int i = 0; i < 2; ++i) {
        FluidAnimation *f = &a->fluids[i];
        f->tile = i == 0 ? TILE_WATER : TILE_LAVA;
        f->tick = -1;
        char leaf[96], tiles[PATH_MAX_LEN], path[PATH_MAX_LEN];
        snprintf(leaf, sizeof(leaf), "%s.bmp", names[i]);
        if (pack && strcmp(pack, "Default") != 0 &&
            texture_atlas_pack_tiles_dir(tiles, sizeof(tiles), "resourcepacks", pack) == 0 &&
            path_join(path, sizeof(path), tiles, leaf) == 0) {
            BmpImage override = {0};
            bool custom = bmp_load_file(path, &override) == 0 && bmp_is_tile(&override);
            bmp_free(&override);
            if (custom)
                continue;
        }
        snprintf(path, sizeof(path), "generated/animations/%s.bmp", names[i]);
        if (bmp_load_mcasset(path, 0, 0, &f->image) != 0 || f->image.width != 16 || f->image.height < 16 ||
            f->image.height % 16 != 0) {
            bmp_free(&f->image);
            /* A clean checkout still animates its original fallback art. */
            f->image.width = 16;
            f->image.height = 16 * 32;
            f->image.px = (unsigned char *)malloc(16 * 16 * 32 * 4);
            if (!f->image.px)
                continue;
            for (int tick = 0; tick < 32; ++tick)
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x) {
                        float phase = (float)tick * 0.19634954f;
                        float ripple =
                            sinf((float)x * 0.78539816f + phase) * 0.6f + sinf((float)y * 0.78539816f - phase) * 0.4f;
                        unsigned char *p = f->image.px + ((tick * 16 + y) * 16 + x) * 4;
                        if (i == 0) {
                            p[0] = p[1] = p[2] = (unsigned char)(166 + ripple * 22);
                            p[3] = 180;
                        } else {
                            p[0] = (unsigned char)(228 + ripple * 25);
                            p[1] = (unsigned char)(83 + ripple * 32);
                            p[2] = (unsigned char)(12 + ripple * 6);
                            p[3] = 255;
                        }
                    }
        }
    }
    return a;
}

void texture_atlas_animations_update(AtlasAnimations *a, unsigned int atlas, double seconds)
{
    if (!a || !atlas || !minec_glTexSubImage2D || !minec_glBindTexture || !isfinite(seconds))
        return;
    for (int i = 0; i < 2; ++i) {
        FluidAnimation *f = &a->fluids[i];
        if (!f->image.px)
            continue;
        int count = f->image.height / 16;
        int tick = (int)fmod(floor(fmax(0.0, seconds) * 20.0), count);
        if (tick == f->tick)
            continue;
        f->tick = tick;
        unsigned char frame[16 * 16 * 4], mip[16 * 16 * 4];
        for (int y = 0; y < 16; ++y)
            memcpy(frame + y * 16 * 4, f->image.px + ((tick * 16 + 15 - y) * 16) * 4, 16 * 4);
        minec_glBindTexture(MINEC_GL_TEXTURE_2D, atlas);
        const unsigned char *pixels = frame;
        int size = 16;
        for (int level = 0; level <= 4; ++level) {
            minec_glTexSubImage2D(MINEC_GL_TEXTURE_2D, level, (f->tile % 16) * size, (f->tile / 16) * size, size, size,
                                  MINEC_GL_RGBA, MINEC_GL_UNSIGNED_BYTE, pixels);
            if (size == 1)
                break;
            texture_atlas_downsample(pixels, size, size, mip);
            pixels = mip;
            size /= 2;
        }
    }
}

void texture_atlas_animations_destroy(AtlasAnimations *a)
{
    if (!a)
        return;
    for (int i = 0; i < 2; ++i)
        bmp_free(&a->fluids[i].image);
    free(a);
}
