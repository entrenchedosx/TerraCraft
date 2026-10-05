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
| Dropped item lifetime | 6,000 ticks (5 real minutes at 20 TPS) |
| GUI timing | Inventory/workbench continue single-player simulation; pause menu freezes it |

These values are useful targets, but a matching constant alone does not
prove matching behavior. Acceleration, collision, update ordering, input,
and rendering all affect what a player observes.

## Current implementation

### Simulation and movement

- `app_run()` schedules authoritative world updates through
  `SimulationClock` at 20 TPS (`src/game/simulation_clock.c`). It runs at most
  five catch-up ticks per rendered frame, preserves the fractional remainder,
  and logs any whole ticks dropped after a long stall. Rendering and chunk
  refresh run once per frame. Movement input is sampled per tick; discrete
  key/button presses are latched across down/up events until consumed. Quick
  jump taps use the same buffer. Time spanning a frozen-to-live state change
  is discarded so pause/death time does not advance on resume.
- Player collision still subdivides each 0.05 s world tick into 1/60 s
  `PLAYER_STEP_DT` steps, with its existing five-step cap. This preserves the
  current collision integration while the surrounding world becomes ticked.
  Render interpolation between authoritative positions is not implemented.
- Player physics subdivides each authoritative world tick into smaller
  collision steps; those steps do not replace the 20 TPS world scheduler.
- Standing body dimensions are 0.6×1.8 blocks and eye height is 1.62
  (`player_init()` in `src/game/player.c`). Sneaking changes the speed and
  flag only. There are no crouch/swim/crawl dimensions or headroom checks.
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

- Inventory and workbench screens continue advancing the world at 20 TPS
  with player movement and actions disabled. Pause and death freeze it
  (`game_state_ticks_world()` in `src/game/game_state.c`). Loading continues
  chunk streaming without advancing gameplay ticks.
- Hunger and exhaustion exist, but there is no saturation value or saturation
  refill path. Exhaustion directly removes hunger. Sprint input is not yet
  blocked at low hunger, and regeneration is a simplified fixed timer.
- Dropped items age for 300 seconds and wait 0.5 seconds before pickup
  (`src/game/entity.h`). Their lifetime now spans 6,000 world ticks. Item
  motion uses small substeps and gravity; item stack merging is not
  implemented.
- The day cycle is 1,200 seconds (`TIME_DEFAULT_SPEED` in
  `src/game/time_system.h`), equivalent to 24,000 ticks at 20 TPS. It advances
  through the fixed world tick and freezes with pause and death.

### Rendering, lighting, and fluids

- The atlas combines procedural pixels with optional owner-local or user
  pack tiles. All 32 registered item types map to a named atlas tile. A
  confirmed custom-pack path alias prevented pack discovery and item texture
  overrides; the path builder and live settings reload are fixed, with
  coverage in `test_atlas_resource_pack_paths`. The user-reported visual
  issue still needs a screenshot or the affected item/screen details to
  confirm that this was the observed cause.
- The cow uses a 6-cuboid model with face UVs from the 64×64 skin's box nets.
  Mob parts rotate around the creature root, and render positions ease toward
  simulation positions each frame so movement and targeting stay aligned. The
  first-person view draws an original procedural sleeve and hand, a held-item
  sprite or block cube, and a short shoulder-driven swing. This is a
  first-person viewmodel; a third-person player body is not implemented.
- Lighting uses a global day/night value, a per-column occlusion heuristic,
  and ambient occlusion. It has no separate propagated sky-light and
  block-light channels, cross-chunk light queue, or torch emission.
- Water is a static non-solid block. Fluid flow, swimming/crawling, and
  oxygen are not implemented.

## Confirmed fixes in this audit

- Entity interaction reach changed from 4.5 to 3 blocks.
- Grass plants, flowers, and torches are replaceable when placing a block;
  Survival spawns the replaced decor's usual drop.
- Item registry-to-atlas mapping and nonempty procedural fallback pixels
  now have explicit test coverage.
- Resource-pack documentation now lists all 46 current tile names and the
  current sound event stems.
- A fixed 20 TPS simulation clock now drives the world and logs dropped
  catch-up ticks; inventory/workbench continue world simulation while pause
  and death freeze it.
- Custom resource-pack discovery and tile loading now build paths through
  non-aliased buffers, and selecting a pack reloads the live atlas.
- Key and mouse press edges now survive a release until consumed by a world
  tick or UI frame; focus loss clears queued and held input.

## Known deviations

1. The simulation clock is fixed at 20 TPS, but gameplay movement is not
   yet frame-rate independence tested, and rendering lacks position
   interpolation. Discrete toggles and hotbar changes still process after
   each event drain rather than inside a world tick.
2. Direct-velocity movement, no player pose geometry, and no edge-safe
   sneaking or step-up behavior.
3. Hunger has no saturation, and sprint hunger gating is absent.
4. Lighting has no propagated sky/block channels; water does not flow.
5. A hands-on fidelity playtest and visual comparison against a live 26.3
   reference remain outstanding.

The phase is not complete until the measurable behavior is tested and an
experienced player can complete the ordinary movement, mining, placement,
inventory, and Survival loop without immediately noticing mismatches. See
[Fidelity Bugs](FIDELITY_BUGS.md) for the persistent issue log and manual
checklist.
