# Foundation overhaul

Status on 2026-10-30: **Phase 1 implemented in part; quality gate OPEN.**
The eight-phase overhaul is not complete and no release is being created.
The reference remains Java Edition 26.3 as selected by the existing project;
no direct Java runtime comparison has been performed during this pass.

## Starting point

Work started from `b0fa313` plus substantial existing uncommitted changes.
Those changes were preserved byte-for-byte in an ignored baseline snapshot,
rebuilt independently and tested: 243 registered tests passed in Release.
Commit `7dc326d` checkpoints that existing movement/animation/LAN work.
The following changes are separate from that checkpoint.

README, fidelity reports, worldgen/resource-pack documentation and backlog
were read. Audits covered UI, renderer/font/atlas, gameplay/controller,
simulation, world generation/storage, LAN dispatch, assets and tests. The
working source takes precedence over stale historical milestone entries.

## Phase 1 changes and root causes

| Problem | Cause | Implemented change |
| --- | --- | --- |
| Blurry panorama | Native 1024-square faces reduced to a 1024x512 image, then only a small crop enlarged | Native six-face perspective renderer; aspect-correct camera rays and slow yaw/pitch; linear filtering for the background only |
| Flat, inconsistent controls | Two separate button drawing functions; usable local widget sprites never converted | Shared normal/highlighted/disabled skins, nine-slice corners, pressed tint, coherent original fallback, click audio |
| High-DPI click mismatch | Logical SDL cursor coordinates compared against drawable-pixel geometry | Per-axis logical-to-drawable mapping, followed by the same canvas mapping used by rendering |
| Tiny menus at high resolution | Most screens used fixed pixels; title scaling capped separately | Shared integer native GUI scale 2/3/4/6 at 720p/1080p/1440p/4K; logical canvas applied to menus, inventory, crafting, HUD and chat |
| Uneven text/centering | Fractional glyph magnification and mismatched measurement | Shared scale quantization in measurement/drawing; drawable-pixel origin snapping |
| Missing keyboard focus | Per-screen special cases and no shared control state | Tab/Shift+Tab, arrow navigation where appropriate, disabled-item skipping, Return activation, slider arrow adjustment and text-field focus |
| Accidental button activation | Press-edge activation and no gesture capture | Press/release capture, outside-release cancellation, no transfer across screens, support for complete taps between render frames |
| World browser overflow | Narrow rows and raw epoch timestamps | Wider rows, readable UTC dates, consistent selection border and wheel selection |
| Stale executable-local assets | Copy ran only after linking; directory-level lookup hid valid files in the working tree | Always-run asset synchronization dependency; per-file lookup, dimensions and source-path diagnostics |
| First-launch profile failure | Launcher working directory could be unwritable/unrelated | Preserve an existing launch-directory runtime tree; otherwise select executable directory before initialization |
| Small/overflowing chat | Fixed tiny font, no measured wrapping | Shared canvas, two-pixel baseline text, bounded proportional wrapping and input-tail clipping; ring history/fade semantics retained |
| Native BMP rejection/unsafe heights | 4MiB bound rejected a 1024-square RGBA image plus header; INT32_MIN height negation and overlapping header offsets unchecked | Bounded 16MiB+4096 reader, native image tests, safe height and data-offset rejection |
| Game Rules guest privilege mismatch | Read-only guest UI but accepted guest RULES_SET packets | Reject remote rule-change requests; retain host-local edits, RULES synchronization and persistence |

### Assets and fallback

Only existing owner-supplied files were converted. Buttons are 200x20 with
source borders 3/3/1. Panorama faces retain their native 1024x1024 pixels.
The converter also retains legacy output for compatibility. Present but
invalid inputs fail conversion with diagnostics; missing optional inputs
are reported. No Minecraft download or proprietary asset is committed.

Screenshots containing owner-local artwork stay in ignored
`build/foundation/evidence/`. Clean checkouts retain original fallback
textures/font/landscape. Their live visual acceptance is still OPEN.

### Compatibility

No terrain-generation function, terrain profile, block ID, save layout,
inventory transaction or movement constant was changed by this UI pass.
New worlds still use profile 4; profiles 1–3 remain available. The existing
LAN protocol remains v4. Reserved RULES_SET is rejected; legitimate
host-local rule changes still broadcast and persist. No slash commands
are exposed yet.

## Exact source changes after the checkpoint

- `src/core/app.c`, `src/core/app.h`: UI input/group state, chat presentation,
  remote world-rule request rejection.
- `src/core/bmp.c`, `src/core/bmp.h`: native bounded images and per-file lookup.
- `src/core/path.c`, `src/core/path.h`: runtime-directory selection.
- `src/core/launch_options.c`, `src/core/launch_options.h`, `src/main.c`:
  validated `--window-size WIDTHxHEIGHT` and `--help`.
- `src/platform/window.c`, `src/platform/window.h`: pointer DPI mapping.
- `src/render/menu_panorama.c`, `src/render/menu_panorama.h`: owned native
  panorama pipeline and cleanup/state restoration.
- `src/render/renderer.c`, `src/render/renderer.h`: button skins, logical UI
  canvas, pixel font alignment and HUD scaling.
- `src/ui/ui.c`, `src/ui/ui.h`: shared control capture/focus.
- `src/ui/ui_style.c`, `src/ui/ui_style.h`: pure geometry/scaling/focus helpers.
- `src/ui/chat_layout.c`, `src/ui/chat_layout.h`: bounded ASCII layout.
- `src/ui/screens.c`: shared styling, canvas, field/slider focus and browser.
- `tools/convert_mcassets.py`, `tools/test_ui_asset_conversion.py`:
  native menu conversion and synthetic tests.
- `tests/test_ui_metrics.c`, `tests/test_ui_controls.c`,
  `tests/test_menu_bitmaps.c`, `tests/test_launch_options.c`,
  `tests/test_chat_layout.c`, `tests/test_lan.c`: regression coverage.
- `CMakeLists.txt`, `tests/test_main.c`, `tests/test_main.h`: build/test and
  asset synchronization registration.

## Phase progression

| Phase | State |
| --- | --- |
| 1 UI | IN PROGRESS: code and mouse-driven runtime checks; remaining visual gates listed separately |
| 2 First-person animations | AUDITED, NOT STARTED: two attack clocks and frame-rate/geometry checks remain |
| 3 Movement/camera | AUDITED, NOT STARTED: source uses 60 TPS; local render interpolation missing |
| 4 Lighting/rendering | AUDITED, NOT STARTED: separate sky/block light and torch propagation absent |
| 5 Terrain | NOT MODIFIED: profile 4 preserved; new before/after seed comparison pending |
| 6 Commands | NOT STARTED: existing chat does not have a command registry |
| 7 Final foundation polish | AUDITED, NOT COMPLETE |
| 8 Final verification | PARTIAL: this phase's tests do not certify the whole overhaul |

Phases advance only when the preceding phase's quality gate passes. A
successful compilation or one good screenshot does not close a gate.
See [test results](FOUNDATION_TEST_RESULTS.md) and
[remaining issues](FOUNDATION_REMAINING_ISSUES.md).
