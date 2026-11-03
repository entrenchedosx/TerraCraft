# Known differences from Minecraft Java 1.21.11

Complements the matrix. Each entry: what differs, why, how it would be fixed.

## First-person hand (after the 2026-11-02 pass)

- **Pixels unverified.** Matrices match reference math; no frame comparison.
- Held-item sprites are a flat 1/16-thick plane; vanilla extrudes every
  non-transparent pixel into a 3D rim.
- No offhand/left hand, no slim arm, no per-item overrides (bow pose in
  hand model, shield, crossbow, trident, spear, brush, map).
- Hand is drawn at 70° regardless of FOV via an XY rescale; vanilla also
  applies a sprint/speed FOV modifier to the hand projection.
- Lighting uses TerraCraft's vertex light, not vanilla's `lightCoords` + entity
  diffuse.
- The swap-dip rate uses TerraCraft's per-tool cooldown seconds ×20 as the
  attack delay; vanilla uses 20 / attack-speed (bare hand 5 ticks; we use 8).
  Tool cooldowns should be re-derived from item attribute modifiers.
- Item identity for the swap check is the item type only. Vanilla also
  suppresses the dip via `ItemModelResolver.shouldPlaySwapAnimation` for some
  items; not ported.
- Camera bob now follows vanilla `bobView`, but `walkDist`/`bob` are derived
  from TerraCraft's displacement per 20 Hz tick, not from vanilla
  `deltaMovement` after friction.

## Physics

Walking, swimming and falling run the vanilla 20 TPS model (see
[PHYSICS_PARITY.md](PHYSICS_PARITY.md)); creative flight still uses the old
60 Hz velocity model. Not ported or approximated:

- Water current pushing (flowing water), lava, bubble columns, cobwebs,
  berry bushes and other `makeStuckInBlock` blocks.
- Ladders / vines climbing, scaffolding, honey, slime, ice and soul-sand
  friction/jump factors (all blocks use friction 0.6 and factor 1.0).
- `moveTowardsClosestSpace` (ejection from embedded blocks) and
  `minorHorizontalCollision` are approximated or absent.
- Auto-jump is removed from the walking path (vanilla option, off by default).
- Swimming pose exists in the state but sprint-swimming is not driven by
  input; elytra, riding, spectator and creative flight differences remain.
- Collision uses full unit cubes only (no slabs, stairs, fences, carpets).
- No differential harness against real 1.21.11 physics: expected values are
  derived from client bytecode and the well-known published figures.

## World / content

- Terrain, biomes, caves, ores, structures: see [WORLDGEN_PARITY.md](WORLDGEN_PARITY.md).
- Only ~7 biomes, a small block/item set, 3 mob types, no redstone, fluids
  limited to bounded water, no lava physics, no Nether/End.
- Saves and LAN protocol are TerraCraft's own formats.
- Supplied `mcassets/` predates 1.21.11 (0 spear models vs 7 in the 1.21.11
  jar). Content scale: vanilla has 1,166 blocks / 29,671 block states, 1,505
  items, 157 entity types (from the 1.21.11 data generator); TerraCraft has
  tens of each.

## World generation core (not wired into the game yet)

- The vanilla-exact generator (`jrandom` ... `jfeature`, `jstructure`) runs only in the differential harness and in
  `worldgen_diff`; the game still uses its own terrain profiles.
- The core tracks block *names*, not block states (no `axis`, `half`, `distance`, `waterlogged`, ...) and caps the registry at
  255 names; leaf decay distances, double-plant halves and similar properties are therefore absent.
- Features implemented and verified: ores, 22 vegetation patch features, structure-set placement. Trees and most other feature
  types, structure piece generation, strongholds (concentric rings), Nether and End are not implemented.
- See [WORLDGEN_PARITY.md](WORLDGEN_PARITY.md) for the per-stage evidence.
