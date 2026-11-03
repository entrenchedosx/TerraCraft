#pragma once

/* Vanilla 1.21.11 multi-noise biome lookup: Climate.TargetPoint
 * quantisation, the 7-dimensional Climate.RTree (built exactly like
 * Climate.RTree.create, including its sort order and the per-thread
 * "last result" search shortcut) and the overworld parameter list loaded at
 * run time from the vanilla data generator's reports directory:
 *   <root>/reports/biome_parameters/minecraft/overworld.json
 * Nothing from that file is compiled into or committed with TerraCraft.
 */

#include "world/jdensity.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct JBiomeList JBiomeList;

/* Climate.TargetPoint: quantised (x10000) parameters. */
typedef struct JTargetPoint {
    int64_t temperature, humidity, continentalness, erosion, depth, weirdness;
} JTargetPoint;

/* Climate.quantizeCoord: (long)(f * 10000.0F), float arithmetic. */
int64_t jclimate_quantize(float f);

/* Climate.Sampler.sample(quartX, quartY, quartZ) over the router outputs. */
JTargetPoint jclimate_sample(const JRandomState *rs, int qx, int qy, int qz);

/* Load and index the overworld parameter list. NULL if the report is missing. */
JBiomeList *jbiome_load(const char *datapack_root);
void jbiome_free(JBiomeList *l);

int jbiome_leaf_count(const JBiomeList *l);
/* Order-sensitive hash of every leaf (same recipe as the Java reference dump). */
int64_t jbiome_list_hash(const JBiomeList *l);

/* Climate.ParameterList.findValue (R-tree, with the last-result shortcut).
 * Returns the biome id ("minecraft:plains"). Query order matters for exact
 * ties, as in vanilla (thread-local last leaf). */
const char *jbiome_find(JBiomeList *l, const JTargetPoint *t);
/* Climate.ParameterList.findValueBruteForce (first minimum). */
const char *jbiome_find_brute(const JBiomeList *l, const JTargetPoint *t);

/* Distinct biome ids in order of first appearance in the parameter list (this is the
 * order of MultiNoiseBiomeSource.possibleBiomes()). */
int jbiome_name_count(const JBiomeList *l);
const char *jbiome_name(const JBiomeList *l, int i);
