cmake_minimum_required(VERSION 3.20)

project(terracraft VERSION 0.1.0 LANGUAGES C)

# --- Language settings -------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# --- Build type default (single-config generators) ---------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# --- Warnings ----------------------------------------------------------------
# TERRACRAFT_WERROR=ON turns warnings into errors (used by CI; default OFF so
# a new compiler's novel remark never blocks local iteration).
option(TERRACRAFT_WERROR "Treat compiler warnings as errors" OFF)
if(MSVC)
  add_compile_options(/W4 /permissive-)
  if(TERRACRAFT_WERROR)
    add_compile_options(/WX)
  endif()
  # fopen/fprintf/etc. are used with explicit bounds checking throughout;
  # silence the MSVC deprecation nag (not a real diagnostic).
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(TERRACRAFT_WERROR)
    add_compile_options(-Werror)
  endif()
endif()

# macOS 10.14+ deprecates OpenGL; TerraCraft targets GL 3.3 core
# deliberately, so silence the deprecation warning build-wide on Apple.
if(APPLE)
  add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

# --- Dependencies ------------------------------------------------------------
# SDL2: prefer CONFIG package (vcpkg / upstream), fall back to Find module.
find_package(SDL2 CONFIG QUIET)
if(SDL2_FOUND)
  # vcpkg / SDL2 CMake config provides SDL2::SDL2 (and SDL2::SDL2main).
  if(TARGET SDL2::SDL2main)
    set(MINEC_SDL2_TARGETS SDL2::SDL2main SDL2::SDL2)
  else()
    set(MINEC_SDL2_TARGETS SDL2::SDL2)
  endif()
  set(MINEC_SDL2_FROM_CONFIG TRUE)
else()
  find_package(SDL2 REQUIRED)
  # Module mode defines SDL2_INCLUDE_DIRS / SDL2_LIBRARIES.
  set(MINEC_SDL2_FROM_CONFIG FALSE)
endif()

find_package(OpenGL REQUIRED)

# --- Core library (all engine code except main) -------------------------------
set(TERRACRAFT_SOURCES
  src/audio/audio.c
  src/core/log.c
  src/core/app.c  src/core/mem.c
  src/core/time.c
  src/core/noise.c
  src/core/hashmap.c
  src/core/path.c
  src/core/seed.c
  src/core/settings.c
  src/core/bmp.c
  src/platform/window.c
  src/platform/gl_ctx.c
  src/render/renderer.c
  src/render/shader.c
  src/render/camera.c
  src/render/mesher.c
  src/render/vao_manager.c
  src/render/texture_atlas.c
  src/render/font.c
  src/world/block.c
  src/world/chunk.c
  src/world/world.c
  src/world/world_gen.c
  src/world/streamer.c
  src/world/biome.c
  src/world/world_meta.c
  src/world/world_save.c
  src/world/entity_save.c
  src/game/player.c
  src/game/player_animation.c
  src/game/simulation_clock.c
  src/game/raycast.c
  src/game/interaction.c
  src/game/time_system.c
  src/game/game_state.c
  src/game/session.c
  src/game/item.c
  src/game/inventory.c
  src/game/input_buffer.c
  src/game/recipe.c
  src/game/particle.c
  src/game/mob.c
  src/game/mob_model.c
  src/game/projectile.c
  src/game/pathfind.c
  src/game/survival.c
  src/game/entity.c
  src/ui/hud.c
  src/ui/ui.c
  src/ui/screens.c
)

add_library(terracraft_core STATIC ${TERRACRAFT_SOURCES})
target_include_directories(terracraft_core PUBLIC src)

if(MINEC_SDL2_FROM_CONFIG)
  target_link_libraries(terracraft_core PUBLIC ${MINEC_SDL2_TARGETS} OpenGL::GL)
else()
  target_include_directories(terracraft_core PUBLIC ${SDL2_INCLUDE_DIRS})
  # Module mode often reports .../include/SDL2; also add its parent so that
  # both <SDL.h> and <SDL2/SDL.h> resolve regardless of distro layout.
  foreach(_sdl_dir IN LISTS SDL2_INCLUDE_DIRS)
    get_filename_component(_sdl_parent "${_sdl_dir}" DIRECTORY)
    target_include_directories(terracraft_core PUBLIC "${_sdl_parent}")
  endforeach()
  target_link_libraries(terracraft_core PUBLIC ${SDL2_LIBRARIES} OpenGL::GL)
endif()

