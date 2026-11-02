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

- Movement is a 60 Hz continuous model with custom time constants; vanilla is
  a 20 TPS discrete model (friction 0.6 × 0.91, 0.98 drag, 0.08 gravity,
  0.42 jump, 0.1 acceleration constants, sprint ×1.3, etc.). Trajectories
  will not match tick-for-tick.
- Water exit: the old clamber (`pos.y += 1.0`) was a teleport and has been
  replaced with vanilla's jump-out impulse (0.3 blocks/tick when blocked and
  the body raised 0.6 is free). Surrounding fluid forces (drag 0.8, 0.02
  acceleration, depth strider, sprint-swim) are still custom.
- No swimming pose/collision shape, no ladders/vines/slime/honey/ice
  behaviours, no step height 0.6 (only auto-jump 1.0 when enabled), no
  collision shapes below full-cube.

## World / content

- Terrain, biomes, caves, ores, structures: see [WORLDGEN_PARITY.md](WORLDGEN_PARITY.md).
- Only ~7 biomes, a small block/item set, 3 mob types, no redstone, fluids
  limited to bounded water, no lava physics, no Nether/End.
- Saves and LAN protocol are TerraCraft's own formats.
- Supplied `mcassets/` predates 1.21.11 (0 spear models vs 7 in the 1.21.11
  jar). Content scale: vanilla has 1,166 blocks / 29,671 block states, 1,505
  items, 157 entity types (from the 1.21.11 data generator); TerraCraft has
  tens of each.
