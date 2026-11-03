#!/usr/bin/env bash
# Regenerate tests/fixtures/worldgen/*.csv from the real 1.21.11 server jar.
# Needs reference/1.21.11 (tools/reference/fetch_reference.sh) and JDK 21.
# Developer-only; the shipped game and the default test run never use the jar.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
R="$ROOT/reference/1.21.11"
OUT="${1:-$ROOT/tests/fixtures/worldgen}"
WORK="$(mktemp -d)"
if command -v cygpath >/dev/null 2>&1; then WORK="$(cygpath -m "$WORK")"; R="$(cygpath -m "$R")"; fi

python -I - "$R" <<'PY'
import sys, zipfile, os
r = sys.argv[1]
z = zipfile.ZipFile(os.path.join(r, "server.jar"))
for n in z.namelist():
    if n.startswith(("META-INF/libraries/", "META-INF/versions/")) and not n.endswith("/"):
        if not os.path.exists(os.path.join(r, "bundle", n)):
            z.extract(n, os.path.join(r, "bundle"))
PY

SEP=";"; case "$(uname -s)" in Linux*|Darwin*) SEP=":";; esac
LIBS="$(find "$R/bundle/META-INF/libraries" -name '*.jar' | tr '\n' "$SEP")"
CP="$WORK$SEP$R/bundle/META-INF/versions/1.21.11/server-1.21.11.jar$SEP$LIBS"
javac -d "$WORK" "$ROOT/tools/reference/WorldgenRefDump.java" "$ROOT/tools/reference/WorldgenRouterDump.java" "$ROOT/tools/reference/WorldgenBiomeDump.java" "$ROOT/tools/reference/WorldgenChunkDump.java" "$ROOT/tools/reference/WorldgenCarverDump.java" "$ROOT/tools/reference/WorldgenSurfaceDump.java" "$ROOT/tools/reference/WorldgenFeatureDump.java" "$ROOT/tools/reference/WorldgenStructureDump.java"
mkdir -p "$OUT"
# ONLY=carver / surface / feature regenerate just those fixtures (slow: replay NoiseChunk fills).
if [ "${ONLY:-all}" = "all" ]; then
java -cp "$CP" tcref.WorldgenRefDump "$R/server_mappings.txt" "$OUT"
(cd "$WORK" && java -cp "$CP" tcref.WorldgenRouterDump "$R/server_mappings.txt" "$OUT")
(cd "$WORK" && java -cp "$CP" tcref.WorldgenBiomeDump "$R/server_mappings.txt" "$OUT")
(cd "$WORK" && java -cp "$CP" tcref.WorldgenChunkDump "$R/server_mappings.txt" "$OUT" ${CHUNK_FULL_DIR:-})
fi
if [ "${ONLY:-all}" = "all" ] || [ "${ONLY:-all}" = "carver" ]; then
(cd "$WORK" && java -cp "$CP" tcref.WorldgenCarverDump "$R/server_mappings.txt" "$OUT" "$R/run/out")
fi
if [ "${ONLY:-all}" = "all" ] || [ "${ONLY:-all}" = "surface" ]; then
(cd "$WORK" && java -cp "$CP" tcref.WorldgenSurfaceDump "$R/server_mappings.txt" "$OUT" "$R/run/out")
fi
if [ "${ONLY:-all}" = "all" ] || [ "${ONLY:-all}" = "structure" ]; then
(cd "$WORK" && java -cp "$CP" tcref.WorldgenStructureDump "$R/server_mappings.txt" "$OUT")
fi
if [ "${ONLY:-all}" = "all" ] || [ "${ONLY:-all}" = "feature" ]; then
(cd "$WORK" && java -cp "$CP" tcref.WorldgenFeatureDump "$R/server_mappings.txt" "$OUT" "$R/run/out")
fi
