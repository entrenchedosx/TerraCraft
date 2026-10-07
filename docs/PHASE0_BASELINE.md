# Phase 0 — Baseline and Repository Audit

**Status: BLOCKED**  
**Audit date:** 2026-10-07  
**Starting revision:** `6d14d8c5912c362faa747a8cb1c9b58844e66faf` (`main`)  
**Machine:** Windows 11 Home, AMD Ryzen 5 7500F (6 cores)  
**Build generator:** Visual Studio 17 2022; `TERRACRAFT_WERROR=ON`

Phase 0 remains **BLOCKED**. The exact workspace Release executable was
launched and a saved Survival world was played for about four minutes with
runtime output captured. This establishes a representative live-scene
performance sample and a menu/pause/resume path. A seed-123456 screenshot and
saved metadata are consistent with a player near spawn, but no runtime log
binds that screenshot to the exact run. Live water behavior is still
unobserved. A second log-backed run from the Release working directory remains
open in a different, paused save; its latest state is described below. The
screenshots still need matching logs to explain their frame readings. No
gameplay implementation changes have been made.

## Build

- Clean Debug build of `terracraft` and `terracraft_tests`: succeeded.
- Clean Release build of `terracraft`, `terracraft_tests`,
  `terracraft_bench`, and `terracraft_bench_mobs`: succeeded.
- C compiler warnings/errors: **0** in both configurations. The configured
  cache treats compiler warnings as errors.
- CMake did print an upstream SDL2 package deprecation notice about its
  minimum supported CMake version while regenerating the build. This is a
  dependency configuration notice, not a C source compiler warning.

## Tests

- At the starting revision, the custom runner had **190 tests**; the initial
  clean Debug suite passed **190/190**.
- Phase 0 diagnostics added one test for the process-memory query. The current
  runner has **191 tests**.
- Current Debug: CTest **1/1 passed**; direct runner **191 passed, 0 failed**.
- Current Release: CTest **1/1 passed**; direct runner **191 passed, 0 failed**.
- Captured current Debug build, Debug/Release test-build, CTest, and direct
  runner outputs are in `docs/phase0-*-build.log`,
  `docs/phase0-*-ctest.log`, and `docs/phase0-*-runner.log`. The Release game
  executable was not relinked during this check because that exact executable
  is still running; its full build succeeded in the earlier clean build.
- The negative-save fixtures intentionally emit error logs, and audio tests
  intentionally exercise unavailable audio drivers. Those messages are
  expected test cases and did not fail the suite.
- `README.md` previously reported 184 tests; that count was stale.

## Runtime scenarios

- The exact workspace `build/Release/terracraft.exe` ran from 11:28:22 to
  11:32:36 on 2026-10-07. The full stdout log is preserved at
  [phase0-runtime.log](../phase0-runtime.log). It records a 1280x720 drawable,
  NVIDIA GeForce RTX 5060, OpenGL 3.3 / driver 610.88, successful asset and
  renderer initialization, and a normal `Main loop exited` shutdown. The log
  contains no `ERROR` or `WARN` entries.
- The run followed PROFILE -> MAIN_MENU -> WORLD_SELECT -> LOADING -> PLAYING,
  opened the existing `sss` Survival save (seed `-185253974`, about 81 chunks),
  recorded movement and block interactions, and saved the world successfully.
  PLAYING -> PAUSED -> PLAYING was also observed. The player later reached
  DEAD with 0/20 health and 0 hunger, after which the world closed and the
  application quit normally. The save was modified during this playthrough;
  it is not treated as an untouched baseline fixture.
- This run did not create a fresh world, so it does not prove initial-spawn
  clearance or reproduce the supplied screenshot's seed `123456`. It contains
  no water interaction or visual fluid-flow observation. A pause and resume
  were observed, but the complete menu/settings/resolution path was not
  exercised. Crash/assertion/NaN results outside the captured paths are **not
  measured**, not zero.
- Existing app flow: first launch enters the profile screen if no profile is
  saved; otherwise it enters the main menu. World creation/loading is a staged
  state before play. F3 toggles the in-world diagnostic overlay.
