# Player physics parity (Java 1.21.11)

Status: **core ported, verified against analytic vanilla figures; no live
differential harness yet.** Code: `src/game/vphys.[ch]`, wired into
`src/game/player.c`. Tests: `tests/test_vphys.c` and the ported
`tests/test_physics.c`.

## Method

The rules were read from the 1.21.11 client bytecode (`javap -c` on the
deobfuscated jar with Mojang mappings), not from memory. Methods followed:

- `LivingEntity.aiStep`, `travel`, `travelInAir`, `travelInWater`,
  `jumpFromGround`, `jumpInLiquid`, `jumpOutOfFluid`-style logic
- `Player.travel`, `Player.updatePlayerPose`
- `LocalPlayer.aiStep` (input scaling, sprint start/stop rules)
- `Entity.move`, `collide`, `collideWithShapes`, `maybeBackOffFromEdge`,
  `checkFallDamage` / fall distance bookkeeping, `updateFluidHeightAndDoFluidPushing`
- `Mth.sin` / `Mth.cos` (65536-entry table, factor 10430.378350470453)

## Model

- State is double precision, velocity in blocks per tick, one step per
  1/20 s (`VPHYS_TICK_SECONDS = 0.05`). `player_update` accumulates frame time
  and runs whole ticks; `player_interpolate` blends the previous and current
  tick positions (and the camera eye height) for rendering.
- Input: forward/left scaled by 0.98, sneak ×0.3, diagonal fix
  (`modifyInputSpeedForSquareMovement`), sprint needs forward ≥ 0.8 and is
  cancelled by sneaking or a horizontal collision.
- Air: friction `0.6 × 0.91`, ground acceleration `0.1 × (0.216 / f³)`, air
  acceleration 0.02 (0.026 sprinting), gravity 0.08, vertical drag 0.98.
- Jump: 0.42 impulse, +0.2 horizontal when sprinting, 10-tick delay.
- Water: drag 0.8, 0.02 acceleration, gravity/16 sink, swim-up +0.04 per tick;
  `jumpOutOfFluid` impulse of 0.3 only when the body shifted up 0.6 is free of
  collision and liquid — this is the vanilla replacement of the old teleport.
- Collision: Y first, then the smaller horizontal axis, step height 0.6,
  sneak edge back-off, fall distance reset in water, landing report.
- Pose: standing 0.6 × 1.8 (eye 1.62), crouching 1.5 (eye 1.27), swimming
  0.6 × 0.6; the camera eye eases toward the pose eye at 0.5 per tick.

## Verification

| Check | Expected | Result |
|---|---|---|
| Walk pace | 4.317 blocks/s | Pass (±0.01) |
| Sprint pace | 5.612 blocks/s | Pass (±0.01) |
| Sneak pace | 1.295 blocks/s | Pass (±0.01) |
| Jump apex | 1.2522 blocks | Pass (±0.0005) |
| Terminal velocity | -3.92 blocks/tick | Pass (±0.005) |
| Fall distance on landing | 10 blocks from 10 up | Pass (±0.05) |
| Water sink speed | -0.025 blocks/tick | Pass (±0.003) |
| Water exit | impulse < 0.45 blocks/tick, never a position jump | Pass |
| Diagonal scaling | 1/0.98 | Pass |
| Sneak edge / wall stop | stays on edge; stops at 0.3 from wall | Pass |

Those figures are the long-published vanilla values and are also derivable from
the bytecode constants; they are not output of a running 1.21.11 client.

## Not verified / not ported

See [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md#physics). The next verification
step is a Java reflection dumper (like the worldgen ones in `tools/reference/`)
that drives a real `Player` through scripted inputs on a flat world and emits
per-tick position/velocity fixtures to compare bit-for-bit.
