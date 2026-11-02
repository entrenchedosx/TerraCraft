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
javac -d "$WORK" "$ROOT/tools/reference/WorldgenRefDump.java" "$ROOT/tools/reference/WorldgenRouterDump.java" "$ROOT/tools/reference/WorldgenBiomeDump.java" "$ROOT/tools/reference/WorldgenChunkDump.java"
mkdir -p "$OUT"
java -cp "$CP" tcref.WorldgenRefDump "$R/server_mappings.txt" "$OUT"
(cd "$WORK" && java -cp "$CP" tcref.WorldgenRouterDump "$R/server_mappings.txt" "$OUT")
(cd "$WORK" && java -cp "$CP" tcref.WorldgenBiomeDump "$R/server_mappings.txt" "$OUT")
(cd "$WORK" && java -cp "$CP" tcref.WorldgenChunkDump "$R/server_mappings.txt" "$OUT" ${CHUNK_FULL_DIR:-})
