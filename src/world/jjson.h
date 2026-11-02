#pragma once

/* Minimal JSON reader for loading the vanilla data pack (density functions,
 * noise parameters, noise settings) from a local, user-supplied directory.
 * Numbers keep their source text so callers can parse them as float or
 * double exactly like Gson/Java would. No data pack content is compiled into
 * or shipped with TerraCraft. */

#include <stdbool.h>
#include <stddef.h>

typedef enum JJType { JJ_NULL, JJ_BOOL, JJ_NUM, JJ_STR, JJ_ARR, JJ_OBJ } JJType;

typedef struct JJson {
    JJType type;
    bool boolean;          /* JJ_BOOL. */
    char *text;            /* JJ_NUM source text or JJ_STR contents. */
    int count;             /* JJ_ARR / JJ_OBJ element count. */
    struct JJson **items;  /* JJ_ARR elements / JJ_OBJ values. */
    char **keys;           /* JJ_OBJ keys. */
} JJson;

/* Parse a NUL-terminated JSON document. Returns NULL on a syntax error. */
JJson *jj_parse(const char *text);
/* Read and parse a file. Returns NULL if unreadable or malformed. */
JJson *jj_parse_file(const char *path);
void jj_free(JJson *j);

/* Object member lookup (NULL if absent or j is not an object). */
const JJson *jj_get(const JJson *j, const char *key);
double jj_double(const JJson *j);  /* strtod of a JJ_NUM (0 otherwise). */
float jj_float(const JJson *j);    /* strtof of a JJ_NUM (0 otherwise). */
