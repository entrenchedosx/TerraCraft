# World-generation parity

**Status: not vanilla-compatible. No parity is claimed.**

TerraCraft's generator (profiles 1–5, `src/world/world_gen.c`,
`worldgen_v4.c`, `worldgen_v5.c`; see [WORLD_GENERATION.md](WORLD_GENERATION.md))
is an original density-function design that aims at the *look* of 1.21.11
terrain. It does not use Java's `RandomSource`/`LegacyRandomSource`/
`XoroshiroRandomSource`, vanilla `NormalNoise` parameters, the noise router,
spline definitions, multi-noise biome parameter table, carvers, aquifer
algorithm, surface rules, or feature/structure placement. A given seed
therefore produces unrelated terrain to vanilla.

## Differential harness (implemented)

- **Reference side** - `tools/reference/WorldgenRefDump.java` and
  `WorldgenRouterDump.java`, `WorldgenBiomeDump.java` run the *real 1.21.11 server jar's own classes*
  by reflection, resolving names through Mojang's mapping file
  (`RandomSource`, `PositionalRandomFactory`, `ImprovedNoise`,
  `PerlinNoise`, `NormalNoise`, and the full `RandomState` +
  `NoiseRouter` of the overworld built from the vanilla data pack).
  `tools/reference/run_worldgen_ref.sh` regenerates the fixtures.
- **Fixtures** - `tests/fixtures/worldgen/*.csv` (about 1.6 MB, numbers and biome ids only). 8 seeds: `0, 1, 42, -1, 123456789012345,
  Long.MIN_VALUE, Long.MAX_VALUE, -8760239871234567`.
- **C side** - `src/world/jrandom.[ch]`, `jnoise.[ch]`, `jjson.[ch]`,
  `jdensity.[ch]`, `jbiome.[ch]`, `worldgen_parity.[ch]`; CLI `worldgen_diff`
  (`worldgen_diff <fixtures> [mismatches.csv|-] [datapack_root]`); unit test
  `test_worldgen_parity_primitives`. Comparisons are exact bit patterns, no
  tolerance. The router stage loads the vanilla data pack at run time from
  `reference/1.21.11/run/out` (git-ignored, produced by the vanilla data
  generator); without it the stage is reported as skipped. No Mojang data is
  compiled into or committed with the game.

## Status by stage (2026-11-02)

