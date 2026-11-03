#pragma once

/* Vanilla 1.21.11 TreeFeature (minecraft:tree configured features): trunk placers, foliage placers,
 * feature sizes and tree decorators, evaluated on the generation region of jfeature.c. Configurations are
 * read from the local vanilla data pack (configured_feature/*.json); nothing from it is compiled in.
 *
 * Only block names are tracked (no block-state properties), see docs/KNOWN_DIFFERENCES.md. */

#include "world/jfeature_internal.h"

#include <stdbool.h>

typedef struct JTreeCfg JTreeCfg;

/* Parse the "config" object of a minecraft:tree configured feature. NULL (with a note on stderr) if it uses a
 * trunk placer, foliage placer, decorator or root placer that is not ported yet. */
JTreeCfg *jtree_parse(const JJson *config);
void jtree_free(JTreeCfg *t);

/* TreeFeature.place(context) with origin (x, y, z); `l->rnd` is the feature random. */
bool jtree_place(Level *l, const JTreeCfg *t, int x, int y, int z);
