#pragma once

/* Seed utilities (M5): deterministic text->seed hashing and seed-field
 * parsing. Never uses rand(). Pure CPU, headless-testable.
 */

#include <stdbool.h>
#include <stddef.h>

/* FNV-1a 64-bit hash of a byte string (deterministic across platforms).
 *
 * Args:
 *   text: bytes to hash (NULL treated as empty).
 *   len: byte count.
 *
 * Returns: 64-bit digest.
 */
unsigned long long seed_hash_text(const char *text, size_t len);

/* Parse a create-world seed field into a world seed.
 * Rules: blank/whitespace-only sets *is_blank (caller generates one);
 * otherwise try base-0 integer parse (decimal, 0x hex, 0 octal); on
 * failure (or overflow of the integer range) fall back to FNV-1a text
 * hashing. The same input always yields the same seed.
 *
 * Args:
 *   field: user input (NULL treated as blank).
 *   is_blank: receives true when the field was blank (must not be NULL).
 *
 * Returns: parsed/hashed seed (as long; 64-bit text hashes truncate on
 * 32-bit-long platforms — documented, deterministic per platform).
 */
long seed_parse(const char *field, bool *is_blank);
