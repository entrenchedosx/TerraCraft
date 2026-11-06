#include "world/vgen.h"

#include "core/log.h"
#include "core/path.h"
#include "core/time.h"
#include "world/biome.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/jcarver.h"
#include "world/jdensity.h"
#include "world/jfeature.h"
#include "world/jfill.h"
#include "world/jsurface.h"

/* Portable SDL include (same convention as platform/window.c). */
#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#elif __has_include(<SDL.h>)
#include <SDL.h>
#else
#error "SDL headers not found"
#endif
#else
#include <SDL2/SDL.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE_CAP 192     /* fill + surface + carver results kept (about 98 KB each) */
#define DECO_CAP 320     /* decoration change lists kept */
#define REQ_CAP 128
#define DONE_CAP 48
#define BLOCKS_BYTES ((size_t)16 * 16 * JFILL_HEIGHT)
#define FAILED_CAP 16

/* Block ids that need the neighbouring cells to resolve (see convert_chunk). */
#define VM_DOUBLE_PLANT 0xF002u /* both halves of a tall plant share one name: the upper half is air */

typedef struct BaseEntry {
    bool used;
    int cx, cz;
    uint64_t stamp;
    uint8_t *blocks;
    JHeightmaps hm;
} BaseEntry;

typedef struct DecoEntry {
    bool used;
    int cx, cz;
    uint64_t stamp;
    int count;
    uint32_t *changes; /* x48 (6 bits) | z48 (6) | y (9) | block (8), relative to the 3 x 3 region */
} DecoEntry;

struct VGen {
    int64_t seed;
    JRandomState *rs;
    JSurface *surf;
    JCarvers *cv;
    JFeatures *feats;
    BaseEntry base[BASE_CAP];
    DecoEntry deco[DECO_CAP];
    uint64_t clock;
    uint16_t map[256];
    bool mapped[256];
    uint8_t *region[9];  /* scratch copies of the base chunks a decoration may modify */
    JHeightmaps region_hm[9];
    uint8_t *final_blocks;
    SDL_mutex *gen_mutex;

    /* Asynchronous interface. */
    SDL_Thread *thread;
    SDL_mutex *q_mutex;
    SDL_cond *q_cond;
    bool stop;
    int req_x[REQ_CAP], req_z[REQ_CAP];
    int req_count;
    bool busy;
    int busy_x, busy_z;
    Chunk *done[DONE_CAP];
    int done_count;
    int failed_x[FAILED_CAP], failed_z[FAILED_CAP];
    int failed_count;
    int center_x, center_z, radius;
};

/* ---- data pack location ------------------------------------------------------------------------- */

static bool pack_ok(const char *root)
{
    char probe[PATH_MAX_LEN];
    if (path_join(probe, sizeof(probe), root, "data/minecraft/worldgen/noise_settings/overworld.json") != 0) {
        return false;
    }
    FILE *f = fopen(probe, "rb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

bool vgen_find_datapack(char *out, int cap)
{
    if (out == NULL || cap <= 0) {
        return false;
    }
    const char *env = getenv("TERRACRAFT_DATAPACK");
    char cand[PATH_MAX_LEN];
    if (env != NULL && env[0] != '\0' && pack_ok(env) && strlen(env) < (size_t)cap) {
        strcpy(out, env);
        return true;
    }
    if (path_mcassets_dir(cand, sizeof(cand), "generated/datapack") == 0 && pack_ok(cand) &&
        strlen(cand) < (size_t)cap) {
        strcpy(out, cand);
        return true;
    }
    static const char *const fallbacks[] = {"reference/1.21.11/run/out", "../reference/1.21.11/run/out",
                                            "../../reference/1.21.11/run/out"};
    for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); ++i) {
        if (pack_ok(fallbacks[i]) && strlen(fallbacks[i]) < (size_t)cap) {
            strcpy(out, fallbacks[i]);
            return true;
        }
    }
    return false;
}

bool vgen_available(void)
{
    /* The create-world screen asks every frame: look at the disk at most every two seconds. */
    static double checked_at = -1000.0;
    static bool result = false;
    double now = time_now_seconds();
    if (now - checked_at >= 2.0) {
        char root[PATH_MAX_LEN];
        result = vgen_find_datapack(root, (int)sizeof(root));
        checked_at = now;
    }
    return result;
}

