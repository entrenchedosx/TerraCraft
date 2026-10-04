#include "game/simulation_clock.h"

#include <math.h>
#include <stddef.h>

/* The epsilon only absorbs floating-point undershoot when repeated frame
 * partitions mathematically sum to an exact tick boundary. It is much smaller
 * than any meaningful simulation interval. */
#define SIMULATION_CLOCK_EPSILON 1e-12

void simulation_clock_init(SimulationClock *clock)
{
    if (clock == NULL) {
        return;
    }
    clock->accumulator = 0.0;
    clock->tick_count = 0;
    clock->dropped_ticks = 0;
    clock->dropped_time = 0.0;
}

void simulation_clock_reset_phase(SimulationClock *clock)
{
    if (clock != NULL) {
        clock->accumulator = 0.0;
    }
}

SimulationAdvance simulation_clock_advance(SimulationClock *clock, double elapsed_seconds)
{
    SimulationAdvance result = {0, 0.0, 0, 0.0};
    if (clock == NULL || !isfinite(elapsed_seconds) || elapsed_seconds <= 0.0) {
        if (clock != NULL) {
            result.alpha = clock->accumulator / SIMULATION_TICK_SECONDS;
        }
        return result;
    }

    double elapsed = clock->accumulator + elapsed_seconds;
    double quotient = floor((elapsed + SIMULATION_CLOCK_EPSILON) / SIMULATION_TICK_SECONDS);
    if (quotient < 0.0) {
        quotient = 0.0;
    }

    /* Convert only after bounding to the integer counter's range. For
     * physically implausible intervals beyond that range, modulo still
     * preserves the fractional remainder and the counters saturate. */
    uint64_t due_ticks;
    if (quotient >= (double)UINT64_MAX) {
        due_ticks = UINT64_MAX;
    } else {
        due_ticks = (uint64_t)quotient;
    }

    unsigned run_ticks = due_ticks > SIMULATION_MAX_CATCH_UP_TICKS
                             ? SIMULATION_MAX_CATCH_UP_TICKS
                             : (unsigned)due_ticks;
    uint64_t dropped_ticks = due_ticks - (uint64_t)run_ticks;

    double remainder;
    if (quotient >= (double)UINT64_MAX) {
        remainder = fmod(elapsed, SIMULATION_TICK_SECONDS);
    } else {
        remainder = elapsed - (double)due_ticks * SIMULATION_TICK_SECONDS;
    }
    if (remainder < 0.0 && remainder > -SIMULATION_CLOCK_EPSILON) {
        remainder = 0.0;
    }
    if (remainder >= SIMULATION_TICK_SECONDS) {
        remainder = fmod(remainder, SIMULATION_TICK_SECONDS);
    }
    clock->accumulator = remainder;

    if (UINT64_MAX - clock->tick_count < (uint64_t)run_ticks) {
        clock->tick_count = UINT64_MAX;
    } else {
        clock->tick_count += (uint64_t)run_ticks;
    }
    if (UINT64_MAX - clock->dropped_ticks < dropped_ticks) {
        clock->dropped_ticks = UINT64_MAX;
    } else {
        clock->dropped_ticks += dropped_ticks;
    }

    double dropped_time = (double)dropped_ticks * SIMULATION_TICK_SECONDS;
    clock->dropped_time += dropped_time;
    result.ticks = run_ticks;
    result.alpha = clock->accumulator / SIMULATION_TICK_SECONDS;
    result.dropped_ticks = dropped_ticks;
    result.dropped_time = dropped_time;
    return result;
}
