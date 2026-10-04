# Fidelity bug log

Keep resolved entries as regression history. Record measured behavior and
the test evidence; do not mark a visual issue resolved from a code reading
alone.

## TC-FID-001 — Item textures reported broken (open)

- **Subsystem:** Item atlas, inventory UI, dropped-item rendering.
- **Observed TerraCraft behavior:** The user reports that textures are broken
  for some items. Which item names and which screen or world rendering path
  are affected is not yet known.
- **Expected Java-like behavior:** Each item should show its own recognizable
  icon or dropped sprite, with transparent pixels clear and no neighboring
  atlas tile bleeding into the image.
- **Reproduction:** Open the inventory or Creative catalogue and inspect the
  affected item. Also drop it in the world. The affected item names, active
  resource pack, and a screenshot will make the report reproducible.
- **Root cause:** Not confirmed. Static inspection found the current
  `ItemInfo.tile` mappings, 46-entry file table, atlas UV calculation, BMP
  row conversion, and UI/world draw paths consistent. Sampled local BMP art
  files were nonempty. This does not verify the rendered result.
- **Fix:** Added a complete registry-to-tile and procedural-pixel test. A
  rendered-only fix remains pending reproduction evidence.
- **Test added:** `test_item_texture_mapping` checks all 32 registered items,
  expected tile IDs, file stems, and visible fallback pixels.
- **Human verified?:** No; a screenshot or identified item/path is still
  needed.

## Manual playtest checklist

Record TerraCraft version/build, display size, mode, seed, resource-pack
selection, and Java Edition reference version before comparing results.

- [ ] Launch the title screen, create a Survival world, load it, pause, resume,
  save and quit, then reload the same world.
- [ ] Walk, sprint, sneak, jump, collide with a wall, and test movement near
  an unsupported edge and under a low ceiling.
- [ ] Mine stone, dirt, and wood with an empty hand and the matching tools;
  check drops, mining progress, durability, and break feedback.
- [ ] Place blocks against a face, replace a grass plant, flower, and torch,
  and verify placement is blocked when the new block would overlap the player.
- [ ] Open inventory and a workbench while a dropped item or creature is
  active; observe world time and entity motion. Compare with the pause menu.
- [ ] Craft a recipe, move/split stacks, close each screen, and verify items
  return to the inventory.
- [ ] Check health, hunger, eating, damage, death, respawn, and survival
  drops; repeat Creative flight, free placement, and instant mining.
- [ ] Fight a passive creature, a melee hostile, and a skeleton; fire an
  arrow, then save and reload.
- [ ] Inspect all available item icons in the hotbar, inventory, Creative
  catalogue, cursor, and dropped world sprites. Repeat with `Default` and
  the selected resource pack; capture each reported texture failure.
- [ ] Check day/night appearance, torches in dark areas, water, footsteps,
  block sounds, creature sounds, and interface feedback.

For each mismatch, add the steps, expected result, observed result, and a
screenshot to the relevant issue above. Mark an issue resolved only after
the affected behavior has been reproduced and checked in a running build.

## TC-FID-002 — Entity reach too long (resolved)

- **Subsystem:** Player targeting and melee.
- **Observed TerraCraft behavior:** `PLAYER_ATTACK_REACH` was 4.5 blocks.
- **Expected Java-like behavior:** Ordinary entity interaction reach is 3
  blocks for this pass.
- **Reproduction:** Aim at a creature between 3 and 4.5 blocks away and try
  to strike it.
- **Root cause:** The block reach value had also been used for entity aim.
- **Fix:** Set the shared player entity reach to 3 blocks.
- **Test added:** `test_mob_raycast` asserts the configured reach; existing
  raycast tests continue to cover hit and miss behavior.
- **Human verified?:** No.

## TC-FID-003 — Decor blocks block placement (resolved)

- **Subsystem:** Block placement.
- **Observed TerraCraft behavior:** Placement rejected an occupied cell even
  when it contained a grass plant, flower, or torch.
- **Expected Java-like behavior:** Small vegetation gives way when a block is
  placed into its cell.
- **Reproduction:** Aim at the adjacent plant, flower, or torch cell and
  place a solid block.
- **Root cause:** The placement policy treated only air and water as
  replaceable.
