#pragma once

/* Block interaction (M3): break/place with cross-chunk dirty marking.
 * Pure CPU, headless-testable. Particle/feedback effects are log stubs.
 */

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
typedef struct World World;
typedef struct Player Player;
typedef struct HitResult HitResult;

/* Break the targeted block: AIR (unless bedrock/air/miss), dirty-mark the
 * chunk and any same-level neighbours sharing the block border.
 * Logs a particle stub line on success.
 *
 * Args:
 *   w: world (must not be NULL).
 *   hit: raycast result (must not be NULL).
 *
 * Returns: broken block ID, or -1 when nothing broke.
 */
int interaction_break(World *w, const HitResult *hit);

/* Place the selected hotbar block against the targeted face. Clicking
 * non-solid decor replaces its own cell rather than placing on top of it.
 * Flowers and grass plants additionally require grass, dirt, or snow beneath
 * them.
 * Placement is rejected when the target cell is outside [0,255], occupied
 * by a non-replaceable block, unsupported, or overlaps the player AABB.
 *
 * Args:
 *   w: world (must not be NULL).
 *   p: player for self-overlap check (must not be NULL).
 *   hit: raycast result (must not be NULL).
 *   block_id: block to place (AIR never placed).
 *   out_replaced_block: optional; receives the replaced block on success,
 *     or AIR when placement fails or the cell was empty.
 *
 * Returns: true when a block was placed.
 */
bool interaction_place(World *w, const Player *p, const HitResult *hit, uint16_t block_id,
                       uint16_t *out_replaced_block);