/* ---- vanilla block -> game block ------------------------------------------------------------------ */

typedef struct MapEntry {
    const char *name;
    uint16_t block;
} MapEntry;

static const MapEntry BLOCK_MAP[] = {
    {"air", BLOCK_AIR}, {"cave_air", BLOCK_AIR}, {"void_air", BLOCK_AIR},
    {"stone", BLOCK_STONE}, {"andesite", BLOCK_ANDESITE}, {"diorite", BLOCK_DIORITE}, {"granite", BLOCK_GRANITE},
    {"tuff", BLOCK_TUFF}, {"calcite", BLOCK_CALCITE}, {"dripstone_block", BLOCK_TUFF}, {"basalt", BLOCK_STONE},
    {"smooth_basalt", BLOCK_STONE}, {"blackstone", BLOCK_STONE}, {"magma_block", BLOCK_STONE},
    {"infested_stone", BLOCK_STONE}, {"mossy_cobblestone", BLOCK_STONE}, {"amethyst_block", BLOCK_STONE},
    {"budding_amethyst", BLOCK_STONE}, {"deepslate", BLOCK_DEEPSLATE}, {"infested_deepslate", BLOCK_DEEPSLATE},
    {"bedrock", BLOCK_BEDROCK},
    {"dirt", BLOCK_DIRT}, {"coarse_dirt", BLOCK_COARSE_DIRT}, {"rooted_dirt", BLOCK_DIRT}, {"podzol", BLOCK_PODZOL},
    {"mycelium", BLOCK_MYCELIUM}, {"mud", BLOCK_MUD}, {"clay", BLOCK_CLAY}, {"muddy_mangrove_roots", BLOCK_MUD},
    {"grass_block", BLOCK_GRASS}, {"moss_block", BLOCK_GRASS}, {"pale_moss_block", BLOCK_GRASS},
    {"gravel", BLOCK_GRAVEL}, {"sand", BLOCK_SAND}, {"red_sand", BLOCK_RED_SAND}, {"soul_sand", BLOCK_SAND},
    {"sandstone", BLOCK_SANDSTONE}, {"red_sandstone", BLOCK_RED_SANDSTONE},
    {"terracotta", BLOCK_TERRACOTTA}, {"orange_terracotta", BLOCK_ORANGE_TERRACOTTA},
    {"white_terracotta", BLOCK_WHITE_TERRACOTTA}, {"yellow_terracotta", BLOCK_YELLOW_TERRACOTTA},
    {"brown_terracotta", BLOCK_BROWN_TERRACOTTA}, {"red_terracotta", BLOCK_RED_TERRACOTTA},
    {"light_gray_terracotta", BLOCK_LIGHT_GRAY_TERRACOTTA},
    {"snow_block", BLOCK_SNOW}, {"powder_snow", BLOCK_SNOW}, {"packed_ice", BLOCK_PACKED_ICE},
    {"blue_ice", BLOCK_PACKED_ICE}, {"ice", BLOCK_ICE},
    {"birch_log", BLOCK_BIRCH_LOG}, {"birch_wood", BLOCK_BIRCH_LOG}, {"spruce_log", BLOCK_SPRUCE_LOG},
    {"spruce_wood", BLOCK_SPRUCE_LOG}, {"jungle_log", BLOCK_JUNGLE_LOG}, {"jungle_wood", BLOCK_JUNGLE_LOG},
    {"acacia_log", BLOCK_ACACIA_LOG}, {"acacia_wood", BLOCK_ACACIA_LOG}, {"dark_oak_log", BLOCK_DARK_OAK_LOG},
    {"dark_oak_wood", BLOCK_DARK_OAK_LOG}, {"birch_leaves", BLOCK_BIRCH_LEAVES},
    {"spruce_leaves", BLOCK_SPRUCE_LEAVES}, {"jungle_leaves", BLOCK_JUNGLE_LEAVES},
    {"acacia_leaves", BLOCK_ACACIA_LEAVES}, {"dark_oak_leaves", BLOCK_DARK_OAK_LEAVES},
    {"water", BLOCK_WATER}, {"lava", BLOCK_LAVA},
    {"coal_ore", BLOCK_COAL_ORE}, {"deepslate_coal_ore", BLOCK_COAL_ORE},
    {"iron_ore", BLOCK_IRON_ORE}, {"deepslate_iron_ore", BLOCK_IRON_ORE},
    {"gold_ore", BLOCK_GOLD_ORE}, {"deepslate_gold_ore", BLOCK_GOLD_ORE},
    {"diamond_ore", BLOCK_DIAMOND_ORE}, {"deepslate_diamond_ore", BLOCK_DIAMOND_ORE},
    {"copper_ore", BLOCK_COPPER_ORE}, {"deepslate_copper_ore", BLOCK_COPPER_ORE},
    {"lapis_ore", BLOCK_LAPIS_ORE}, {"deepslate_lapis_ore", BLOCK_LAPIS_ORE},
    {"redstone_ore", BLOCK_REDSTONE_ORE}, {"deepslate_redstone_ore", BLOCK_REDSTONE_ORE},
    {"emerald_ore", BLOCK_EMERALD_ORE}, {"deepslate_emerald_ore", BLOCK_EMERALD_ORE},
    {"raw_iron_block", BLOCK_RAW_IRON}, {"raw_copper_block", BLOCK_RAW_COPPER},
    {"beehive", BLOCK_PLANKS}, {"mangrove_roots", BLOCK_WOOD},
    /* Ground cover and plants. */
    {"short_grass", BLOCK_GRASS_PLANT}, {"fern", BLOCK_GRASS_PLANT}, {"bush", BLOCK_GRASS_PLANT},
    {"dead_bush", BLOCK_GRASS_PLANT},
    {"tall_grass", (uint16_t)(VM_DOUBLE_PLANT)}, {"large_fern", (uint16_t)(VM_DOUBLE_PLANT)},
    {"dandelion", BLOCK_FLOWER}, {"poppy", BLOCK_FLOWER}, {"blue_orchid", BLOCK_FLOWER},
    {"allium", BLOCK_FLOWER}, {"azure_bluet", BLOCK_FLOWER}, {"red_tulip", BLOCK_FLOWER},
    {"orange_tulip", BLOCK_FLOWER}, {"white_tulip", BLOCK_FLOWER}, {"pink_tulip", BLOCK_FLOWER},
    {"oxeye_daisy", BLOCK_FLOWER}, {"cornflower", BLOCK_FLOWER}, {"lily_of_the_valley", BLOCK_FLOWER},
    {"torchflower", BLOCK_FLOWER}, {"wither_rose", BLOCK_FLOWER}, {"closed_eyeblossom", BLOCK_FLOWER},
    {"open_eyeblossom", BLOCK_FLOWER},
    {"sunflower", (uint16_t)(VM_DOUBLE_PLANT + 1u)}, {"lilac", (uint16_t)(VM_DOUBLE_PLANT + 1u)},
    {"rose_bush", (uint16_t)(VM_DOUBLE_PLANT + 1u)}, {"peony", (uint16_t)(VM_DOUBLE_PLANT + 1u)},
};

