/* worldgen_diff: compare TerraCraft's Java-compatible world-generation
 * primitives with fixtures produced by the vanilla 1.21.11 server jar.
 *
 *   worldgen_diff <fixture_dir> [mismatch_report.csv]
 *
 * Exit status 0 only when every compared value is bit-identical and every
 * fixture file was found. Mismatches are written as CSV
 * (suite,key,expected_hex,actual_hex) to the report path (default: stdout
 * is not used; no report file is written when the path is omitted).
 */

#include "world/worldgen_parity.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <fixture_dir> [mismatch_report.csv]\n", argv[0]);
        return 2;
    }
    FILE *report = NULL;
    if (argc >= 3) {
        report = fopen(argv[2], "w");
        if (report == NULL) {
            fprintf(stderr, "cannot open report file %s\n", argv[2]);
            return 2;
        }
    }
    WorldgenParityStats st;
    uint64_t bad = worldgen_parity_run(argv[1], report, &st);
    if (report != NULL) {
        fclose(report);
    }
    printf("checked=%llu mismatched=%llu missing_files=%llu\n", (unsigned long long)st.checked,
           (unsigned long long)st.mismatched, (unsigned long long)st.missing);
    return bad == 0 ? 0 : 1;
}
