# TerraCraft — Backlog

Future tasks. M0-M4 done. **M5 done (verified MSVC, 63/63 tests, warning-free /W4).**

## Milestone 1 — Voxel Data Structures & Naive Meshing (DONE)
- [x] Finalize `Chunk` layout: 16x256x16 flat uint16_t, `(y*256)+(z*16)+x`, heap create/destroy.
- [x] Implement `chunk_get_block/set_block/fill/is_empty` with bounds validation + tests (legacy u8 wrappers kept).
- [x] Implement naive mesher: positions/normals/colors per visible face, CCW (0,1,2,2,1,3), cross-chunk neighbors.
- [x] Wire `Renderer` to real VAO/VBO/EBO + `Shader` compile/link (GLSL 330 core, Lambert+ambient).
- [x] Implement FPS `Camera` (yaw/pitch, clamp ±89°) + WASD/Space/Shift + mouse (app.c input, window helpers).
- [x] Procedural `World` gen stub: 3x3 around origin, H=64+sin(x*.1)*cos(z*.1)*8, bedrock/stone/dirt/grass layers.
- [x] Extend tests: chunk indexing, block IDs/colors, gen determinism, mesher face counts (1 block=24v/36i, 2 blocks=40v/60i, 3x3x3=54 faces, border culling).
- [x] Perf: mesher build time logged via LOG_DEBUG (sub-ms for isolated blocks; full terrain chunks ~1-5 ms typical, check stdout).

## Milestone 2 — Streaming, Atlas, Culling (DONE)
- [x] Texture atlas 256x256 RGBA8, 16 procedural tiles, NEAREST + CLAMP_TO_EDGE.
- [x] Chunk streaming: hashmap lookup, ring-ordered budgeted loads, unload outside R, neighbour dirty-marking (default R=4).
- [x] Frustum culling per chunk AABB (Gribb/Hartmann planes, positive-vertex test).
- [x] Textured shaders (atlas sample, alpha cutout, Lambert) + single-pass blending (documented artifacts).
- [x] Noise terrain (value fBm, beaches/snow/slopes/ores/water); sine stub frozen for tests.
- [x] Perf metrics (mesh/upload/draw ms, drawn/culled) in FPS log; GL loader 27 -> 39 symbols.
- [ ] Shader hot-reload from disk (deferred to M4 polish).
- [ ] Face-based AO stub (deferred; flat lighting + textures for M2).

## Milestone 3 — Player, Physics & Interaction (DONE)
- [x] AABB player (0.6x1.8m, eye 1.62), gravity 32, jump 8.8, terminal -55, fixed 1/60 s substeps.
- [x] Axis-separated collision (X/Z/Y, snap-out, grounded on landing, wall slide); sneak 0.5x, sprint, fly no-clip on F.
- [x] DDA raycast (Amanatides & Woo, 5 blocks, face normals) from eye and camera.
- [x] Break (bedrock/air safe, border dirty, particle log stub) / place (air/water only, self-overlap reject).
- [x] HUD overlay: ortho flat-shader pass, crosshair, 9-slot hotbar + selection + icons; debug text stays in logs.
- [x] Input: WASD/Space/Shift/Ctrl/F/1-9/wheel/LMB/RMB with edge detection; sky tint mode feedback.
- [x] Tests: fall/land, high-drop no-tunnel, wall slide, jump height, spawn finder, break/place rules, ray faces/miss/range.

## Milestone 4 — Lighting, AO & Sky (DONE)
- [x] Day/night clock (360 s cycle, keyframed sky/sun/intensity, 0.15 night floor).
- [x] Per-vertex AO (side/corner heuristic, 4-level table, cross-chunk seamless).
- [x] Skylight heuristic (per-face column scan, 1.0/0.45 folded into AO channel).
- [x] 9-float verts + location-3 attrib; dynamic sun/skylight/AO/fog shader.
- [x] Renderer TimeSystem integration (sky clear color, 7 lighting uniforms, fog 0.014).
- [x] Tests: time phases/sun/wrap, AO levels/flat/corner/seam/shade.