static bool ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

uint16_t vgen_map_block_name(const char *name)
{
    if (name == NULL) {
        return BLOCK_AIR;
    }
    if (strncmp(name, "minecraft:", 10) == 0) {
        name += 10;
    }
    for (size_t i = 0; i < sizeof(BLOCK_MAP) / sizeof(BLOCK_MAP[0]); ++i) {
        if (strcmp(BLOCK_MAP[i].name, name) == 0) {
            return BLOCK_MAP[i].block;
        }
    }
    if (ends_with(name, "_log") || ends_with(name, "_wood") || (ends_with(name, "_stem") && strstr(name, "mushroom") == NULL)) {
        return BLOCK_WOOD;
    }
    if (ends_with(name, "_leaves")) {
        return BLOCK_LEAVES;
    }
    if (ends_with(name, "_planks")) {
        return BLOCK_PLANKS;
    }
    if (ends_with(name, "terracotta")) {
        return BLOCK_TERRACOTTA;
    }
    if (ends_with(name, "_ore")) {
        return BLOCK_STONE;
    }
    /* Everything else is a plant, decoration or block with no counterpart yet: leave the cell empty. */
    return BLOCK_AIR;
}

int vgen_biome_class(const char *id)
{
    if (id == NULL) {
        return BIOME_PLAINS;
    }
    if (strncmp(id, "minecraft:", 10) == 0) {
        id += 10;
    }
    if (strstr(id, "ocean") != NULL) {
        return BIOME_OCEAN;
    }
    if (strstr(id, "beach") != NULL) {
        return BIOME_BEACH;
    }
    if (strstr(id, "desert") != NULL || strstr(id, "badlands") != NULL || strstr(id, "savanna") != NULL) {
        return BIOME_DESERT;
    }
    if (strstr(id, "snowy") != NULL || strstr(id, "frozen") != NULL || strstr(id, "ice") != NULL ||
        strstr(id, "grove") != NULL) {
        return BIOME_SNOW;
    }
    if (strstr(id, "windswept") != NULL || strstr(id, "peaks") != NULL || strstr(id, "stony") != NULL ||
        strstr(id, "mountain") != NULL) {
        return BIOME_MOUNTAINS;
    }
    if (strstr(id, "forest") != NULL || strstr(id, "taiga") != NULL || strstr(id, "jungle") != NULL ||
        strstr(id, "swamp") != NULL || strstr(id, "mangrove") != NULL || strstr(id, "bamboo") != NULL ||
        strstr(id, "birch") != NULL) {
        return BIOME_FOREST;
    }
    return BIOME_PLAINS;
}

