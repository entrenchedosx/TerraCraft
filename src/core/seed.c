#include "core/seed.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* FNV-1a 64-bit: offset basis ^ each byte, prime multiply. */
unsigned long long seed_hash_text(const char *text, size_t len)
{
    unsigned long long h = 14695981039346656037ULL;
    if (text == NULL) {
        return h;
    }
    for (size_t i = 0; i < len; ++i) {
        h ^= (unsigned long long)(unsigned char)text[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* True for ASCII whitespace (space, tab, CR, LF, VT, FF). */
static bool seed_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/* Parse a seed field.
 *
 * Args:
 *   field: user input.
 *   is_blank: receives blank flag.
 *
 * Returns: seed value.
 */
long seed_parse(const char *field, bool *is_blank)
{
    static bool dummy = false;
    bool *blank = is_blank != NULL ? is_blank : &dummy;
    *blank = true;
    if (field == NULL) {
        return 0L;
    }
    /* Trim ASCII whitespace on both ends into a bounded copy. */
    while (seed_is_space(*field)) {
        ++field;
    }
    size_t len = strlen(field);
    while (len > 0 && seed_is_space(field[len - 1])) {
        --len;
    }
    if (len == 0) {
        return 0L;
    }
    char buf[64];
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, field, len);
    buf[len] = '\0';

    /* Integer attempt (base 0: decimal, 0x hex, 0 octal), fully consumed. */
    char *end = NULL;
    long long v = strtoll(buf, &end, 0);
    if (end != NULL && *end == '\0' && end != buf) {
        *blank = false;
        if (v > (long long)LONG_MAX) {
            return LONG_MAX;
        }
        if (v < (long long)LONG_MIN) {
            return LONG_MIN;
        }
        return (long)v;
    }
    /* Text fallback: deterministic FNV-1a hash of the trimmed field. */
    *blank = false;
    unsigned long long h = seed_hash_text(buf, len);
    long out = 0L;
    memcpy(&out, &h, sizeof(out) < sizeof(h) ? sizeof(out) : sizeof(h));
    if (out == 0L) {
        out = 1L; /* Keep 0 reserved for "blank, generate me". */
    }
    return out;
}
