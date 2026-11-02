#!/usr/bin/env bash
# Download the Mojang 1.21.11 reference artefacts into the git-ignored
# reference/1.21.11/ folder and regenerate the data-generator dumps.
# Developer-only; nothing downloaded here is committed or redistributed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
D="$ROOT/reference/1.21.11"
mkdir -p "$D/run"
get() { # url sha1 file
  [ -f "$D/$3" ] || curl -sSL -o "$D/$3" "$1"
  echo "$2 *$D/$3" | sha1sum -c -
}
B=https://piston-data.mojang.com/v1/objects
get $B/ba2df812c2d12e0219c489c4cd9a5e1f0760f5bd/client.jar ba2df812c2d12e0219c489c4cd9a5e1f0760f5bd client.jar
get $B/64bb6d763bed0a9f1d632ec347938594144943ed/server.jar 64bb6d763bed0a9f1d632ec347938594144943ed server.jar
get $B/031a68bebf55d824f66d6573d8c752f0e1bf232a/client.txt 031a68bebf55d824f66d6573d8c752f0e1bf232a client_mappings.txt
get $B/5621e9253f05fd57872bbe7f8ddf5f9a7d525955/server.txt 5621e9253f05fd57872bbe7f8ddf5f9a7d525955 server_mappings.txt
# Registry reports + the full data pack (worldgen density functions, noise
# settings, biome parameters, structures...) from the vanilla data generator.
(cd "$D/run" && java -DbundlerMainClass=net.minecraft.data.Main -jar ../server.jar --reports --server --output out)