/* Fill in the game id of every registered vanilla block id that has not been mapped yet. */
static void update_block_map(VGen *g)
{
    int n = jfill_block_count();
    for (int i = 0; i < n && i < 256; ++i) {
        if (!g->mapped[i]) {
            g->map[i] = vgen_map_block_name(jfill_block_name((JFillBlock)i));
            g->mapped[i] = true;
        }
    }
}

/* ---- lifetime --------------------------------------------------------------------------------------- */

VGen *vgen_create(const char *datapack_root, int64_t seed)
{
    if (datapack_root == NULL) {
        return NULL;
    }
    VGen *g = (VGen *)calloc(1, sizeof(VGen));
    if (g == NULL) {
        return NULL;
    }
    g->seed = seed;
    g->gen_mutex = SDL_CreateMutex();
    g->q_mutex = SDL_CreateMutex();
    g->q_cond = SDL_CreateCond();
    g->final_blocks = (uint8_t *)malloc(BLOCKS_BYTES);
    for (int i = 0; i < 9; ++i) {
        g->region[i] = (uint8_t *)malloc(BLOCKS_BYTES);
    }
    g->rs = jrs_create(datapack_root, "overworld", seed);
    g->surf = g->rs != NULL ? jsurface_create(g->rs) : NULL;
    g->feats = g->surf != NULL ? jfeatures_load(datapack_root, g->surf) : NULL;
    g->cv = jcarvers_load(datapack_root);
    bool ok = g->gen_mutex != NULL && g->q_mutex != NULL && g->q_cond != NULL && g->final_blocks != NULL &&
              g->feats != NULL && g->cv != NULL;
    for (int i = 0; i < 9 && ok; ++i) {
        ok = g->region[i] != NULL;
    }
    if (!ok) {
        LOG_ERROR("vgen: could not set up the generator from %s", datapack_root);
        vgen_destroy(g);
        return NULL;
    }
    jfeatures_set_quiet(g->feats, true);
    return g;
}

int64_t vgen_seed(const VGen *g)
{
    return g != NULL ? g->seed : 0;
}

void vgen_destroy(VGen *g)
{
    if (g == NULL) {
        return;
    }
    if (g->thread != NULL) {
        SDL_LockMutex(g->q_mutex);
        g->stop = true;
        SDL_CondSignal(g->q_cond);
        SDL_UnlockMutex(g->q_mutex);
        SDL_WaitThread(g->thread, NULL);
        g->thread = NULL;
    }
    for (int i = 0; i < g->done_count; ++i) {
        chunk_destroy(g->done[i]);
    }
    for (int i = 0; i < BASE_CAP; ++i) {
        free(g->base[i].blocks);
    }
    for (int i = 0; i < DECO_CAP; ++i) {
        free(g->deco[i].changes);
    }
    for (int i = 0; i < 9; ++i) {
        free(g->region[i]);
    }
    free(g->final_blocks);
    jfeatures_free(g->feats);
    jcarvers_free(g->cv);
    jsurface_free(g->surf);
    jrs_destroy(g->rs);
    if (g->q_cond != NULL) {
        SDL_DestroyCond(g->q_cond);
    }
    if (g->q_mutex != NULL) {
        SDL_DestroyMutex(g->q_mutex);
    }
    if (g->gen_mutex != NULL) {
        SDL_DestroyMutex(g->gen_mutex);
    }
    free(g);
}

