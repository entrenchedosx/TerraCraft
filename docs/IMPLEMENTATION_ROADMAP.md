# Parity roadmap

Ordering follows the visible-failure priority in the directive, adjusted for
dependencies (explained per stage).

| # | Stage | State | Depends on |
|---|---|---|---|
| 1 | First-person hand/animation | **Math + timing done**, pixel comparison pending | – |
| 1b | Hand remainder: offhand, slim arm, sprite extrusion, bow golden test, spear animation, `bobHurt`, hand FOV modifier | Open | 1 |
| 2 | **Vanilla 20 TPS player physics** (`vphys`: travel, move/collide with step 0.6, sneak edge, jump, water, fall distance) — core done; remaining: block friction/jump factors, ladders/vines, flowing water, lava, non-cube collision shapes, a Java reference dump for differential checks | **Core done**, extras open | tick-driven sim with render interpolation |
| 3 | World-gen differential harness + RNG/noise/density/biome stages | **In progress**: harness + RNG, noise, density router, climate + biome lookup, NoiseChunk fill with aquifers and ore veins, cave/canyon carvers, and the surface stage (rules, bedrock, badlands, icebergs, heightmaps, biome zoom) done and exact (stages 1-5); features/structures open, nothing wired into the game yet | - |
| 4 | Block rendering, lighting, sky, fog fidelity with frame-compare tool | Open | needs a screenshot/capture path in the game (none exists) |
| 5 | HUD/inventory fidelity pass using supplied GUI sprites | Open | 4 (capture tool) |
| 6 | Gameplay breadth: registries, recipes, entities, redstone, dimensions | Open | 3 for content that depends on generation |

## Why physics before world generation

World generation parity cannot start without the reference jar, whereas
physics can be made exact from knowledge of the 20 TPS rules and verified by
trajectory tests; it also unblocks correct water behaviour and the hand's
`walkDist`/`bob` inputs.

## Prerequisites

1. ~~1.21.11 jar download~~ - done 2026-11-02 (`tools/reference/fetch_reference.sh`).
2. Replace `mcassets/` with assets extracted from the 1.21.11 `client.jar`
   (current pack predates it). Local only; never committed.
3. A frame-capture path (offscreen render or `--screenshot` flag) so rendering
   can be compared at fixed FOV/resolution/skin.
