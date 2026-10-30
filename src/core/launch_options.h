#pragma once

/* Parse launch options without creating a window. Outputs are required and
 * must be distinct. The default is 1280x720. Recognized options:
 *   --window-size WIDTHxHEIGHT  (320..7680 by 240..4320)
 *   --help
 * Decimal dimensions may have leading zeroes, but not signs/whitespace.
 * Duplicate options, unknown flags, extra arguments, missing values and
 * malformed/oversized dimensions are rejected. All arguments are validated
 * even when --help is present. argc=0 is an empty option list.
 * Returns 0 to launch, 1 for help, -1 for invalid input. On invalid arguments,
 * valid output pointers receive defaults rather than partially parsed values.
 */
int launch_options_parse(int argc, char **argv, int *width, int *height);