bool vgen_chunk_may_be_land(VGen *g, int cx, int cz)
{
    if (g == NULL) {
        return false;
    }
    SDL_LockMutex(g->gen_mutex);
    /* Quart coordinates of the chunk centre; y = 63 is quart 15. */
    const char *biome = jsurface_noise_biome(g->surf, cx * 4 + 2, 15, cz * 4 + 2);
    SDL_UnlockMutex(g->gen_mutex);
    return strstr(biome, "ocean") == NULL && strstr(biome, "river") == NULL;
}

/* ---- base chunk: fill + surface + carvers ------------------------------------------------------------ */

static BaseEntry *get_base(VGen *g, int cx, int cz)
{
    BaseEntry *victim = NULL;
    for (int i = 0; i < BASE_CAP; ++i) {
        BaseEntry *e = &g->base[i];
        if (e->used && e->cx == cx && e->cz == cz) {
            e->stamp = ++g->clock;
            return e;
        }
        /* Replace an empty slot first, otherwise the least recently used one. */
        if (victim == NULL || (victim->used && (!e->used || e->stamp < victim->stamp))) {
            victim = e;
        }
    }
    if (victim->blocks == NULL) {
        victim->blocks = (uint8_t *)malloc(BLOCKS_BYTES);
        if (victim->blocks == NULL) {
            return NULL;
        }
    }
    victim->used = false;
    JChunk *ch = jchunk_create(g->rs, cx, cz);
    if (ch == NULL || !jchunk_fill(ch, victim->blocks)) {
        jchunk_free(ch);
        return NULL;
    }
    jheightmaps_prime(&victim->hm, victim->blocks);
    if (!jsurface_build(g->surf, ch, victim->blocks, &victim->hm)) {
        jchunk_free(ch);
        return NULL;
    }
    JAquifer *aq = jaquifer_create(ch);
    JSurfaceTopUser top = {g->surf, ch, victim->blocks, &victim->hm};
    JCarveSurface cs = {&victim->hm, jsurface_top_material, &top};
    jchunk_carve(ch, g->cv, aq, victim->blocks, NULL, NULL, &cs);
    jaquifer_free(aq);
    jchunk_free(ch);
    victim->used = true;
    victim->cx = cx;
    victim->cz = cz;
    victim->stamp = ++g->clock;
    return victim;
}

/* ---- decoration ---------------------------------------------------------------------------------------- */