- Existing spawn code requires a solid support block and two non-solid blocks
  above a candidate column. The spiral search prefers a candidate with air at
  feet+1, but its fallback can use a candidate without that dry-clearance
  check. A saved spawn may bypass fresh-spawn validation. Existing spawn tests
  cover a simple solid floor and an empty column; they do not cover water,
  hazards, enclosure, player overlap, or 1,000 seeded worlds.
- Water runs on a 0.25-second step with a 1,024-cell update budget per step
  and a bounded rescan. Existing tests cover propagation, retraction, chunk
  edges, and queue recovery; there is no live visual synchronization result.
- Menu state transitions and UI have automated tests. The live log confirms
  profile, main-menu, world-select, loading, pause, resume, death, and quit
  state changes; mouse/keyboard coverage of all menu screens and settings,
  plus a resize/fullscreen pass, remains unverified.

### User-provided live capture

Saved to [docs/shots/phase0-f3.png](shots/phase0-f3.png). The F3 overlay shows
seed `123456`, Default pack, Survival, position `(8.67, 72.06, 8.88)`, 81 loaded
chunks, `0.12 ms/tick` simulation time at `60.0 ticks/s`, and `158 MiB` process
resident memory. The scene shows the player above grass in a loaded world.
The latest user upload was byte-identical to this saved image, so it adds no
new runtime evidence.

The overlay reports **1221.7 FPS** and **0.24 ms/frame** together. Those values
do not agree: 1221.7 FPS corresponds to about **0.82 ms/frame**. In the current
source both figures come from the same frame count and elapsed interval, so
they should be reciprocals. The screenshot does not identify the exact
executable path or provide a log to reconcile this discrepancy; the frame-time
number remains unverified. The image does not show a water interaction or a
menu transition, and it cannot prove the displayed position was the initial
spawn point.

### Seed-123456 screenshot

Saved to [docs/shots/phase0-f3-seed123456.png](shots/phase0-f3-seed123456.png).
It shows Survival at `(8.42, 72.00, 8.35)`, seed `123456`, Default pack,
81 loaded chunks, and `152 MiB` resident memory. The player appears on open
grass terrain near the world origin, consistent with a spawn area. The image
does not identify the save by name or show the world-load transition. A
matching workspace save provides corroborating metadata:
`build/Release/saves/World (2)/world.meta` has internal name `World`, seed
`123456`, Survival mode, player position `(8.421, 72.000, 8.348)`, and spawn
`(8.500, 72.000, 8.500)`. The metadata was saved at 11:39:07; the screenshot
file was saved at 11:39:12, and its displayed coordinates match the saved
player position. This is consistent with a single-seed spawn sample near the
recorded spawn, with visible open terrain. It does not test other seeds or
prove the full player clearance volume, and the timestamps do not replace a
log linking the image to the executable session.

This overlay reads `5258.2 FPS` and `0.18 ms/frame`; the source's shared-window
calculation implies about `0.19 ms/frame` at that FPS. It also reads
`0.00 ms/tick` and `0.0 ticks/s`, so the image does not show that simulation
was advancing during the sample. A process for the exact workspace Release
executable was observed shortly before this screenshot, and the matching
world save is in that executable's working directory, but there is no
associated log recording this sample's duration or state. There is no visible
water or water interaction in the scene.

### Exact workspace Release run

The first captured log belongs to the Release executable in this workspace,
launched from the repository root with stdout/stderr redirected to
`phase0-runtime.log`. That working directory selected the root
`config/settings.cfg` (render distance 4, FOV 70, VSync on). The in-world
portion ran from 11:28:36 until normal
shutdown at 11:32:36. It loaded the existing `sss` save at render distance 4;
this was not the screenshot's seed-123456 scene and was not a fresh spawn
validation. There were 211 once-per-second gameplay metric samples between
11:29:00 and 11:32:33, excluding the logged pause interval and death/quit.

Across those active-play samples, the observed ranges were 171-182 FPS,
5.49-5.85 ms/frame, 0.01-0.20 ms CPU per fixed simulation tick, 42.0-60.7
simulation ticks/second, and 146-167 MiB process working set. The FPS and
frame-time readings are reciprocal within display rounding. This was a moving,
interactive session rather than a fixed-position 30-second capture; transient
chunk loading and player activity were included. No memory-leak conclusion can
be drawn from this sample.

