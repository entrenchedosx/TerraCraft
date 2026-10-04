#include "test_main.h"

#include "game/simulation_clock.h"

#include <math.h>
#include <stddef.h>

static uint64_t ticks_for_render_rate(unsigned fps)
{
    SimulationClock clock;
    simulation_clock_init(&clock);
    uint64_t total = 0;
    for (unsigned frame = 0; frame < fps; ++frame) {
        SimulationAdvance advance = simulation_clock_advance(&clock, 1.0 / (double)fps);
        total += advance.ticks;
        if (advance.dropped_ticks != 0) {
            return UINT64_MAX;
        }
    }
    return total;
}

int test_simulation_clock_render_rates(void)
{
    int failures = 0;
    const unsigned rates[] = {30, 60, 144, 240};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        TEST_ASSERT(ticks_for_render_rate(rates[i]) == 20);
    }
    return failures;
}

int test_simulation_clock_catch_up_and_remainder(void)
{
    int failures = 0;
    SimulationClock clock;
    simulation_clock_init(&clock);

    /* 0.37 seconds is seven whole ticks plus 0.02 seconds. Five ticks run,
     * two are explicitly accounted as dropped, and the fraction remains. */
    SimulationAdvance advance = simulation_clock_advance(&clock, 0.37);
    TEST_ASSERT(advance.ticks == 5);
    TEST_ASSERT(advance.dropped_ticks == 2);
    TEST_ASSERT(fabs(advance.dropped_time - 0.1) < 1e-9);
    TEST_ASSERT(fabs(advance.alpha - 0.4) < 1e-9);
    TEST_ASSERT(clock.tick_count == 5);
    TEST_ASSERT(clock.dropped_ticks == 2);
    TEST_ASSERT(fabs(clock.dropped_time - 0.1) < 1e-9);

    advance = simulation_clock_advance(&clock, 0.03);
    TEST_ASSERT(advance.ticks == 1);
    TEST_ASSERT(advance.dropped_ticks == 0);
    TEST_ASSERT(clock.tick_count == 6);
    TEST_ASSERT(clock.dropped_ticks == 2);
    TEST_ASSERT(fabs(advance.alpha) < 1e-9);
    return failures;
}

int test_simulation_clock_invalid_elapsed(void)
{
    int failures = 0;
    SimulationClock clock;
    simulation_clock_init(&clock);

    SimulationAdvance advance = simulation_clock_advance(&clock, 0.025);
    TEST_ASSERT(advance.ticks == 0);
    TEST_ASSERT(fabs(advance.alpha - 0.5) < 1e-9);

    advance = simulation_clock_advance(&clock, -1.0);
    TEST_ASSERT(advance.ticks == 0);
    TEST_ASSERT(fabs(clock.accumulator - 0.025) < 1e-9);
    advance = simulation_clock_advance(&clock, NAN);
    TEST_ASSERT(advance.ticks == 0);
    TEST_ASSERT(fabs(clock.accumulator - 0.025) < 1e-9);
    advance = simulation_clock_advance(&clock, INFINITY);
    TEST_ASSERT(advance.ticks == 0);
    TEST_ASSERT(fabs(clock.accumulator - 0.025) < 1e-9);

    advance = simulation_clock_advance(&clock, 0.025);
    TEST_ASSERT(advance.ticks == 1);
    TEST_ASSERT(clock.tick_count == 1);
    TEST_ASSERT(fabs(advance.alpha) < 1e-9);
    return failures;
}
