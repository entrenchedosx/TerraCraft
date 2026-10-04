#pragma once

/* High-resolution monotonic-ish timer for FPS calculation and dt.
 * Implemented with C11 `timespec_get` so core/ does not depend on SDL.
 */

/* Get seconds since an unspecified epoch (monotonic where available).
 * Call once at startup and subtract to get elapsed time.
 *
 * Returns: seconds as double (fractional). Never negative in practice.
 */
double time_now_seconds(void);
