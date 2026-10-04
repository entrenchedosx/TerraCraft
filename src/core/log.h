#pragma once

/* Logging system: leveled, timestamped output to stdout/stderr.
 *
 * Thread-safety: M0 is single-threaded. Each log call performs a single
 * fprintf + fflush, which is atomic enough for our purposes on hosted
 * implementations. A mutex will be added when worker threads land (M1+).
 */

#include <stdarg.h>

/* Log severity levels, ordered by verbosity. */
typedef enum LogLevel {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel;

/* Initialise the logger (currently a no-op, returns 0 on success).
 * Kept for symmetry with other subsystems and future file-sink support.
 *
 * Returns: 0 on success.
 */
int log_init(void);

/* Set the minimum level that will be emitted. Messages below this are dropped.
 *
 * Args:
 *   level: minimum LogLevel to emit.
 */
void log_set_level(LogLevel level);

/* Core log function. Prefer the LOG_* macros below.
 *
 * Args:
 *   level: severity of this message.
 *   file: source file (__FILE__).
 *   line: source line (__LINE__).
 *   fmt: printf-style format string.
 *   ...: format arguments.
 */
void log_write(LogLevel level, const char *file, int line, const char *fmt, ...);

/* Convenience macros that capture file/line automatically. */
#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
