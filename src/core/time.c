#include "core/time.h"

#include <time.h>

/* Get current time in seconds.
 *
 * Returns: seconds since TIME_UTC epoch as double.
 */
double time_now_seconds(void)
{
    struct timespec ts;
    /* timespec_get is C11; with __STDC_VERSION__ >= 201112L it exists.
     * MSVC (VS2015+) supports it as well. */
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}
