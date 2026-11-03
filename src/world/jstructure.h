#pragma once

/* Vanilla 1.21.11 structure-set placement (StructurePlacement.isStructureChunk) for the
 * random-spread sets: spacing/separation/salt with linear or triangular spread, the four
 * frequency-reduction methods and exclusion zones against other sets. The sets are read at run
 * time from data/minecraft/worldgen/structure_set/*.json of the local vanilla data pack.
 *
 * This decides which chunks may start a structure of a set. It does not decide whether a structure
 * actually generates there (biome validity, terrain checks) and does not build any structure pieces;
 * concentric-ring sets (strongholds) are not supported.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct JStructureSets JStructureSets;

JStructureSets *jstructures_load(const char *datapack_root);
void jstructures_free(JStructureSets *s);

int jstructures_count(const JStructureSets *s);
/* Registry id ("minecraft:villages") and whether the set is random spread (supported). */
const char *jstructures_name(const JStructureSets *s, int index);
bool jstructures_is_random_spread(const JStructureSets *s, int index);
/* Index of a set by id, or -1. */
int jstructures_find(const JStructureSets *s, const char *name);

/* RandomSpreadStructurePlacement.getPotentialStructureChunk: the single chunk of the spacing region
 * of (chunk_x, chunk_z) that may host a structure. */
void jstructures_potential_chunk(const JStructureSets *s, int index, int64_t seed, int chunk_x, int chunk_z, int *out_x,
                                 int *out_z);
/* StructurePlacement.isStructureChunk (placement chunk, frequency reduction, exclusion zone). */
bool jstructures_is_structure_chunk(const JStructureSets *s, int index, int64_t seed, int chunk_x, int chunk_z);
