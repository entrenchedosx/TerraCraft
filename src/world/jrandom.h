#pragma once

/* Java Edition 1.21.11 random sources, reproduced bit for bit.
 *
 * Covers net.minecraft.world.level.levelgen.LegacyRandomSource (the
 * java.util.Random LCG), XoroshiroRandomSource (Xoroshiro128++ with the
 * splitmix-style seed upgrade) and their PositionalRandomFactory forks.
 * All 64-bit arithmetic uses explicit unsigned wraparound so results do not
 * depend on C signed-overflow behaviour. Differentially tested against the
 * 1.21.11 server jar (docs/WORLDGEN_PARITY.md, tests/test_worldgen_parity.c).
 *
 * Strings given to fromHashOf/hashCode must be ASCII (all vanilla noise,
 * octave and feature names are).
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum JrKind {
    JR_LEGACY = 0,    /* java.util.Random 48-bit LCG. */
    JR_XOROSHIRO = 1  /* Xoroshiro128++. */
} JrKind;

typedef struct JrRandom {
    JrKind kind;
    uint64_t a;                /* Legacy: 48-bit seed. Xoroshiro: seedLo. */
    uint64_t b;                /* Xoroshiro: seedHi. */
    double next_gaussian;      /* MarsagliaPolarGaussian cache. */
    bool have_gaussian;
} JrRandom;

typedef struct JrPositional {
    JrKind kind;
    uint64_t a;                /* Legacy: seed. Xoroshiro: seedLo. */
    uint64_t b;                /* Xoroshiro: seedHi. */
} JrPositional;

/* new LegacyRandomSource(seed). */
void jr_legacy_init(JrRandom *r, int64_t seed);
/* new XoroshiroRandomSource(seed): upgradeSeedTo128bit(seed). */
void jr_xoroshiro_init(JrRandom *r, int64_t seed);
/* new XoroshiroRandomSource(seedLo, seedHi) (all-zero state is fixed up). */
void jr_xoroshiro_init128(JrRandom *r, int64_t seed_lo, int64_t seed_hi);

int32_t jr_next_int(JrRandom *r);
int32_t jr_next_int_bound(JrRandom *r, int32_t bound); /* bound > 0 */
int64_t jr_next_long(JrRandom *r);
bool jr_next_boolean(JrRandom *r);
float jr_next_float(JrRandom *r);
double jr_next_double(JrRandom *r);
double jr_next_gaussian(JrRandom *r);

/* RandomSource.forkPositional(). Consumes one (legacy) or two (xoroshiro)
 * longs from r. */
JrPositional jr_fork_positional(JrRandom *r);
/* PositionalRandomFactory.at(x, y, z). */
JrRandom jr_pos_at(const JrPositional *f, int32_t x, int32_t y, int32_t z);
/* PositionalRandomFactory.fromHashOf(name). */
JrRandom jr_pos_from_hash_of(const JrPositional *f, const char *name);
/* PositionalRandomFactory.fromSeed(seed). */
JrRandom jr_pos_from_seed(const JrPositional *f, int64_t seed);

/* Mth.getSeed(x, y, z). */
int64_t jr_mth_get_seed(int32_t x, int32_t y, int32_t z);
/* String.hashCode() for ASCII strings. */
int32_t jr_java_string_hash(const char *s);
