#pragma once

/* Differential comparison of TerraCraft's Java-compatible world-generation
 * primitives against fixtures produced by the real 1.21.11 server jar
 * (tools/reference/WorldgenRefDump.java).
 *
 * Every comparison is exact (bit pattern equality); there is no tolerance.
 * Mismatches are written as machine-readable CSV rows:
 *   suite,key,expected,actual
 */

#include <stdint.h>
#include <stdio.h>

typedef struct WorldgenParityStats {
    uint64_t checked;    /* Values compared. */
    uint64_t mismatched; /* Values that differed. */
    uint64_t missing;    /* Fixture files that could not be read. */
} WorldgenParityStats;

/* Run every available stage against <fixture_dir>/*.csv. report may be NULL;
 * otherwise it receives a header and one row per mismatch. Returns the number
 * of mismatches plus missing files (0 = zero unexplained mismatches). */
uint64_t worldgen_parity_run(const char *fixture_dir, FILE *report, WorldgenParityStats *stats);
