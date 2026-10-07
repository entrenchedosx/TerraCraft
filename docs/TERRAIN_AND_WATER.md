# Terrain profiles and water simulation

This note describes how TerraCraft creates its surface and advances water,
including what a save-version change means for existing worlds. It is a
description of the current implementation, not a claim that the systems
match Minecraft in every detail.

## Terrain profiles

Terrain generation is deterministic from the world seed and world X/Z
coordinates. Chunk generation calls the versioned functions in
`src/world/world_gen.c`; neighboring chunks therefore agree on shared
columns without needing generation order or neighbor data.

### Profile 1: legacy worlds

Profile 1 is the original column-height formula. It combines a low-frequency
continental field, a mountain-biome mask, rolling hills, and small detail,
then clamps the surface to Y 4 through 200. Existing worlds without a
`terrain_version` metadata key select this profile. Keeping that choice when
an untouched chunk is generated later prevents a boundary between terrain
shapes inside one save.

### Profile 2: previous new-world profile

Worlds created during the previous terrain pass use profile 2. Its height
calculation combines:

- A low-frequency warp (about 104 blocks in either horizontal direction)
  applied before sampling the main fields. The warp breaks up regular noise
  contours over long distances.
- Five-octave continental noise at a 0.0017 coordinate scale, broad hills at
  0.0065, and rolling terrain at 0.018.
- A ridge field at 0.0044 with a sharper peak field at 0.012. A smooth mask
  turns only the upper ridge values into mountain lift.
- A separate meandering valley field at 0.00145. It carves more deeply
  through raised inland terrain; small-scale detail is added afterward.
- A final surface clamp to Y 4 through 200. The existing biome surface,
  caves, ores, trees, and vegetation systems then use that height.

This is still a 2D height map. The meandering field shapes river-like
valleys, but does not guarantee a continuous river channel at every seed or
turn every valley into flowing water. Terrain profile 2 is intentionally
versioned rather than silently replacing profile 1 in older worlds.

### Profile 3: new worlds

Newly created worlds use profile 3. It improves the large-scale structure
while preserving deterministic shared chunk borders:

- A stronger low-frequency warp feeds a broad continental field, so coastlines
  and inland landforms vary at different scales.
- Separate mountain-range and ridge fields form connected highlands with
  sharper peaks, while an erosion field leaves quieter foothills and plains.
- A meandering river field cuts lowland channels down to the sea-level water
  table. Existing basin filling turns those channel cells into water.
- The surface is clamped to Y 4 through 224. Mountain slopes are stone, with
  snow caps on the highest peaks; existing biome, cave, ore, tree, and
  vegetation systems continue to use the generated surface.

This is a substantial height-map upgrade, not a reproduction of Minecraft's
full 3D density router: terrain still has one top surface per X/Z column. The
existing 3D cave carver remains separate, and profile 3 does not add
overhangs, aquifers, or cave biomes.

The metadata file remains format version 1 with an optional
`terrain_version=1`, `terrain_version=2`, or `terrain_version=3` key.
New-world creation writes 3; existing profile 1 and 2 worlds retain their
generator, and older metadata that omits the key reads as 1. Unknown terrain
versions are rejected rather than blended with a known generator. See
`src/world/world_meta.c` and `src/game/session.c`.

## Water states and update order

The original `BLOCK_WATER` ID remains the full-height source. Appended block
IDs represent horizontal flow levels 1 through 7 and a falling column. Each
chunk stores a bitset that prevents the same coordinate from appearing in
the update queue more than once at a time.

At each 0.25-second fluid interval, the simulation processes at most 384
queued cells. A fluid cell first attempts to flow down. If blocked, it
spreads horizontally: a source starts at level 1 and each horizontal step
loses one level, ending after level 7. Flow levels are removed when they no
longer have a valid lower-level neighbor, so a stream can retract after its
source disappears. Two adjacent source blocks over a solid floor can refill
an empty middle cell.

Only loaded chunks participate. Fluid never writes into an unloaded chunk;
when a neighboring chunk is added, water along the shared border is queued
again. A full rebuild queue stays capped at 32,768 coordinate updates. An
incremental scan of loaded chunk cells recovers water work that did not fit
in the queue, with a 4,096-cell scan budget per fluid interval. This bounds
memory and per-interval work, while allowing large bodies of water to drain
through the queue over time.

Water flow states are stored as block IDs in chunk-file format version 2.
The payload size did not change. Version 1 files still load; any ID that was
not valid in that format is mapped to the established safe fallback block.
New fluid changes mark the edited chunk for saving, and changes on chunk
borders invalidate both meshes.

The transparent mesher uses each flow level's fractional height. Flow level
1 is 7/8 of a block high and level 7 is 1/8; sources and falling columns are
full height. When adjacent cells have different heights, it draws the exposed
step between the surfaces and culls equal-height internal faces. Water does
not collide with the player. The player controller
detects water in its standing body, lowers horizontal speed, increases drag,
and applies a simple vertical response: jump rises and sneak sinks. It does
not yet implement a swimming pose, the short swimming hitbox, oxygen,
drowning, waterlogged blocks, or current-driven movement.

## Checks

The test runner includes deterministic profile checks, loaded-chunk edge
wakeup, bounded queue behavior, source/flow/retraction rules, fractional
water-mesh heights, version 1/2 save compatibility, and player buoyancy.
Those checks are headless: they do not judge the terrain's visual quality,
river continuity, water transparency, or comparison with a live Java
Edition client.

The high-level terrain direction follows documented Java terrain-generation
work on integrated terrain, mountains, and rivers, while the flow rules use
the familiar source/refill and horizontal-spread model described in Mojang's
snapshot notes. These sources are references for behavior, not evidence of
full parity:

- [Minecraft Java experimental world generation](https://www.minecraft.net/en-us/article/new-world-generation-java-available-testing)
- [Minecraft Java 18w16a water behavior notes](https://feedback.minecraft.net/hc/en-us/articles/360004167171-Minecraft-Java-Edition-Snapshot-18W16A)
