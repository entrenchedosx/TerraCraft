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
  `WorldgenRouterDump.java` run the *real 1.21.11 server jar's own classes*
  by reflection, resolving names through Mojang's mapping file
  (`RandomSource`, `PositionalRandomFactory`, `ImprovedNoise`,
  `PerlinNoise`, `NormalNoise`, and the full `RandomState` +
  `NoiseRouter` of the overworld built from the vanilla data pack).
  `tools/reference/run_worldgen_ref.sh` regenerates the fixtures.
- **Fixtures** - `tests/fixtures/worldgen/*.csv` (about 0.9 MB, numbers only,
  hex-encoded doubles). 8 seeds: `0, 1, 42, -1, 123456789012345,
  Long.MIN_VALUE, Long.MAX_VALUE, -8760239871234567`.
- **C side** - `src/world/jrandom.[ch]`, `jnoise.[ch]`, `jjson.[ch]`,
  `jdensity.[ch]`, `worldgen_parity.[ch]`; CLI `worldgen_diff`
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
| 3. Climate sampling -> multi-noise biome lookup | Not started |
| 4. `NoiseChunk` cell interpolation, `NoiseBasedChunkGenerator` fill, aquifers, ore veins, carvers | Not started |
| 5. Surface rules, bedrock, heightmaps | Not started |
| 6. Features / decorators / structures | Not started |
| 7. Nether, End (End islands, legacy random source settings) | Not started |

**Important:** stage 2 proves the *point-evaluated* router. It does not yet
mean TerraCraft generates vanilla chunks: the live game still uses its own
profile-4/5 generator. Chunk-level parity (stages 3-6) must pass before the
game switches over.

## Remaining steps (ordered)

3. `Climate.Sampler` (quantisation x10000, `TargetPoint`), `Climate.ParameterList`
   nearest-point search over `reports/biome_parameters/.../overworld.json`;
   differential biome id test at quart positions.
4. `NoiseChunk`: 4x8 cell interpolation of `final_density` (the router above is
   evaluated at cell corners and trilinearly interpolated), `NoiseBasedChunkGenerator.doFill`,
   aquifers, ore veins; compare block states at world coordinates and chunk hashes.
5. `SurfaceSystem` + surface rules, bedrock; heightmaps; compare.
6. Carvers, features, structures in vanilla's decoration order.
7. Nether/End.

Each step adds a reference dump (Java, reflection on the server jar) and a C
comparison; a stage is marked *Pass* only with zero mismatches.
