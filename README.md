# TerraCraft

TerraCraft is an independent voxel sandbox written in C17. It supports
single-player worlds and direct player-hosted multiplayer over a local
network (LAN); it has no dedicated or internet server.
It has a streamed block world, Survival and Creative modes, persistent
saves, crafting, tools, food, day and night, creatures, melee combat, and
arrows. The renderer uses SDL2 and OpenGL 3.3.

The project is an original implementation. It is not affiliated with or
endorsed by Mojang or Microsoft. It does not include Minecraft code or
assets. Procedural textures and synthesized sounds keep a fresh checkout
playable; optional local assets and user resource packs are described in
[Resource Packs](docs/RESOURCE_PACKS.md).

## Current status

The current milestone is M9 (v0.9.0), with an active fidelity and LAN
multiplayer pass. This is a playable prototype, and its core rules are still
being refined for familiar voxel-game movement and interaction. The active
pass includes pixel-art survival vitals, persistent living mobs, versioned
terrain profiles, bounded water flow, player profiles, and LAN play. See
[Java Fidelity](docs/JAVA_FIDELITY.md) for behavior targets and known
differences; the project's documented parity and visual checklist remains
open.

## Features

- Seeded terrain with seven biomes, caves, ores, trees, and vegetation.
- Versioned terrain profiles for legacy-save continuity, with connected
  mountain belts, sharper peaks, varied foothills, and lowland river channels
  in newly created worlds.
- Bounded source/flow/falling water with partial-height rendering and
  depth-aware player, mob, and dropped-item buoyancy.
- Continuous fixed-tick sand physics with collision, chain reactions,
  water displacement, safe save/unload settling, and a separate falling-block
  render pass.
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
- A local username profile; first launch asks for a name, and leaving it blank
  generates a two-word name with a four-digit suffix.
- Direct LAN play: one player opens a world to LAN, and up to eight other
  players can join by the host computer's local IPv4 address. Player movement,
  chat, and block changes made during the session are shared.
- 191 registered headless test functions in the custom test runner.

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
| `T` | Open LAN chat while playing |

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

On first launch, choose a username or leave it blank to generate one. Then
choose **Singleplayer**, create a world, and select Survival or Creative.
Worlds are stored under `saves/` and settings and the local profile under
`config/` beside the game.

## LAN multiplayer

Create or open a world, press `Esc`, and choose **Open to LAN**. The host
continues playing and owns the world for the session. On each other computer,
choose **Multiplayer (LAN)** from the title menu and enter the host's local
IPv4 address. All players must be on the same local network; TerraCraft uses
TCP port `25566` and has no internet matchmaking, relay, or dedicated server.
Press `T` in the world to open chat, type a message, then press `Enter` to
send it (`Esc` closes chat without sending).

This initial LAN implementation shares player positions, chat, and block
changes made while peers are connected. Clients generate terrain from the
host's seed and terrain version and keep a local save copy. It does not yet
transfer edits made before a client joins, inventories, mobs, dropped items,
or other entity state. Chat accepts printable ASCII and is limited to 160
characters. LAN sessions are intended for trusted local networks.

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
- [Fluid scheduling, falling-block physics, persistence, and current limits](docs/WORLD_PHYSICS.md)
- [Fidelity bug log and playtest checklist](docs/FIDELITY_BUGS.md)
- [Gauntlet review procedure and round notes](docs/GAUNTLET.md)
- [Phase 0 baseline and runtime evidence](docs/PHASE0_BASELINE.md)
- [LAN setup, controls, and synchronization scope](docs/LAN_MULTIPLAYER.md)
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
