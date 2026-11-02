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
| 4c. Carvers | Not started |
| 5. Surface rules, bedrock, heightmaps | Not started |
| 6. Features / decorators / structures | Not started |
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
4. ~~`NoiseChunk` density, aquifers, ore veins (`doFill`)~~ - done (exact; `jfill.c`). Next: carvers
   (cave/canyon carvers run after surface rules), then surface rules and bedrock.
5. `SurfaceSystem` + surface rules, bedrock; heightmaps; compare.
6. Carvers, features, structures in vanilla's decoration order.
7. Nether/End.

Each step adds a reference dump (Java, reflection on the server jar) and a C
comparison; a stage is marked *Pass* only with zero mismatches.
