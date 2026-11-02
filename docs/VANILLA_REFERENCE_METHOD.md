# How TerraCraft derives and checks vanilla behaviour

## Available references (audited 2026-11-02)

| Reference | Where | Use |
|---|---|---|
| Installed game jars `26.2`, `26.3`, `26.4-snapshot-*` | `%APPDATA%\.minecraft\versions\` | **Not 1.21.11.** Unobfuscated; class files are Java 25 (v69). Used read-only (`javap -c`) to cross-check constants of subsystems that are unchanged in code shape. Each use is flagged in the doc that cites it. |
| `1.21.11` jar | **Not installed.** `version_manifest_v2.json` lists it (and `1.21.11-rc*`) | Needed for any world-generation differential test. Downloading it needs the owner's go-ahead (see roadmap). |
| Supplied assets `mcassets/` (git-ignored) | repo root | Real models/blockstates/textures/sounds. No `spear` item models, so the pack predates 1.21.11 content. |
| `eaglercraft/` | repo root (git-ignored) | 1.8-era client sources; only useful for old-behaviour cross-checks, never as a 1.21.11 reference. |

No Mojang code is copied into the repository. Constants and call sequences are
read from the installed game and re-implemented in C; the Java utility below
only *executes the game's own math classes* to produce golden vectors.

## Method used for the first-person hand

1. `javap -c -p -constants` on `FirstPersonHandsAndItemsRenderer`,
   `FirstPersonHandsAndItems`, `GameRenderer`, `PlayerModel`,
   `ModelPart$Cube/$Polygon`, `AvatarRenderer`, `Mth` to read every constant
   and call order (translate/rotate sequence, 0.4 clamp, 0.1 sway factor,
   `zRot = 0.1`, Mth's 65536-entry sine table with factor
   `10430.378350470453`).
2. `tools/reference/HandGolden.java` replays those sequences through the
   game's real `PoseStack`, `Axis` and `Mth` and prints column-major matrices.
   `tests/fixtures/hand_golden.h` is its output; `tests/test_hand_view.c`
   compares the C port against it (tolerance **1e-4 absolute**).
3. Tick-state rules (swing 0→5/6, equip dip sequence, attack-strength cube,
   walk-bob smoothing) are unit-tested against sequences derived by hand from
   the tick rules in the same bytecode.

Regenerating the fixture needs a game jar. Because the installed jars are
class-version 69 and the available JDK is 21, `tools/reference/gen_hand_golden.sh`
extracts the needed classes, downgrades their class-file version byte, and
runs the generator on JDK 21. This is a developer-only tool; it is not part of
the shipped game or the default test run.

## Limits of this evidence

- 26.3 refactored the hand renderer (`FirstPersonHandsAndItemsRenderer`;
  equip target uses `getItemSwapScale` instead of attack-strength³). The
  1.21.11 behaviour used here (cube of attack strength) is from knowledge of
  that release, **not** verified against a 1.21.11 binary. See KNOWN_DIFFERENCES.
- Golden matrices prove the transform *composition*. They do not prove the
  rendered pixels; no frame-vs-frame comparison has been run yet.
- For the world generator the reference method is to run vanilla 1.21.11's
  own chunk generator headlessly from a 1.21.11 server jar and dump biomes,
  heightmaps and block states to CSV for the C harness to diff. This has not
  been started.
