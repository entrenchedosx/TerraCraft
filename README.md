# TerraCraft

TerraCraft is an independent, single-player voxel sandbox written in C17.
It has a streamed block world, Survival and Creative modes, persistent
saves, crafting, tools, food, day and night, creatures, melee combat, and
arrows. The renderer uses SDL2 and OpenGL 3.3.

The project is an original implementation. It is not affiliated with or
endorsed by Mojang or Microsoft. It does not include Minecraft code or
assets. Procedural textures and synthesized sounds keep a fresh checkout
playable; optional local assets and user resource packs are described in
[Resource Packs](docs/RESOURCE_PACKS.md).

## Current status

The current milestone is M9 (v0.9.0). This is a playable prototype, and its
core rules are still being refined for familiar voxel-game movement and
interaction. The active fidelity pass now includes pixel-art survival
vitals, persistent living mobs, versioned terrain profiles, and bounded water
flow. See
[Java Fidelity](docs/JAVA_FIDELITY.md) for behavior targets and known
differences; the project's documented parity and visual checklist remains
open.

## Features

- Seeded terrain with seven biomes, caves, ores, trees, and vegetation.
- Versioned terrain profiles for legacy-save continuity, with connected
  mountain belts, sharper peaks, varied foothills, and lowland river channels
  in newly created worlds.
- Bounded source/flow/falling water with partial-height rendering and basic
  player buoyancy.
- Chunk streaming, block editing, lighting, ambient occlusion, fog, and a
  day/night cycle.
- Creative flight and Survival health, hunger, mining, tool wear, food,
  crafting, item drops, and death recovery.
- Pixel-art full/half/empty heart and hunger icons in Survival.
- Cows, zombies, and skeletons with real skins, pathfinding, melee, bows,
  and arrows. Living mobs retain their type, position, facing, and health
  when a world is saved and reopened.
- Persistent named worlds with versioned block, player, inventory, and
  entity data.
- A resource-pack system with procedural fallback art and sound.
- A 60 ticks/second world simulation; inventory and crafting keep the world
  moving, while pause and death stop it.
- 171 registered headless test functions in the custom test runner.

## Controls

| Input | Action |
| --- | --- |
| `W A S D` | Move |
| `Space` | Jump; fly upward in Creative |
| `Shift` | Sneak; fly downward in Creative |
| `Ctrl` | Sprint |
| `F` | Toggle Creative flight |
| `1`–`9`, mouse wheel | Select hotbar slot |
| Left mouse | Mine a block or attack a creature |
| Right mouse | Place/use the selected item |
| `E` | Open inventory |
| `Esc` | Pause or close the current screen |
| `F3` | Toggle debug overlay |
| `F5` | Toggle first/third-person camera |

Developer-only keys are kept in source comments and are not needed for
ordinary play.

## Build and run

### Dependencies

- CMake 3.20 or newer
- A C17 compiler
- SDL2 development files
- OpenGL 3.3 Core support

Ubuntu/Debian:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake libgl1-mesa-dev libglu1-mesa-dev libsdl2-dev
```

Fedora:

```sh
sudo dnf install -y gcc cmake mesa-libGL-devel SDL2-devel
```

macOS with Homebrew:

```sh
brew install cmake sdl2
# Install Xcode Command Line Tools if needed: xcode-select --install
```

Windows with MSVC and vcpkg:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install sdl2:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

Configure and build on Linux, macOS, or Windows:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Run the game:

```sh
# Linux / macOS
./build/terracraft

# Windows MSVC
.\build\Release\terracraft.exe
```

The first screen is the title menu. Choose **Singleplayer**, create a world,
and select Survival or Creative. Worlds are stored under `saves/` and
settings under `config/` beside the game.

## Tests

```sh
ctest --test-dir build -C Release --output-on-failure
```

For a Debug build, configure a separate directory and run the same suite:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --config Debug
ctest --test-dir build/debug -C Debug --output-on-failure
```

The test runner covers CPU-side gameplay, world generation, persistence,
render geometry, atlas mappings, audio parsing, and UI logic. It does not
replace a hands-on playtest; the current visual and movement checklist is
in [docs/FIDELITY_BUGS.md](docs/FIDELITY_BUGS.md).

## Documentation

- [Java behavior targets and current differences](docs/JAVA_FIDELITY.md)
- [Terrain profiles, water states, and save compatibility](docs/TERRAIN_AND_WATER.md)
- [Fidelity bug log and playtest checklist](docs/FIDELITY_BUGS.md)
- [Gauntlet review procedure and round notes](docs/GAUNTLET.md)
- [Gauntlet Loop research and review protocol](docs/GAUNTLET.md)
- [Build and test decisions](docs/DECISIONS.md)
- [Milestone history and backlog](docs/BACKLOG.md)
- [World and entity file formats](docs/FORMAT.md)
- [Texture and sound packs](docs/RESOURCE_PACKS.md)

## Repository contents and reuse

`src/` contains the engine and gameplay code, `tests/` the headless test
suite, `bench/` small performance tools, and `tools/` the optional asset
converter. Local `saves/`, `config/`, `resourcepacks/`, `mcassets/`, and
assistant memory files are excluded from Git. A clean clone builds without
those files and uses its procedural/synthesized fallbacks.

No project license is included yet. Ask the repository owner before
redistributing or reusing TerraCraft code.
