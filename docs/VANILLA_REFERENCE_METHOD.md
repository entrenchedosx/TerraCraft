# How TerraCraft derives and checks vanilla behaviour

## Available references (audited 2026-11-02)

| Reference | Where | Use |
|---|---|---|
| Installed game jars `26.2`, `26.3`, `26.4-snapshot-*` | `%APPDATA%\.minecraft\versions\` | **Not 1.21.11.** Unobfuscated; class files are Java 25 (v69). Used read-only (`javap -c`) to cross-check constants of subsystems that are unchanged in code shape. Each use is flagged in the doc that cites it. |
| **`1.21.11` client + server jars, official Mojang mappings** | `reference/1.21.11/` (git-ignored). Downloaded 2026-11-02 with the owner's approval from `piston-data.mojang.com`; SHA-1s verified (`tools/reference/fetch_reference.sh`). client `ba2df812...` (31 MB), server `64bb6d76...` (56 MB), mappings 12 MB + 8.7 MB. Runs on JDK 21. | **The authoritative target.** Obfuscated; read with `javap` + `client_mappings.txt`. The server jar's data generator (`--reports --server`) produced `reference/1.21.11/run/out/`: registries, `blocks.json` (1,166 blocks, **29,671 block states**), 1,505 items, 157 entity types, and the complete worldgen data pack (density functions, noise settings, biome parameter lists, carvers, features, structures). |
| Supplied assets `mcassets/` (git-ignored) | repo root | Real models/blockstates/textures/sounds. No `spear` item models, so the pack predates 1.21.11 content. |
| `eaglercraft/` | repo root (git-ignored) | 1.8-era client sources; only useful for old-behaviour cross-checks, never as a 1.21.11 reference. |

No Mojang code is copied into the repository. Constants and call sequences are
read from the installed game and re-implemented in C; the Java utility below
only *executes the game's own math classes* to produce golden vectors.

## Method used for the first-person hand

1. `javap -c -p -constants` on `FirstPersonHandsAndItemsRenderer`,
   `FirstPersonHandsAndItems`, `GameRenderer`, `PlayerModel`,
   `ModelPart$Cube/$Polygon`, `AvatarRenderer`, `Mth` to read every constant
   and call order (translate/rotate sequence, 0.4 clamp, 0.1 sway factor,
   `zRot = 0.1`, Mth's 65536-entry sine table with factor
   `10430.378350470453`).
2. `tools/reference/HandGolden.java` replays those sequences through the
   game's real `PoseStack`, `Axis` and `Mth` and prints column-major matrices.
   `tests/fixtures/hand_golden.h` is its output; `tests/test_hand_view.c`
   compares the C port against it (tolerance **1e-4 absolute**).
3. Tick-state rules (swing 0→5/6, equip dip sequence, attack-strength cube,
   walk-bob smoothing) are unit-tested against sequences derived by hand from
   the tick rules in the same bytecode.

Regenerating the fixture needs a game jar. Because the installed jars are
class-version 69 and the available JDK is 21, `tools/reference/gen_hand_golden.sh`
extracts the needed classes, downgrades their class-file version byte, and
runs the generator on JDK 21. This is a developer-only tool; it is not part of
the shipped game or the default test run.

## Verification against the 1.21.11 jar itself

Done after the jar became available (2026-11-02):

- `ItemInHandRenderer` (`hof`): its 91 distinct float/double constants are
  **identical** to 26.3's `FirstPersonHandsAndItemsRenderer`, so the golden
  matrices (generated through 26.3's math classes) are valid for 1.21.11.
- `Mth` (`bgj`): same table factor `10430.378350470453` and `+16384` cosine
  offset. `GameRenderer.bobView` constants (0.5, 3.0, 5.0, 0.2) match.
- Item display transforms in the 1.21.11 jar's `item/generated`, `handheld`
  and `block/block` equal the supplied `mcassets` values.
- **Correction found by this check:** 1.21.11 already uses
  `Player.getItemSwapScale` (separate `itemSwapTicker`). The hand dips when
  the main-hand item *type* changes or on an air-swing miss
  (`resetAttackStrengthTicker` resets both tickers), but a melee hit
  (`onAttack` -> `resetOnlyAttackStrengthTicker`) does **not** dip the hand.
  The first version of `hand_view` assumed a dip on hits; it is fixed and
  tested.
- The supplied `mcassets/` has 0 spear item models; the 1.21.11 jar has 7. The
  pack predates 1.21.11 content and should be replaced by assets extracted from
  `client.jar` (local only; assets must not be committed).

## Limits of this evidence

- Golden matrices prove the transform *composition*. They do not prove the
  rendered pixels; no frame-vs-frame comparison has been run yet.
- The world-generation reference data now exists locally but no differential
  harness has been written yet (see WORLDGEN_PARITY.md).

## Method used for carvers (worldgen stage 4c)

1. `tools/reference/javap_mapped.py` extracts classes from the server jar,
   disassembles them with `javap -c -p`, and rewrites class/field/method
   references through `server_mappings.txt` so the bytecode of `WorldCarver`,
   `CaveWorldCarver`, `CanyonWorldCarver`, `NoiseBasedChunkGenerator.applyCarvers`,
   `Mth`, the value providers and `WorldgenRandom` reads with Mojang names.
   The C port (`src/world/jcarver.c`) follows that bytecode, including its
   float/double mixing and argument evaluation order.
2. `tools/reference/WorldgenCarverDump.java` runs the real classes: it stores the
   NoiseChunk fill in a real `ProtoChunk`, then replays the `applyCarvers` loop
   (17 x 17 neighbour chunks, carver biome from the multi-noise sampler, real
   `ConfiguredWorldCarver.carve` with the NoiseChunk's own aquifer and
   `CarvingMask`). The registry lookup used by the tool has no tags bound, so
   `#minecraft:overworld_carver_replaceables` is rebuilt from the data pack tag
   JSON and patched into each carver config.