static DecoEntry *get_deco(VGen *g, int cx, int cz)
{
    DecoEntry *victim = NULL;
    for (int i = 0; i < DECO_CAP; ++i) {
        DecoEntry *e = &g->deco[i];
        if (e->used && e->cx == cx && e->cz == cz) {
            e->stamp = ++g->clock;
            return e;
        }
        if (victim == NULL || (victim->used && (!e->used || e->stamp < victim->stamp))) {
            victim = e;
        }
    }
    BaseEntry *src[9];
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            BaseEntry *b = get_base(g, cx + dx, cz + dz);
            if (b == NULL) {
                return NULL;
            }
            src[(dz + 1) * 3 + (dx + 1)] = b;
        }
    }
    /* Vanilla reads the stored biomes of the 5 x 5 chunks around the decorated one. */
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            jsurface_prepare_chunk(g->surf, cx + dx, cz + dz);
        }
    }
    JRegion region;
    memset(&region, 0, sizeof(region));
    for (int i = 0; i < 9; ++i) {
        memcpy(g->region[i], src[i]->blocks, BLOCKS_BYTES);
        g->region_hm[i] = src[i]->hm;
        region.blocks[i] = g->region[i];
        region.heightmaps[i] = &g->region_hm[i];
    }
    region.target_x = cx;
    region.target_z = cz;
    /* Every feature type the port implements, including the selectors that pick between tree features. */
    static const char *const types[] = {"minecraft:ore",
                                        "minecraft:random_patch",
                                        "minecraft:flower",
                                        "minecraft:simple_block",
                                        "minecraft:tree",
                                        "minecraft:random_selector",
                                        "minecraft:simple_random_selector",
                                        "minecraft:random_boolean_selector"};
    jfeatures_decorate(g->feats, g->surf, g->seed, &region, types, (int)(sizeof(types) / sizeof(types[0])), NULL, 0);
    jfeatures_region_reset(&region);

    int count = 0;
    for (int slot = 0; slot < 9; ++slot) {
        const uint8_t *a = src[slot]->blocks, *b = g->region[slot];
        for (size_t i = 0; i < BLOCKS_BYTES; ++i) {
            count += a[i] != b[i];
        }
    }
    uint32_t *list = count > 0 ? (uint32_t *)malloc((size_t)count * sizeof(uint32_t)) : NULL;
    if (count > 0 && list == NULL) {
        return NULL;
    }
    int n = 0;
    for (int slot = 0; slot < 9; ++slot) {
        const uint8_t *a = src[slot]->blocks, *b = g->region[slot];
        int ox = (slot % 3) * 16, oz = (slot / 3) * 16;
        for (size_t i = 0; i < BLOCKS_BYTES; ++i) {
            if (a[i] == b[i]) {
                continue;
            }
            int y = (int)(i % JFILL_HEIGHT);
            int z = (int)((i / JFILL_HEIGHT) % 16);
            int x = (int)(i / JFILL_HEIGHT / 16);
            list[n++] = ((uint32_t)(ox + x) << 23) | ((uint32_t)(oz + z) << 17) | ((uint32_t)y << 8) | (uint32_t)b[i];
        }
    }
    free(victim->changes);
    victim->changes = list;
    victim->count = n;
    victim->used = true;
    victim->cx = cx;
    victim->cz = cz;
    victim->stamp = ++g->clock;
    return victim;
}

/* ---- conversion to a game chunk ---------------------------------------------------------------------- */

/* Blocks a snow layer may settle on (vanilla: any block with a full top face except ice). Logs and leaves keep
 * their look, and the layer itself is shown by turning the ground block into a snow block. */
static bool snow_settles_on(uint16_t b)
{
    if (!block_is_solid(b) || block_is_leaves(b) || block_is_log(b) || block_is_cross(b)) {
        return false;
    }
    return b != BLOCK_ICE && b != BLOCK_PACKED_ICE && b != BLOCK_SNOW && b != BLOCK_GLASS && b != BLOCK_LAVA &&
           b != BLOCK_BEDROCK;
}

static void convert_chunk(VGen *g, const uint8_t *fin, Chunk *out, int cx, int cz)
{
    update_block_map(g);
    uint16_t *dst = out->blocks;
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            const uint8_t *col = fin + ((size_t)x * 16 + (size_t)z) * JFILL_HEIGHT;
            int top = 0;
            for (int y = 0; y < CHUNK_Y; ++y) {
                uint16_t m = g->map[col[y]];
                if (m >= VM_DOUBLE_PLANT) {
                    /* Both halves of a tall plant carry one name: only the lower half is a block here. */
                    if (y > 0 && col[y - 1] == col[y]) {
                        m = BLOCK_AIR;
                    } else {
                        m = m == VM_DOUBLE_PLANT ? BLOCK_GRASS_PLANT : BLOCK_FLOWER;
                    }
                }
                dst[chunk_index(x, y, z)] = m;
                if (m != BLOCK_AIR) {
                    top = y;
                }
            }
            const int wx = cx * 16 + x, wz = cz * 16 + z;
            const char *biome = jsurface_biome_at(g->surf, wx, top - 64, wz);
            out->biome[z * 16 + x] = (uint8_t)vgen_biome_class(biome);

            /* The freeze_top_layer feature, which the port does not have: in biomes that are cold enough at the
             * surface, exposed water freezes and exposed ground gets its snow layer (plants under it vanish). */
            int ground = top;
            while (ground > 0 && block_is_cross(dst[chunk_index(x, ground, z)])) {
                --ground;
            }
            const bool open_above = ground > 0 && ground + 1 < CHUNK_Y &&
                                    (dst[chunk_index(x, ground + 1, z)] == BLOCK_AIR ||
                                     block_is_cross(dst[chunk_index(x, ground + 1, z)]));
            if (open_above) {
                uint16_t b = dst[chunk_index(x, ground, z)];
                bool water = block_is_water(b);
                if ((water || snow_settles_on(b)) &&
                    jsurface_biome_temperature(g->surf, biome, wx, ground + 1 - 64, wz) < 0.15f) {
                    dst[chunk_index(x, ground, z)] = water ? BLOCK_ICE : BLOCK_SNOW;
                    if (ground + 1 < CHUNK_Y && block_is_cross(dst[chunk_index(x, ground + 1, z)])) {
                        dst[chunk_index(x, ground + 1, z)] = BLOCK_AIR;
                    }
                }
            }
        }
    }
}

