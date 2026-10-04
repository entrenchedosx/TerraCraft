# Gauntlet Loop

## What this means here

The [Gauntlet Loop](https://somethingbig.ai/gauntlet-loop) is a product
iteration method: set a clear quality bar, make a concrete result, have an
independent critic inspect the result, close the largest gap, and repeat.
The loop stops when the stated bar is met or a remaining dependency needs
human input. The source describes a general method, not a software test
standard or a TerraCraft-specific recipe.

TerraCraft applies that idea to a fidelity pass. Each round uses evidence a
reviewer can check: source locations, a working build, automated test
results, documentation, repository contents, and (where available) a live
playtest. A code-reading claim is not treated as visual verification.

## Exit bar for this pass

- The current Debug and Release builds succeed and their registered test
  suites pass.
- Every fix in the round has a focused regression check where the behavior
  can be tested without a running game.
- The independent review is answered; all high-impact review findings are
  fixed or recorded with a specific reason and evidence.
- Docs describe observed behavior and the remaining work without claiming
  unmeasured Java parity.
- The published repository contains source and documentation, not
  `AI_MEMORY` files, owner-local `mcassets/`, saves, settings, or resource
  packs; its history starts at a clean snapshot.
- The reported texture problem and ordinary Survival/Creative flow have a
  human-visible playtest record before the fidelity pass is called done.

## Rounds in this workspace

### Round 0 — Baseline and evidence inventory

- Existing checkout started clean at M9, with 146 registered test
  functions.
- Existing Visual Studio Debug and Release builds succeeded, as did their
  CTest suites.
- A fresh CMake configure in a separate Debug directory could not locate
  SDL2 package configuration. The already configured Visual Studio build
  supplies the local SDL2 dependency; the failure is recorded rather than
  hidden.
- Source inspection found no 20 TPS authoritative loop, several material
  movement and survival gaps, and a stale resource-pack tile list.
- A local asset preview showed sampled item artwork present. It did not
  reproduce the user's in-game texture report.

### Round 1 — Static critic and focused corrections

- A separate review checked item IDs, item tile indices, atlas file stems,
  UV calculations, BMP row conversion, and icon/drop render paths. It found
  no mapping mismatch; that only rules out one class of cause.
- Added `test_item_texture_mapping` for all 32 registered items and their
  procedural fallback pixels.
- Corrected entity reach to 3 blocks. Made grass plants, flowers, and torches
  replaceable; placement reports the replaced block so Survival can spawn
  its configured drop. Regression coverage checks these cases.
- Rewrote the resource-pack and project docs to match current source and
  distinguish tested behavior from unfinished fidelity work.
- Debug and Release builds succeeded; their CTest suites each passed. The
  Release runner reports 147 tests, 0 failures.
- A fresh critic found that ignored paths were still tracked in the legacy
  tree and that the first decor replacement patch discarded Survival drops.
  Decor drops now spawn above the newly placed block to stay visible.
- The second critic confirmed the Survival spawn route by code inspection.
  Headless coverage verifies replaced IDs and configured drops, but does not
  drive the app input path or assert an entity's live position; the manual
  playtest remains the visual check.
- Publication hygiene is verified: commit `codex/terracraft-release` is a
  parentless root with 141 paths; its tree contains no `AI_MEMORY` or
  `mcassets/` paths. The local asset directory remains in the workspace.
- The private GitHub repository is published at
  [entrenchedosx/TerraCraft](https://github.com/entrenchedosx/TerraCraft).
  Its `main` branch points at the same parentless root commit; GitHub reports
  141 file entries and no `AI_MEMORY` or `mcassets/` paths.
- Outcome: source-level checks are stronger; the texture bug is still open
  because the affected items and screen location are unknown. The hands-on
  fidelity playtest also remains outstanding.

### Remaining visual round

Capture the affected item in the hotbar/inventory/catalogue and as a dropped
world sprite, with the selected resource pack. Compare those observations
with the shared `ItemInfo.tile` path. Reproduce and fix the actual rendering
fault, then verify the same case in a running game. If no mismatch appears,
record the exact version, asset layer, and tested items so the issue can be
triaged against the user's report.

## Research

- [Gauntlet Loop](https://somethingbig.ai/gauntlet-loop) — original
  description of the iterative quality method.
- [Building effective agents](https://www.anthropic.com/research/building-effective-agents)
  — Anthropic's evaluator-optimizer pattern, a related research framing for
  iterating on a draft with separate evaluation feedback.
