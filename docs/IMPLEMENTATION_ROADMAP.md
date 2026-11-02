# Parity roadmap

Ordering follows the visible-failure priority in the directive, adjusted for
dependencies (explained per stage).

| # | Stage | State | Depends on |
|---|---|---|---|
| 1 | First-person hand/animation | **Math + timing done**, pixel comparison pending | – |
| 1b | Hand remainder: offhand, slim arm, sprite extrusion, bow golden test, spear animation, `bobHurt`, hand FOV modifier | Open | 1 |
| 2 | **Vanilla 20 TPS player physics** (`LivingEntity.travel`, `Entity.collide` Y-then-dominant-axis order, step 0.6, friction by block, sneak edge, ladders/vines, slime/honey/ice, fall damage) and full fluid model; water exit already fixed without teleport | Open (next) | tick-driven sim with render interpolation (the hand module already shows the pattern) |
| 3 | World-gen differential harness + RNG/noise/density/biome stages | Blocked on 1.21.11 jar | owner approval to download `1.21.11` |
| 4 | Block rendering, lighting, sky, fog fidelity with frame-compare tool | Open | needs a screenshot/capture path in the game (none exists) |
| 5 | HUD/inventory fidelity pass using supplied GUI sprites | Open | 4 (capture tool) |
| 6 | Gameplay breadth: registries, recipes, entities, redstone, dimensions | Open | 3 for content that depends on generation |

## Why physics before world generation

World generation parity cannot start without the reference jar, whereas
physics can be made exact from knowledge of the 20 TPS rules and verified by
trajectory tests; it also unblocks correct water behaviour and the hand's
`walkDist`/`bob` inputs.

## Immediate prerequisites that need the owner

1. Permission to download `1.21.11` (client/server jar, official manifest) into
   a git-ignored `reference/` folder. Without it no vanilla-vs-C diff for
   world generation or 1.21.11-specific behaviour can be produced.
2. A frame-capture path (offscreen render or `--screenshot` flag) so rendering
   can be compared at fixed FOV/resolution/skin.
