# Foundation remaining issues

2026-10-30: **OPEN unless explicitly marked otherwise.** Phases2–8 have
not been represented as finished. Current source and reproducible runtime
observations take precedence over historical milestone claims.

## Phase 1 gate

| Severity | Issue | Required closure evidence |
| --- | --- | --- |
| P1 | Complete keyboard/text flow not established during automation | Physical Tab/ShiftTab/Return, text entry, E/Esc/T; first-launch username and Create World |
| P1 | All interfaces not yet playtested | Inventory/storage split/merge, crafting, Creative selection, pause/rules, death/respawn, loading, chat fade/history |
| P2 | Entire1440p/4K windows not captured | Full-size capture with no obstructing security dialog; assess labels, margins and each action |
| P2 | Live high-DPI input mapping unverified | >1x display, resize/move across monitors, hover/click/slider alignment |
| P2 | Original clean-checkout fallback not visually accepted | Run without optional owner assets; check font, button states and background |
| P2 | Inventory/catalogue still use original panel/slot art | Integrate suitable authorized local container artwork or refine original layout without replacing inventory logic; visually verify |
| P2 | Long world names/labels and tooltip polish incomplete | Constrain text to bounds; long names, resource-pack names, tooltips and narrow windows |
| P2 | UI sound timing not heard/compared | Mouse and keyboard activation, denied/disabled clicks, pack sounds |
| P3 | Transition polish incomplete | Assess subtle movement/focus transitions without delaying input |

## Audited later-phase work

- **P1 / Phase2:** Attack pose is driven by both simulation-updated
  `PlayerAnim` and a wall-clock punch override in `app.c`. Unify action timing,
  then verify idle/mining/placing/tool/use/eating/bow at30/60/144/240 FPS and
  low/high FOV. Current visual capture shows an arm; it does not certify its
  orientation, attachment, clipping, animation or skin.
- **P1 / Phase3:** Authoritative simulation is60 TPS. Local player/mob
  positions still render directly from current simulation state; render
  interpolation remains absent. Preserve collision/aim/save authority.
  Recheck shore exit, sneak edges, acceleration, jumps and sprint FOV.
- **P1 / Phase4:** Chunk data has no sky/block-light channels. The mesher's
  short skylight probe/AO and global sun shader do not implement torch
  propagation. Test a roof more than12 blocks above, torch add/remove and
  cross-chunk changes when implementing real lighting.
- **P2 / Phase4:** Transparency sorts chunks rather than individual faces;
  glass/water intersections can remain wrong. Fog, water, seam, gamma and
  viewmodel-depth checks remain open.
- **P2 / Phase5:** No terrain rule changed. Profile4 must remain frozen.
  Future tuning affecting unvisited chunks needs a new version plus seed,
  negative-coordinate, order, spawn and save/reload diagnostics.
- **P1 / Phase6:** Slash commands, command history, registry, completion,
  relative coordinates and permissioned authoritative dispatch do not exist.
  Implement actual supported handlers only after preceding gates pass.
  Mode/time/fill replication need explicit networking design.
- **P2 / Phase7:** End-to-end focus, Alt-Tab, resizing/fullscreen, asset
  catalogue, sounds, save/reload, prolonged memory use and Release-only
  behavior still need a complete playtest. This pass cannot promise zero
  defects or zero memory leaks.
- **P2 / Phase8:** Full regression and two-machine LAN/manual Java comparison
  remain required even though headless tests pass.

## Closed source defects with limited verification

- High-DPI coordinate mismatch: fixed, pure regression tests pass;
  live multi-DPI check still open.
- Ignored button art and destructive panorama downsampling: fixed;
  loaded native sprites/faces confirmed in runtime logs and captures.
- Guest Game Rules privilege mismatch: fixed by rejecting RULES_SET from
  guests; tests exercise denial and legitimate host synchronization.
- BMP minimum signed height and header overlap: fixed and covered by
  malformed-input regression cases.

## Documentation contradictions

`FIDELITY_BUGS.md` TC-FID-004 still describes20 TPS; source is60 TPS.
TC-FID-008 says static water/no swim, but fluid flow/buoyancy/shore exit exist.
`JAVA_FIDELITY.md` contains historical profile3 and item-count claims.
The backlog's old lighting-done checkbox refers to the heuristic, not
propagated light. Treat those as historical records; this report identifies
the current implementation gaps without silently closing old playtest issues.
