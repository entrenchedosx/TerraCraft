# Lighting and terrain continuation — 2026-10-31

**Status: lighting readability accepted; complete pass acceptance remains OPEN.**
The user inspected the updated night build and reported “PERFECT LIGHTING”.
Profile 5 is implemented and regression-tested, but the required multi-seed
walking/flying comparison has not been completed. No advanced gameplay
systems or release were added.

## What was wrong

At midnight the sun was below the horizon, so the direct-light term vanished.
Upward-facing ambient light came from the almost black sky multiplied by
0.4 (about 1–4% texture brightness). Roof shade and geometry AO were then
multiplied into the same channel. The nominal time-system intensity floor
could not help a direct-sun term already multiplied by zero. There was no
independent moonlight or propagated torch light. A twelve-block roof probe
also misclassified deep roofs and could not represent light around corners.

Profile 4 was coherent noise, not independent random columns. Its main shape
problem was `1-abs(weirdness)` saturating the peak response across common
noise values, combined with unconditional mountain uplift. Rivers sampled
an unrelated field and pulled terrain toward almost constant Y60. Integer
warps, uniform biome tree probabilities, an abrupt tree-height cutoff, square
tunnel intersections, and a hard noodle gate weakened geographic coherence.

Runtime inspection also reproduced a separate save-browser defect: insertion
sorting shifted an array over the entry being inserted, duplicating world
names and directory references. The entry now lives outside the shifted array.

## Lighting changes

- Separate AO, skylight and block-light vertex channels (11 floats instead of 9).
- Full-height direct sky scans plus six-neighbor propagation through a
  fifteen-cell halo. Glass transmits sky; leaves and water attenuate it.
- Independent torch/lava emission and propagation, blocked by opaque walls.
  Removing a source recomputes the field. Opacity/emission edits invalidate
  the surrounding 3×3 chunk neighborhood, including interior edits.
- Neutral moonlit outdoor ambient, a soft directional moon term, smooth
  dawn/dusk interpolation, and weaker night AO. Sealed caves keep only a
  small ambient floor; local light remains useful. Sun/torch contributions
  are max-combined to avoid adding excessive daytime brightness.
- Fog uses the camera's local light visibility; underwater fog retains its
  own short sightline and night-adjusted colour. Entity and viewmodel
  vertices sample the terrain lighting cache. Skin UVs cannot accidentally
  trigger atlas torch/lava emission rules.
- Derived packed lighting is renderer-owned and freed on pruning, session
  exit and renderer destruction. Empty/transparent-only chunks retain
  addressable lighting caches. No lighting data changes the chunk save format.

This remains a per-face light sample with per-vertex AO, not Java's exact
lightmap, gamma response, smooth-light interpolation or moon-phase model.
Missing streaming neighbors temporarily read as air and invalidate adjacent
meshes when they arrive. Light-changing edits can cause remeshing spikes.

## Terrain profile and compatibility

**New worlds now use profile 5. Existing worlds retain profiles 1–4.**
Existing chunk files still take precedence, and the saved profile governs
unvisited chunks. The metadata format stays version 1; unknown profile 6
is rejected. LAN welcome validation uses the supported profile range;
older builds reject an unsupported profile 5 rather than inventing terrain.

Profile 5 adds an original continental shelf/coast/inland spline, float
domain warps, erosion-conditioned rolling terrain and foothills, and
resistant mountain zones. Valley zero contours and flanking ridge crests
share a weirdness field. Lowlands get wet channels; mountain valleys have
graded dry floors. Ridge response falls again beyond the crest rather than
forming a large saturated cap. Climate, slope and elevation influence
surface rock/snow and coherent woodland patches. Paired cave fields are
rounded and noodle thickness changes continuously. The existing cached
density lattice, aquifers, ore budgets and actual-block spawn checks remain.

The shared material kernel takes an explicit profile. Public profile-4
samplers still force 4. Eight complete-chunk hashes captured before the
refactor remain identical, including positive/negative coordinates and seeds.
Tests also preserve edits and the selected profile through save/reload and
compare unvisited chunks after reload.

