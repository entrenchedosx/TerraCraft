#pragma once

#include <stdbool.h>

/* Optional native-resolution menu environment. All calls require the current
 * OpenGL 3.3 context. The renderer owns the returned handle; missing/invalid
 * owner-local faces return NULL so its original fallback remains available. */
typedef struct MenuPanorama MenuPanorama;

MenuPanorama *menu_panorama_create(void);
void menu_panorama_destroy(MenuPanorama *panorama);

/* Perspective camera with a 70-degree vertical field of view. Width/height
 * determine aspect; the caller owns the actual framebuffer viewport. The
 * pass restores incoming depth, blend, cull, scissor and binding state. */
bool menu_panorama_draw(MenuPanorama *panorama, int width, int height, float seconds);
