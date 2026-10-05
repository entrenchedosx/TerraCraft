#pragma once

/* Small, pure helpers for first-person hand animation. */

/* Total time from the start of a punch to its resting pose. */
#define PLAYER_SWING_DURATION 0.42f

/* Return a clamped 0..1 position through a swing for elapsed seconds. */
float player_swing_phase(float elapsed_seconds);

/* Return the smooth 0..1 swing envelope for a clamped phase. */
float player_swing_weight(float phase);
