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

## What exact parity requires (ordered)

1. Reference: obtain the 1.21.11 server/client jar (manifest lists `1.21.11`;
   not installed – needs owner approval to download) and a headless dump tool
   (Java) that, for fixed seeds (positive, negative, 0, max/min long), writes
   CSV of biome id, surface/ocean-floor/motion-blocking heights and block
   state at sampled coordinates plus per-chunk hashes.
2. `JavaRandom` (48-bit LCG), `Xoroshiro128++` with seed upgrade
   (`RandomSupport.upgradeSeedTo128bit`, splitmix/mixStafford13) and
   positional forks (`forkPositional`, `at(x,y,z)`, `fromHashOf(name)`), with
   64-bit wraparound written via `uint64_t`/`int64_t` casts.
3. `ImprovedNoise`/`PerlinNoise`/`NormalNoise` with the data-pack noise
   parameters (first octave, amplitudes) and Java's evaluation order.
4. Density-function graph + noise router for the overworld
   (`overworld.json` from the 1.21.11 data pack), cubic splines for
   continentalness/erosion/weirdness/ridges, final density and
   `NoiseChunk` interpolation (cell sizes 4×8 blocks, 4×4×8 interpolation).
5. Multi-noise biome source with the climate parameter list; `Climate`
   quantisation (×10000, `(long)` truncation).
6. Aquifers, ore veins, noodle/cheese/spaghetti caves from the router, carvers.
7. Surface rules, bedrock, then ordered feature decoration steps with
   per-feature seeded populations; structure sets and placements.
8. Nether and End.

Each step gets its own differential test (biome → heights → block states →
heightmaps → fluids → features → structures → chunk hashes) with a
machine-readable mismatch report (`x,z,y,expected,actual`). **Required end
state: zero unexplained mismatches.** Until a stage passes, it is listed as
*Missing* in the parity matrix.

## Harness layout (planned, not yet created)

- `tools/reference/WorldDump.java` – runs against a 1.21.11 jar, no game code
  shipped.
- `tests/fixtures/worldgen/seed_<n>.csv` – small committed samples (not the jar).
- `tools/worldgen_diff.c` – loads CSV, runs the C generator, writes
  `build/parity/worldgen_mismatch_<seed>.csv`.

## Not started this session

No generation code was changed. Existing profile-4/5 frozen-hash tests remain
the only guard and test *internal* stability, not vanilla equivalence.
