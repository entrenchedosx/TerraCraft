/* Repeatable GPU visual check. Uses the real renderer and exports its own
 * framebuffer, without keyboard/mouse input or opening any player save. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include "core/log.h"
#include "platform/window.h"
#include "platform/gl_ctx.h"
#include "render/renderer.h"
#include "render/camera.h"
#include "render/texture_atlas.h"
#include "game/hand_view.h"
#include "game/player.h"
#include "game/player_animation.h"
#include "game/time_system.h"
#include "game/inventory.h"
#include "world/world.h"
#include "world/chunk.h"
#include "world/world_gen.h"
#include "world/block.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write32(unsigned char *p, uint32_t x)
{
    for (int i = 0; i < 4; ++i)
        p[i] = (unsigned char)(x >> (i * 8));
}

static int export_frame(const char *path, int width, int height)
{
    size_t bytes = (size_t)width * height * 4;
    unsigned char *pixels = (unsigned char *)malloc(bytes);
    if (!pixels)
        return 1;
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    unsigned int error = glGetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "OpenGL error: %u\n", error);
        free(pixels);
        return 1;
    }
    unsigned char header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    write32(header + 2, (uint32_t)(54 + bytes));
    write32(header + 10, 54);
    write32(header + 14, 40);
    write32(header + 18, (uint32_t)width);
    write32(header + 22, (uint32_t)height);
    header[26] = 1;
    header[28] = 32;
    write32(header + 34, (uint32_t)bytes);
    for (size_t i = 0; i < bytes; i += 4) {
        unsigned char t = pixels[i];
        pixels[i] = pixels[i + 2];
        pixels[i + 2] = t;
        pixels[i + 3] = 255;
    }
    FILE *file = fopen(path, "wb");
    int ok = file && fwrite(header, 1, 54, file) == 54 && fwrite(pixels, 1, bytes, file) == bytes;
    if (file && fclose(file) != 0)
        ok = 0;
    free(pixels);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 6 ||
        (strcmp(argv[1], "snow") && strcmp(argv[1], "water") && strcmp(argv[1], "forest") &&
         strcmp(argv[1], "underwater") && strcmp(argv[1], "cave") && strcmp(argv[1], "cave-dark") &&
         strncmp(argv[1], "third-", 6))) {
        fprintf(stderr, "Usage: visual_probe snow|water|forest|underwater|cave|cave-dark OUTPUT.bmp [DAY_PROGRESS "
                        "[ANIMATION_SECONDS [PACK]]]\n");
        return 1;
    }
    bool third = strncmp(argv[1], "third-", 6) == 0;
    bool snow = strcmp(argv[1], "snow") == 0, forest = strcmp(argv[1], "forest") == 0 || third;
    bool underwater = strcmp(argv[1], "underwater") == 0, cave = strncmp(argv[1], "cave", 4) == 0;
    SDL_SetMainReady();
    log_set_level(LOG_LEVEL_WARN);
    Window *window = window_create("TerraCraft visual verification", 1280, 720);
    if (!window)
        return 1;
    SDL_HideWindow((SDL_Window *)window_native_handle(window));
    GlContext *gl = gl_ctx_init(window);
    if (!gl) {
        window_destroy(window);
        return 1;
    }
    (void)SDL_GL_SetSwapInterval(0);
    Renderer *renderer = renderer_create(gl);
    if (renderer && argc >= 6 && renderer_reload_atlas(renderer, argv[5]) != 0)
        return 1;
    World *world = world_create();
    Camera *camera = camera_create();
    if (!renderer || !world || !camera)
        return 1;
    world->seed = snow ? -1506115370L : (forest ? 905840877L : -1436341459L);
    world->terrain_version = 5;
    Vec3 eye = snow ? mmath_vec3(376.184f, 85.62f, -274.348f)
                    : (forest ? mmath_vec3(-16.185f, 73.62f, -109.046f) : mmath_vec3(-318.750f, 65.62f, -194.507f));
    float yaw = snow ? 0.8772f : (forest ? 4.5883f : 5.0147f), pitch = -0.12f;
    if (underwater)
        eye.y = 64.4f;
    if (cave) {
        eye = mmath_vec3(0.5f, 66.62f, 6.5f);
        yaw = 0;
    }
    int cx = (int)floorf(eye.x / 16), cz = (int)floorf(eye.z / 16), radius = 5;
    for (int z = cz - radius; z <= cz + radius; ++z)
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (cave) {
                Chunk *chunk = chunk_create(x, z);
                if (!chunk)
                    return 1;
                for (int y = 0; y <= 72; ++y)
                    for (int lz = 0; lz < 16; ++lz)
                        for (int lx = 0; lx < 16; ++lx) {
                            int wx = x * 16 + lx, wz = z * 16 + lz;
                            bool air = wx >= -5 && wx <= 5 && wz >= -24 && wz <= 12 && y >= 64 && y <= 69;
                            chunk_set_block(chunk, lx, y, lz, air ? BLOCK_AIR : BLOCK_STONE);
                        }
                if (world_add_chunk(world, chunk) != 0)
                    return 1;
            } else if (world_generate_chunk(world, x, z) != 0)
                return 1;
        }
    if (cave && strcmp(argv[1], "cave-dark") != 0)
        (void)world_set_block(world, 0, 65, -5, BLOCK_TORCH);
    bool dirty = true;
    while (dirty) {
        renderer_refresh_world(renderer, world);
        dirty = false;
        for (size_t i = 0; i < world->count; ++i)
            if (world->chunks[i]->dirty)
                dirty = true;
    }
    Player player;
    PlayerAnim panim;
    player_init(&player);
    player_anim_init(&panim);
    if (third) {
        /* Third-person body poses: the player stands where the forest camera was, the camera orbits it. */
        player.pos = mmath_vec3(eye.x, eye.y - 1.62f, eye.z);
        player.render_pos = player.pos;
        player.yaw = yaw;
        player.pitch = pitch;
        player.body_yaw = player.body_yaw_prev = yaw;
        player.grounded = true;
        player.interp_alpha = 1.0f;
        player.anim_time = 3.0f;
        bool front = strstr(argv[1], "front") != NULL;
        if (strstr(argv[1], "walk") != NULL || strstr(argv[1], "sprint") != NULL) {
            player.limb_speed = player.limb_speed_prev = 0.86f;
            player.limb_pos = 9.0f;
            player.sprinting = strstr(argv[1], "sprint") != NULL;
        }
        if (strstr(argv[1], "sneak") != NULL) {
            player.sneaking = true;
        }
        if (strstr(argv[1], "turn") != NULL) {
            player.body_yaw = player.body_yaw_prev = yaw + 0.6f; /* head turned 34 degrees from the body */
        }
        if (strstr(argv[1], "attack") != NULL) {
            panim.state = PLAYER_ANIM_ATTACK;
            panim.t = 0.10f;
        }
        player.inv.slots[0].item = BLOCK_WOOD;
        player.inv.slots[0].count = 1;
        player.hotbar_sel = 0;
        float cp = cosf(pitch);
        Vec3 fwd = mmath_vec3(-sinf(yaw) * cp, sinf(pitch), -cosf(yaw) * cp);
        eye = mmath_vec3_add(eye, mmath_vec3_scale(fwd, front ? 3.2f : -3.2f));
        if (strstr(argv[1], "side") != NULL) {
            /* Camera 3.2 blocks to the player's right, looking at the body from the side. */
            eye = mmath_vec3(player.pos.x + cosf(yaw) * 3.2f, player.pos.y + 1.2f, player.pos.z - sinf(yaw) * 3.2f);
            yaw += MMATH_PI * 0.5f;
            pitch = 0.0f;
        } else if (front) {
            yaw += MMATH_PI;
            pitch = -pitch;
        }
        yaw += 0.0f;
    }
    camera_set_position(camera, eye);
    camera_set_yaw_pitch(camera, yaw, pitch);
    camera_set_fov_y(camera, 70);
    renderer_set_render_distance(renderer, radius);
    renderer_set_underwater(renderer, underwater);
    TimeSystem clock;
    time_system_init(&clock, argc >= 4 ? (float)atof(argv[3]) : 0.5f, 0);
    renderer_advance_visuals(renderer, argc >= 5 ? atof(argv[4]) : 4.0);
    int width, height;
    window_get_size(window, &width, &height);
    renderer_set_viewport(renderer, width, height);
    renderer_draw_world(renderer, world, camera, (float)width / (float)height, &clock);
    if (third) {
        renderer_draw_player(renderer, camera, (float)width / (float)height, &clock, &player, 1.0f, &panim);
        glFinish();
        int tr = export_frame(argv[2], width, height);
        printf("%s: %s\n", argv[1], tr ? "FAILED" : argv[2]);
        return tr;
    }
    HandView hand;
    hand_view_init(&hand);
    HandViewSample sample = {0};
    sample.held_key = snow ? BLOCK_SNOW : 0;
    sample.yaw_deg = -yaw * 180 / MMATH_PI;
    sample.pitch_deg = -pitch * 180 / MMATH_PI;
    sample.on_ground = true;
    sample.attack_delay_ticks = 5;
    hand_view_advance(&hand, 1, &sample);
    HandViewFrame frame;
    hand_view_frame(&hand, 1, sample.yaw_deg, sample.pitch_deg, snow ? HAND_ITEM_BLOCK : HAND_ITEM_NONE, NULL, &frame);
    renderer_draw_player_arm(renderer, camera, (float)width / (float)height, &clock, snow ? TILE_SNOW : -1,
                             snow ? BLOCK_SNOW : 0, &frame);
    ItemStack hotbar[9] = {{0}};
    hotbar[0].item = snow ? BLOCK_SNOW : BLOCK_GRASS;
    hotbar[0].count = 64;
    hotbar[1].item = BLOCK_WOOD;
    hotbar[1].count = 16;
    renderer_draw_hud(renderer, width, height, hotbar, 0, 20, 20, 17, 20, true, -1, -1, 0);
    glFinish();
    int result = export_frame(argv[2], width, height);
    printf("%s: %dx%d; %zu chunks; %s; %s\n", argv[1], width, height, world->count, gl_ctx_get_renderer(),
           result ? "FAILED" : argv[2]);
    renderer_destroy(renderer);
    camera_destroy(camera);
    world_destroy(world);
    gl_ctx_destroy(gl);
    window_destroy(window);
    return result;
}