## Milestone 5 — Actual Game Shell (DONE)
- [x] Game states (menu/select/create/loading/playing/paused/settings/quit); boot to title; validated transitions.
- [x] Menus: singleplayer list (name/seed/mode/played, sorted), create form (name/seed/mode), pause (resume/settings/save+quit), settings (RD/sens/FOV/vol/vsync/pack), loading progress, delete with confirm.
- [x] Original 8x8 bitmap font (95 glyphs); text measure/draw; F3 debug overlay (FPS/XYZ/chunk/chunks/seed/day/mode/block/pack/perf).
- [x] Immediate-mode widgets (buttons, text fields, sliders); headless-tested logic.
- [x] Saves/: world.meta v1 + chunks/c_X_Z.bin (MNC1, LE, validated); dirty-only writes; save-on-unload; autosave 30 s + pause/quit/menu.
- [x] Seeds: blank→clock-derived, integer (dec/hex/oct), text→FNV-1a64; deterministic.
- [x] Biomes ×7 (ocean/beach/plains/forest/desert/snow/mountains); order-independent trees; plants/flowers; 3D-noise caves; clustered ores.
- [x] Two-pass rendering (opaque + blended far-to-near, depth-write off); leaves/plants stay cutout.
- [x] Resource packs: resourcepacks/<pack>/tiles/*.bmp overrides + procedural fallback; BMP parser from scratch; adapter doc.
- [x] Block IDs frozen append-only (18 total); forward-compat policy (unknown versions/chunks rejected, unknown IDs clamped).
- [x] Tests: meta/chunk roundtrips, corruption rejection, sanitize/traversal, biome coverage, tree/cave/ore/vegetation determinism, settings/seed/bmp, states/font/widgets.

## Milestone 6 — Inventory + Items + Survival Foundation (DONE)
- [x] ItemStack/inventory model (36 slots, hotbar 0..8, per-item max stacks, sanitize) + survival inventory GUI (LMB move/merge/swap, RMB split/place-one) + creative catalogue (free full stacks, editable hotbar).
- [x] Item/block distinction (stable IDs: blocks mirror BlockType, coal=100, tools 200..205); block drops + pickup entities (pool 128, 0.5 s delay, 1.5 radius, 300 s lifetime).
- [x] Health/hunger foundation (20/20, activity-only drain, regen/starve every 4 s), mining times + tools (MC formula: hardness*1.5/speed correct, hardness*5 hand/wrong; bedrock unbreakable), fall damage (3 free, 1/block), game-mode behavior (creative fly/instant/infinite/immune).
- [x] Death/respawn foundation (survival drops scatter, death screen, respawn at stable spawn); saves stay v1-compatible (vitals/spawn/inv are optional keys, M5 files load with migration defaults).
- [x] Tests: mine times/drops/mining progress, damage/heal gates, fall, hunger/regen/starve, respawn, entity pickup/lifetime, M6 roundtrip, M5 migration (78 total, warning-free).

## Milestone 6.1 — Stabilization Gate (DONE)
- [x] In-game verification: Release boot/menu/create/load/play/mine/pickup confirmed live; transition/ESC/wheel/edge fixes from the prior audit.
- [x] Dropped-item persistence: versioned `entities.bin` (magic MNCE, v1, LE fixed-width 38 B records, whole-file reject on corruption); saved on autosave/pause/quit/unload, loaded on open, deleted with the world; M5/M6 worlds load zero entities.
- [x] Ownership: world-level atomic entity list (never per-chunk; boundary-safe by construction); despawned/picked-up entities never persist.
- [x] CI: GitHub Actions (Windows SDL2 2.30.8 download, Ubuntu apt, macOS brew) — configure, Release build, `ctest -C Release`; `TERRACRAFT_WERROR` (MSVC `/WX`, GCC/Clang `-Werror`) ON in CI.
- [x] `.clang-format` (matches repo style) + `docs/FORMAT.md`; mesher benchmark target `terracraft_bench` (solid/surface/cave/veg/checkerboard + counts).
- [x] SDL3: deferred (no SDL2 blocker; documented in DECISIONS).
- [x] Tests: entity save roundtrips (single/multi/negative/partial/max), corruption rejection (bad magic/version/truncation/trailing/excessive/invalid id/count/wear), missing-file migration, transient exclusion (11 tests).

## Milestone 7 — Crafting, Audio, Particles & Expanded Survival (DONE)
- [x] Recipes (`game/recipe.h/.c`): data-driven shaped (offset search, NO mirroring — documented) + shapeless multiset (exact, duplicates required); 12 original recipes (planks/stick/torch/workbench/wood+stone tools).
- [x] 2x2 player crafting in the survival inventory + workbench block (RMB opens 3x3, sneak bypasses to place) + CRAFTING state reusing slot/cursor/output-take logic; the bench screen shows the full inventory (27 + hotbar) under a plain "Crafting Table" title; closing returns ingredients (stash/drop/void, never deleted).
- [x] Durability as wear (0 = new, breaks at max; wood 64 / stone 160 original tuning): decrement only on successful breaks, merge requires equal wear, HUD bars, break sound; `invdur=` optional meta key (M6 files load full; corrupt wear clamps, never deletes).
- [x] Food (apple, +4, deterministic 1-in-8 leaf bonus by cell hash) + hold-to-eat state machine in survival policy (interrupt-safe, full-hunger deny, creative/dead gated) + eat HUD bar.
- [x] Hunger expansion: sprint/jump/regen exhaustion (4 pts burn 1 hunger, frame-rate independent, pause-frozen).
- [x] Particles (bounded 256 pool, deterministic spray, no per-frame alloc via renderer-owned scratch): break bursts, mining puffs, pickup poofs; damage = HUD red flash + sound.
- [x] Audio engine (`audio/audio.h/.c`, SDL2 device, synthesized original SFX, 8 voices, master+effects volumes): 14 events incl. footsteps by material class; WAV pack overrides with fallback; no-device runs silent with a warning.
- [x] New blocks/items/tiles: workbench (18), planks (19), apple (101), stick (102); 4 atlas tiles + pack names; catalogue updated.
- [x] Tests: recipes (registry/shapeless/shaped/orientation/bench-offset/consume), durability (use/merge/save/migration/corrupt), food/eat/exhaustion/leaf-bonus, particles (bound/expiry/determinism), audio (stems/volumes/WAV/dummy-device), settings sfx migration (105 total, warning-free).
- [x] No mobs (M8); no SDL3 migration.

## Milestone 7.1 — Owner Asset Refresh (DONE)
- [x] `tools/convert_mcassets.py` (Pillow + soundfile): 41 tiles to 16x16 BMP + 80 sounds to 22050 Hz mono WAV under `mcassets/generated/` (zero missing); rerunnable, deterministic.
- [x] 9 new atlas tiles (log top, workbench top/side, 6 per-material tools) + per-face log/workbench mapping; pack table at 41 stems.
- [x] Atlas loads `mcassets/generated/tiles/` between procedural base and user packs (41/41 live); missing dir keeps procedural (CI unaffected).
- [x] Material-aware audio bank (break/place/step × 6 materials, round-robin variants; 80/80 converted files live); user-pack WAVs still override; synth fallback retained.
- [x] `mcassets/` gitignored (owner-local, never committed); full suite green without it (108 tests).
- [x] Font/models/lang intentionally untouched (no engine consumer / text-renderer rewrite out of scope).

## Milestone 9 — Projectiles + Bow + Skeleton Ranged Combat (DONE)
- [x] Projectile framework (`game/projectile.h/.c`): fixed pool 32, stable type IDs (ARROW=1), data-driven definitions (gravity 20, drag 0.82/s, fly 6 s / embed 12 s), fixed 1/60 s substeps, swept DDA voxel + swept segment-vs-AABB entity collision, nearest-wins (ties to block), owner grace 0.2 s (re-arms), damage/knockback only via `living_entity_damage_src`, no-penetration, fail-clean cap.
- [x] Bow (`ITEM_BOW`=206: unstackable, 128 uses, `TOOL_BOW`) + Arrow (`ITEM_ARROW`=103: 64-stack); bench-only recipes (limbs / coal-tipped shafts → 4); tiles 41/42 + stems; catalogue extended; registry 14 recipes.
- [x] Draw/charge/fire: RMB hold (both modes), charge draw_t/1.0 s, < 0.15 taps cancel, speed 0.75 curve 10→45 m/s + damage 2→6 HP + 6.0 knock, eye+0.6 forward origin, 0.5x move slow; slot/wheel/menu/death/unload/no-arrow cancels consume nothing; survival 1 arrow + 1 wear per real fire (pool-full fails consume nothing); creative free.
- [x] Feedback: amber charge bar (pale at full), `BOW_DRAW/FIRE` + `ARROW_STICK` synth events, oriented shaft+head rendering (shared mob scratch, frustum-culled, embed keeps orientation).
- [x] Skeleton (`ENTITY_SKELETON`=4): HP 20, 6-part model with raised bow arm, AI AIM/ATTACK on the reused FSM (approach > 14, retreat < 6, hold+draw in band, per-tick LOS recheck, distance-scaled jitter, 2.2 s cooldown / 0.8 s draw, melee fallback 2 dmg), shots through the `MobFrameEvents` queue into the same `projectile_fire`; shared hostile cap + ~35% night rotation; kind-4 persistence (projectiles never persist).
- [x] Debug: F3 arrows line; F11 spawn skeleton, F12 clear arrows, Shift+F6 bow+arrows; bench arrows/skeleton cases (0.85/0.35 ms); 146/146 tests (11 new), Debug+Release green, Release boots v0.9.0.
- [x] No armor/more mobs/crossbows (M10+); gaps: no arrow recovery, no saturation, no skeleton footsteps.

## Fidelity pass — current verification round (ACTIVE)
- [x] Record Java Edition 26.3 behavior targets and measured TerraCraft differences in `docs/JAVA_FIDELITY.md`.
- [x] Open a persistent reproduction log and manual playtest checklist in `docs/FIDELITY_BUGS.md`.
- [x] Add coverage for all 33 item registry-to-atlas mappings and nonempty procedural fallback tiles; correct entity interaction reach and replaceable decor placement with Survival drops.
- [x] Refresh the resource-pack reference to the current 46 atlas tiles and sound events.
- [x] Fix custom resource-pack path construction/discovery and reload the live atlas when the selected pack changes; headless tests cover pack listing and item-tile application.
- [x] Add a tested 60 TPS scheduler with bounded catch-up, dropped-time accounting, event-latched input edges, and world simulation during inventory/workbench screens.
- [x] Replace the stretched cow skin mapping with per-face box-net UVs and skin-proportioned cuboids; align melee ray bounds to yawed rendered models, allow hits from inside, and retry held attacks after cooldown.
- [x] Add a procedural first-person sleeve and hand, held-item sprite, and independently timed swing; Debug and Release CTest both pass with 155 registered cases.
- [x] Prevent stacked flowers and grass plants: clicking decor replaces its cell, and new vegetation requires grass, dirt, or snow support.
- [ ] Reproduce the user's item texture report with the affected item names, render location, selected pack, and screenshot; verify the custom-pack fix in-game or identify another runtime cause.
- [ ] Add render interpolation, then validate ticked movement/collision, survival, lighting, and fluid behaviors before claiming the fidelity pass complete.
- [ ] Complete a hands-on Survival/Creative playtest against a running Java Edition 26.3 reference.
- [x] Round 6 verification (historical): plant placement is supported/non-stacking; first-person arm skin, scale/framing, grip, and FOV behavior were corrected. Debug and Release builds and CTest passed (160 tests); independent source review was complete.
- [x] Audit max stack sizes against Java Edition behavior for every registered item; all seven tools/bow are already single-stack, with regression checks covering inventory and save sanitization.
- [x] Correct first-person arm camera-facing skin UVs and grip; replace survival vitals bars with full/half/empty pixel-art hearts and food icons.
- [x] Add terrain profile 2 with warped landforms, ridged highlands, and carved valleys, while keeping existing unversioned worlds on profile 1.
- [x] Add source/flow/falling water, down-first flow and horizontal decay, two-source refill, stream retraction, loaded-chunk boundary wakeups, bounded updates, partial-height meshes, and simple buoyancy.
- [x] Add regression coverage for fluid simulation/rendering, chunk v1/v2 compatibility, and water movement.
- [x] Round 7 verification: Debug and Release builds and CTest pass (167 tests); independent source review found no code-level blocker. Keep `AI_MEMORY` and `mcassets/` excluded from Git.
- [ ] Manual game verification remains open for plant replacement/support, cow targeting and appearance, and first-person hand/swing at low and high FOV. The reported item texture issue also needs the item, screen path, and resource pack to reproduce.
- [ ] Inspect the updated HUD, arm, terrain, and water in a running build; automated checks cannot verify visual quality or Minecraft parity.
- Gauntlet procedure and round notes: `docs/GAUNTLET.md`.

## Milestone 8 — Entity Framework + Creatures + Combat (DONE)
- [x] Living-entity framework (`game/mob.h/.c`): generational `EntityId` handles, fixed pool (64, zero per-frame alloc; no slot reuse within a tick), per-type definitions (size/HP/speed/drops/resists), shared AABB physics (gravity 32, axis-separated collision, 1-block auto step-up, ground-stick, fall damage via `survival_fall_damage`).
- [x] Exactly three real mobs: Cow (passive, HP 10, 1-3 beef + 0-2 leather) + Zombie (hostile melee, HP 20, 3 dmg / 1.6 s, night spawns, 0-2 rotten flesh) + Skeleton archer (HP 20, arrows + bones). No placeholder creatures remain in the roster.
- [x] AI at 10 Hz (`ai_t` accumulator; physics every frame): IDLE/WANDER/CHASE/ATTACK/HURT/DEAD/FLEE/GRAZE states, deterministic `mob_rand` wander, LOS-gated aggro (12 detect / 18 lose), 1 s repath cooldown, stale-path (> 2 cells) refresh, no-target despawn policy (hostiles > 80, full-pool pressure only).
- [x] Bounded A* (`game/pathfind.h/.c`, radius 16 / y +-6 / 4096 expands, 4-dir + step +-1, corner rules, heap with deterministic tie-break, malloc-per-call freed before return; 0.17 ms worst-case measured).
- [x] Combat: LMB entity-first targeting (nearest mob inside reach wins unless a block is closer), per-tool damage/cooldown (fist 1/0.4 s, shovel 2, pick 3, axe 4/0.8 s), 0.4 s hurt window (gated strikes silent — no fake feedback), knockback resist + clamp, death drops exactly once through the shared item-entity system.
- [x] Spawning: 5 s tick, ring 24..48, surface scan (standable + light rule: hostile only at night/day_progress-based or dark caves, passive day), water/stone rejected, caps 10 passive + 10 hostile, sim range 64.
- [x] Persistence: `entities.bin` v2 (`MNCE` ver 2: kind byte + 38 B item / 28 B mob records); v1 still loads (mobs start empty); corrupt whole-file reject; dead mobs, velocities, targets, and paths never persist (re-seeded deterministic rng).
- [x] Presentation: cuboid part models (`game/mob_model.h/.c`, validated tables) rendered as rotated boxes with walk swing / hurt flash / death tilt, frustum-culled, in world + inventory/creative states; MOB_HURT/MOB_DIE synth events + death particle bursts; F3 shows mob/ai-draw counters; F7 strike-aimed, F9 spawn mossling, F10 spawn gloomstalker (dev keys, same family as F6/F8).
- [x] Tests: 135 total (23 M8 + 1 audio pack-swap regression: handles/physics/defs/AI/damage+gate/raycast/tools/spawn-rules/save-v2/path/model/fall-events); `terracraft_bench_mobs` (idle 64 / chase 20 / raw path cases); warning-free /W4.
- [x] No mobs catalogue beyond the two; no armor/enchants/projectiles/multiplayer (M9+).

## Save format forward compatibility (binding policy)
- `world.meta`: `format_version` integer, current = 1. Readers reject
  `version <= 0` and `version > 1` (never migrate silently). Unknown keys
  within v1 are ignored (forward tolerant). Missing required fields fail.
- `chunks/c_X_Z.bin`: magic `MNC1`, u16 version = 2 (v1 remains readable), i32 coords, 65536 LE u16
  block IDs. Wrong magic/version/coords/size truncations are rejected;
  unknown block IDs clamp to stone (never crash).
- Block IDs are append-only forever: existing values never change meaning
  or get reused. New blocks take the next free ID + new atlas tile.
- Chunk payload is always full 16x256x16 (no dimension field needed yet;
  a future version bump would add explicit extents before any resize).

## Tech Debt / Infra
- [x] Persist dropped-item entities in saves (done in M6.1: `entities.bin`).
- [x] CI: GitHub Actions (done in M6.1: windows/ubuntu/macos).
- [x] `.clang-format` enforcement + `cmake --build` warnings-as-errors on CI (done in M6.1: config + `TERRACRAFT_WERROR`).
- [x] Benchmark harness for mesher (done in M6.1: `terracraft_bench`).
- [ ] Evaluate SDL3 migration once packaging matures.
