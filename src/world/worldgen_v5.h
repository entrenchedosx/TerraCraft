#pragma once
#include "world/worldgen_v4.h"

/* Original Java 1.21.11-inspired geography. No Java seed parity. Profile4
 * remains frozen; profile5 shares its cached material/ore kernel. */
GenClimate worldgen5_climate(long seed, int x, int z);
GenDensity worldgen5_density(long seed, int x, int y, int z);
GenAquifer worldgen5_aquifer(long seed, int x, int y, int z);
int worldgen5_height(long seed, int x, int z);
uint16_t worldgen5_surface(long seed, int x, int z, int height);
uint16_t worldgen5_base_block(long seed, int x, int y, int z);
void worldgen5_fill(Chunk *chunk, long seed);

/* Pure vegetation decisions, including slope and coherent woodland cover. */
bool worldgen5_tree(long seed, int x, int z, int *trunk);
uint16_t worldgen5_vegetation(long seed, int x, int z, int height);
