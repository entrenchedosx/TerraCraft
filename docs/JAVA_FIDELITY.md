# Java gameplay fidelity audit

**Reference target:** observable single-player Survival and Creative behavior
from Minecraft: Java Edition 26.3, the release named in the project brief.
The official [26.3 release notes](https://www.minecraft.net/en-us/article/minecraft-java-edition-26-3)
identify that edition and its release date. The numerical behavior targets
below are carried over from the fidelity brief and still need direct
in-game measurement against that release. TerraCraft does not copy
Mojang's source code or require its assets.

This page records what the current source does, not a claim of parity. The
code locations are included so later rounds can remeasure a rule after a
change.

## Reference targets

| Behavior | Target used for this pass |
| --- | --- |
| Gameplay rate | 20 simulation ticks per second; 0.05 s per tick |
| Player dimensions | Standing 0.6×1.8 blocks; crouched 0.6×1.5; swimming/crawling 0.6×0.6 |
| Standing eye height | About 1.62 blocks |
| Horizontal speed | Walk about 4.317 blocks/s; sprint about 5.612; sneak about 1.295 |
| Block reach | 4.5 blocks in Survival; 5 in Creative |
| Entity reach | 3 blocks for ordinary interaction |
| Day length | 24,000 ticks (20 real minutes at 20 TPS) |
 | Dropped item lifetime | 18,000 ticks (5 real minutes at 60 TPS) |
| GUI timing | Inventory/workbench continue single-player simulation; pause menu freezes it |

These values are useful targets, but a matching constant alone does not
prove matching behavior. Acceleration, collision, update ordering, input,
and rendering all affect what a player observes.

## Current implementation

### Simulation and movement

- `app_run()` schedules authoritative world updates through
  `SimulationClock` at 60 TPS (`src/game/simulation_clock.c`). It runs at most
  five catch-up ticks per rendered frame, preserves the fractional remainder,
  and logs any whole ticks dropped after a long stall. Rendering and chunk
  refresh run once per frame. Movement input is sampled per tick; discrete
  key/button presses are latched across down/up events until consumed. Quick
  jump taps use the same buffer. Time spanning a frozen-to-live state change
  is discarded so pause/death time does not advance on resume.
- Player collision still subdivides each 1/60 s world tick into 1/60 s
  `PLAYER_STEP_DT` steps (one-to-one at full rate), with its existing
  five-step cap. Render positions track the sim exactly each frame
  (no smoothing lag); the old damped chase that hid 20 Hz stair-steps
  was removed with the rate change.
- Player physics subdivides each authoritative world tick into smaller
  collision steps; those steps do not replace the 60 TPS world scheduler.
- Standing body dimensions are 0.6×1.8 blocks and eye height is 1.62
  (`player_init()` in `src/game/player.c`). Sneaking changes the speed and
  flag only. There are no crouch/swim/crawl dimensions or headroom checks.
  Water detection samples the standing collision body; movement slows in
  water and jump/sneak add upward/downward buoyancy, but it does not switch
  to Minecraft's swimming pose or compact collision shape.
- Walk and sprint speed values match the reference targets. The controller
  assigns horizontal velocity directly from input and applies drag only
  when input is released (`player_step()` in `src/game/player.c`); this
  skips the gradual acceleration and surface friction that shape the
  reference movement. Sneak uses 0.3× walk speed, or about 1.295 blocks/s.
- Jump velocity is 8.8 blocks/s and gravity is 32 blocks/s². The continuous
  ballistic apex from those values is about 1.21 blocks; the simulation
  uses discrete 1/60 s steps.
- Collision separates X, Z, and Y and slides along walls. Automatic
  player step-up and edge-safe sneak are not implemented.
- The camera follows the current player eye and rotation. It has no
  simulation-position interpolation, sprint FOV response, or implemented
  view bob.

### Targeting, mining, and placing

- Survival block reach is 4.5 and Creative block reach is 5
  (`src/game/survival.h`). Entity attack reach is now 3
  (`PLAYER_ATTACK_REACH` in `src/game/mob.h`).
- Melee uses yaw-aligned model bounds with a small margin for the rendered
  head and limb motion. A ray beginning inside a mob counts as a hit. Holding
  left mouse retries a mob attack when its tool cooldown ends; an aimed mob
  keeps the click from mining a block behind it (`mob_raycast()` and
  `app_try_attack()` in `src/game/mob.c` and `src/core/app.c`).
- Mining progress is accumulated in seconds and updated at the fixed world
  tick rate.
  Releasing, changing target, changing held item, or losing reach resets
  progress. Airborne and underwater mining penalties are not implemented.
- Placing blocks rejects solid targets and player overlap. Air, water, grass
  plants, flowers, and torches can be replaced. The placement API returns
  the replaced block; Survival resolves its usual drop, while Creative
  discards it. `test_interaction_break_place` covers replacement IDs and
  the decor drop definitions.
- The player collision box has no explicit pose or block-state system.

### Inventory and survival

- **Stack-limit audit (2026-10-06):** the registered item table contains 33
  items. Its six damageable tools (wood/stone pickaxes, axes, and shovels) and
  its damageable bow have `max_stack = 1`. The other 26 registered items,
  including grass plants and flowers, have `max_stack = 64`; none of the
  implemented items uses the 16-item limit. This matches the Java Edition
  item-stack rule that counts are capped by each item's own maximum, and its
  item components disallow a stack limit above 1 together with durability
  (`minecraft:max_damage`). See Mojang's [Java Edition 1.20.5 item-component
  notes](https://www.minecraft.net/en-us/article/minecraft-java-edition-1-20-5)
  and [24w09a stack-format notes](https://feedback.minecraft.net/hc/en-us/articles/24531969592077-Minecraft-Java-Edition-Snapshot-24w09a).
  `test_item_stackability_contract` exhaustively checks all registered ID
  ranges and runs every singleton through merge, add, inventory insertion,
  one-item splitting, and inventory sanitization (the save-load clamp).
  Additional Minecraft items such as swords, hoes, armor, shields, buckets,
  and potions are not registered yet, so their limits are outside this audit.
- Inventory and workbench screens continue advancing the world at 60 TPS
  with player movement and actions disabled. Pause and death freeze it
  (`game_state_ticks_world()` in `src/game/game_state.c`). Loading continues
  chunk streaming without advancing gameplay ticks.
- Hunger and exhaustion exist, but there is no saturation value or saturation
  refill path. Exhaustion directly removes hunger. Sprint input is not yet
  blocked at low hunger, and regeneration is a simplified fixed timer.
- Survival HUD vitals draw ten pixel-art hearts and ten food icons. Each icon
  represents two health/food points and renders full, half, or empty; the
  underlying values remain continuous. These shapes come from small pixel
  masks rather than Minecraft's native GUI atlas.
- Dropped items age for 300 seconds and wait 0.5 seconds before pickup
  (`src/game/entity.h`). Their lifetime now spans 6,000 world ticks. Item
  motion uses small substeps and gravity; item stack merging is not
  implemented.
- The day cycle is 1,200 seconds (`TIME_DEFAULT_SPEED` in
  `src/game/time_system.h`), equivalent to 72,000 ticks at 60 TPS. It advances
  through the fixed world tick and freezes with pause and death.

### Rendering, lighting, and fluids

- The atlas combines procedural pixels with optional owner-local or user
  pack tiles. All 33 registered item types map to a named atlas tile. A
  confirmed custom-pack path alias prevented pack discovery and item texture
  overrides; the path builder and live settings reload are fixed, with
  coverage in `test_atlas_resource_pack_paths`. The user-reported visual
  issue still needs a screenshot or the affected item/screen details to
  confirm that this was the observed cause.
- The cow uses a 6-cuboid model with face UVs from the 64×64 skin's box nets.
  Mob parts rotate around the creature root, and render positions ease toward
  simulation positions each frame so movement and targeting stay aligned. The
  first-person view draws a Steve-sourced sleeve and hand (converter crops,
  procedural fallback), a held-item sprite or block cube, and a controller
  with idle/walk/sprint/air/land/attack/use/hurt/sneak states. F5 switches
  to a third-person chase camera rendering the articulated Steve body
  (verified in-game by screenshot).
- Lighting uses a global day/night value, a per-column occlusion heuristic,
  and ambient occlusion. It has no separate propagated sky-light and
  block-light channels, cross-chunk light queue, or torch emission.
- New worlds use terrain profile 2: warped continental noise, broad rolling
  terrain, ridge-shaped highlands, and carved meandering river valleys. The
  surface is still a 2D column field rather than Minecraft's full 3D density
  system. Worlds with no `terrain_version` key stay on profile 1, so newly
  generated chunks do not create seams in old saves.
- Water has a source, seven horizontal flow-depth states, and a falling state.
  It attempts downward flow first, then spreads horizontally up to seven
  cells, can refill between two sources over a solid floor, and retracts
  orphaned flow. Only loaded chunks are simulated; the deduplicated work
  queue is bounded and uses an incremental recovery scan at capacity. Flow
  meshes use partial heights, and newly loaded chunk borders wake adjacent
  water. Chunk format v2 preserves the new IDs while v1 chunks retain their
  legacy interpretation. This remains simplified: waterlogged blocks,
  underwater breathing/drowning, a swim pose, and current-driven movement are
  absent.

## Confirmed fixes in this audit

- Entity interaction reach changed from 4.5 to 3 blocks.
- Grass plants, flowers, and torches are replaceable when placing a block;
  Survival spawns the replaced decor's usual drop.
- Item registry-to-atlas mapping and nonempty procedural fallback pixels
  now have explicit test coverage.
- Resource-pack documentation now lists all 46 current tile names and the
  current sound event stems.
- A fixed 60 TPS simulation clock now drives the world and logs dropped
  catch-up ticks; inventory/workbench continue world simulation while pause
  and death freeze it.
- Custom resource-pack discovery and tile loading now build paths through
  non-aliased buffers, and selecting a pack reloads the live atlas.
- Key and mouse press edges now survive a release until consumed by a world
  tick or UI frame; focus loss clears queued and held input.

## Known deviations

1. The simulation clock is fixed at 60 TPS so rendered motion advances
   every frame; render positions track the sim exactly (no smoothing
   lag). Discrete toggles and hotbar changes still process after
   each event drain rather than inside a world tick.
2. Direct-velocity movement, no player pose geometry, and no edge-safe
   sneaking or step-up behavior.
3. Hunger has no saturation, and sprint hunger gating is absent.
4. Lighting has no propagated sky/block channels. Fluid timing and player
   buoyancy are simplified, and underwater breathing/drowning are absent.
5. A hands-on fidelity playtest and visual comparison against a live 26.3
   reference remain outstanding.

The phase is not complete until the measurable behavior is tested and an
experienced player can complete the ordinary movement, mining, placement,
inventory, and Survival loop without immediately noticing mismatches. See
[Fidelity Bugs](FIDELITY_BUGS.md) for the persistent issue log and manual
checklist.
