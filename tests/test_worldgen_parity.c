#include "test_main.h"

#include "world/jnoise.h"
#include "world/jrandom.h"
#include "world/worldgen_parity.h"

#include <stdio.h>

#ifndef TC_SOURCE_DIR
#define TC_SOURCE_DIR "."
#endif

/* Full differential run against the committed 1.21.11 fixtures: zero
 * mismatches allowed, bit-exact. On failure the first mismatches are
 * printed and the full CSV report is written next to the test binary. */
int test_worldgen_parity_primitives(void)
{
    const char *datapack_probe_path =
        TC_SOURCE_DIR "/reference/1.21.11/run/out/data/minecraft/worldgen/noise_settings/overworld.json";
    int failures = 0;
    WorldgenParityStats st;
    /* The router stage needs the locally generated vanilla data pack; without it
     * the stage is skipped (and counted as skipped) so CI stays green. */
    const char *datapack = TC_SOURCE_DIR "/reference/1.21.11/run/out";
    FILE *probe = fopen(datapack_probe_path, "r");
    if (probe != NULL) {
        fclose(probe);
    } else {
        datapack = NULL;
    }
    FILE *report = fopen("worldgen_parity_mismatches.csv", "w");
    uint64_t bad = worldgen_parity_run(TC_SOURCE_DIR "/tests/fixtures/worldgen", datapack, report, &st);
    if (report != NULL) {
        fclose(report);
    }
    printf("  worldgen parity: checked=%llu mismatched=%llu missing=%llu skipped=%llu\n",
           (unsigned long long)st.checked, (unsigned long long)st.mismatched, (unsigned long long)st.missing,
           (unsigned long long)st.skipped);
    TEST_ASSERT(st.missing == 0);
    TEST_ASSERT(st.checked > 4000);
    TEST_ASSERT(bad == 0);
    return failures;
}

/* Spot checks that pin documented vanilla constants independently of the
 * fixtures (Mth.getSeed, String.hashCode, all-zero Xoroshiro fix-up). */
int test_worldgen_parity_known_values(void)
{
    int failures = 0;
    TEST_ASSERT(jr_java_string_hash("") == 0);
    TEST_ASSERT(jr_java_string_hash("a") == 97);
    TEST_ASSERT(jr_java_string_hash("hello") == 99162322); /* Java: "hello".hashCode() */
    TEST_ASSERT(jr_mth_get_seed(0, 0, 0) == 0);

    JrRandom a, b;
    jr_xoroshiro_init128(&a, 0, 0);
    jr_xoroshiro_init128(&b, (int64_t)0x9E3779B97F4A7C15ULL, (int64_t)0x6A09E667F3BCC909ULL);
    TEST_ASSERT(jr_next_long(&a) == jr_next_long(&b));

    /* java.util.Random(0).nextInt() is the well-known -1155484576. */
    JrRandom l;
    jr_legacy_init(&l, 0);
    TEST_ASSERT(jr_next_int(&l) == -1155484576);
    return failures;
}
