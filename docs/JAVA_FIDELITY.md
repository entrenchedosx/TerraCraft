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

- `app_tick_playing()` receives variable frame time and updates most gameplay
  systems once per rendered frame (`src/core/app.c`). The app clamps a long
  frame, but does not yet own a 20 TPS simulation accumulator.
- Player collision uses fixed 1/60 s substeps (`PLAYER_STEP_DT` in
  `src/game/player.h`), with a five-step cap. It is not the requested
  authoritative 20 TPS tick.
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
- Mining progress is accumulated in seconds and updated with frame time.
  Releasing, changing target, changing held item, or losing reach resets
  progress. Airborne and underwater mining penalties are not implemented.
- Placing blocks rejects solid targets and player overlap. Air, water, grass
  plants, flowers, and torches can be replaced. The placement API returns
  the replaced block; Survival resolves its usual drop, while Creative
  discards it. `test_interaction_break_place` covers replacement IDs and
  the decor drop definitions.
- The player collision box has no explicit pose or block-state system.

### Inventory and survival

- Inventory, workbench, death, and pause screens currently freeze the world
  (`game_state_is_live()` in `src/game/game_state.c`). Only the pause menu
  should freeze the world under the target behavior. Inventory and workbench
  simulation are still a known deviation.
- Hunger and exhaustion exist, but there is no saturation value or saturation
  refill path. Exhaustion directly removes hunger. Sprint input is not yet
  blocked at low hunger, and regeneration is a simplified fixed timer.
- Dropped items age for 300 seconds and wait 0.5 seconds before pickup
  (`src/game/entity.h`). They use small frame-dt substeps and gravity; item
  stack merging is not implemented. Their lifetime matches 6,000 ticks only
  when the simulation runs at 20 TPS, which it does not yet do.
- The day cycle is 1,200 seconds (`TIME_DEFAULT_SPEED` in
  `src/game/time_system.h`), equivalent to 24,000 ticks at 20 TPS. Its
  advance is currently frame-time driven.

### Rendering, lighting, and fluids

- The atlas combines procedural pixels with optional owner-local or user
  pack tiles. All 32 registered item types map to a named atlas tile; the
  headless `test_item_texture_mapping` checks that mapping and visible
  procedural fallback pixels. The user's report of broken item art still
  needs a screenshot or the names of affected items and screen location to
  identify a rendered-only defect.
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

## Known deviations

1. No authoritative 20 TPS simulation loop or tick-input edge queue.
2. Direct-velocity movement, no player pose geometry, and no edge-safe
   sneaking or step-up behavior.
3. Inventory and workbench freeze single-player simulation.
4. Mining, hunger timers, mob logic, and dropped items are frame-time based
   rather than tick-quantized.
5. Hunger has no saturation, and sprint hunger gating is absent.
6. Lighting has no propagated sky/block channels; water does not flow.
7. A hands-on fidelity playtest and visual comparison against a live 26.3
   reference remain outstanding.

The phase is not complete until the measurable behavior is tested and an
experienced player can complete the ordinary movement, mining, placement,
inventory, and Survival loop without immediately noticing mismatches. See
[Fidelity Bugs](FIDELITY_BUGS.md) for the persistent issue log and manual
checklist.