bool vgen_build_chunk(VGen *g, Chunk *out, int cx, int cz)
{
    if (g == NULL || out == NULL) {
        return false;
    }
    SDL_LockMutex(g->gen_mutex);
    bool ok = true;
    BaseEntry *center = get_base(g, cx, cz);
    DecoEntry *decos[9];
    for (int dz = -1; dz <= 1 && ok; ++dz) {
        for (int dx = -1; dx <= 1 && ok; ++dx) {
            decos[(dz + 1) * 3 + (dx + 1)] = get_deco(g, cx + dx, cz + dz);
            ok = decos[(dz + 1) * 3 + (dx + 1)] != NULL;
        }
    }
    if (center == NULL) {
        ok = false;
    }
    if (ok) {
        memcpy(g->final_blocks, center->blocks, BLOCKS_BYTES);
        for (int slot = 0; slot < 9; ++slot) {
            const DecoEntry *d = decos[slot];
            /* Region slot s of the decoration of chunk (cx + dx, cz + dz) covers chunks
             * (cx + dx - 1 + s % 3, cz + dz - 1 + s / 3); the one equal to (cx, cz) is s = 2 - dx etc. */
            int dx = slot % 3 - 1, dz = slot / 3 - 1;
            int want_x = 1 - dx, want_z = 1 - dz; /* region column/row holding chunk (cx, cz) */
            for (int i = 0; i < d->count; ++i) {
                uint32_t c = d->changes[i];
                int x48 = (int)(c >> 23) & 63, z48 = (int)(c >> 17) & 63, y = (int)(c >> 8) & 511;
                if (x48 / 16 != want_x || z48 / 16 != want_z) {
                    continue;
                }
                g->final_blocks[JFILL_INDEX(x48 & 15, z48 & 15, y - 64)] = (uint8_t)(c & 255u);
            }
        }
        out->cx = cx;
        out->cz = cz;
        convert_chunk(g, g->final_blocks, out, cx, cz);
        out->dirty = true;
        out->save_dirty = false;
    }
    SDL_UnlockMutex(g->gen_mutex);
    return ok;
}

/* ---- asynchronous interface ----------------------------------------------------------------------------- */

static int dist2(int ax, int az, int bx, int bz)
{
    int dx = ax - bx, dz = az - bz;
    return dx * dx + dz * dz;
}

static int worker_main(void *p)
{
    VGen *g = (VGen *)p;
    for (;;) {
        SDL_LockMutex(g->q_mutex);
        while (!g->stop && (g->req_count == 0 || g->done_count >= DONE_CAP)) {
            SDL_CondWait(g->q_cond, g->q_mutex);
        }
        if (g->stop) {
            SDL_UnlockMutex(g->q_mutex);
            break;
        }
        int best = 0;
        for (int i = 1; i < g->req_count; ++i) {
            if (dist2(g->req_x[i], g->req_z[i], g->center_x, g->center_z) <
                dist2(g->req_x[best], g->req_z[best], g->center_x, g->center_z)) {
                best = i;
            }
        }
        int cx = g->req_x[best], cz = g->req_z[best];
        g->req_x[best] = g->req_x[g->req_count - 1];
        g->req_z[best] = g->req_z[g->req_count - 1];
        --g->req_count;
        g->busy = true;
        g->busy_x = cx;
        g->busy_z = cz;
        SDL_UnlockMutex(g->q_mutex);

        Chunk *c = chunk_create(cx, cz);
        if (c != NULL && !vgen_build_chunk(g, c, cx, cz)) {
            chunk_destroy(c);
            c = NULL;
        }

        SDL_LockMutex(g->q_mutex);
        g->busy = false;
        if (c != NULL) {
            g->done[g->done_count++] = c;
        } else if (g->failed_count < FAILED_CAP) {
            g->failed_x[g->failed_count] = cx;
            g->failed_z[g->failed_count++] = cz;
        }
        SDL_UnlockMutex(g->q_mutex);
    }
    return 0;
}

