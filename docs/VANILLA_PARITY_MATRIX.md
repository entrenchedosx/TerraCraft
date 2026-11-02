# Vanilla parity matrix (target: Minecraft Java Edition 1.21.11)

Status as of the 2026-11-02 parity pass. Rules for this file:

- **Pass** is only recorded when a comparison against reference-derived data
  exists and is cited in the Verification column. Nothing else is "exact".
- *Partial* means some behaviour exists but is known to differ.
- Differences are itemised in [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md).

| System | Current state | Vanilla reference | Verification | Priority |
|---|---|---|---|---|
| First-person hand: transform chain, swing, equip, sway, view bob | **Supported** (new `src/game/hand_view.c`) | `FirstPersonHandsAndItemsRenderer` / `GameRenderer.bobView` constants read from installed game bytecode; matrices replayed through the game's own `PoseStack`/`Axis`/`Mth` | **Pass (math)**: 49 golden matrices match to 1e-4 (`test_hand_golden_*`). **Pending (pixels)**: no side-by-side frame comparison yet | Critical |
| First-person hand: arm geometry + skin UVs | Supported (`ModelPart.Cube` port, wide arm + sleeve) | 26.3 `PlayerModel`/`ModelPart$Cube` bytecode | Pass (UV/vertex unit tests). Slim (Alex) arm not implemented | Critical |
| First-person held item (sprite / block) | Partial | `item/generated`, `item/handheld`, `block/block` display transforms from supplied assets | Matrix Pass; sprites are flat (no per-pixel extruded rim); no offhand; no per-item display overrides (bow, shield, trident, crossbow…) | High |
| Hand: eat/drink, bow draw | Supported (transform) | `applyEatTransform`, bow branch | Eat Pass (golden); bow transform ported, not golden-tested | High |
| Hand: block, crossbow, map, brush, spear (1.21.11), trident, shield | **Missing** | 1.21.11 | – | High |
| Player physics (walk/sprint/jump/gravity/friction) | **Partial – vanilla 20 TPS tick model (`vphys`), bytecode-derived** | `LivingEntity.travel`, 20 TPS | Pass on analytic vanilla figures (4.317 / 5.612 / 1.295 b/s, 1.2522 jump, -3.92 terminal, water -0.025); no live differential harness yet; see [PHYSICS_PARITY.md](PHYSICS_PARITY.md) | Critical |
| Water movement | Partial. Exit no longer teleports (jump-out impulse 0.3 b/tick); rest of fluid model is custom | `LivingEntity.travelInFluid`, `FlowingFluid` | No-teleport regression test only; vanilla trajectory comparison Pending | Critical |
| Terrain generation | **Partial**: the game still uses its own profile-4/5 generator, but a vanilla-exact RNG/noise/density-function/router library now exists (`jrandom`, `jnoise`, `jdensity`) | `NoiseBasedChunkGenerator`, noise router, splines | Router: **Pass** (10,800 exact point evaluations over 8 seeds). `doFill` (density, aquifers, ore veins) per block: **Pass** (128 chunks, 12.6 M blocks). Not yet wired into the game; surface rules, carvers, features: not started | Critical |
| Biome selection | **Partial**: game still uses its own 7-biome selector; a vanilla-exact climate sampler + 7,593-leaf multi-noise R-tree exists (`jbiome`) but is not wired into the game | Multi-noise climate parameters, 54 biomes in the overworld list | Library: **Pass** (25,474 exact biome/climate comparisons, 0 mismatches). Game output: Fail until wired | Critical |
| Caves / aquifers / ores / features / structures | Approximation (3 cave scales, local aquifers, 8 ores; no structures) | Carvers, `aquifer`, configured/placed features | Fail | High |
| Lighting | Custom propagation + moonlit profile | `LightEngine`, `lightmap.fsh` | Pending – no reference frames | High |
| Water rendering / transparency | Partial | `translucent` render type | Pending | Critical |
| Sky, fog, clouds, weather | Procedural approximation | `LevelRenderer` sky/fog classes | Pending | High |
| HUD, hotbar, inventory | Partial – shared textured UI (commit 18c2e36) | `gui` sprites, `Gui`, `InventoryScreen` | Pending | High |
| Creative inventory, recipes, containers | Partial (32 items, small recipe set) | Registries | Pending | High |
| Blocks and items | Partial (tens, vs 1,166 blocks / 29,671 states / 1,505 items in 1.21.11) | Registries | Pending | High |
| Entities and animation | Partial (cow, zombie, skeleton; 3rd-person player) | Entity models/AI | Pending | High |
| 1.21.11 content (spear, nautilus, zombie nautilus, camel husk, parched) | **Missing** (supplied `mcassets` also has no spear item models) | 1.21.11 | – | Medium |
| Redstone, fire, farming, brewing, enchanting, villagers, portals, Nether/End | **Missing** | – | – | Medium |
| Persistence | Supported (own format, versioned) | Anvil/NBT | Own tests only; not vanilla-format compatible | Medium |
| Multiplayer | Partial (own LAN protocol; not vanilla protocol) | Java protocol | – | Low |
| Differential test harness vs vanilla | **Partial**: hand math (`HandGolden.java`) and world-gen stages 1-3 (`WorldgenRefDump.java`, `WorldgenRouterDump.java`). 1.21.11 jars and data-generator dumps present locally; world-gen harness implemented through stage 2 (see WORLDGEN_PARITY.md) | `reference/1.21.11/` | 49,786 exact comparisons, 0 mismatches | Critical |

See [IMPLEMENTATION_ROADMAP.md](IMPLEMENTATION_ROADMAP.md) for ordering.