The earlier user F3 screenshot remains separate evidence: it reads seed
`123456`, `1221.7 FPS`, `0.24 ms/frame`, `0.12 ms/tick`, `60.0 ticks/s`, and
`158 MiB`. The exact Release log does not reproduce those frame values (its F3
source uses the same frame-window counts and elapsed time as the logged
figures), and that first screenshot has no associated executable or log.

The later seed-123456 screenshot matches the `World (2)` save metadata and
coordinates. A process at the exact workspace Release path was observed at
11:37:57, and the matching save is under `build/Release/saves`; that working
directory's settings are FOV 110 with VSync off. By comparison, the first
runtime log was launched from the repository root and used FOV 70 with VSync
on. These settings explain why the runs' FPS levels are not directly
comparable. They do not reconcile the later screenshot's `5258.2 FPS` and
`0.18 ms/frame` pair; its displayed FPS implies about `0.19 ms/frame`.

The requested seed-123456 save is at
`build/Release/saves/phase0-baseline-20261007/world.meta`. Its metadata records
Survival, terrain version 3, player `(8.627, 72.000, 9.214)`, and spawn
`(8.500, 72.000, 8.500)`. Its adjacent `config/settings.cfg` has render
distance 4, FOV 110, and VSync off; settings are relative to the process
working directory. The exact Release executable is now running from
`build/Release`, with output captured to
[phase0-seed123456-runtime.log](../phase0-seed123456-runtime.log). This run
started from the main menu at 11:48:54. It confirms navigation through the
world-select and LAN menus, then opened the unrelated `dd` world (seed
`-2046695565`) and entered PAUSED at 11:50:56. It resumed at 11:53:35 and
paused again at 11:54:44. At the latest inspected sample (12:04:45), the
process remained open in `dd`, stationary at `(-250.7, 25.3, 376.1)`, with
0.0 ticks/s and 114 MiB reported RSS. That world is not being counted as the
seed-123456 gameplay check. No water interaction has been logged.

At 11:48:55, the uncapped main-menu sample was `3317.0 FPS`, `0.30 ms/frame`,
and `76 MiB`. After entering `dd` and pausing, a later sample was `4683.9 FPS`,
`0.21 ms/frame`, `0.0 ticks/s`, and `140 MiB`. Both FPS/frame pairs are
reciprocal within rounding. This confirms that VSync-off runs can reach
thousands of FPS and that a paused world can render while simulation ticks
remain at zero. It does not establish that the screenshot itself was paused.

## Performance

The existing renderer debug data reports last mesh, upload, and draw CPU
durations. F3 previously showed FPS but not average frame duration, simulation
cost, or process memory. Phase 0 added these read-only diagnostics to F3 and
the once-per-second log. Simulation cost uses SDL's high-resolution performance
counter around each fixed simulation tick; frame average is derived from the
existing one-second FPS window. Frame/FPS and renderer timings use the current
`TIME_UTC` clock helper, so a system-clock adjustment can skew those values;
the new per-tick simulation timing uses SDL's monotonic counter.

The first live screenshot reports `0.12 ms/tick` and `60.0 ticks/s`. It also
reports `1221.7 FPS` and `0.24 ms/frame`, which conflict with each other and
with the current source's shared-window calculation. The later screenshot's
`5258.2 FPS` / `0.18 ms/frame` pair is also not reciprocal at the displayed
precision and reads `0.0 ticks/s`. The exact executable was observed running
with VSync off in the matching working directory, and the new log confirms
that a paused world can report high FPS and zero simulation ticks. Neither
screenshot has a matching log for its exact sample, so the pause state and
frame-time discrepancy remain unverified. The workspace Release runs provide
internally consistent measurements with their actual settings recorded above;
neither is a controlled 30-second stationary trace. Debug microbench results
were captured on 2026-10-07 (evidence: `docs/phase0-debug-bench-mesher.log`,
`docs/phase0-debug-bench-mobs.log`); Debug is slower than Release and these
are CPU workload baselines only, not frame-time targets:

