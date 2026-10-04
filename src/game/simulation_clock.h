#pragma once

/* Fixed-rate simulation scheduler, independent of rendering and SDL.
 *
 * Call simulation_clock_advance once per rendered frame with elapsed wall
 * time. It reports how many authoritative simulation ticks the caller must
 * execute. The clock limits catch-up work, accounts for any whole ticks it
 * drops, and retains the fractional remainder for interpolation.
 */

#include <stdint.h>

#define SIMULATION_TICKS_PER_SECOND 20
#define SIMULATION_TICK_SECONDS (1.0 / (double)SIMULATION_TICKS_PER_SECOND)
#define SIMULATION_MAX_CATCH_UP_TICKS 5

typedef struct SimulationClock {
    double accumulator;       /* Fractional elapsed simulation time, [0, tick). */
    uint64_t tick_count;       /* Ticks scheduled for simulation (excludes drops). */
    uint64_t dropped_ticks;    /* Whole overdue ticks discarded by the catch-up cap. */
    double dropped_time;       /* Total simulated time represented by dropped ticks. */
} SimulationClock;

typedef struct SimulationAdvance {
    unsigned ticks;            /* Ticks caller should execute this frame. */
    double alpha;              /* Interpolation fraction, [0, 1). */
    uint64_t dropped_ticks;     /* Whole ticks discarded by this advance. */
    double dropped_time;        /* Time discarded by this advance, in seconds. */
} SimulationAdvance;

/* Clear the clock and its counters. Safe on NULL. */
void simulation_clock_init(SimulationClock *clock);

/* Clear only the fractional phase, preserving lifetime diagnostic counters.
 * Use across pause/menu transitions so paused wall time is never caught up.
 */
void simulation_clock_reset_phase(SimulationClock *clock);

/* Schedule fixed-rate work from elapsed wall time. Negative, NaN, and
 * infinite elapsed values are ignored. At most SIMULATION_MAX_CATCH_UP_TICKS
 * are returned; any additional whole overdue ticks are counted as dropped,
 * while the sub-tick remainder is retained. Safe on NULL (returns zeros).
 */
SimulationAdvance simulation_clock_advance(SimulationClock *clock, double elapsed_seconds);
