#pragma once

/* Shared internals of jfeature.c and the feature implementations split out of it (jtree.c):
 * the generation-level view (3 x 3 chunk region), block tags, int providers and state providers.
 * Not part of the public world-generation API. */

#include "world/jfeature.h"
#include "world/jjson.h"
#include "world/jrandom.h"

#include <stdbool.h>
#include <stdint.h>

#define JF_MIN_Y (-64)
#define JF_HEIGHT 384
#define JF_NAME_LEN 72

#define JF_WEIGHTED_MAX 4

typedef struct IntProv {
    int kind; /* 0 constant, 1 uniform, 2 weighted_list of constants, -1 unsupported */
    int a, b;
    int wv[JF_WEIGHTED_MAX], ww[JF_WEIGHTED_MAX], wn, wtotal;
} IntProv;

/* BlockStateProvider subset: simple_state_provider, weighted_state_provider. */
typedef struct StateProvider {
    int kind; /* 0 simple, 1 weighted, -1 unsupported */
    int states[16];
    int weights[16];
    int count;
    int total_weight;
} StateProvider;

/* One recorded block write (used by the differential tree test to diff a single placement). */
typedef struct JfLogEntry {
    int x, y, z;
    uint8_t before;
} JfLogEntry;

typedef struct Level {
    JFeatures *f;
    JSurface *surface;
    JRegion *region;
    JrRandom *rnd;
    bool logging;
    JfLogEntry *log;
    int log_n, log_cap;
} Level;

/* Block tags used by tree placement, by block id (rebuilt when new block names were registered). */
typedef struct JTreeTags {
    int built_for;
    bool leaves[256];
    bool replaceable_by_trees[256];
    bool logs[256];
    bool dirt[256];
} JTreeTags;

const char *jf_jstr(const JJson *j);
const char *jf_local_name(const char *id);
IntProv jf_parse_int_provider(const JJson *j);
int jf_sample_int(const IntProv *p, JrRandom *r);
StateProvider jf_parse_provider(const JJson *j);
int jf_provider_state(const StateProvider *sp, JrRandom *r);

int jf_level_get(const Level *l, int x, int y, int z);
/* ChunkAccess.setBlockState through WorldGenRegion.setBlock: block plus all generation heightmaps. */
void jf_level_set(Level *l, int x, int y, int z, int state);
/* LevelReader.getHeight(type, x, z): 0 WORLD_SURFACE_WG, 1 OCEAN_FLOOR_WG, 2 MOTION_BLOCKING. */
int jf_level_height(Level *l, int type, int x, int z);
bool jf_level_can_write(const Level *l, int x, int z);
bool jf_tag_has(JFeatures *f, const char *tag, int block);
const JTreeTags *jf_tree_tags(Level *l);

/* Mth.sin(double) / Mth.cos(double) (65536-entry float table) */
float jf_mth_sin(double d);
float jf_mth_cos(double d);
