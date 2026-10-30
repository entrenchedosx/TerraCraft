# Foundation test results

Date: 2026-10-30. **Partial verification, not overhaul acceptance.**

## Automated checks

| Check | Result / evidence |
| --- | --- |
| Exact original dirty-workspace snapshot, Release | 243 tests passed; `build/foundation/reference-tests.log` |
| Final UI implementation, Release | 254 tests passed, including permission denial; `build/foundation/final-release-tests.log` |
| Final UI implementation, Debug | 254 tests passed, including permission denial; `build/foundation/final-debug-tests.log` |
| Asset converter synthetic fixtures | 8 tests passed; `build/foundation/converter-tests.log` |
| Native panorama GLSL | Vertex/fragment stages validated offline during implementation; runtime shader initialized successfully |
| Local conversion | 73 tiles including two arm crops, 108 sounds, four skins, font, three button states and six native panorama faces |

The 11 added registered test functions cover nine-slice bounds/UVs and
tiny destinations; pointer DPI mapping; GUI scales at four resolutions;
enabled focus navigation and captured/fast clicks; native bitmap bounds
and file fallback; strict window-size arguments; wrapping/input tails.
The existing LAN rule-editing test now checks guest denial both with and
without cheats, followed by legitimate host broadcast/persistence.

Builds use the existing Visual Studio/CMake configuration and SDL 2.30.8.
The old cached SDL path referred to a deleted temporary directory. The
same dependency version was restored from the official SDL release into
ignored `build/foundation/dependencies/`; no dependency was vendored.
Warnings remain errors (`TERRACRAFT_WERROR=ON`).

## Actual game observations

The game **was launched and visually inspected**. Mouse interaction tested
the title screen, Options/Back, world list, Create World and new-world
loading. The created Survival test world is the separate runtime save
`build/Release/saves/World (3)` (seed 1100512252, terrain profile 4).
It reached a dry surface with the HUD and first-person arm visible.
That observation is not an animation or movement acceptance test.

| Resolution/check | Observation |
| --- | --- |
| 1280x720 | Full title/profile/Options/world-browser and gameplay captures; mouse actions worked |
| 1920x1080 | Log confirms drawable1920x1080; full menu capture inspected, Quit button exited normally |
| 2560x1440 | Log confirms native drawable; desktop capture exposes only a1920x1080 crop: whole-window visual gate OPEN |
| 3840x2160 | Log confirms native drawable; visible crop is obstructed by a Windows security prompt: visual gate OPEN |
| High-DPI >1x monitor | Coordinate transformations pass unit tests; live non-1x monitor verification UNVERIFIED |
| Keyboard/text | Arrow focus observed; Return/text/E/Esc/T automation unreliable. Physical user check requested, OPEN |
| Inventory/crafting/Creative/death/loading/chat | Shared code integrated; complete live screen acceptance OPEN |
| Existing save reload | Existing worlds were listed; current-run reload acceptance OPEN; automated save compatibility tests included |
| Two-machine LAN | UNVERIFIED; loopback transport/dispatcher tests included |

A Windows Firewall dialog for the test executable was discovered covering
the desktop. It may explain unreliable injected keys. No automated action
was taken on that security prompt; the user was asked to dismiss it.
No firewall setting was changed by the agent. The user subsequently reported
the prompt dismissed and offered to perform the physical keyboard checks;
results of those checks are still pending.

### Local screenshot evidence

These files are deliberately excluded from Git because they include
owner-local Minecraft artwork. They are runtime captures, not mockups:

- `build/foundation/evidence/main-menu-720p.jpg`
- `build/foundation/evidence/settings-720p.jpg`
- `build/foundation/evidence/world-browser-720p.jpg`
- `build/foundation/evidence/gameplay-720p.jpg`
- `build/foundation/evidence/main-menu-1080p.jpg`
- `build/foundation/evidence/main-menu-1440p-visible-crop.jpg`

The first720p capture preceded the last field/browser refinements. Newer
code does not gain visual acceptance merely by reusing an earlier image.

## Performance and memory

Reference snapshot menu samples: approximately178–180 FPS,5.56–5.62ms/frame,
140MiB resident memory (`reference-runtime.log`). Updated1080p menu samples:
approximately180 FPS,5.56ms/frame,89–92MiB resident memory. Updated4K menu
samples:180 FPS,5.56ms/frame,149MiB resident memory. These are app-reported
frame averages while multiple test windows could exist, with frame limiting
active; they are **not a controlled GPU benchmark or a measured speedup**.

Native1024-square RGBA faces require24MiB of texture payload before driver
overhead. The legacy stitched texture is no longer loaded when native
faces are usable. Resources have explicit partial-failure and destroy
cleanup paths. This does not prove absence of leaks over long play sessions.
Frame-time percentile/stall tracing, GPU timings and a prolonged leak test
remain UNVERIFIED.

A separate no-local-assets smoke run started successfully with the original
landscape/font/button fallbacks and sampled180 FPS,5.56ms/frame,78–81MiB RSS.
This was a startup/log check, not a visual acceptance or leak test.

## Reproduction

```powershell
cmake --build build --config Release --target terracraft terracraft_tests --parallel 4
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Debug --target terracraft terracraft_tests --parallel 4
ctest --test-dir build -C Debug --output-on-failure
python tools/test_ui_asset_conversion.py
build/Release/terracraft.exe --window-size 1920x1080
```

Use the same executable with1280x720,2560x1440 and3840x2160. The CLI rejects
malformed/overflowing dimensions. Owner-local conversion remains optional:
`python tools/convert_mcassets.py`. Build asset synchronization runs even
when the executable does not need relinking.