## Reference and remaining differences

The active target is **Minecraft Java Edition 1.21.11**, not the historical
26.3 reference retained for frozen profile 4. Research used
[official 1.21.11 notes](https://www.minecraft.net/en-us/article/minecraft-java-edition-1-21-11),
[Mojang's terrain overview](https://www.minecraft.net/en-us/article/new-world-generation-java-available-testing),
[cave/aquifer descriptions](https://www.minecraft.net/en-us/article/minecraft-snapshot-21w37a),
and selected configuration JSON identified by the
[official version metadata](https://piston-meta.mojang.com/v1/packages/96403ac4a56447675325d2b1b5e320d76a5b5625/1.21.11.json).
Archive SHA-1: `ba2df812c2d12e0219c489c4cd9a5e1f0760f5bd`.
No Mojang source was copied; no downloaded client archive or proprietary
assets are committed.

Important differences remain: 256-block world height, seven biome categories,
value noise and different random streams, local horizontal aquifer cells,
adapted legacy ore ranges, limited vegetation and overhangs, and zero-contour
river channels rather than watershed hydrology. This is neither seed parity
nor an exact 1.21.11 ore, biome, lighting or terrain reproduction.

## Verification

| Check | Result |
| --- | --- |
| MSVC Debug and Release game/test builds, existing CMake configuration | Passed, warnings treated as errors |
| Final complete Release suite | **264/264 passed**, `build/foundation/v5-ores-tests-release.log` |
| Final complete Debug suite | **264/264 passed**, `build/foundation/v5-ores-tests-debug.log` |
| Lighting regressions | Roof/shaft, glass/water attenuation, wall occlusion, torch removal, negative-coordinate seams, interior-edit invalidation passed |
| Profile 4 compatibility | All eight pre-refactor full-chunk hashes unchanged |
| Profile 5 generation | Reversed order, negative coordinates, scalar/fill occupancy and height agreement, coherent fields, cave families and aquifers passed |
| Profile 4/5 save/reload | Edited chunks and subsequently generated unvisited chunks match |
| Ore regression | Both profiles: all eight ores across lowland/mountain samples, deep diamond bias, higher coal bias and relative rarity passed |
| Spawn probe | **500 seeds, zero invalid spawns, zero rescue fallbacks**, 34.237 seconds |
| Spawn simulation | Repeated deterministic placement, standing and movement over 30 profile-5 seeds passed |
| Large regions | Three seeds: `123456`, `-7654321`, `42`; 4096×4096 field maps and actual 32×32-chunk regions generated and inspected |
| Native game | Release launched repeatedly; shader compilation, world loading, night scene and save browser visually inspected |
| User playtest | Night readability accepted with “PERFECT LIGHTING”; individual cave/water/torch-edit actions were not separately reported |
| Terrain native acceptance | **OPEN**: profile-5 world launched, but initial scripted camera placements were inside terrain or beyond useful view distance; these captures were rejected as acceptance evidence |
| Manual multi-seed walking/flying; natural cave entrances; underwater traversal; live torch edits | **UNVERIFIED** |

The computer-use keyboard injection was unreliable in SDL. Desktop control
was subsequently stopped with physical Escape and was not resumed after that
stop. Offline maps, fixed-tick tests and static scenes do not substitute for
the outstanding manual traversal. LAN regression tests passed; a new
two-machine LAN play session was not performed.

### Performance and memory

Single-process Release probe, 1024 chunks, seed 123456: profile 4 averaged
5.368 ms/chunk; profile 5 averaged 5.999 ms/chunk (+11.8%). Other profile-5
seeds measured 5.813 and 5.875 ms/chunk. Offline working set was about
5.41–5.42 MiB; these numbers exclude the graphical game.

Isolated CPU mesher benchmarks: representative terrain changed from
1.60–1.70 to 3.55–3.80 ms/rebuild; solid chunk 4.90→5.95 ms; pathological
checkerboard 24.30→27.15 ms. Lighting is baked once and shared by both
render passes, not recalculated every frame. Scratch storage is about
5.2 MiB; retained packed light costs 64 KiB per meshed chunk (about 5.1 MiB
at 81 chunks). Vertex storage rises from 36 to 44 bytes.

On this RTX 5060 machine, the settled 1280×720 night fixture held 180 FPS
(5.56 ms/frame), approximately 60 simulation ticks/s and about 153 MiB RSS
after settling. A 289-chunk inspection scene reached about 424 MiB RSS.
These capped samples are **not** a controlled FPS improvement claim, a
long-duration leak audit or a frame-time percentile measurement.

## Evidence and reproduction

Owner-local texture screenshots remain ignored under `build/foundation/evidence/`:

- `night-before.jpg` / `night-after.jpg`: same constructed midnight scene,
  feet `(0.5,65,8.5)`, FOV 70, render distance 4, identical block fixture.
  Mobs and procedural clouds move, so these are comparable scenes rather
  than pixel-identical frames. The old ground is crushed into black; the new
  grass, shoreline and mob are readable, with a darker roofed passage and
  warm torch light.

Original generated maps are committed and contain no Minecraft artwork:

| Evidence | Link |
| --- | --- |
| Profile 4 before, seed 123456 | [field atlas](worldgen/seed-123456-atlas.png) |
| Profile 5 after, seed 123456 | [field atlas](worldgen/profile5/seed123456-atlas.png), [density/caves](worldgen/profile5/seed123456-density.png), [actual region](worldgen/profile5/seed123456-region.png) |
| Other seeds | [42](worldgen/profile5/seed42-atlas.png), [-7654321](worldgen/profile5/seed-negative7654321-atlas.png) |
| Spawn records | [500-seed CSV](worldgen/profile5/spawn-validation-500.csv) |

Reproduce with `worldgen_probe PREFIX SEED SPAWN_COUNT PROFILE` and
`python tools/plot_worldgen.py PREFIX` (NumPy, Matplotlib and Pillow).
`worldgen_probe --hash4` prints the compatibility hashes. Research, runtime
fixtures, dependencies, logs and optional owner assets remain workspace-local
and ignored. `AI_MEMORY` files remain excluded.

## Exact changed files

Lighting: `src/game/time_system.c`, `src/game/time_system.h`,
`src/render/mesher.c`, `src/render/mesher.h`, `src/render/renderer.c`,
`src/render/shader.c`, `src/render/vao_manager.c`, `src/render/vao_manager.h`,
`src/render/voxel_light.c`, `src/render/voxel_light.h`, `src/world/block.c`,
`src/world/block.h`, `src/world/world.c`, `tests/test_mesher_ao.c`,
`tests/test_voxel_light.c`.

Terrain: `src/world/world.h`, `src/world/world_gen.c`, `src/world/world_gen.h`,
`src/world/world_meta.h`, `src/world/worldgen_v4.c`, `src/world/worldgen_v4.h`,
`src/world/worldgen_v5.c`, `src/world/worldgen_v5.h`, `tests/test_worldgen_v4.c`,
`tests/test_worldgen_v5.c`, `tools/worldgen_probe.c`, `tools/plot_worldgen.py`.

Save browser and shared integration: `src/game/session.c`, `tests/test_save.c`,
`tests/test_main.c`, `tests/test_main.h`, `CMakeLists.txt`.

Documentation: `README.md`, `docs/BACKLOG.md`, `docs/FIDELITY_BUGS.md`,
`docs/FOUNDATION_REMAINING_ISSUES.md`, `docs/JAVA_FIDELITY.md`,
`docs/WORLD_GENERATION.md`, this report, and the six files under
`docs/worldgen/profile5/` listed in the evidence table.

## Remaining acceptance work

1. Walk/fly the prepared profile-5 mountain, forest, coast and valley scenes
   across multiple seeds with a useful surface camera and view distance.
2. Inspect natural cave entrances/interiors and nighttime underwater views.
3. Place/remove torches during play, including across a chunk boundary.
4. Record traversal frame-time spikes and a longer streaming/memory run.
5. Compare the resulting scenes directly against a consistent Java 1.21.11
   reference. Do not close the terrain or full-pass visual gate before this.
