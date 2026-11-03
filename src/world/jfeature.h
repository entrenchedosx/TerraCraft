#pragma once

/* Vanilla 1.21.11 biome decoration (ChunkGenerator.applyBiomeDecoration) for the overworld:
 * FeatureSorter step lists, decoration/feature seeding, the PlacedFeature placement-modifier
 * pipeline and the feature types ported so far (currently: ore). Feature and placement
 * definitions are read at run time from the local vanilla data pack:
 *   data/minecraft/worldgen/biome/*.json, placed_feature/*.json, configured_feature/*.json,
 *   data/minecraft/tags/block/**.json
 * Nothing from the data pack is compiled into or committed with TerraCraft.
 *
 * Only block names are tracked (no block-state properties) - see docs/KNOWN_DIFFERENCES.md.
 */

#include "world/jfill.h"
#include "world/jsurface.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct JFeatures JFeatures;

/* Load the overworld feature lists and run FeatureSorter over the possible biomes of `surface`.
 * NULL if a data file is missing or malformed. */
JFeatures *jfeatures_load(const char *datapack_root, JSurface *surface);
void jfeatures_free(JFeatures *f);

/* FeatureSorter result: number of features in a decoration step and the id of one of them. */
int jfeatures_step_count(const JFeatures *f);
int jfeatures_step_size(const JFeatures *f, int step);
const char *jfeatures_step_feature(const JFeatures *f, int step, int index);

/* The generation region around the decorated chunk: 3 x 3 chunks, index (dz + 1) * 3 + (dx + 1),
 * each with its block array (JFILL_INDEX layout) and generation heightmaps. */
typedef struct JRegion {
    int target_x, target_z;
    uint8_t *blocks[9];
    JHeightmaps *heightmaps[9];
} JRegion;

/* applyBiomeDecoration for the target chunk, restricted to placed features whose configured feature
 * type is in `types` (e.g. {"minecraft:ore"}); seed is the world seed. Returns false if an allowed
 * feature uses a placement modifier or feature type that is not implemented. If name_count > 0 only
 * the listed placed features (ids like "minecraft:patch_grass_plain") run, in addition to the type filter. */
bool jfeatures_decorate(JFeatures *f, JSurface *surface, int64_t seed, JRegion *region, const char *const *types,
                        int type_count, const char *const *names, int name_count);
/* Number of blocks changed by the most recent decorate call. */
int jfeatures_last_changed(const JFeatures *f);
