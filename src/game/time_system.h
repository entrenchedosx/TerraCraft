#pragma once

/* Day/night time system (M4): 0.0..1.0 day progress driving sky color,
 * sun direction, and global light intensity. Pure CPU, headless-testable.
 *
 * Phase map: 0.00 midnight, 0.25 sunrise, 0.50 noon, 0.75 sunset.
 */

#include "math/mmath.h"

/* Default cycle speed: one full day per 1200 seconds (MC: 20 minutes). */
#define TIME_DEFAULT_SPEED (1.0f / 1200.0f)

/* Minimum global light so nights stay navigable (never pitch black). */
#define TIME_MIN_INTENSITY 0.15f

/* Day/night state. */
typedef struct TimeSystem {
    float day_progress; /* 0.0..1.0, wraps. */
    float speed;        /* Cycles per second (0 pauses). */
} TimeSystem;

/* Initialise the clock.
 *
 * Args:
 *   t: system to init (must not be NULL).
 *   start_progress: initial 0.0..1.0 (wraps into range).
 *   speed: cycles per second (negative clamps to 0 = paused).
 */
void time_system_init(TimeSystem *t, float start_progress, float speed);

/* Advance the clock, wrapping at 1.0.
 *
 * Args:
 *   t: system (must not be NULL).
 *   dt: seconds to advance (>= 0).
 */
void time_system_update(TimeSystem *t, float dt);

/* Interpolated sky color for the current phase (dawn/noon/dusk/night keys).
 *
 * Args:
 *   t: system (must not be NULL).
 *
 * Returns: RGB sky color (deep blue at midnight).
 */
Vec3 time_get_sky_color(const TimeSystem *t);

/* Sun direction (unit): rises east at 0.25, zenith at 0.50, sets west
 * at 0.75, below the horizon all night (diffuse term then yields 0).
 *
 * Args:
 *   t: system (must not be NULL).
 *
 * Returns: normalized direction to the sun.
 */
Vec3 time_get_sun_dir(const TimeSystem *t);

/* Global light multiplier in [TIME_MIN_INTENSITY, 1.0].
 *
 * Args:
 *   t: system (must not be NULL).
 *
 * Returns: intensity scalar.
 */
float time_get_light_intensity(const TimeSystem *t);