bool vgen_async_start(VGen *g)
{
    if (g == NULL) {
        return false;
    }
    if (g->thread != NULL) {
        return true;
    }
    g->stop = false;
    g->thread = SDL_CreateThread(worker_main, "vgen", g);
    if (g->thread == NULL) {
        LOG_ERROR("vgen: could not start the generator thread: %s", SDL_GetError());
        return false;
    }
    return true;
}

void vgen_async_set_center(VGen *g, int cx, int cz, int radius)
{
    if (g == NULL) {
        return;
    }
    SDL_LockMutex(g->q_mutex);
    g->center_x = cx;
    g->center_z = cz;
    g->radius = radius;
    for (int i = 0; i < g->req_count;) {
        int dx = g->req_x[i] - cx, dz = g->req_z[i] - cz;
        if (dx < 0) {
            dx = -dx;
        }
        if (dz < 0) {
            dz = -dz;
        }
        if ((dx > dz ? dx : dz) > radius + 1) {
            g->req_x[i] = g->req_x[g->req_count - 1];
            g->req_z[i] = g->req_z[g->req_count - 1];
            --g->req_count;
        } else {
            ++i;
        }
    }
    SDL_UnlockMutex(g->q_mutex);
}

static bool has_locked(const VGen *g, int cx, int cz)
{
    for (int i = 0; i < g->req_count; ++i) {
        if (g->req_x[i] == cx && g->req_z[i] == cz) {
            return true;
        }
    }
    if (g->busy && g->busy_x == cx && g->busy_z == cz) {
        return true;
    }
    for (int i = 0; i < g->done_count; ++i) {
        if (g->done[i]->cx == cx && g->done[i]->cz == cz) {
            return true;
        }
    }
    return false;
}

bool vgen_async_request(VGen *g, int cx, int cz)
{
    if (g == NULL || g->thread == NULL) {
        return false;
    }
    SDL_LockMutex(g->q_mutex);
    bool ok = g->req_count < REQ_CAP && !has_locked(g, cx, cz);
    for (int i = 0; ok && i < g->failed_count; ++i) {
        ok = !(g->failed_x[i] == cx && g->failed_z[i] == cz);
    }
    if (ok) {
        g->req_x[g->req_count] = cx;
        g->req_z[g->req_count++] = cz;
        SDL_CondSignal(g->q_cond);
    }
    SDL_UnlockMutex(g->q_mutex);
    return ok;
}

Chunk *vgen_async_poll(VGen *g)
{
    if (g == NULL) {
        return NULL;
    }
    SDL_LockMutex(g->q_mutex);
    Chunk *c = NULL;
    if (g->done_count > 0) {
        c = g->done[0];
        memmove(g->done, g->done + 1, (size_t)(g->done_count - 1) * sizeof(Chunk *));
        --g->done_count;
        SDL_CondSignal(g->q_cond);
    }
    SDL_UnlockMutex(g->q_mutex);
    return c;
}

int vgen_async_pending(VGen *g)
{
    if (g == NULL) {
        return 0;
    }
    SDL_LockMutex(g->q_mutex);
    int n = g->req_count + (g->busy ? 1 : 0);
    SDL_UnlockMutex(g->q_mutex);
    return n;
}

bool vgen_async_has(VGen *g, int cx, int cz)
{
    if (g == NULL) {
        return false;
    }
    SDL_LockMutex(g->q_mutex);
    bool has = has_locked(g, cx, cz);
    SDL_UnlockMutex(g->q_mutex);
    return has;
}