3. `ONLY=carver tools/reference/run_worldgen_ref.sh` regenerates just the carver
   fixtures (`carvers.csv`, `carver_counts.csv`, `carver_starts.csv`).

## Method used for the surface stage (worldgen stage 5)

1. `javap_mapped.py` disassembled `SurfaceSystem`, `SurfaceRules` and its nested
   conditions (`StoneDepthCheck`, `WaterConditionSource`, `YConditionSource`,
   `VerticalGradientConditionSource`, `NoiseThresholdConditionSource`, the
   `Context` helper conditions), `BiomeManager`, `Biome` (temperature and the
   `FROZEN` modifier), `PerlinSimplexNoise`/`SimplexNoise`, `Heightmap`,
   `WorldCarver.carveBlock` and `RandomState`. `jsurface.c` follows that bytecode
   (float/double mixing, `Mth.map`, lazy-condition caching, `Math.round`).
2. The rule tree is not hard-coded: it is parsed at run time from
   `data/minecraft/worldgen/noise_settings/overworld.json` (`surface_rule`), so the
   interpreter only knows rule/condition *types*; unknown types fail loading loudly.
3. `tools/reference/WorldgenSurfaceDump.java` runs the real classes: NoiseChunk fill
   into a `ProtoChunk` with the generation heightmaps primed as `doFill` does, then
   the real `SurfaceSystem.buildSurface` with a real `BiomeManager` over a
   per-chunk biome store, then the replayed `applyCarvers` loop with
   `BiomeManager::getBiome` as the carver biome getter (so `topMaterial` runs).
   It also picks, per seed, chunks in every special surface biome by scanning the
   climate sampler, and dumps `BiomeManager.getBiome`, `Biome.getTemperature` and
   `PerlinSimplexNoise` probes. `ONLY=surface tools/reference/run_worldgen_ref.sh`
   regenerates `surface*.csv`, `pipeline*.csv`, `biome_zoom.csv`, `temperature.csv`,
   `simplex.csv` (about 0.5 MB, one minute).
4. Debugging a mismatch: `SURF_FULL=seed,cx,cz,file ONLY=surface ...` dumps one chunk block
   by block and `worldgen_diff --surface <datapack> <seed> <cx> <cz> <file>` lists the
   differing blocks with their biome. `SURF_PROBE=x,z` prints the real iceberg noise terms.
