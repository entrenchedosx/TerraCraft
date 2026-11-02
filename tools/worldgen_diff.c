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
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <fixture_dir> [mismatch_report.csv] [datapack_root]\n", argv[0]);
        return 2;
    }
    FILE *report = NULL;
    if (argc >= 3 && strcmp(argv[2], "-") != 0) {
        report = fopen(argv[2], "w");
        if (report == NULL) {
            fprintf(stderr, "cannot open report file %s\n", argv[2]);
            return 2;
        }
    }
    WorldgenParityStats st;
    const char *datapack = argc >= 4 ? argv[3] : NULL;
    uint64_t bad = worldgen_parity_run(argv[1], datapack, report, &st);
    if (report != NULL) {
        fclose(report);
    }
    printf("checked=%llu mismatched=%llu missing_files=%llu skipped=%llu\n", (unsigned long long)st.checked,
           (unsigned long long)st.mismatched, (unsigned long long)st.missing, (unsigned long long)st.skipped);
    return bad == 0 ? 0 : 1;
}
