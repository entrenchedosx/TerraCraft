# Gauntlet Loop

## What this means here

The [Gauntlet Loop](https://thrixel.com/learn/gauntlet-loop) is an iterative
quality method: define a bar, produce a result, have an independent critic
compare it with that bar, fix the largest gap, and repeat. Implementations
can bound the review cycles and stop when the bar is met or when a human
decision/playtest is needed. This is a general method, not a formal software
test standard or a TerraCraft-specific recipe. A related open implementation
uses explicit worker/reviewer cycles and a bounded iteration limit:
[gauntlet-loop on GitHub](https://github.com/kamtS/gauntlet-loop).

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

### Round 2 — Texture pack reproduction and fixed tick foundation

- A second independent render-path review found two concrete resource-pack
  faults: path construction aliased `path_join()` input/output buffers, and
  changing a pack did not rebuild the live atlas. No per-item UV or atlas
  index error was found.
- Fixed pack directory construction and discovery. Settings now reload the
  atlas when the selected pack changes. A headless test creates a local pack,
  confirms it is discovered, and verifies its item tile reaches the atlas.
- Added a pure 60 TPS scheduler with a five-tick catch-up cap, fractional
  remainder preservation, dropped-time accounting, and one-second tick-count
  checks at 30/60/144/240 FPS partitions.
- Routed gameplay, inventory, and crafting world updates through fixed
  1/60-second ticks. Pause and death freeze the clock. Mouse/key press edges
  are held across down/up events until consumed; look and rendering remain
  frame-rate driven. Streaming and drawing happen once per rendered frame.
- The independent critic found a quick jump-tap gap and a frozen-to-live
  elapsed-time leak. Both are fixed: jump edges are consumed on a world tick,
  and the first frame after a frozen state contributes no wall time. The
  critic found no additional code blocker and caught two stale documentation
  claims; those are corrected in this round.
- After those code fixes, the Debug and Release builds succeeded and both
  CTest suites passed. The Debug runner reports 154 tests, 0 failures.
- The user's exact item/pack/surface is still unknown, so the texture entry
  remains open for a visual confirmation even though the custom-pack defect
  is now fixed.

### Round 2 verification

- The source and test changes pass the Visual Studio Debug and Release builds.
- `ctest --test-dir build -C Debug --output-on-failure` and the Release
  equivalent both pass. The direct Debug runner reports 154 tests, 0 failed.
- The independent critic verified the fixed jump edge, transition-time
  discard, texture-pack path/reload flow, and current diff; no further
  code-level blocker was found.
- The local `docs/AI_MEMORY.md` and `mcassets/` paths are ignored. The current
  published root tree contains neither, and the staged follow-up file list
  was checked to contain neither path.

### Remaining visual round

Capture the affected item in the hotbar/inventory/catalogue and as a dropped
world sprite, with the selected resource pack. Compare those observations
with the shared `ItemInfo.tile` path. Reproduce and fix the actual rendering
fault, then verify the same case in a running game. If no mismatch appears,
record the exact version, asset layer, and tested items so the issue can be
triaged against the user's report.

### Round 3 — Cow appearance, melee targeting, and first-person swing

- The supplied screenshot showed a cow with stretched, mismatched hide patches.
  Code review traced this to reused per-face skin rectangles and a rendered
  model extending beyond the old square hitbox.
- Replaced the cow's face UVs with the 64×64 skin box-net regions and tuned
  the torso, head, and legs to the authored cuboid proportions. Mob parts
  rotate around the creature root and render positions now track simulation
  movement. Melee bounds follow root yaw and current limb/head pitch with a
  small margin; rays starting inside a mob register immediately.
- Held left mouse now retries attacks after cooldown and a mob under the
  crosshair suppresses block mining behind it. Added an original procedural
  first-person sleeve/hand, a held-item sprite, and a swing envelope separate
  from damage cooldown. Held block cubes use the same per-face textures as
  world blocks (grass, wood, and workbench tops/bottoms included).
- Added regression coverage for cow geometry/UVs, muzzle reach at three yaws,
  inside-box rays, skeleton arm reach, displayed-vs-simulation positions,
  swing timing, and opaque procedural hand textures.
- Visual Studio Debug and Release builds succeeded. Debug and Release CTest
  each passed; the direct runner reports 155 tests, 0 failed.
- Independent source review verified the swing buffer recovery, held block
  geometry, GL state restoration, mob targeting transform, and attack cadence;
  review also caught and fixed the held-block face textures. A running-game
  check of cow appearance, yaw-dependent targeting, FOV/pitch placement, and
  swing visibility remains pending. This round is not visually accepted until
  those checks are done.

### Round 4 — Supported, non-stacking vegetation placement

- The user demonstrated that right-clicking flowers and grass plants could
  create another plant in the next cell above. The ray correctly selected the
  decor, but placement always used the adjacent face cell.
- Clicking cross-sprite decor now replaces the clicked cell instead of using
  it as a shelf. New grass plants and flowers require grass, dirt, or snow
  directly beneath them; snow is included because the snow biome already
  generates grass plants there.
- Regression coverage verifies same-cell replacement, no plant above the
  replaced cell, invalid stone support rejection, and valid grass/dirt/snow
  support.
- The user closed the running game; full Release and Debug builds now succeed,
  including the game executable, and both CTest suites pass.
- Manual in-game placement confirmation remains open.

### Round 5 — First-person arm and held-item correction

- Independent arm review found the held item was positioned above the fist,
  the sleeve was unusually narrow, and the viewmodel layout changed screen
  position with the player's FOV setting.
- Widened the arm cuboids and placed the held item at the hand. Camera-local
  X/Y coordinates use the tangent ratio between live FOV and authored
  70-degree FOV while depth stays fixed, preserving perspective placement.
  Pose offsets translate the arm vertices with their swing pivot. World
  projection and FOV are unchanged.
- Added a projection regression check at 40, 70, and 120-degree FOV, including
  invalid-reference and null-camera fallback cases.
- Debug and Release builds both succeed. CTest passes in both configurations;
  the Release runner reports 159 tests, 0 failures. `git diff --check` passes.
- An independent reviewer caught that an initial uniform xyz scale canceled
  under perspective. The code now scales only viewmodel X/Y and has a direct
  projection regression check. Human visual verification in first-person
  mode, with empty and occupied hands at low and high FOV, remains open.

### Round 6 — First-person arm skin, framing, and stackability

- The latest screenshot still showed a pole-like flat teal/brown arm. Review
  traced it to repeated 16x16 fallback atlas swatches and a model placed only
  0.58 blocks from the camera; the loaded 64x64 player skin was not used by
  the first-person pass.
- The first-person arm now uses the right-arm face UVs from the loaded player
  skin, drawn in a separate skin-texture pass. It rests farther away in the
  lower-right corner with a modest inward cant; the held item has a matching
  grip/depth and retains its atlas texture. Depth is cleared for the overlay,
  then enabled so the arm's own cuboid surfaces occlude correctly. The punch
  now swings toward the camera, and the atlas arm remains as a no-assets
  fallback.
- The catalog audit found 33 registered items: six tools and the bow already
  have `max_stack = 1`; all other 26 stack to 64. No registered item needs a
  source limit correction. New exhaustive regression coverage checks limits,
  merges, inventory insertion, split behavior, and sanitization for every
  unstackable item. Swords, armor, buckets, and other absent catalog items
  remain outside this pass.
- Mojang's Java Edition component notes and 24w09a stack-format notes are
  recorded in `docs/JAVA_FIDELITY.md` as the stack-limit references.
- Debug and Release game builds and CTest pass; the direct Release runner
  reports 160 tests, 0 failures. Independent arm and stackability reviews
  found no remaining code-level blocker. A new live screenshot is still
  needed to accept the arm's appearance.

### Round 7 — Arm-facing UV, survival icons, terrain profiles, and water

- Corrected the viewmodel's camera-facing arm UV region and increased its
  sleeve/hand proportions; added a direct mapping regression check. A live
  screenshot is still needed to confirm the arm's appearance in motion.
- Replaced survival health and hunger bars with ten pixel-art hearts and ten
  food icons. Full, half, and empty states represent 20 total points; source
  values remain continuous.
- Added deterministic terrain profile 2 for new worlds: warped continental
  noise, broad hills, ridge-shaped highlands, and meandering carved valleys.
  It remains a 2D height field rather than a full 3D density router. A saved
  `terrain_version` leaves unversioned worlds on profile 1 and prevents
  generation seams when untouched chunks are visited.
- Added persistent source, flow-level, and falling-water IDs; downward-first
  flow, seven-step horizontal decay, two-source refill, orphan retraction,
  and loaded-chunk-only updates. The deduplicated queue is capped at 32,768
  cells and triggers a bounded incremental scan at capacity. Chunk loading
  wakes fluid on shared edges; transparent meshes use partial water heights
  and expose only the visible step between different levels.
  Chunk format v2 preserves flow states and v1 chunks retain their legacy
  block interpretation. Player water movement has reduced speed, drag, and
  jump/sneak buoyancy, without oxygen or a swimming pose.
- Added regression coverage for finite flow and retraction, cross-chunk flow
  wakeup, partial-height meshes, old/new chunk-save compatibility, and water
  movement.
- Visual Studio Debug and Release game builds both succeeded. Debug and
  Release CTest pass; the direct Release runner reports 167 tests, 0 failures.
  A hands-on visual check remains outstanding; automated coverage does not
  prove Minecraft visual or mechanical parity.

### Round 8 — Mob silhouette, persistence, arm pose, and terrain profile 3

- Reposed the first-person arm from the inverted/outward placement, with a
  pure-transform regression for its rest and swing directions.
- Rebuilt the cow as a horizontal quadruped with a protruding muzzle, horns,
  ears, udder, and articulated legs; mob ray bounds follow rendered parts.
- Fixed mob frustum bounds to include articulated model geometry beyond the
  collision AABB. A regression confirms the cow culling envelope exceeds its
  collision footprint, and death-pose bounds include the feet-pivoted fall,
  avoiding early disappearance at view edges.
- Added entity-save format v3 for live mob type, position, yaw, and health.
  Autosave, pause, and normal close write the entity file; opening a world
  restores mobs. Version 1 and 2 readers remain available. Transient AI and
  velocity state restart safely, and a forced process termination can still
  lose changes since the last successful save.
- New worlds use deterministic terrain profile 3: warped continental
  landforms, connected ridges, sculpted foothills, and lowland river channels.
  Existing v1/v2 worlds retain their generation profile. This remains a 2D
  height field without density-based caves, overhangs, or aquifers.
- The model-culling review finding was fixed before the final build. Debug and
  Release builds and CTest pass; the direct runner reports 171 tests,
  0 failures. Live game visual verification remains outstanding, so arm/cow
  appearance and terrain quality are not marked visually verified.

### Round 9 — F5 head pivot, profiles, and LAN play

- Corrected the third-person head's pivot to the neck hinge at 1.35 blocks;
  a focused model regression checks the pivot value. The player screenshot
  that motivated this pass showed the camera looking down from overhead, but
  a new running-game capture is still needed to verify the rendered pose.
- Added a required first-run username screen. Names are validated and saved
  locally; an empty field generates two random words and a four-digit suffix.
- Added direct TCP LAN hosting from the pause screen, IPv4 joining from the
  title menu, chat on `T`, and replication for player snapshots, chat, and
  block changes made while peers are connected. The host remains the world
  authority; there is no dedicated or internet server.
- Joining clients generate matching terrain from the host seed/version and
  keep a local save copy. This round does not send pre-existing block edits,
  mobs, dropped items, inventories, or other persistent entity state to a new
  client. The LAN scope and setup steps are recorded in
  `docs/LAN_MULTIPLAYER.md`.
- An independent code review found that an empty join address was reset to
  loopback each frame, unregistered peers could submit block edits, and the
  listener accepted routed public addresses. Those code issues are fixed:
  the address field stays blank and validates IPv4, peers must complete one
  username handshake before gameplay messages, and host-side peer addresses
  must be local IPv4 ranges. Failed client-world setup also removes its
  partial save.
- The review also confirmed that this first LAN slice does not transfer
  pre-existing world edits or persistent entity state. That larger snapshot
  protocol remains explicitly tracked in the backlog and setup guide; clients
  currently keep a generated local save copy, so repeated sessions may leave
  additional `LAN - <host>` saves.
- Host block reach uses client-reported positions, so it is a gameplay check
  for honest clients, not an anti-cheat boundary. The setup guide limits LAN
  play to trusted peers and records this constraint.
- Debug and Release builds and CTest passed on Windows; the direct runner
  reports 176 tests, 0 failures. The focused LAN transport tests cover
  loopback connection, framing, payload limits, and disconnection. A
  two-computer playtest and live F5 head check remain needed for visual and
  end-to-end acceptance.
- `AI_MEMORY` is ignored and excluded from the commit and push.

## Research

- [Gauntlet Loop](https://thrixel.com/learn/gauntlet-loop) — description of
  the iterative quality method.
- [gauntlet-loop on GitHub](https://github.com/kamtS/gauntlet-loop) — an
  implementation with bounded worker/reviewer cycles.
- [Building effective agents](https://www.anthropic.com/research/building-effective-agents)
  — Anthropic's evaluator-optimizer pattern, a related research framing for
  iterating on a draft with separate evaluation feedback.