# On some Linux setups SDL2 needs dl/pthread/m; harmless elsewhere.
if(UNIX AND NOT APPLE)
  target_link_libraries(terracraft_core PUBLIC m dl pthread)
endif()

# Require OpenGL 3.3 availability note for MSVC (opengl32 ships with SDK).
if(WIN32)
  target_link_libraries(terracraft_core PUBLIC opengl32)
endif()

# --- Main executable ---------------------------------------------------------
add_executable(terracraft src/main.c)
target_link_libraries(terracraft PRIVATE terracraft_core)

# Windows (VC / vcpkg layout): copy SDL2.dll next to the exe so it runs
# without manual PATH setup. Harmless when the DLL is not found.
if(WIN32)
  find_file(MINEC_SDL2_DLL NAMES SDL2.dll
    PATHS
      "${SDL2_DIR}/../lib/x64"
      "${SDL2_DIR}/../../lib/x64"
      "${CMAKE_PREFIX_PATH}"
    PATH_SUFFIXES bin lib/x64 lib
    NO_DEFAULT_PATH)
  # Fall back to a default-path search (e.g. vcpkg installed dir on PATH).
  if(NOT MINEC_SDL2_DLL)
    find_file(MINEC_SDL2_DLL NAMES SDL2.dll)
  endif()
  if(MINEC_SDL2_DLL)
    message(STATUS "Found SDL2.dll: ${MINEC_SDL2_DLL} (will copy next to terracraft)")
    add_custom_command(TARGET terracraft POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${MINEC_SDL2_DLL}" $<TARGET_FILE_DIR:terracraft>)
  else()
    message(STATUS "SDL2.dll not found for auto-copy; you may need to copy it manually next to terracraft.exe")
  endif()
endif()

# --- Tests -------------------------------------------------------------------
enable_testing()

add_executable(terracraft_tests
  tests/test_main.c
  tests/test_input_buffer.c
  tests/test_core.c
  tests/test_world.c
  tests/test_mesher.c
  tests/test_texture.c
  tests/test_streamer.c
  tests/test_physics.c
  tests/test_raycast.c
  tests/test_time.c
  tests/test_simulation_clock.c
  tests/test_mesher_ao.c
  tests/test_item.c
  tests/test_save.c
  tests/test_biome.c
  tests/test_settings.c
  tests/test_ui.c
  tests/test_gameplay.c
  tests/test_esave.c
  tests/test_craft.c
  tests/test_fx.c
  tests/test_mob.c
  tests/test_path.c
  tests/test_projectile.c
)
target_link_libraries(terracraft_tests PRIVATE terracraft_core)

# Tests link SDL2 transitively: on Windows the loader needs the DLL beside
# the test exe too (ctest runs it directly, no PATH setup guaranteed).
if(WIN32 AND MINEC_SDL2_DLL)
  add_custom_command(TARGET terracraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINEC_SDL2_DLL}" $<TARGET_FILE_DIR:terracraft>)
endif()

# Owner-converted assets beside the exe: the game resolves mcassets/
# exe-relative first, so any working directory works. Copies only when
# the source tree exists (absent on CI/fresh clones -> quiet skip, the
# procedural/synth fallbacks cover it).
if(EXISTS "${CMAKE_SOURCE_DIR}/mcassets/generated")
  add_custom_command(TARGET terracraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/mcassets/generated"
            "$<TARGET_FILE_DIR:terracraft>/mcassets/generated")
endif()

add_test(NAME terracraft_tests COMMAND terracraft_tests)

# --- Mesher benchmark (manual regression sniffing, not part of ctest) ------
add_executable(terracraft_bench bench/bench_mesher.c)
target_link_libraries(terracraft_bench PRIVATE terracraft_core)
if(WIN32 AND MINEC_SDL2_DLL)
  add_custom_command(TARGET terracraft_bench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINEC_SDL2_DLL}" $<TARGET_FILE_DIR:terracraft_bench>)
endif()

# --- Mob simulation microbenchmark (manual runs only, not part of ctest) ------
add_executable(terracraft_bench_mobs bench/bench_mobs.c)
target_link_libraries(terracraft_bench_mobs PRIVATE terracraft_core)
if(WIN32 AND MINEC_SDL2_DLL)
  add_custom_command(TARGET terracraft_bench_mobs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINEC_SDL2_DLL}" $<TARGET_FILE_DIR:terracraft_bench>)
endif()

# --- Install (optional) ------------------------------------------------------
install(TARGETS terracraft RUNTIME DESTINATION bin)
