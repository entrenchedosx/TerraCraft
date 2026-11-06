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

/* Java String.hashCode over the UTF-16 code units of a UTF-8 string. */
static int32_t java_string_hash(const unsigned char *p, size_t len)
{
    uint32_t h = 0;
    for (size_t i = 0; i < len;) {
        uint32_t cp = p[i];
        size_t n = 1;
        if (cp >= 0xF0 && i + 3 < len) {
            cp = ((cp & 0x07u) << 18) | ((p[i + 1] & 0x3Fu) << 12) | ((p[i + 2] & 0x3Fu) << 6) | (p[i + 3] & 0x3Fu);
            n = 4;
        } else if (cp >= 0xE0 && i + 2 < len) {
            cp = ((cp & 0x0Fu) << 12) | ((p[i + 1] & 0x3Fu) << 6) | (p[i + 2] & 0x3Fu);
            n = 3;
        } else if (cp >= 0xC0 && i + 1 < len) {
            cp = ((cp & 0x1Fu) << 6) | (p[i + 1] & 0x3Fu);
            n = 2;
        }
        i += n;
        if (cp >= 0x10000u) {
            cp -= 0x10000u;
            h = h * 31u + (0xD800u + (cp >> 10));
            h = h * 31u + (0xDC00u + (cp & 0x3FFu));
        } else {
            h = h * 31u + cp;
        }
    }
    return (int32_t)h;
}

int64_t seed_parse_vanilla(const char *field, bool *is_blank)
{
    static bool dummy = false;
    bool *blank = is_blank != NULL ? is_blank : &dummy;
    *blank = true;
    if (field == NULL) {
        return 0;
    }
    while (seed_is_space(*field)) {
        ++field;
    }
    size_t len = strlen(field);
    while (len > 0 && seed_is_space(field[len - 1])) {
        --len;
    }
    if (len == 0) {
        return 0;
    }
    *blank = false;
    /* Long.parseLong: optional sign, then decimal digits that fit in 64 bits. */
    size_t i = 0;
    bool neg = false;
    if (field[0] == '-' || field[0] == '+') {
        neg = field[0] == '-';
        i = 1;
    }
    bool numeric = i < len;
    uint64_t mag = 0;
    for (size_t k = i; k < len && numeric; ++k) {
        if (field[k] < '0' || field[k] > '9') {
            numeric = false;
            break;
        }
        uint64_t d = (uint64_t)(field[k] - '0');
        if (mag > (UINT64_MAX - d) / 10u) {
            numeric = false;
            break;
        }
        mag = mag * 10u + d;
    }
    if (numeric && (neg ? mag <= (uint64_t)INT64_MAX + 1u : mag <= (uint64_t)INT64_MAX)) {
        return neg ? (int64_t)(0u - mag) : (int64_t)mag;
    }
    return (int64_t)java_string_hash((const unsigned char *)field, len);
}
