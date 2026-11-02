# First-person animation parity

Implementation: `src/game/hand_view.[ch]` (state + matrices, pure C, no GL),
drawing in `renderer_draw_player_arm()` (`src/render/renderer.c`), wiring in
`src/core/app.c`, view bob in `camera_set_view_bob()`.

## Architecture

- **Simulation** runs on the vanilla 20 Hz clock inside `HandView`. The app
  feeds each 60 Hz play step; the module accumulates 0.05 s and runs whole
  ticks. Tick order follows `Minecraft.tick`: `ItemInHandRenderer.tick`
  (equip) first, then the entity tick (swing time, walkDist, bob, sway,
  `attackStrengthTicker++`).
- **Presentation** uses `partial = (tick accumulator + unsimulated frame time) / 0.05`,
  so motion is smooth at any frame rate while remaining tick-exact.
  Rendered through `hand_view_frame()`, which composes the PoseStack chain.
- Sine/cosine use Java's 65536-entry table (`Mth.sin/cos`, factor
  `10430.378350470453`) so first-person angles quantise like the reference.

## What is ported

| Behaviour | Source in reference | Status |
|---|---|---|
| Empty-hand arm (`renderPlayerArm`): translate/rotate chain, swing curves (√s, s², sin) | `FirstPersonHandsAndItemsRenderer.renderPlayerArm` | Golden-tested |
| Arm ModelPart: origin (-5,2,0)/16, `zRot = 0.1`, wide box (-3,-2,-2,4,12,4), sleeve +0.25 | `AvatarRenderer.renderHand`, `PlayerModel` | UV/vertex tests; slim arm missing |
| Item held, not in use (`applyItemArmTransform` + attack transform + swing translate) | same | Golden-tested |
| Eat/drink (`applyEatTransform`) | same | Golden-tested |
| Bow draw branch | same | Ported, **not golden-tested** |
| Item display transforms (firstperson_righthand) for sprite and block models | supplied `models/item/generated.json`, `handheld.json`, `block/block.json` | Golden-tested (matrix) |
| Swing: `swing()` restart rule, 6-tick duration, `getAttackAnim` interpolation | `LivingEntity` | Unit-tested |
| Equip/unequip: 0.4/tick clamp, <0.1 swap, attack-strength³ dip, hands-busy | `ItemInHandRenderer.tick` (1.21.11 form from knowledge; 26.3 differs) | Unit-tested |
| Turn sway: xBob/yBob chase 0.5/tick, ×0.1 rotation | `LocalPlayer.aiStep`, `renderHandsWithItems` | Constants verified; yaw unwrapped to avoid seam spikes |
| View bob (`bobView`): translate + roll 3° + pitch 5°, applied to **both** world view and hand | `GameRenderer.bobView` | Golden-tested; replaces the old custom eye-offset bob |
| Only the item is drawn when something is held; arm only for empty hand | `submitArmWithItem` | Implemented |

## Not yet ported

Offhand/left-hand, slim arm, spear (1.21.11 `swing_animation`/kinetic weapon
animations), crossbow, shield/block, trident, brush, maps, per-item display
overrides, hand FOV modifier (sprint/speed FOV), `bobHurt` damage tilt,
haste/mining-fatigue swing duration, sneaking/swimming hand differences (the
vanilla hand is the same pose; only bob changes with swimming), per-pixel
extruded sprite rim, vanilla hand lighting (uses TerraCraft's vertex light).

## Acceptance status

- Math: matches reference matrices within 1e-4 (49 golden cases).
- Timing: 20 Hz tick behaviour unit-tested and frame-rate independent
  (30/60/144/240 Hz produce identical tick state).
- **Pixels: not yet compared.** The first-person arm has not been checked
  against a 1.21.11 screenshot at equal FOV/resolution/skin, and the
  rewritten drawing path was not eyeballed in a running window this session.
  Treat on-screen placement as unverified until that comparison is done.

The legacy `PlayerAnim`/`player_viewmodel_*` code remains for the
third-person body and existing tests; it no longer drives the first-person arm.
