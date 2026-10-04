# TerraCraft — Architecture Decisions

Log of architectural choices. New decisions append with date + rationale.

## 2026-10-04 — Publish a clean TerraCraft snapshot and audit fidelity

The published `TerraCraft` repository is a clean root snapshot so it does
not inherit private assistant-memory or owner-local asset files from the
existing repository's history. `AI_MEMORY` files and `mcassets/` stay in the
workspace and are ignored by Git. The project remains playable without
either directory through procedural texture and synthesized audio fallbacks.

The fidelity pass uses an inspectable review loop: baseline build and tests,
source measurements against the supplied behavior targets, a separate
critic review of the implementation and evidence, focused fixes, then a
repeat of the checks. This documents process; it does not establish Java
parity. Item-to-tile mapping has headless coverage, but the reported visual
texture issue stays open until its item and rendered location can be
reproduced. See `docs/GAUNTLET.md` and `docs/FIDELITY_BUGS.md`.

The pass also corrected entity interaction reach to 3 blocks and lets grass
plants, flowers, and torches be replaced by placed blocks. The placement API
returns the replaced block so Survival can spawn its usual drop; those
changes have regression tests. Further deviations and acceptance work
remain listed in `docs/JAVA_FIDELITY.md` and `docs/BACKLOG.md`.

## 2026-10-04 — Ranged combat: one projectile system, swept collision, transient arrows

### One pool for every shooter (no PlayerArrow/SkeletonArrow split)
Player draws and skeleton AI both end at `projectile_fire` with an
owner handle, a launch vector, and scaled damage — the pool cannot
tell who fired. The skeleton never touches projectile storage: it
queues `ProjectileShot` requests in `MobFrameEvents` and the app fires
them, exactly like melee damage flows through events. A second
projectile type later is one table row, not a second physics system.

### Swept collision because 40 m/s arrows cross blocks per substep
Per-frame position checks tunnel at full draw (0.67 blocks per 1/60 s
substep). Every substep collides the segment previous→new: DDA voxel
traversal for terrain, segment-vs-expanded-AABB for mobs and the
player, nearest impact wins with ties going to the block. Walls are
therefore unshootable-through by construction, and the ordering tests
(entity-first vs block-first vs two-mob lines) pin it.

### Owner grace, not owner immunity
Fresh arrows ignore their shooter for 0.2 s, then the owner re-arms
as a valid target. Permanent immunity would foreclose reflection
mechanics and hide aiming bugs; the grace window only covers muzzle
exit (spawns sit 0.6 ahead of the eyes, already clear of the body).
Stale owners are safe by construction: mob handles are generational,
so a dead shooter's slot either fails resolution or belongs to
someone else with a new generation.

### Projectiles never persist
Flying arrows live 6 s, embedded 12 s — both shorter than any
reasonable save cadence, and serializing velocity/owner/paths would
grow the entity format for zero gameplay value. Save-and-quit with
arrows mid-flight reloads a valid world with an empty sky (tested).
Skeletons persist like other mobs (kind 4, AIM state loads); their
cached paths, targets, and in-flight arrows rebuild live.

### No arrow recovery in M9
Embedded arrows despawn rather than converting to pickups. Recovery
would entangle the projectile pool with the item pool (ownership of
the slot, pickup radius vs embed position, save semantics for
half-recovered states). The embed timer is generous enough to read
as intentional; recovery is a clean M10+ slice if wanted.

### Bow charge: 1.0 s full draw, 0.75 power curve, weak taps cancel
Charge is draw time mapped, never frame-counted. Speed rides
pow(charge, 0.75) so half-draws still loft usefully; damage is
linear 2→6 HP so full draws stay meaningfully lethal without
one-shotting gloomstalkers. Taps under 0.15 cancel instead of
firing spitballs — a deliberate feel call, documented in the tuning
block so it can be retuned without touching the state machine.

