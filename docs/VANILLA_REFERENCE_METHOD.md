# How TerraCraft derives and checks vanilla behaviour

## Available references (audited 2026-11-02)

| Reference | Where | Use |
|---|---|---|
| Installed game jars `26.2`, `26.3`, `26.4-snapshot-*` | `%APPDATA%\.minecraft\versions\` | **Not 1.21.11.** Unobfuscated; class files are Java 25 (v69). Used read-only (`javap -c`) to cross-check constants of subsystems that are unchanged in code shape. Each use is flagged in the doc that cites it. |
| **`1.21.11` client + server jars, official Mojang mappings** | `reference/1.21.11/` (git-ignored). Downloaded 2026-11-02 with the owner's approval from `piston-data.mojang.com`; SHA-1s verified (`tools/reference/fetch_reference.sh`). client `ba2df812...` (31 MB), server `64bb6d76...` (56 MB), mappings 12 MB + 8.7 MB. Runs on JDK 21. | **The authoritative target.** Obfuscated; read with `javap` + `client_mappings.txt`. The server jar's data generator (`--reports --server`) produced `reference/1.21.11/run/out/`: registries, `blocks.json` (1,166 blocks, **29,671 block states**), 1,505 items, 157 entity types, and the complete worldgen data pack (density functions, noise settings, biome parameter lists, carvers, features, structures). |
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

## Verification against the 1.21.11 jar itself

Done after the jar became available (2026-11-02):

- `ItemInHandRenderer` (`hof`): its 91 distinct float/double constants are
  **identical** to 26.3's `FirstPersonHandsAndItemsRenderer`, so the golden
  matrices (generated through 26.3's math classes) are valid for 1.21.11.
- `Mth` (`bgj`): same table factor `10430.378350470453` and `+16384` cosine
  offset. `GameRenderer.bobView` constants (0.5, 3.0, 5.0, 0.2) match.
- Item display transforms in the 1.21.11 jar's `item/generated`, `handheld`
  and `block/block` equal the supplied `mcassets` values.
- **Correction found by this check:** 1.21.11 already uses
  `Player.getItemSwapScale` (separate `itemSwapTicker`). The hand dips when
  the main-hand item *type* changes or on an air-swing miss
  (`resetAttackStrengthTicker` resets both tickers), but a melee hit
  (`onAttack` -> `resetOnlyAttackStrengthTicker`) does **not** dip the hand.
  The first version of `hand_view` assumed a dip on hits; it is fixed and
  tested.
- The supplied `mcassets/` has 0 spear item models; the 1.21.11 jar has 7. The
  pack predates 1.21.11 content and should be replaced by assets extracted from
  `client.jar` (local only; assets must not be committed).

## Limits of this evidence

- Golden matrices prove the transform *composition*. They do not prove the
  rendered pixels; no frame-vs-frame comparison has been run yet.
- The world-generation reference data now exists locally but no differential
  harness has been written yet (see WORLDGEN_PARITY.md).