| Workload | Measured result (Debug) |
| --- | ---: |
| Checkerboard stone/air chunk mesh | 62.2 ms average, 20 rebuilds |
| Idle mob scenario, 64 mobs | 2.25 ms per 60-tick set |
| Chase mob scenario, 20 mobs | 0.80 ms per 60-tick set |
| Pathfinding | 0.125 ms/request, 200/200 found |
| 32 arrows vs 40 mobs | 0.70 ms per 60-tick set |
| Skeleton scenario, 10 mobs | 0.45 ms per 60-tick set |

## Memory

The prior code had no process working-set display. Phase 0 added an OS
working-set query (Windows, macOS, Linux), sampled once per second and
displayed in F3/log output. The automated test only checks the call is
safe and repeatable (a nonzero-or-zero assertion would fail in sandboxes
where the OS masks the value).

The exact workspace run reported 146-167 MiB working set during active play;
the supplied screenshot separately reports 158 MiB. The process grew from
68-78 MiB in the profile/menu screens to 146-167 MiB after loading the world
and later varied within the active-play range. That pattern is compatible with
loading resources and world chunks, but this short interactive run cannot rule
out a leak. Working set includes the process and its dependencies, is affected
by OS paging, and is not a leak detector. Memory growth during a fixed-duration
stress run is **not measured**.

## Known remaining issues

- Both screenshots' FPS/frame-time disagreement remains unexplained; the exact
  workspace Release run has consistent but separate readings.
- One seed-123456 screenshot is consistent with saved world metadata, but no
  log ties the image to that save or exact executable session. It does not
  establish behavior across seeds. The `sss` save used for the first workspace
  run was modified.
- A live water-flow/retraction observation and visual comparison against
  authoritative fluid state are still missing.
- The seed-123456 screenshot has no associated log or timed capture; its
  `0.0 ticks/s` reading leaves it unclear whether the simulation was advancing
  or the game was paused.
- Full menu/settings/resolution interaction is not covered by the observed
  profile-to-world path and pause/resume transition.
- A fixed-duration memory stress trace and leak detector are unavailable.
- Spawn validation, water appearance, and UI navigation coverage remain
  limited to the code and automated-test observations above.
- The current frame and renderer timing paths use the wall-clock helper;
  unexpected system-clock changes can skew them.
- No leak detector, GPU timer-query data, or separate fluid/physics/animation/
  particle timing is available yet.

## Diagnostic changes made for Phase 0

- Added average frame milliseconds, average fixed-tick simulation CPU time,
  simulation ticks/second, and process resident memory to F3 and periodic
  logs.
- Added a focused test for the OS working-set query.
- No gameplay mechanics were changed.

## Status

**BLOCKED**: builds, tests, a seed-123456 screenshot consistent with saved
metadata, and live gameplay/performance are evidenced. The uploaded image in
this turn duplicates the earlier screenshot. Live water flow/presentation, a
log-backed sample from the seed-123456 save, and the screenshot telemetry
discrepancy remain unresolved. The universal finite-state gate also has no
runtime instrumentation that checks positions, velocities, transforms, and
physics state together. Phase 1 has not started.

## To unblock Phase 0

Use the existing seed-123456 save at
`build/Release/saves/phase0-baseline-20261007` or the matching screenshot save
`build/Release/saves/World (2)`. The exact workspace Release executable is
open from `build/Release` with output captured to
`phase0-seed123456-runtime.log`; its settings are render distance 4, FOV 110,
VSync off, Default pack, and 1280x720. Return from the currently open `dd`
world to world select and open the seed-123456 save. Once chunks settle, keep
the player still for 30 seconds with the simulation running, then capture F3
and retain the matching log samples. Observe water flow and retraction if
water is nearby (otherwise record that it is unavailable). Exercise
pause/resume and return to the main menu once. Close normally and keep the
resulting log and screenshot. Add focused diagnostics for non-finite
simulation state before claiming the universal runtime gate passes. These
observations are required before Phase 0 can be marked complete and Phase 1
can begin.
