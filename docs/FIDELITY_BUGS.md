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
- **Root cause:** A confirmed custom-pack path bug passed one buffer as both
  the destination and an input to `path_join()`. Since `path_join()` clears
  its destination first, the generated path became just `tiles/`; pack
  discovery and tile overrides therefore failed. Changing packs in Settings
  also did not reload the live atlas. No equivalent atlas UV or item-ID
  mismatch was found for `Default`.
- **Fix:** Pack paths now use a separate intermediate buffer, discovery uses
  the same checked path helper, and changing the selected pack reloads the
  atlas immediately. The static mapping and BMP checks remain intact.
- **Tests added:** `test_atlas_resource_pack_paths` verifies the pack path,
  discovery, and application of a tile with an item icon index. The existing
  `test_item_texture_mapping` covers all 32 registered items and fallback
  pixels.
- **Human verified?:** No. The custom-pack cause is fixed, but the user's
  affected item, screen path, and active pack are still unknown; visual
  confirmation is needed before closing the report.

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
- [ ] From several yaw angles, hit a cow at its body and muzzle, hold attack
  through cooldown, confirm a mob prevents mining the block behind it, and
  inspect the first-person hand/swing with empty and occupied hotbar slots.
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

## TC-FID-004 — Gameplay lacks position interpolation (open)

- **Subsystem:** Simulation timing and input.
- **Observed TerraCraft behavior:** The app now advances world systems at 20
  TPS with latched input edges, but draws the latest authoritative positions
  directly after each render frame.
- **Expected Java-like behavior:** Rendered positions interpolate between
  adjacent simulation ticks without changing collision, raycast, or save
  state.
- **Reproduction:** Observe the player, dropped items, mobs, and projectiles
  at high render rates while the simulation runs at 20 TPS.
- **Root cause:** Position snapshots and interpolation have not been added
  to the rendering boundary.
- **Fix:** None yet.
- **Test added:** Clock tests cover tick counts at 30/60/144/240 Hz and
  catch-up accounting; interpolation tests remain to be added.
- **Human verified?:** No.

## TC-FID-005 — Movement lacks acceleration, poses, and edge-safe sneak (implemented; playtest open)

- **Subsystem:** Player movement and collision.
- **Observed TerraCraft behavior:** Input sets horizontal velocity directly.
  Sneaking uses 0.3x speed, crouched dimensions 0.6x1.5 with eye 1.27
  (standing 0.6x1.8 eye 1.62); releasing sneak stands only with headroom,
  otherwise the crouch persists. Sneak holds unsupported edges via per-axis
  clips plus a diagonal corner-cut guard (1.5-era moveEntity rule; 1-block
  descents hold until sneak is released — verified headless, needs
  playtest). First-person view bobbing is distance-driven from walk_phase
  (gentle lateral dip, `view_bobbing` default ON, toggle in Settings). A
  1-block auto step-up exists for dry ledges and shores, gated on
  `auto_jump` (default OFF, MC-faithful; toggle in Settings).
- **Expected Java-like behavior:** Movement builds and loses momentum; poses
  change body dimensions only when there is room; sneaking prevents walking
  off a supported edge.
- **Reproduction:** Walk, release movement, crouch under a low ceiling, and
  sneak toward and beyond an unsupported edge (straight, corner diagonal,
  and narrow 1-wide bridge); walk to feel the first-person bob.
- **Root cause:** The controller is an arcade-style FPS model; pose and
  edge-support systems are now added, acceleration/friction remain simplified.
- **Fix:** `player_has_support` per-axis clips + `player_has_support_at`
  corner guard in `src/game/player.c`; `player_can_stand` headroom gate with
  `PLAYER_SNEAK_HEIGHT/EYE` in `src/game/player.h`; `player_view_bob_offset`
  + `app_position_camera` bob in `src/core/app.c`; `view_bobbing` setting in
  `src/core/settings.*` with Settings toggle in `src/ui/screens.c`.
