#include "world/jstructure.h"

#include "world/jjson.h"
#include "world/jrandom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

#define NAME_LEN 64

typedef enum SpreadType { SPREAD_LINEAR, SPREAD_TRIANGULAR } SpreadType;
typedef enum Reduction { RED_DEFAULT, RED_LEGACY1, RED_LEGACY2, RED_LEGACY3 } Reduction;

typedef struct StructureSet {
    char name[NAME_LEN];
    bool random_spread;
    int spacing, separation, salt;
    SpreadType spread;
    float frequency;
    Reduction reduction;
    char exclusion_set[NAME_LEN];
    int exclusion_chunks;
} StructureSet;

struct JStructureSets {
    StructureSet *sets;
    int count;
};

static const char *jstr(const JJson *j)
{
    return (j != NULL && j->type == JJ_STR) ? j->text : "";
}

static int floor_div(int a, int b)
{
    int q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/* WorldgenRandom.setLargeFeatureWithSalt(seed, x, z, salt) over a legacy source. */
static void seed_with_salt(JrRandom *r, int64_t seed, int x, int z, int salt)
{
    uint64_t s = (uint64_t)(int64_t)x * 341873128712ULL + (uint64_t)(int64_t)z * 132897987541ULL + (uint64_t)seed +
                 (uint64_t)(int64_t)salt;
    jr_legacy_init(r, (int64_t)s);
}

/* WorldgenRandom.setLargeFeatureSeed(seed, x, z) */
static void seed_large_feature(JrRandom *r, int64_t seed, int x, int z)
{
    jr_legacy_init(r, seed);
    int64_t l = jr_next_long(r);
    int64_t m = jr_next_long(r);
    int64_t n = (int64_t)(((uint64_t)(int64_t)x * (uint64_t)l) ^ ((uint64_t)(int64_t)z * (uint64_t)m) ^ (uint64_t)seed);
    jr_legacy_init(r, n);
}

static int spread_evaluate(SpreadType t, JrRandom *r, int bound)
{
    if (t == SPREAD_LINEAR) {
        return jr_next_int_bound(r, bound);
    }
    int a = jr_next_int_bound(r, bound);
    int b = jr_next_int_bound(r, bound);
    return (a + b) / 2;
}

void jstructures_potential_chunk(const JStructureSets *s, int index, int64_t seed, int chunk_x, int chunk_z, int *out_x,
                                 int *out_z)
{
    const StructureSet *set = &s->sets[index];
    int i = floor_div(chunk_x, set->spacing);
    int j = floor_div(chunk_z, set->spacing);
    JrRandom r;
    seed_with_salt(&r, seed, i, j, set->salt);
    int bound = set->spacing - set->separation;
    int k = spread_evaluate(set->spread, &r, bound);
    int l = spread_evaluate(set->spread, &r, bound);
    *out_x = i * set->spacing + k;
    *out_z = j * set->spacing + l;
}

static bool frequency_ok(const StructureSet *set, int64_t seed, int x, int z)
{
    if (!(set->frequency < 1.0f)) {
        return true;
    }
    JrRandom r;
    switch (set->reduction) {
    case RED_DEFAULT:
        seed_with_salt(&r, seed, x, z, set->salt);
        return jr_next_float(&r) < set->frequency;
    case RED_LEGACY1: {
        int i = x >> 4, j = z >> 4;
        jr_legacy_init(&r, (int64_t)((int64_t)(i ^ (j << 4)) ^ seed));
        jr_next_int(&r);
        return jr_next_int_bound(&r, (int)(1.0f / set->frequency)) == 0;
    }
    case RED_LEGACY2:
        seed_with_salt(&r, seed, x, z, 10387320);
        return jr_next_float(&r) < set->frequency;
    case RED_LEGACY3:
        seed_large_feature(&r, seed, x, z);
        return jr_next_double(&r) < set->frequency;
    }
    return false;
}

bool jstructures_is_structure_chunk(const JStructureSets *s, int index, int64_t seed, int chunk_x, int chunk_z)
{
    const StructureSet *set = &s->sets[index];
    if (!set->random_spread) {
        return false;
    }
    int px, pz;
    jstructures_potential_chunk(s, index, seed, chunk_x, chunk_z, &px, &pz);
    if (px != chunk_x || pz != chunk_z) {
        return false;
    }
    if (!frequency_ok(set, seed, chunk_x, chunk_z)) {
        return false;
    }
    if (set->exclusion_chunks > 0) {
        int other = jstructures_find(s, set->exclusion_set);
        if (other >= 0 && other != index) {
            for (int i = chunk_x - set->exclusion_chunks; i <= chunk_x + set->exclusion_chunks; ++i) {
                for (int j = chunk_z - set->exclusion_chunks; j <= chunk_z + set->exclusion_chunks; ++j) {
                    if (jstructures_is_structure_chunk(s, other, seed, i, j)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

int jstructures_count(const JStructureSets *s)
{
    return s->count;
}

const char *jstructures_name(const JStructureSets *s, int index)
{
    return s->sets[index].name;
}

bool jstructures_is_random_spread(const JStructureSets *s, int index)
{
    return s->sets[index].random_spread;
}

int jstructures_find(const JStructureSets *s, const char *name)
{
    for (int i = 0; i < s->count; ++i) {
        if (strcmp(s->sets[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void jstructures_free(JStructureSets *s)
{
    if (s != NULL) {
        free(s->sets);
        free(s);
    }
}

static bool add_set(JStructureSets *out, const char *root, const char *file_base)
{
    char path[1300];
    snprintf(path, sizeof(path), "%s/data/minecraft/worldgen/structure_set/%s.json", root, file_base);
    JJson *j = jj_parse_file(path);
    if (j == NULL) {
        return false;
    }
    out->sets = (StructureSet *)realloc(out->sets, (size_t)(out->count + 1) * sizeof(StructureSet));
    StructureSet *set = &out->sets[out->count++];
    memset(set, 0, sizeof(*set));
    snprintf(set->name, sizeof(set->name), "minecraft:%s", file_base);
    const JJson *p = jj_get(j, "placement");
    const char *type = jstr(jj_get(p, "type"));
    set->random_spread = strcmp(type, "minecraft:random_spread") == 0;
    set->frequency = 1.0f;
    if (set->random_spread) {
        set->spacing = (int)jj_double(jj_get(p, "spacing"));
        set->separation = (int)jj_double(jj_get(p, "separation"));
        set->salt = (int)jj_double(jj_get(p, "salt"));
        set->spread = strcmp(jstr(jj_get(p, "spread_type")), "triangular") == 0 ? SPREAD_TRIANGULAR : SPREAD_LINEAR;
        const JJson *fr = jj_get(p, "frequency");
        if (fr != NULL) {
            set->frequency = jj_float(fr);
        }
        const char *m = jstr(jj_get(p, "frequency_reduction_method"));
        set->reduction = strcmp(m, "legacy_type_1") == 0 ? RED_LEGACY1
                       : strcmp(m, "legacy_type_2") == 0 ? RED_LEGACY2
                       : strcmp(m, "legacy_type_3") == 0 ? RED_LEGACY3 : RED_DEFAULT;
        const JJson *ex = jj_get(p, "exclusion_zone");
        if (ex != NULL) {
            snprintf(set->exclusion_set, sizeof(set->exclusion_set), "%s", jstr(jj_get(ex, "other_set")));
            set->exclusion_chunks = (int)jj_double(jj_get(ex, "chunk_count"));
        }
        if (set->spacing <= 0 || set->spacing <= set->separation) {
            jj_free(j);
            return false;
        }
    }
    jj_free(j);
    return true;
}

JStructureSets *jstructures_load(const char *datapack_root)
{
    if (datapack_root == NULL) {
        return NULL;
    }
    JStructureSets *s = (JStructureSets *)calloc(1, sizeof(JStructureSets));
    if (s == NULL) {
        return NULL;
    }
    char dir[1200];
    snprintf(dir, sizeof(dir), "%s/data/minecraft/worldgen/structure_set", datapack_root);
    char names[64][NAME_LEN];
    int n = 0;
#if defined(_WIN32)
    char pattern[1300];
    snprintf(pattern, sizeof(pattern), "%s/*.json", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            size_t len = strlen(fd.cFileName);
            if (len > 5 && n < 64 && len - 5 < NAME_LEN) {
                memcpy(names[n], fd.cFileName, len - 5);
                names[n][len - 5] = '\0';
                ++n;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    if (d != NULL) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t len = strlen(e->d_name);
            if (len > 5 && strcmp(e->d_name + len - 5, ".json") == 0 && n < 64 && len - 5 < NAME_LEN) {
                memcpy(names[n], e->d_name, len - 5);
                names[n][len - 5] = '\0';
                ++n;
            }
        }
        closedir(d);
    }
#endif
    if (n == 0) {
        free(s);
        return NULL;
    }
    for (int i = 0; i < n; ++i) {
        if (!add_set(s, datapack_root, names[i])) {
            jstructures_free(s);
            return NULL;
        }
    }
    return s;
}
