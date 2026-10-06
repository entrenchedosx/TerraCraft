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
        TEST_ASSERT(ticks_for_render_rate(rates[i]) == 60);
    }
    return failures;
}

int test_simulation_clock_catch_up_and_remainder(void)
{
    int failures = 0;
    SimulationClock clock;
    simulation_clock_init(&clock);

    /* 0.37 seconds is 22 whole ticks plus 0.2 of a tick at 60 Hz. Five
     * ticks run, seventeen are explicitly accounted as dropped, and the
     * fraction remains. */
    SimulationAdvance advance = simulation_clock_advance(&clock, 0.37);
    TEST_ASSERT(advance.ticks == 5);
    TEST_ASSERT(advance.dropped_ticks == 17);
    TEST_ASSERT(fabs(advance.dropped_time - 17.0 / 60.0) < 1e-9);
    TEST_ASSERT(fabs(advance.alpha - 0.2) < 1e-9);
    TEST_ASSERT(clock.tick_count == 5);
    TEST_ASSERT(clock.dropped_ticks == 17);
    TEST_ASSERT(fabs(clock.dropped_time - 17.0 / 60.0) < 1e-9);

    advance = simulation_clock_advance(&clock, 0.035);
    TEST_ASSERT(advance.ticks == 2);
    TEST_ASSERT(advance.dropped_ticks == 0);
    TEST_ASSERT(clock.tick_count == 7);
    TEST_ASSERT(clock.dropped_ticks == 17);
    TEST_ASSERT(fabs(advance.alpha - 0.3) < 1e-6);
    return failures;
}

int test_simulation_clock_invalid_elapsed(void)
{
    int failures = 0;
    SimulationClock clock;
    simulation_clock_init(&clock);

    /* 0.008 s is 0.48 ticks at 60 Hz: no tick, alpha carries. */
    SimulationAdvance advance = simulation_clock_advance(&clock, 0.008);
    TEST_ASSERT(advance.ticks == 0);
    TEST_ASSERT(fabs(advance.alpha - 0.48) < 1e-9);

    advance = simulation_clock_advance(&clock, -1.0);
    TEST_ASSERT(advance.ticks == 0);
    TEST_ASSERT(fabs(clock.accumulator - 0.008) < 1e-9);
    advance = simulation_clock_advance(&clock, NAN);
    TEST_ASSERT(advance.ticks == 0);
    TEST_ASSERT(fabs(clock.accumulator - 0.008) < 1e-9);
    advance = simulation_clock_advance(&clock, INFINITY);
    TEST_ASSERT(advance.ticks == 0);
    TEST_ASSERT(fabs(clock.accumulator - 0.008) < 1e-9);

    /* Another 0.009 s crosses the boundary: 1 tick, alpha 0.02. */
    advance = simulation_clock_advance(&clock, 0.009);
    TEST_ASSERT(advance.ticks == 1);
    TEST_ASSERT(clock.tick_count == 1);
    TEST_ASSERT(fabs(advance.alpha - 0.02) < 1e-6);
    return failures;
}