| Stage | Result |
|---|---|
| 1. `LegacyRandomSource`, `XoroshiroRandomSource` (incl. 128-bit seeds, all-zero fix-up), `nextInt/Long/Boolean/Float/Double/Gaussian`, positional `at/fromHashOf/fromSeed` (MD5), `Mth.getSeed`, `String.hashCode` | **Pass**: 3,400+ exact comparisons, 0 mismatches |
| 1. `ImprovedNoise`, `PerlinNoise`, `NormalNoise` (value + max value, y-fudge variant) | **Pass**: 160 + 880 comparisons, 0 mismatches |
| 2. Density-function graph + noise router for the overworld: `barrier`, `fluid_level_floodedness`, `fluid_level_spread`, `lava`, `temperature`, `vegetation`, `continents`, `erosion`, `depth`, `ridges`, `preliminary_surface_level`, `final_density`, `vein_toggle`, `vein_ridged`, `vein_gap` (28 node types incl. splines, `old_blended_noise`, `find_top_surface`, `weird_scaled_sampler`) | **Pass**: 10,800 point evaluations (8 seeds x 15 outputs x 90 points, y from -64 to 319), 0 mismatches, 0 skipped. Debug and Release |
| 3. Climate sampling (`Climate.Sampler`, quantisation x10000 in float) and multi-noise biome lookup through the 7-D `Climate.RTree` (7,593 overworld leaves, built with vanilla's sort/bucketize order, last-result shortcut included) | **Pass**: leaf list count + order-sensitive hash equal; 4,000 random targets (54 distinct biomes; the R-tree and brute force disagree on 62, and C reproduces both exactly); 2,496 sampler-derived points over 8 seeds (42 distinct biomes) with all six quantised climate values and the biome identical; 0 mismatches |
| 4a. `NoiseChunk` wrapping: 4x8x4 cell interpolation of `interpolated` nodes (Mth.lerp3 form of the CacheAllInCell fill and the Y,X,Z chain form), `FlatCache` quart lattice, corner memo | **Pass**: bit-exact per-block final density for 40 chunks (8 seeds x 5 chunks, all 24 sections, 3.9 M values): 960 section hashes, 0 mismatches |
| 4b. `Aquifer.NoiseBasedAquifer` (nearest-4 aquifer centres on the 16x12x16 grid, `computeFluid` with the 13 surface-sampling offsets, surface level via floodedness/spread/erosion/depth, lava noise, barrier pressure), `OreVeinifier` (copper/iron veins, granite/tuff filler, raw ore blocks), material rule list and default block - i.e. `doFill` block states | **Pass**: 128 chunks (8 seeds x 16 chunks, 12.6 M blocks: air, stone, water, lava, tuff, granite, copper/iron ore, raw copper/iron blocks): per-section hashes of density bits, solid mask and block names (3,072 section triples) all identical, 0 mismatches. Debug and Release |
| 4c. Carvers: `CaveWorldCarver` (cave, cave_extra_underground), `CanyonWorldCarver`, `CarvingMask`, `WorldgenRandom.setLargeFeatureSeed`, float/height providers, lava level, aquifer fluid in carved blocks, `applyCarvers` 17 x 17 neighbour loop | **Pass**: the same 128 chunks as 4b after carving (12.6 M blocks): 3,072 per-section block-name hashes, 3,072 per-section carving-mask hashes and 384 per-carver start counts identical (395 sections change relative to the uncarved fill, 696 sections have mask bits), 0 mismatches. Debug and Release. Reference carves with the real biome source; all 54 overworld biomes list the same three carvers |
| 5. `SurfaceSystem.buildSurface` with the `SurfaceRules` interpreter driven by the overworld `surface_rule` JSON (bedrock floor and deepslate transition via `vertical_gradient`, grass/dirt/sand/gravel/mud/podzol/mycelium/calcite/powder-snow top layers, clay-band badlands, ice/packed-ice/snow, `steep`, `hole`, `water`, `y_above`, `stone_depth` incl. the secondary-noise range, `noise_threshold`, `temperature`, `above_preliminary_surface`), eroded-badlands pillars, frozen-ocean icebergs, generation heightmaps (`WORLD_SURFACE_WG`, `OCEAN_FLOOR_WG` incl. `Heightmap.update` rescans), `BiomeManager.getBiome` (SHA-256 `obfuscateSeed`, hashed 8-corner zoom) over per-chunk stored biomes, `Biome.getTemperature` with `PerlinSimplexNoise`, and the carver top-material swap (`CarvingContext.topMaterial`) | **Pass**: 200 chunks (8 seeds x 16 fixed chunks + 3 seeds x 24 chunks chosen so every special surface biome is hit: eroded/plain/wooded badlands, frozen and deep frozen ocean, ice spikes, jagged/frozen/stony peaks, snowy slopes, grove, swamp, mangrove swamp, desert, beaches, taiga variants, mushroom fields...). Per-section block-name hashes after the surface stage and after surface + carvers (4,800 each), carving-mask hashes (4,800), both heightmaps after each stage (800), 384 `getBiome` probes, 180 temperature floats, 120 simplex doubles: 72,198 comparisons in total for the whole harness, 0 mismatches, Debug and Release. Mutation checks (clay-band scale, badlands pillar base) are caught |
| 6a. Biome decoration framework: `FeatureSorter` step lists (overworld), `WorldgenRandom` decoration/feature seeding, candidate biomes of the 3 x 3 region, the `PlacedFeature` placement pipeline (`count`, `noise_threshold_count`, `in_square`, `height_range`, `heightmap` incl. lazily primed `MOTION_BLOCKING`, `rarity_filter`, `biome`, `block_predicate_filter`), block-predicate subset (`matching_blocks`, `matching_fluids`, `replaceable`, `not`, `all_of`, `any_of`, `true`), tag-driven rule tests, and the **ore** feature (`OreFeature`, every overworld `ore` placed feature) | **Pass**: the FeatureSorter order of all 164 overworld placed features is identical (164 rows); 18 decorated regions (3 seeds x 6 target chunks, each a 3 x 3 region generated through fill, surface and carvers) give identical per-section block-name hashes for all 9 chunks (3,888 hashes) with ores only |
| 6b. Biome decoration, vegetation patches: `random_patch` / `flower` with `simple_block`, `simple_state_provider` / `weighted_state_provider`, plant survival rules (dirt tag, dry-vegetation tag, double plants) for 22 placed features (short grass, tall grass, ferns, dead bushes, poppy/dandelion/orchid flowers, sunflowers, pumpkins, melons) | **Pass**: the same 18 regions, vegetation only, 3,888 section hashes identical, 0 mismatches |
| 6c. Structure-set placement: `RandomSpreadStructurePlacement` (linear and triangular spread, salts), all four `FrequencyReductionMethod`s, exclusion zones (pillager outposts vs villages) for the 19 random-spread structure sets | **Pass**: 8 seeds x 19 sets scanned over +-64 chunks (+-12 for the densest set): 5,733 structure chunks plus per-set counts identical. Says where a structure *may* start; piece generation, biome validity and terrain checks of the structure start are not ported. Strongholds (concentric rings) not ported |
| 6d. Trees: `TreeFeature` with the `straight`, `forking`, `giant`, `mega_jungle`, `dark_oak`, `fancy` and `bending` trunk placers; `blob`, `bush`, `spruce`, `pine`, `acacia`, `mega_pine`, `jungle`, `dark_oak`, `fancy` and `random_spread` foliage placers; two/three-layer feature sizes; the `beehive`, `trunk_vine`, `leave_vine`, `cocoa`, `place_on_ground` and `alter_ground` decorators (with Java `HashSet` iteration order and the stable Y sort of `TreeDecorator.Context`); `random_selector`, `simple_random_selector`, `random_boolean_selector`, `weighted_list` int providers, `surface_water_depth_filter`, `would_survive` for saplings; the lazily primed `MOTION_BLOCKING_NO_LEAVES`, `OCEAN_FLOOR`, `WORLD_SURFACE` heightmaps | **Pass** for 32 of the 39 vanilla tree configurations (`oak`, `birch`, `spruce`, `pine`, `fancy_oak`, `dark_oak`, `acacia`, `jungle_tree`, `jungle_bush`, `mega_jungle_tree`, `mega_pine`, `mega_spruce`, `swamp_oak`, `azalea_tree`, `super_birch_*` and every `*_bees_*` / `*_leaf_litter` variant): 32 configurations x 12 columns x 18 regions are generated by the real jar, each placement independent (the world is restored after every placement), and block name, placement result and a hash of every changed block are identical (6,912 rows, 27,648 checks). Plus the placed features `trees_swamp` / `trees_meadow` end to end (feature order, selectors, `surface_water_depth_filter`, decorator random consumption). **Open** (not enabled in the game data path): `cherry` (ported, but its leaf holes differ from the reference, so it is disabled unless `TERRACRAFT_EXPERIMENTAL_CHERRY` is set), `mangrove` / `tall_mangrove` (upwards-branching trunk, mangrove roots, `attached_to_leaves`), the three pale oaks (`pale_moss`, `creaking_heart`) |
| 6e. Remaining features: fallen trees, huge mushrooms, lakes, geodes, icebergs, forest rocks, large dripstone, monster rooms, fossils, desert wells, ice spikes, disks, springs, kelp/seagrass/coral, bamboo, vines, block columns (sugar cane, cactus), sculk, freeze top layer, `rooted_azalea_tree` (root system), `environment_scan` placement; structure pieces (villages, strongholds, mineshafts, ...) | Not started |
| 7. Nether, End (End islands, legacy random source settings) | Not started |

**Important:** stage 2 proves the *point-evaluated* router. It does not yet
mean TerraCraft generates vanilla chunks: the live game still uses its own
profile-4/5 generator. Chunk-level parity (stages 3-6) must pass before the
game switches over.

## Remaining steps (ordered)

3. ~~Climate sampler and biome lookup~~ - done (exact). Note: vanilla's R-tree keeps a
   thread-local last result that decides exact distance ties, so biome results at ties depend on
   query order; the harness replays queries in the reference order. A world generator must
   therefore query in the same order as vanilla's chunk generation to be tie-exact.
   The surface stage settles this the way vanilla does: biomes are stored per chunk once
   (`ChunkAccess.fillBiomesFromNoise`: sections bottom to top, quarts in x, y, z order) and
   `BiomeManager` only reads stored values (`jsurface_prepare_biomes` fills a chunk and its 8
   neighbours in a fixed order). Both the C port and the reference harness use that order and
   one R-tree per generation run. A frozen-ocean chunk in the first fixture draft exposed the
   problem (a few columns resolved a tied biome differently); real vanilla has the same
   thread-order sensitivity at exact ties, which a C port can only fix by fixing the order.
4. ~~`NoiseChunk` density, aquifers, ore veins (`doFill`)~~ - done (exact; `jfill.c`).
   ~~Carvers~~ - done (exact; `jcarver.c`), including the grass/mycelium top-material swap, which is checked
   in the `pipeline_*` suites (fill -> surface -> carvers) together with the surface stage.
5. ~~`SurfaceSystem` + surface rules, bedrock; heightmaps~~ - done (exact; `jsurface.c`). Next: features.
6. Features and structures in vanilla's decoration order. Done and exact: ores, vegetation patches (a subset), structure-set
   placement. Open: cherry/mangrove/pale oak trees and the rest of 6e. Every feature type needs its own port (trunk/foliage placers for trees, ...),
   the block-state registry beyond block names, and a Java reference run behind the stand-in `WorldGenLevel` of
   `tools/reference/WorldgenFeatureDump.java`.
7. Nether/End.

Each step adds a reference dump (Java, reflection on the server jar) and a C
comparison; a stage is marked *Pass* only with zero mismatches.

## Notes from the decoration stage (6a-6c)

- **The decoration random is not a Xoroshiro random.** `WorldgenRandom` extends `LegacyRandomSource`: `nextInt(bound)`,
  `nextFloat`, `nextDouble` and `nextLong` use the *legacy algorithms* on top of the wrapped source's `next(bits)`, which for a
  Xoroshiro source is `nextLong() >>> (64 - bits)`. TerraCraft models this as `JR_WORLDGEN` in `jrandom.c`. Using plain Xoroshiro
  methods looked plausible and produced completely different placements.
- **Block tags are not bound** in a registry created outside a running server. The reference tool binds the data-pack block tags
  into `BuiltInRegistries.BLOCK` (unfreezing it briefly); without that `TagMatchTest` ore targets match nothing and no ore is placed.
- Feature indices are positions inside the per-step `FeatureSorter` lists, and every feature reseeds with
  `setFeatureSeed(decorationSeed, index, step)`, so features are independent of one another's random draws. That is what makes
  testing a subset of features sound: the reference and the port skip the same features.
- Only block *names* are tracked by the worldgen core (no block-state properties such as `axis`, `half`, `distance`); the parity
  hashes are name-level. Up to 255 distinct names fit the one-byte block ids (`jfill_block_register`).
- The test region is 3 x 3 chunks around the decorated chunk, as in `WorldGenRegion`; ores write sections directly (no heightmap
  update), `setBlock` features update the generation heightmaps.

## Notes from the tree stage (6d)

- **Heightmaps during decoration.** `ChunkStatusTasks.generateFeatures` primes `MOTION_BLOCKING`, `MOTION_BLOCKING_NO_LEAVES`,
  `OCEAN_FLOOR` and `WORLD_SURFACE` of the decorated chunk *before* decoration starts. While it runs the chunk's persisted status is
  still CARVERS, so `ProtoChunk.setBlockState` only maintains `OCEAN_FLOOR_WG` and `WORLD_SURFACE_WG`: the four primed maps are
  never updated, which is why grass still grows under tree canopies. Chunks around it prime them lazily at the first query. This is
  now modelled exactly (`JPostHeightmaps`); the earlier ore/vegetation fixtures were regenerated with the corrected reference and
  are byte-identical.
- **Evaluation order.** C leaves the evaluation order of `a() + b()` unspecified; two random draws in one expression made Release
  and Debug builds disagree. Every multi-draw Java expression is split into sequential statements.
- **`HashSet<BlockPos>` order.** Tree decorators iterate the trunk/leaf sets; their order (hash bucket order of
  `(y + z * 31) * 31 + x`, resize splitting) decides which log gets a bee nest or vine. `PosSet` in `jtree.c` reproduces it.
- **Large trunks** use `min(|dx|, |dx - 1|)` in `shouldSkipLocationSigned`, not `|dx| - 1`.
- The bee nest block entity is created on demand by `WorldGenRegion.getBlockEntity`; the reference stand-in does the same, so the
  `2 + nextInt(2)` / `nextInt(599)` draws of `BeehiveDecorator` are consumed.
- Fancy oak uses `Math.sin/cos`/`sqrt` in doubles; the C runtime and the JVM could differ in the last bit. No mismatch was seen.
- Test design: `tools/reference/WorldgenFeatureDump.java` with `TREE_TEST=1` places every configured tree at 12 fixed columns of
  each generated region (`TREE_NAMES`, `TREE_SKIP`, `TREE_K`, `TREE_DUMP`), restoring the world after each placement; the rows
  carry the feature seed so subsets can be tested.
