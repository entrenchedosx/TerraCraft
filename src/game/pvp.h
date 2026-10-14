#pragma once

/* Player-versus-player melee helpers (pure CPU, headless-testable).
 *
 * Integrates with the existing combat path: same reach, same block
 * occlusion rule (solid blocks shield; plants/torches do not), same
 * cooldown/hurt-window discipline as mob melee. The app owns raycasts,
 * cooldowns, damage application, and LAN validation; this module only
 * answers geometry + policy questions so they are unit-testable without
 * a window, world, or socket.
 *
 * Conventions: positions are FEET. Player AABB is width 0.6, height
 * 1.8 standing / 1.5 sneaking (see game/player.h).
 */

#include "math/mmath.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PVP_REACH 3.0f
#define PVP_TARGET_WIDTH 0.6f
#define PVP_TARGET_HEIGHT_STAND 1.8f
#define PVP_TARGET_HEIGHT_SNEAK 1.5f

/* Ray vs player AABB (feet-origin box). Slab test, pure math.
 *
 * Args:
 *   eye: ray origin (world).
 *   dir: ray direction (need not be normalized; zero reads as miss).
 *   max_dist: range limit (> 0).
 *   target_feet: victim feet position.
 *   target_width/target_height: victim hitbox extents (> 0).
 *   out_dist: receives ray distance on hit (may be NULL).
 *
 * Returns: true when the ray enters the box within max_dist.
 */
bool pvp_ray_hits_player(Vec3 eye, Vec3 dir, float max_dist, Vec3 target_feet, float target_width,
                         float target_height, float *out_dist);

/* Validate an authoritative PvP strike request (host-side policy).
 *
 * Rejects: pvp disabled, attacker/victim dead, self-hit, non-positive
 * damage, out-of-range, stale/duplicate sequence numbers.
 *
 * Args:
 *   pvp_enabled: world rule.
 *   attacker_alive/victim_alive: liveness gates.
 *   attacker_id/victim_id: game player IDs (host is 0).
 *   damage: requested damage (> 0, capped by caller to weapon max).
 *   distance: measured attacker-to-victim ray distance (>= 0).
 *   seq: monotonically increasing per-attacker sequence number.
 *   last_seq: last applied sequence for this attacker (in/out; NULL
 *     disables duplicate suppression but still validates the rest).
 *
 * Returns: true when the strike may be applied (updates *last_seq).
 */
bool pvp_validate_strike(bool pvp_enabled, bool attacker_alive, bool victim_alive, uint32_t attacker_id,
                         uint32_t victim_id, float damage, float distance, uint32_t seq,
                         uint32_t *last_seq);

/* Clamp client-supplied damage into the weapon-sane range
 * (never trust arbitrary client damage; see app.c).
 *
 * Args:
 *   damage: requested damage.
 *
 * Returns: clamped damage in [0, 20].
 */
float pvp_clamp_damage(float damage);
