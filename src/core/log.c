#include "core/log.h"

#include <stdio.h>
#include <time.h>

/* Current minimum level. Defaults to DEBUG so M0 shows everything. */
static LogLevel s_min_level = LOG_LEVEL_DEBUG;

/* Initialise the logger.
 *
 * Returns: 0 (always succeeds for now).
 */
int log_init(void)
{
    s_min_level = LOG_LEVEL_DEBUG;
    return 0;
}

/* Set the minimum level that will be emitted.
 *
 * Args:
 *   level: minimum LogLevel to emit.
 */
void log_set_level(LogLevel level)
{
    s_min_level = level;
}

/* Format current local time as HH:MM:SS into `buf` (size must be >= 9).
 * Falls back to "??:??:??" if the clock is unavailable.
 */
static void log_timestamp(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size < 9) {
        return;
    }
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        buf[0] = '?';
        buf[1] = '?';
        buf[2] = ':';
        buf[3] = '?';
        buf[4] = '?';
        buf[5] = ':';
        buf[6] = '?';
        buf[7] = '?';
        buf[8] = '\0';
        return;
    }
    struct tm tm_now;
#if defined(_MSC_VER)
    localtime_s(&tm_now, &now);
#else
    struct tm *p = localtime(&now);
    if (p == NULL) {
        buf[0] = '\0';
        return;
    }
    tm_now = *p;
#endif
    strftime(buf, buf_size, "%H:%M:%S", &tm_now);
}

/* Core log function.
 *
 * Args:
 *   level: severity.
 *   file: source file name.
 *   line: source line number.
 *   fmt: printf-style format.
 */
void log_write(LogLevel level, const char *file, int line, const char *fmt, ...)
{
    if (level < s_min_level) {
        return;
    }
    if (fmt == NULL) {
        return;
    }

    const char *level_str = "UNKNOWN";
    FILE *out = stdout;
    switch (level) {
    case LOG_LEVEL_DEBUG:
        level_str = "DEBUG";
        out = stdout;
        break;
    case LOG_LEVEL_INFO:
        level_str = "INFO";
        out = stdout;
        break;
    case LOG_LEVEL_WARN:
        level_str = "WARN";
        out = stderr;
        break;
    case LOG_LEVEL_ERROR:
        level_str = "ERROR";
        out = stderr;
        break;
    default:
        break;
    }

    char ts[16];
    log_timestamp(ts, sizeof(ts));

    fprintf(out, "[%s] [%s] %s:%d: ", ts, level_str, file ? file : "?", line);

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fprintf(out, "\n");
    fflush(out);
}