- **Fix:** Grass plants, flowers, and torches are replaceable. Placement
  returns the replaced block; Survival spawns its usual drop and Creative
  discards it.
- **Test added:** `test_interaction_break_place` replaces each decor type,
  checks the replaced ID and configured Survival drop, and verifies the new
  block.
- **Human verified?:** No.

## TC-FID-004 — Gameplay does not use a 20 TPS simulation (open)

- **Subsystem:** Simulation timing and input.
- **Observed TerraCraft behavior:** Most gameplay updates run once per
  rendered frame; player physics uses 1/60-second substeps.
- **Expected Java-like behavior:** Authoritative gameplay advances at 20 TPS,
  with input edges retained and presentation interpolated between ticks.
- **Reproduction:** Record player movement and item/day timers at 30, 60, 144,
  and 240 render FPS for equal simulated durations.
- **Root cause:** Gameplay updates are split across a render-frame app loop
  and a player-only fixed substep.
- **Fix:** None yet.
- **Test added:** No frame-rate-independence test yet.
- **Human verified?:** No.

## TC-FID-005 — Movement lacks acceleration, poses, and edge-safe sneak (open)

- **Subsystem:** Player movement and collision.
- **Observed TerraCraft behavior:** Input sets horizontal velocity directly;
  sneaking changes speed but not collision dimensions. Automatic player
  step-up and safe edge movement are absent.
- **Expected Java-like behavior:** Movement builds and loses momentum; poses
  change body dimensions only when there is room; sneaking prevents walking
  off a supported edge.
- **Reproduction:** Walk, release movement, crouch under a low ceiling, and
  sneak toward and beyond an unsupported edge.
- **Root cause:** The controller is an arcade-style FPS model without player
  pose or edge-support systems.
- **Fix:** None yet.
- **Test added:** Existing wall/jump tests only; acceptance tests remain to be
  added.
- **Human verified?:** No.

## TC-FID-006 — Inventory and workbench freeze the world (open)

- **Subsystem:** GUI pause semantics.
- **Observed TerraCraft behavior:** Inventory and crafting states stop world
  simulation.
- **Expected Java-like behavior:** Inventory/workbench keep single-player
  simulation running; the pause menu stops it.
- **Reproduction:** Leave a dropped item or mob active, open E or a workbench,
  wait, then compare item age, world time, and mob position. Repeat with the
  pause menu.
- **Root cause:** `game_state_is_live()` currently returns true only for
  `PLAYING` and `LOADING`.
- **Fix:** None yet.
- **Test added:** State tests cover current pause behavior, but not elapsed
  world time and entity aging through inventory.
- **Human verified?:** No.

## TC-FID-007 — Survival state omits saturation and sprint gating (open)

- **Subsystem:** Hunger and regeneration.
- **Observed TerraCraft behavior:** Exhaustion subtracts hunger directly;
  there is no saturation value, and sprint input is not gated at low hunger.
- **Expected Java-like behavior:** Saturation absorbs exhaustion before
  hunger, regeneration follows saturation/hunger conditions, and sprint is
  blocked below the reference hunger threshold.
- **Reproduction:** Compare exhaustion, hunger, regeneration, and sprint
  availability at multiple hunger and saturation levels.
- **Root cause:** Survival is a simplified model without saturation state.
- **Fix:** None yet.
- **Test added:** Existing hunger/exhaustion tests cover the simplified
  policy; target-model tests remain to be added.
- **Human verified?:** No.

## TC-FID-008 — Lighting and water behavior remain simplified (open)

- **Subsystem:** World lighting and fluids.
- **Observed TerraCraft behavior:** Lighting uses global sun intensity, a
  column-occlusion heuristic, and AO. Water is static and has no swim or
  oxygen state.
- **Expected Java-like behavior:** Separate sky/block light responds to
  propagation and removal; water flows and supports the expected player
  states.
- **Reproduction:** Observe a torch in a cave and around a corner, remove it,
  then compare water movement and player behavior in water.
- **Root cause:** No per-voxel light channels, update queue, fluid simulation,
  or swimming state exists.
- **Fix:** None yet.
- **Test added:** Current tests cover AO, global time lighting, and static
  water ray behavior; propagation/flow tests remain to be added.
- **Human verified?:** No.
