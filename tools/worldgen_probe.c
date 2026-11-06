#include "world/worldgen_v4.h"
#include "world/worldgen_v5.h"
#include "world/world_gen.h"
#include "world/world.h"
#include "world/chunk.h"
#include "world/block.h"
#include "core/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>

/* Stable FNV-1a over explicit block-ID bytes, independent of host endian. */
static uint64_t chunk_hash(const Chunk *c)
{
    uint64_t h = UINT64_C(14695981039346656037);
    for (int i = 0; i < 16 * 256 * 16; ++i) { /* y 0..255: the frozen profiles never reach above it */
        h = (h ^ (c->blocks[i] & 255u)) * UINT64_C(1099511628211);
        h = (h ^ (c->blocks[i] >> 8)) * UINT64_C(1099511628211);
    }
    return h;
}

/* Offline field atlas, actual 32x32 chunks and many-seed spawn report. */
static int probe_open(FILE **file, const char *path, const char *mode)
{
    *file = fopen(path, mode);
    return *file ? 0 : 1;
}
int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--hash4") == 0) {
        const int coords[][2] = {{-1, -1}, {0, 0}, {31, -17}, {-63, 42}};
        const long seeds[] = {123456, -7654321};
        for (int s = 0; s < 2; ++s) {
            World *world = world_create();
            if (!world) return 1;
            world->seed = seeds[s];
            world->terrain_version = 4;
            for (int i = 0; i < 4; ++i) {
                if (world_generate_chunk(world, coords[i][0], coords[i][1]) != 0) {
                    world_destroy(world);
                    return 2;
                }
                printf("%ld %d %d %llu\n", seeds[s], coords[i][0], coords[i][1],
                       (unsigned long long)chunk_hash(world_get_chunk(world, coords[i][0], coords[i][1])));
            }
            world_destroy(world);
        }
        return 0;
    }
    const char *prefix = argc > 1 ? argv[1] : "build/worldgen";
    long seed = argc > 2 ? strtol(argv[2], NULL, 10) : 123456;
    int seeds = argc > 3 ? atoi(argv[3]) : 100;
    int profile = argc > 4 ? atoi(argv[4]) : 5;
    if ((profile != 4 && profile != 5) || seeds < 0 || seeds > 10000) {
        fprintf(stderr, "Usage: worldgen_probe [prefix] [seed] [spawn-seeds 0..10000] [profile 4|5]\n");
        return 1;
    }
    printf("TerraCraft profile %d seed %ld\n", profile, seed);
    char path[512];
    FILE *f = NULL;
    snprintf(path, sizeof(path), "%s-meta.json", prefix);
    if (probe_open(&f, path, "w") != 0) return 1;
    fprintf(f, "{\"profile\":%d,\"seed\":%ld,\"spawn_seeds\":%d}\n", profile, seed, seeds);
    fclose(f);
    snprintf(path, sizeof(path), "%s-fields.csv", prefix);
    if (probe_open(&f, path, "w") != 0)
        return 1;
    fprintf(f, "x,z,height,continentalness,erosion,temperature,humidity,weirdness,biome,mountain,river\n");
    clock_t begin = clock();
    for (int z = -2048; z < 2048; z += 8)
        for (int x = -2048; x < 2048; x += 8) {
            GenClimate c = profile == 5 ? worldgen5_climate(seed, x, z) : worldgen_climate(seed, x, z);
            fprintf(f, "%d,%d,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%d,%.5f,%.5f\n", x, z, c.height, c.continentalness,
                    c.erosion, c.temperature, c.humidity, c.weirdness, c.biome, c.mountain, c.river);
        }
    fclose(f);
    snprintf(path, sizeof(path), "%s-density.csv", prefix);
    if (probe_open(&f, path, "w") != 0)
        return 1;
    fprintf(f,
            "x,y,terrain,cheese,spaghetti,noodle,final_density,aquifer_level,wet,barrier,vein,vein_ridge,richness\n");
    for (int y = 0; y < 256; y += 2)
        for (int x = -256; x < 256; x += 2) {
            GenDensity d = worldgen_density_profile(seed, x, y, 0, profile);
            GenAquifer a = worldgen_aquifer_profile(seed, x, y, 0, profile);
            fprintf(f, "%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%.4f,%.4f,%.4f\n", x, y, d.terrain, d.cheese,
                    d.spaghetti, d.noodle, d.final_density, a.level, a.wet, a.barrier, d.vein, d.vein_ridge,
                    d.richness);
        }
    fclose(f);
    snprintf(path, sizeof(path), "%s-section.ppm", prefix);
    if (probe_open(&f, path, "wb") != 0)
        return 1;
    fprintf(f, "P6\n512 256\n255\n");
    for (int y = 255; y >= 0; --y)
        for (int x = -256; x < 256; ++x) {
            uint16_t id = worldgen_base_block_profile(seed, x, y, 0, profile);
            const BlockInfo *b = block_get_info(id);
            unsigned char rgb[3] = {(unsigned char)(b->color_r * 255), (unsigned char)(b->color_g * 255),
                                    (unsigned char)(b->color_b * 255)};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("Maps %.3fs\n", (double)(clock() - begin) / CLOCKS_PER_SEC);
    fflush(stdout);
    World *w = world_create();
    if (!w)
        return 1;
    w->seed = seed;
    w->terrain_version = profile;
    size_t histogram[BLOCK_COUNT] = {0}, depth[16][BLOCK_COUNT] = {0};
    unsigned char *region = malloc(512u * 512u * 3u);
    if (!region) {
        world_destroy(w);
        return 1;
    }
    size_t peak_memory = mem_process_working_set_bytes();
    begin = clock();
    for (int z = -16; z < 16; ++z)
        for (int x = -16; x < 16; ++x) {
            if (world_generate_chunk(w, x, z) != 0)
                return 2;
            Chunk *c = world_get_chunk(w, x, z);
            for (int lz = 0; lz < 16; ++lz)
                for (int lx = 0; lx < 16; ++lx) {
                    int top = 255;
                    while (top > 0 && (chunk_get_block(c, lx, top, lz) == BLOCK_AIR ||
                                       block_is_cross(chunk_get_block(c, lx, top, lz))))
                        --top;
                    const BlockInfo *b = block_get_info(chunk_get_block(c, lx, top, lz));
                    size_t index = ((size_t)((z + 16) * 16 + lz) * 512 + (x + 16) * 16 + lx) * 3;
                    float relief = .65f + (float)top / 400;
                    region[index] = (unsigned char)(fminf(255, b->color_r * 255 * relief));
                    region[index + 1] = (unsigned char)(fminf(255, b->color_g * 255 * relief));
                    region[index + 2] = (unsigned char)(fminf(255, b->color_b * 255 * relief));
                }
            for (int y = 0; y < 256; ++y)
                for (int p = 0; p < 256; ++p) {
                    uint16_t id = c->blocks[y * 256 + p];
                    ++histogram[id];
                    ++depth[y / 16][id];
                }
            world_remove_chunk(w, x, z);
            size_t memory = mem_process_working_set_bytes();
            if (memory > peak_memory)
                peak_memory = memory;
        }
    printf("1024 chunks %.3fs (%.3fms/chunk), peak one chunk\n", (double)(clock() - begin) / CLOCKS_PER_SEC,
           (double)(clock() - begin) / CLOCKS_PER_SEC * 1000 / 1024);
    fflush(stdout);
    printf("Region resident memory peak %.2f MiB\n", (double)peak_memory / (1024 * 1024));
    snprintf(path, sizeof(path), "%s-region.ppm", prefix);
    if (probe_open(&f, path, "wb") != 0) {
        free(region);
        world_destroy(w);
        return 1;
    }
    fprintf(f, "P6\n512 512\n255\n");
    fwrite(region, 1, 512u * 512u * 3u, f);
    fclose(f);
    free(region);
    snprintf(path, sizeof(path), "%s-materials.csv", prefix);
    if (probe_open(&f, path, "w") != 0)
        return 1;
    fprintf(f, "block,total");
    for (int y = 0; y < 16; ++y)
        fprintf(f, ",y%d", y * 16);
    fprintf(f, "\n");
    for (int id = 0; id < BLOCK_COUNT; ++id) {
        fprintf(f, "%s,%zu", block_get_info((uint16_t)id)->name, histogram[id]);
        for (int y = 0; y < 16; ++y)
            fprintf(f, ",%zu", depth[y][id]);
        fprintf(f, "\n");
    }
    fclose(f);
    world_destroy(w);
    int failed = 0, fallback = 0;
    begin = clock();
    snprintf(path, sizeof(path), "%s-spawns.csv", prefix);
    if (probe_open(&f, path, "w") != 0)
        return 1;
    fprintf(f, "seed,x,y,z,status,chunks\n");
    for (int i = 0; i < seeds; ++i) {
        w = world_create();
        if (!w)
            return 1;
        w->seed = (long)((unsigned int)i * 2654435761u);
        w->terrain_version = profile;
        Vec3 pos = mmath_vec3(0, 0, 0);
        int rc = worldgen_find_spawn(w, &pos);
        if (rc < 0 || !worldgen_spawn_valid(w, (int)floorf(pos.x), (int)floorf(pos.z), NULL, NULL))
            ++failed;
        if (rc > 0)
            ++fallback;
        fprintf(f, "%ld,%.1f,%.1f,%.1f,%d,%zu\n", w->seed, pos.x, pos.y, pos.z, rc, w->count);
        world_destroy(w);
        if (i % 50 == 49) {
            printf("Spawn seeds %d/%d failures %d\n", i + 1, seeds, failed);
            fflush(stdout);
        }
    }
    fclose(f);
    printf("Spawn %d seeds: %d failures, %d fallback, %.3fs\n", seeds, failed, fallback,
           (double)(clock() - begin) / CLOCKS_PER_SEC);
    return failed ? 3 : 0;
}
