#!/usr/bin/env bash
# Regenerate tests/fixtures/hand_golden.h from an installed game jar.
# Developer-only. Usage: tools/reference/gen_hand_golden.sh [path-to-game.jar]
# Needs JDK 21+, python, and the launcher's libraries directory.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MC="${MC_DIR:-$APPDATA/.minecraft}"
JAR="${1:-$MC/versions/26.3/26.3.jar}"
WORK="$(mktemp -d)"
mkdir -p "$WORK/classes" "$WORK/out"
# Extract the math classes the generator touches, then downgrade the class
# version (Java 25 -> 21) so JDK 21 can load them. Only plain math is used.
(cd "$WORK/classes" && unzip -qo "$JAR" 'net/minecraft/*' 'com/mojang/*')
python -I - "$WORK/classes" <<'PY'
import os, sys
for root, _, files in os.walk(sys.argv[1]):
    for f in files:
        if f.endswith('.class'):
            p = os.path.join(root, f)
            b = bytearray(open(p, 'rb').read())
            if b[6] == 0 and b[7] > 65:
                b[7] = 65
                open(p, 'wb').write(b)
PY
LIBS="$(find "$MC/libraries" -name '*.jar' | grep -v 'natives\|lwjgl\|joml-1.10.8\|jopt\|netty\|jna\|oshi\|icu4j' | tr '\n' ';')"
CP="$WORK/classes;$LIBS"
javac -cp "$CP" -d "$WORK/out" "$ROOT/tools/reference/HandGolden.java"
java -cp "$WORK/out;$CP" HandGolden > "$ROOT/tests/fixtures/hand_golden.h"
echo "wrote tests/fixtures/hand_golden.h"