### Skeleton band 6..14 with retreat, not chase-and-shoot
The skeleton holds a firing band rather than closing to melee: past
14 (or blind) it pursues with ordinary navigation, under 6 it backs
away along validated ground, inside with LOS it plants and draws.
Jitter scales with distance (0.05 rad base) so close shots threaten
and long shots dodge — an aimbot would make cover meaningless.
Cooldown 2.2 s with a visible 0.8 s AIM gives the player a dodge
window every cycle.

## 2026-10-04 — Real mob skins without bundling Mojang bytes

### Skins convert like tiles, never ship
`skeleton.png` converts to `generated/mobs/skeleton.bmp` at native
64x32 (no resize/crop/tint — part UVs map 1:1 onto Mojang's regions),
loaded exe-relative with the same fallback rule as tiles: missing
file keeps the procedural tile path, so CI stays green with zero
third-party assets in the repo. The renderer binds one texture per
mob model (tile batch on the atlas, then one batch per live skin),
so skins cost one extra draw call per skinned type, not per mob.

### Per-face UV tables, not a remesher
Each skinned part carries 6 pixel rects (Mojang's 64x32 regions for
head/body/arms/legs, left limbs mirroring right per the legacy
format) stretched onto our slightly different boxes. Recognizable
real art beats texel parity — our skeleton proportions are not
Mojang's, and forcing their boxes would change collision tuning.

### Only mobs with a real counterpart get skins
The gloomstalker is an original creature; no MC texture exists for
it, so it keeps the stylized tile path. A full roster (cow, creeper, …)
with real models/AI/drops is a future milestone, not a texture swap —
skinning a creeper-shaped AI onto a gloomstalker body would be a
costume, not accuracy. (The M8 mossling placeholder was replaced by a
real cow under this same rule: kind 2 kept its value so old saves load
their grazers as cows.)

### Skeleton loot goes fully MC: 0-2 arrows + 0-2 bones
This needed a real bone item (104, 64-stack, fallback icon) and a
drop-table change: min_count 0 is now legal (zero-rolls spawn
nothing). Bow/arrow icons also upgraded to converted art (they were
procedural fallbacks — the converter never had sources for them).

## 2026-10-04 — MC-accurate survival tuning (mining, drops, movement, hunger)

### Mining follows the Java formula, with the harvestable divisor right
Break time is `hardness * 1.5 / digSpeed` when harvestable with the
held item (wood 2, stone 4) and `hardness * 5` only for pickaxe-class
blocks (stone, ores) mined without a sufficient pickaxe. Dirt, wood,
leaves, glass, and friends are harvestable bare-handed (MC divisor 30,
not 100), so dirt by hand is 0.75 s, wood 3 s, leaves 0.3 s. An early
revision applied the x5 penalty to every hand/wrong-tool break, which
made dirt take 2.5 s and wood 10 s — correct-looking formula, wrong
divisor branch. Hardness values are the Java ones
(stone 1.5, dirt 0.5, logs 2, ores 3, glass 0.3, snow 0.1).

### Harvest rule: punching stone yields nothing
Pickaxe-class blocks (stone, all ores) drop nothing without a pickaxe
of sufficient tier (`min_tier` already encoded it: wood for
stone/coal, stone for iron/gold/diamond — our best tier stands in for
iron where MC wants better). Everything else drops regardless of tool;
glass never drops. The drop function takes the held item for this
reason; callers pass the hand they mined with.

### Movement numbers that were already MC stay; the rest align
Walk 4.317, sprint 5.612, jump 8.8, and gravity 32 were already the
Java values (32 b/s^2 is exactly MC's 0.08 b/tick^2 initial
acceleration). Aligned: sneak 0.5x -> 0.3x (1.31 m/s), fly 10.0 ->
10.9, terminal -55 -> -78.4 (-3.92 b/tick), melee/block reach 4.0/5.0
-> 4.5 survival / 5.0 creative, fall damage 4-free+2/block -> 3-free
+1/block, eat time 1.2 s -> 1.61 s, day length 6 min -> 20 min.

### Hunger is activity-only; regen costs only what it heals
MC has no passive hunger drain, so the -1/min timer is gone — standing
still never starves. Jump exhaustion was 8x over (0.4 -> 0.05/event),
sprint tracks the per-meter rate (0.56/s), and regen charges exhaustion
only when health actually rises (full HP no longer slow-starves).
Deliberate gap: no saturation system, so regen costs 1.5 instead of
MC's 6.0 — the full rate would make healing net-negative with our
saturation-free apples.

## 2026-10-04 — Audio pack swaps cut voices; same-pack applies are no-ops

### Bank rebuilds must not outlive the voices playing from them
`audio_load_pack` freed and reallocated every sample buffer on each
call, while mixer voices kept raw pointers into the old bank. A freed
block is usually recycled immediately by the re-synth mallocs, so an
in-flight voice played real-but-wrong audio (menu clicks surfacing as
phantom eating). Any real swap now deactivates all voices under the
device lock; callers retrigger what they need.

### "Default" and "" are the same bank
The settings screen live-applies every frame and every world open
re-applied the pack — but `sys->pack` stores `""` for the default bank
while callers pass `"Default"`, so the early-out never fired and each
frame re-read ~92 WAVs from disk. The entry point normalizes all three
spellings (NULL/""/"Default") before comparing: unchanged means
untouched, down to the buffer addresses (pinned by test).

## 2026-10-04 — Entity framework + two mobs + combat (M8)

### Fixed pool of 64 with generational handles, no reuse within a tick
The mob pool is a fixed array (no malloc in the loop) with
generation-bumped `EntityId` handles. A slot freed and respawned in the
same tick keeps a different generation, so the render pass and the
strike raycast can never alias a corpse into a newborn. The 64 cap also
bounds the renderer scratch buffers and the save file by construction.

### AI at 10 Hz, physics every frame
Think ticks accumulate `ai_t` and decide at 10 Hz; steering and physics
integrate every frame. This keeps 64 mobs at ~0.03 ms/tick measured
while movement stays smooth. Path requests are additionally capped at
1/second/mob with stale-target refresh, so the 0.17 ms worst-case A*
never runs hot.

### Entities.bin v2 with a kind byte, v1 still loads
Mob records ride the same world-atomic file as item drops (never
per-chunk: chunk borders must not duplicate or drop mobs). The kind
byte keeps the format extensible; only rest pose persists (position,
health, yaw, state) — velocity, target, and path are runtime-only and
re-derive on load. Whole-file reject on any corruption, same as v1.

### Gated strikes stay silent
The 0.4 s hurt window rejects damage; the app compares health before
and after instead of trusting the bool, so rejected strikes produce no
sound and no particles. Fake feedback is worse than none — it teaches
the player the weapon works when it did nothing.

### Exactly two mobs, cuboid part models, no catalogue
Scope is the feature: one passive + one hostile exercises every path
(spawn rules, AI, combat, drops, saves, rendering) without a content
pipeline. Models are validated part tables rendered as rotated boxes —
no external model format, no loader, no new save surface.

## 2026-10-03 — SDL2 + OpenGL 3.3 Core (M0)

### Chosen: SDL2 for window/input + OpenGL 3.3 Core for rendering

**Alternatives considered:**
- Vulkan: too verbose for M0, steep loader/validation-layer cost, poor payoff before meshing exists.
- Raylib: productive, but hides platform/GL-context details we want to own for learning and AI-maintainability.
  Also adds a heavier external dep vs. SDL2 which is ubiquitous in package managers.
- GLFW: lighter than SDL2, but no audio/gamepad path for later milestones. SDL2 leaves that door open.
- SDL3: newer, but packaging is less mature on LTS distros (Ubuntu 22.04 etc.). SDL2 is the safe baseline.
  Migration path to SDL3 later is small (window/event calls are similar).

**Why OpenGL 3.3 Core:**
- Widest compatibility: Intel iGPU on Windows/Linux/macOS (via legacy 3.3 support).
- Core profile forces VAO/VBO + shaders (no fixed-function), which is what we need for voxel meshing.
- No SDK required (unlike Vulkan), works with system `opengl32` / `libGL` / macOS framework.
- 3.3 is the lowest common denominator that still has GLSL 330, instancing basics, and uniform buffers.

### Minimal embedded GL loader (no GLEW/GLAD)
- We cannot assume GLEW/GLAD is installed on operator machines.
- For M0 we only need `glClearColor`, `glClear`, `glViewport`, `glGetError`, `glEnable/Disable`.
  Those exist in `opengl32.dll` / `libGL` / system headers, but modern functions we will need in M1-M2
  (VAO/VBO, shader compile/link) are NOT exported by system libs on Windows.
- Decision: implement `src/platform/gl_ctx.h/.c` that:
  1. Uses system `<GL/gl.h>` types where available for M0 basics, AND
  2. Loads all M1-relevant entry points explicitly via `SDL_GL_GetProcAddress` into `minec_gl_*`
     function pointers (with typedefs matching Khronos signatures).
  3. Fails init with a clear log if a required symbol is missing.
- This keeps deps at zero, documents exactly which GL API surface we use, and avoids a 10k-line generated loader.

### Custom header-only math (`src/math/mmath.h`)
- Avoids glm/cglm dependency. Keeps to C17, column-major mat4 matching OpenGL.
- Only vec3/mat4/quat helpers needed for camera + meshing. Tested in `tests/test_core.c`.

### Manual memory + arenas
- No GC. `src/core/mem.h` provides `mem_alloc/mem_calloc/mem_free` wrappers with null checks
  plus a simple linear `Arena` for transient per-frame data (used in M1 mesher).
- Ownership rule: creator destroys. `AppContext` owns everything in M0.

### CMake `find_package` strategy
- Try `find_package(SDL2 CONFIG)` first (vcpkg / SDL2 CMake config), fall back to `FindSDL2` module
  (`SDL2_INCLUDE_DIRS` / `SDL2_LIBRARIES`). On Windows without either, error message tells operator
  to install via vcpkg (`vcpkg install sdl2:x64-windows`) and pass `-DCMAKE_TOOLCHAIN_FILE`.
- OpenGL: `find_package(OpenGL REQUIRED)`, link `OpenGL::GL` (+ `opengl32` on Windows implicitly).

## Future Decision Slots
- M1: chunk size (16x256x16 vs 16x16x16 sub-chunks), meshing strategy (naive vs greedy).
- M2: shader hot-reload, texture atlas (procedural PNG vs generated bitmap).
- M3: physics/AABB collision, world-gen (value noise vs Perlin).

## 2026-10-03 — Milestone 2: Streaming, Atlas, Culling

### NEAREST filtering for the atlas
Chunky texels are the voxel aesthetic; LINEAR would blur tile art and worsen
bleeding across tile borders. Half-texel UV insets + CLAMP_TO_EDGE make
NEAREST sampling edge-safe without padding pixels.

### Single-pass transparency (accepted artifacts)
Correct transparency needs opaque + sorted-transparent passes. M2 enables
`GL_BLEND` (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) in one pass plus an alpha-cutout
`discard` for leaves holes: opaque blocks are pixel-perfect; water/glass
blend approximately but can sort wrong against each other. Documented in
`render/renderer.h`. A two-pass pipeline is M4+ work.

### Value noise + fBm instead of Perlin
Classic Perlin needs a permutation table and gradient lattice; seeded value
noise with smootherstep fade + 4-octave fBm gives rolling terrain with ~30
lines of dependency-free code. Deterministic via integer hashing (no rand()).

### Custom open-addressing hashmap for chunk lookup
Linear scan of 1024 chunk slots per block lookup would doom meshing
(65k blocks x neighbours). The map gives O(1) `world_get_chunk`; the dense
array is kept for iteration. Tombstones handle unload churn; resize at 70%.

### Legacy sine stub frozen (not removed)
The M2 spec asked to remove the 3x3 sine generator, but `tests/test_world.c`
pins its exact heights for determinism. It stays frozen; the streamer uses
`world_gen` (noise) for all runtime chunks. Removal would break the suite
for no runtime benefit.

### Default render distance 4 (not 8)
R=8 loads 289 chunks (~37 MB CPU + GPU buffers + multi-second initial mesh).
R=4 loads 81 chunks with the same code path and keeps 60 FPS while flying at
12 u/s (budget 4 gen/frame). The radius is one `#define` (MINEC_RENDER_DISTANCE).

## 2026-10-03 — Milestone 3: Player Physics & Interaction

### Axis-separated AABB collision (not swept volumes)
Swept-AABB gives exact time-of-impact but needs per-axis clipping math and
careful corner handling. Axis-separated move-and-clamp (X, then Z, then Y)
slides along walls for free, lands cleanly, and cannot tunnel at our speeds
(max 55 m/s terminal x 1/60 s step = 0.92 blocks < 1). The fixed 1/60 s
substep + accumulator makes it deterministic; the spiral guard drops excess
time past 5 steps rather than freezing.

### Orthographic flat-shader pass for the HUD
Text rendering would need a font rasterizer (e.g. stb_truetype vendoring) —
overkill for M3. The HUD is untextured colored quads (crosshair, hotbar,
selection frame, block-color icons) drawn with a dedicated UI shader under
`mmath_mat4_ortho`, depth test toggled around the pass. Debug text (FPS, pos,
mode, selection) stays in the stdout log. Text comes with the M4 UI pass.

### No font rasterizer in M3
Same reason: keep deps at SDL2-only. Hotbar icons reuse `BlockInfo` base
colors instead of atlas UVs so the UI shader stays texture-free (one less
sampler state to manage in the overlay pass).

### Input edge detection lives in AppContext
Discrete actions (F toggle, digits, clicks) need press edges; held movement
uses polled state. SDL offers no edge events for our minimal event enum, so
the app stores previous-frame button/key states. Wheel needs accumulation
(burst events), hence the window-side accumulator consumed once per frame.

## 2026-10-03 — Milestone 4: Lighting, AO & Sky

### 4-level AO with a reserved level 1
The M4 side/corner heuristic (either side solid -> 0, else corner -> 2, else
3) never emits level 1, yet the brightness table keeps all four entries
[0.4, 0.6, 0.8, 1.0]. Rationale: the classic smooth falloff needs the slot
when the heuristic is later refined (e.g. anisotropic single-side dimming),
and shaders/tests can rely on a stable 0..3 range. Verified by
`test_ao_levels` plus exact-vertex mesh tests.

### Simplified skylight instead of flood fill
Full BFS light propagation (sunlight + torch channels, 65k cells/chunk kept
in sync across edits) is the largest remaining engine risk and deserves its
own milestone. M4 approximates with a per-face column scan (any solid above
-> 0.45 shade): O(depth) worst case but early-outs in 1-2 steps for surface
faces, which dominate visible geometry. The factor folds into the AO vertex
channel, so no format churn later — a real light engine can reuse the slot.

### Fog hides streaming pop-in
Exponential-squared fog (density 0.014) melts distant chunks into the sky
color at R=4 ranges (~60% fogged at 80 m). Fog color tracks the sky clock,
so dawn/dusk/night all blend correctly. Cheap: one distance computation
per fragment from the already-interpolated world position.

### Creative sky tint retired
The M3 fly-mode tint fought the day/night clear color (two writers, one
framebuffer clear). The clock owns the sky now; mode feedback stays in the
log line. One owner per pixel, no exceptions.

## 2026-10-03 — Milestone 5: Game Shell & Persistence

### Hand-designed bitmap font instead of a font library
stb_truetype would add TTF rasterization + file-I/O surface for one feature
(text). A 95-glyph 8x8 bitmap (760 bytes, authored for this project) covers
all menu/debug needs through the existing flat UI shader with zero new
dependencies and zero licensing questions. A vector/text-rich UI can revisit
this if menus ever need proportional typography.

### From-scratch BMP reader instead of stb_image
Only one format is needed (16x16 uncompressed BMP tile overrides). A ~150-line
purpose-built parser with hard caps (4 MiB, 4096 px, BI_RGB only) has no
license to attribute, no unused codec surface, and fails closed on anything
unexpected. If PNG/JPEG support is ever wanted, stb_image (MIT/public-domain)
is the justified next step — documented in docs/RESOURCE_PACKS.md.

### Dirty-only chunk persistence over full snapshots
Regenerating untouched terrain from the seed is free and exact, so only
player-modified chunks (save_dirty, set solely by break/place) hit the disk.
Autosave writes meta + dirty chunks; unloads flush dirty chunks first, so no
edit is ever lost to streaming. Full snapshots would bloat saves ~130 KiB
per chunk for zero benefit.

### Order-independent decoration via world-coordinate pure functions
Trees/vegetation/caves/ores consult only (x,y,z,seed) — never chunk load
order or RNG streams. A tree stamps only cells inside the chunk being filled,
with trunk-wins-over-leaves and leaves-only-into-air rules, so any generation
order converges to identical bytes (proven by test_tree_determinism both
orders). This is the same principle that keeps biome borders square-free.

### Two-pass transparency with chunk-level sorting
Opaque + cutout first (depth writes on), then blended water/glass far-to-near
by chunk-center distance with depth writes off. Per-face sorting would need
transparent-face buffers + per-frame re-sort; chunk granularity fixes the
visible artifacts (water behind glass, lake surfaces) at negligible cost.
Documented limitation: faces within one chunk keep mesher order.

### No bundled third-party assets, ever
The repo ships procedural pixels + an original font only. Resource packs are
user-supplied folders validated tile-by-tile (16x16 BMP, fallback per tile).
This keeps the project legally clean by construction: there is simply no
pipeline stage where Mojang files could enter the build.

### Block IDs frozen at 18
Saves store raw IDs, so renumbering corrupts worlds. IDs are append-only;
unknown IDs on load clamp to stone (visible, debuggable) rather than
crashing; unknown format versions refuse outright. Policy recorded in
BACKLOG "Save format forward compatibility".

## 2026-10-03 — Milestone 6: Inventory & Survival

### Item IDs mirror blocks; tools/materials live above 100
Block-placeable items reuse the BlockType value (one ID to remember, save
format untouched); WATER is excluded (not an item). Standalone items take
explicit frozen IDs (coal=100, tools 200..205) that never collide with
future block IDs. Lookup is linear-scan by match, never by position.

### Survival policy lives in survival.c, not app.c
Mining times, drops, fall damage, hunger/regen/starve, damage gates, and
respawn are pure functions over Player/World so tests cover them headless.
app.c only sequences: input edges, raycast targets, entity spawns, screen
routing. Creative immunity is enforced inside the policy entry points, so
callers cannot forget the mode check.

### Flat mine_* fields instead of a nested struct
Player already carries body/inventory/survival state flat; mining progress
(target, held item, seconds) follows the same pattern. One struct, one
init, no sub-object lifetime to manage — at the cost of a wider Player.

### Entities are world-positioned, never chunk-bound
Chunk unloads must not eat drops, so the 128-slot pool stores world coords
with stable indices (no compaction, no dangling references). Pickup uses
center distance + 0.5 s delay; a full inventory leaves the entity untouched
(items are never destroyed for lack of space). Pool-full overflow banks
into the inventory, then logs loudly — drops only vanish when both are full.

### Saves stay v1: M6 keys are optional with migration defaults
Bumping format_version would orphan every M5 world. Health/hunger/spawn/inv
are optional keys; absent keys yield full vitals, no stored spawn, and an
empty (sanitized) inventory. Proven by test_save_m5_migration parsing an
M5-era buffer. A breaking layout change still bumps the version per the
BACKLOG policy.

### Mode stored as int to avoid header weight
Player.mode and WorldMeta.mode are plain ints compared against
WORLD_MODE_* (0/1). game/player.h and world/world_meta.h stay decoupled;
survival_is_creative is the single reader. The values are pinned by tests
and the M6 policy note here — change both together or not at all.

## 2026-10-03 — Milestone 6.1: Stabilization Gate

### Entities persist as one atomic world-level file
`entities.bin` (magic `MNCE`, version 1, 38-byte LE records) holds the
whole live pool, written on every save and loaded on open. Per-chunk
storage was rejected: drops live in world coordinates and stream with
nothing, so chunk files can neither duplicate nor lose them at
boundaries — atomicity falls out of the design instead of needing
merge logic. Corrupt files reject wholesale (pool cleared, loud log);
missing files (all M5/M6 worlds) load zero entities. Despawned and
picked-up entities never reach the disk by construction (only active
slots serialize).

### CI pins SDL2 2.30.8 on Windows, system packages elsewhere
Windows runners download the exact SDL2-devel version the project was
built against (no vcpkg build minutes); Ubuntu uses apt, macOS brew.
`TERRACRAFT_WERROR` (off by default, on in CI) maps to `/WX` vs `-Werror`
so a novel warning on a new compiler never blocks local iteration but
always blocks the merge queue.

### SDL3 deferred (again, explicitly)
No SDL2 blocker exists (audio runs on the SDL2 device API we already
linked). Migration would touch input, windowing, audio, and CI at once
for zero player-facing value in M7. Revisit with the CI + benchmark
safety net in place.

## 2026-10-03 — Milestone 7: Crafting, Audio, Particles

### Durability is wear, not health (0 = brand-new)
A fresh tool serializes as `{id, 1, 0}` — identical to every existing
stack literal, so no call site changes meaning. Breakage at
wear == max keeps "full" representable; sanitize clamps corrupt wear to
max (about-to-break) rather than deleting the tool. Merge requires
equal wear, closing the repair-by-stacking exploit at the primitive
level (tools are max_stack 1 anyway).

### `invdur=` stays an optional v1 key (no format bump)
The M6 precedent (optional keys + migration defaults) extends cleanly:
absent wear reads as 0 = full, so every M5/M6 world loads tools at full
durability with zero migration code. A version bump would orphan saves
for no benefit; the bump policy stays reserved for breaking layouts.

### Shaped recipes do not mirror
A 3-wide axe head matches only the orientation written in the table
(proven by a mirrored-pattern rejection test). Mirroring doubles the
match space and hides authoring mistakes; if players ever demand it,
mirror variants become explicit second recipes, not silent matching.

### Eating is survival policy, not app input code
`survival_eat_update(p, holding, dt)` owns the timer/consume/clamp
rules headlessly; app.c only supplies the RMB level and plays the
result sound. This follows the M6 rule (policy in survival.c, sequencing
in app.c) and made interrupted-eat and full-hunger-deny unit-testable.
The same split applies to exhaustion (policy) vs sprint/jump detection
(app input layer).

### Synthesized SFX instead of bundled audio
No audio files ship (legal cleanliness, same argument as textures):
14 event sounds are deterministic synth (tone sweeps + filtered noise)
generated once at init. Pack WAV overrides exist for users with their
own sounds; the parser is strict PCM-only with caps and falls back to
synth per event. No SDL_mixer: one device, one callback, 8 mixed
voices, and ~300 lines of local code beat a new dependency.

### Particles are visual only, renderer-owned scratch
Bursts read block/item colors and tiles but write nothing back — drops,
damage, and mining outcomes never depend on FX state. The transient
cube upload reuses a single renderer-owned scratch buffer (allocated
once, no per-frame malloc), and the pool itself is fixed at 256 with
deterministic spray (no RNG stream to persist or seed).

### Zero AppContext on init (uninitialized-stack crash class)
`AppContext app;` lives on main's stack; field-by-field init left
`audio.device` (and potentially other optionals) as garbage, and
`audio_init`'s re-init guard reads it. `app_init` now memsets the whole
struct first. Lesson: any init function that inspects-then-sets must
either zero upfront or document zeroed-input — caught by a test crash,
not by review.

## 2026-10-04 — Milestone 7.1: Owner Asset Refresh

### Owner asserts a Mojang license; the repo stays shippable without it
The project owner directed replacing procedural placeholders with files
from their local `mcassets/` tree under their own Mojang license claim.
That claim is **unverifiable from here and therefore not relied upon**:
`mcassets/` is gitignored local data (never committed), every layer
falls back to procedural/synth originals, the full suite (108 tests)
runs with zero mcassets files present, and CI never sees them. If the
folder is absent the game is byte-identical in behavior to M7. The
legal responsibility for possessing and using those files sits
entirely with the owner; keep the grant on file.

### Convert offline, never decode PNG/Vorbis in C
The engine parses BMP + WAV only. Rather than vendoring stb_image and
stb_vorbis (thousands of lines plus warning-suppression surface),
`tools/convert_mcassets.py` (Pillow + soundfile, dev-machine only)
pre-converts the ~40 tiles and ~30 sounds once into `mcassets/
generated/`. The C diff is routing, not codecs: one tile overlay pass
and one sound-bank layer reusing the existing tested parsers. Rerun the
script when `mcassets/` changes; the game logs per-layer summaries.

### Material-aware sound bank with round-robin variants
Break/place/step resolve per block material (stone, grass, gravel,
wood, sand, glass) instead of one generic thump, each with up to 4
round-robin variants — deterministic, no RNG stream. Layer order per
set: synth default, then converted files replace (only when at least
one parses), then user-pack WAVs replace wholesale. Empty sets are
silent-safe; the mixer never sees a NULL blob.

### Per-face tiles for logs and workbenches
`block_tile_for_face` already carried the face; it now returns log-end
and workbench top/side art where appropriate, plus six per-material
tool tiles (wood/stone × pickaxe/axe/shovel). Atlas stays 16×16 tiles
(MC Java art is natively 16×16); anything else would resample, but
nothing in the set needs it.

### Foliage arrives grayscale; the converter bakes the biome tint
Grass tops, leaves, and grass tufts ship as near-gray textures that real
Minecraft multiplies by biome colormaps at render time. Our renderer
has no tint stage, so conversion multiplies plains grass green
(#91BD59) and oak foliage green (#77AB2F) into the generated BMPs once
(user pack BMPs carry their own colors). Verified by byte-averaging
the outputs, not by eyeballing.

### Cross-sprites, not cubes, for decor blocks
Plants, flowers, and torches meshed as full cubes read as gray boxes
and red boxes in-game. They now emit two diagonal quads, each wound
both ways (16 verts / 24 indices, full-tile UVs, sunlight without AO
occlusion, depth-tested). The raycast/physics footprint stays a full
cell — only the picture changes.

### Snow gets its own footstep material; sneaking stays silent
Walking the snow biome on grass-crunch sounds read as "random" audio.
Snow now resolves to dig/snow variants, and sneaking emits no steps
(Minecraft parity on both).
