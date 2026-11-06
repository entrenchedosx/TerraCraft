#pragma once

/* Vanilla-exact terrain for the game (terrain_version 6): runs the 1.21.11 generator ports
 * (fill, surface rules, carvers, biome decoration) and converts the result into TerraCraft
 * chunks.
 *
 * Y mapping: Minecraft y -64..319 becomes chunk y 0..383 (+64), so bedrock sits at chunk
 * y 0..4 and the sea surface at chunk y 127.
 *
 * Cross-chunk features. Vanilla decorates a chunk against its already generated neighbours, so the
 * result depends on generation order. The game must give the same chunk whichever order the
 * player explores in, so each chunk is built as
 *   final(C) = base(C) + the decoration of every chunk N in the 3 x 3 around C that lands in C,
 * where base is fill + surface + carvers and the decoration of N is computed once against the
 * pure base chunks of the 3 x 3 around N (never against a neighbour's features). Features of one
 * chunk therefore see each other exactly as in vanilla, while overlaps between features of
 * different chunks (a tree canopy crossing a chunk border) resolve in a fixed order.
 *
 * The generator reads the vanilla data pack from a local directory (see vgen_find_datapack) and
 * is NOT thread-safe by itself: vgen_build_chunk serialises callers with an internal mutex, and
 * the asynchronous interface (vgen_async_*) runs it on one worker thread.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct Chunk Chunk;
typedef struct VGen VGen;

/* Locate the data pack root (a directory holding data/minecraft/... and reports/...):
 * $TERRACRAFT_DATAPACK, <mcassets>/generated/datapack, then reference/1.21.11/run/out.
 * Writes the path to out and returns true when a usable pack was found. */
bool vgen_find_datapack(char *out, int cap);

/* True when a data pack is present, i.e. vanilla worlds can be generated at all. */
bool vgen_available(void);

/* Create a generator for a seed. NULL when the data pack is missing or malformed. */
VGen *vgen_create(const char *datapack_root, int64_t seed);
/* Stops the worker (if running) and frees everything. NULL-safe. */
void vgen_destroy(VGen *g);

/* Seed the generator was created for. */
int64_t vgen_seed(const VGen *g);

/* Build chunk (cx, cz) into out (blocks, biome columns; dirty is set, save_dirty cleared).
 * Blocks until done; safe to call from any thread, one build at a time. */
bool vgen_build_chunk(VGen *g, Chunk *out, int cx, int cz);

/* Cheap screening for the spawn search: false when the biome at the middle of the chunk (at sea level) is an
 * ocean or a river, so no terrain has to be generated to rule it out. */
bool vgen_chunk_may_be_land(VGen *g, int cx, int cz);

/* Map a vanilla block name ("minecraft:oak_log") to a TerraCraft block id.
 * Plants without a counterpart map to BLOCK_AIR. */
uint16_t vgen_map_block_name(const char *name);

/* Biome class (BiomeType) used for tinting for a vanilla biome id ("minecraft:plains"). */
int vgen_biome_class(const char *biome_id);

/* ---- Asynchronous generation (one worker thread) ---------------------------------------------- */

/* Start the worker. Safe to call repeatedly. Returns false if the thread could not start. */
bool vgen_async_start(VGen *g);
/* Tell the worker where the player is: pending requests farther than radius + 1 chunks (Chebyshev)
 * from (cx, cz) are dropped, and the nearest pending request is built first. */
void vgen_async_set_center(VGen *g, int cx, int cz, int radius);
/* Queue a chunk for generation. Returns false when the queue is full or the chunk is already
 * pending or finished (not an error: the caller just asks again later). */
bool vgen_async_request(VGen *g, int cx, int cz);
/* Take a finished chunk (caller owns it) or NULL when none is ready. */
Chunk *vgen_async_poll(VGen *g);
/* Number of chunks queued or being built. */
int vgen_async_pending(VGen *g);
/* True when (cx, cz) is queued, being built or waiting in the finished list. */
bool vgen_async_has(VGen *g, int cx, int cz);