- **Test added:** `test_physics_sneak_edge` (straight + 1-block hold),
  `test_physics_sneak_edge_corner` (outer L-corner diagonal, narrow bridge
  along/sideways + control fall), `test_physics_sneak_posture` (1.5/1.27 vs
  1.8/1.62, forced crouch, fractional 65.4/67 headroom), `test_player_view_bob`
  (exact phases, walk builds/sneak calmer/still+fly decay), settings
  default/parse/roundtrip for `view_bobbing`. Debug+Release 207/207 pass.
- **Human verified?:** No.

## TC-FID-006 — Inventory and workbench world-tick behavior (implemented; playtest open)

- **Subsystem:** GUI pause semantics.
- **Observed TerraCraft behavior:** Inventory and crafting now advance the
  world; player movement and actions are disabled while those screens are
  open.
- **Expected Java-like behavior:** Inventory/workbench keep single-player
  simulation running; the pause menu stops it.
- **Reproduction:** Leave a dropped item or mob active, open E or a workbench,
  wait, then compare item age, world time, and mob position. Repeat with the
  pause menu.
- **Root cause:** Resolved in this round by adding an explicit world-tick
  state policy and routing fixed ticks through inventory and crafting.
- **Fix:** Implemented in `game_state_ticks_world()` and the app update loop.
- **Test added:** State tests cover which states tick; live world-time and
  entity-aging integration tests remain to be added.
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

## TC-FID-009 — Cow appearance, melee targeting, and first-person swing (playtest open)

- **Subsystem:** Mob skin/model, melee targeting, and first-person rendering.
- **Observed TerraCraft behavior:** The supplied cow screenshot showed hide
  patches stretched across the model, and the user reports that clicking the
  cow does not hit it. The latest first-person screenshot shows an oversized,
  flat-colored upright arm covering much of the lower-right view.
- **Expected behavior:** Cow face textures follow their own skin regions;
  visible body parts can be targeted at ordinary entity reach; left-click
  produces a readable hand swing.
- **Root cause:** The cow reused unrelated rectangles on several cuboid
  faces, its muzzle exceeded the old square target box, a ray beginning inside
  a target was rejected, and attacks were only attempted on the press edge.
  The first-person arm reused a tiny atlas swatch on each cuboid face and sat
  unusually close to the camera, which made it look like a large flat pole.
- **Fix:** Replaced cow face UVs and model proportions, aligned target bounds
  with yawed model parts, accepted inside-box rays, and retried held attacks
  after cooldown. The first-person arm now uses the loaded player's per-face
  right-arm skin, has a smaller lower-right rest pose and inward cant, renders
  its own surfaces with depth testing after clearing world depth, and swings
  toward the camera. Its held item keeps the atlas texture and meets the palm;
  pose offsets move the arm vertices with the swing pivot. A procedural atlas
  arm remains available when the skin asset is absent.
- **Review fix:** Held placeable blocks use the block's per-face atlas mapping
  so grass, wood, and workbench faces retain their correct top, bottom, and
  side textures in the first-person view.
- **Tests added:** `test_mob_raycast`, `test_mob_models`,
  `test_player_swing_animation`, `test_atlas_pixels`, and
  `test_camera_viewmodel_fov_compensation` cover the new paths.
- **Human verified?:** Not yet. Check the new lower-right arm and its skin in a
  live first-person game with empty hands, a block, and a flower; verify the
  grip and swing at low and high FOV before closing this report.

## TC-FID-010 — Flowers and grass plants stack vertically (source fix; playtest open)

- **Subsystem:** Block ray targeting and placement.
- **Observed TerraCraft behavior:** The user can place flowers or grass plants
  directly on top of existing flowers/grass plants.
- **Expected behavior:** Small plants occupy one supported cell. Clicking an
  existing plant replaces that cell; flowers and grass plants require grass,
  dirt, or snow beneath them (matching existing snow-biome vegetation).
- **Root cause:** Placement always chose `hit.block + hit.normal`, even when
  the ray hit non-solid decor, and planting had no soil-support validation.
- **Fix:** Clicked cross-sprite decor now supplies its own replacement cell;
  flower/grass placement checks the support block below before writing.
- **Tests added:** `test_interaction_break_place` checks plant replacement,
  no vertical stacking, rejected stone support, and valid grass/dirt/snow
  support.
- **Human verified?:** Not yet. Confirm normal right-click placement and
  replacement behavior in a running game before closing this report.
