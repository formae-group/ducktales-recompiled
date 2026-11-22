cmake_minimum_required(VERSION 3.20)
project(DuckTalesPC C CXX)

set(DUCKTALES_ROM "${CMAKE_SOURCE_DIR}/roms/DuckTales (USA).nes" CACHE FILEPATH
    "DuckTales (USA) iNES image supplied by the user")
option(DUCKTALES_HEADLESS "Build the headless verification host only (no SDL window/audio)" OFF)
option(DUCKTALES_ORACLE "Also build the TriCNES reference machine used by differential tests" OFF)
option(DUCKTALES_DEV_DIAGNOSTICS "Developer diagnostics (typed state log, TCP commands); never in distribution builds" OFF)

set(NESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/external/nesrecomp")
if(NOT EXISTS "${NESRECOMP_ROOT}/runner/cyc/project.cmake")
    message(FATAL_ERROR "external/nesrecomp is missing; run: git submodule update --init external/nesrecomp")
endif()
if(NOT EXISTS "${DUCKTALES_ROM}")
    message(FATAL_ERROR "ROM not found: ${DUCKTALES_ROM}\nCopy DuckTales (USA).nes into roms/ or pass -DDUCKTALES_ROM=<path>.")
endif()

include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")

set(_ducktales_options PLAYERS 1)
if(DUCKTALES_HEADLESS)
    list(APPEND _ducktales_options HEADLESS)
endif()
# The port's systems layer (docs/architecture.md): typed game state, the hook
# sites it alters behaviour at, settings and options; the typed game API and
# its events (include/ducktales/). Both hosts link it.
set(_ducktales_extras
    "${CMAKE_SOURCE_DIR}/src/game/ducktales_state.c"
    "${CMAKE_SOURCE_DIR}/src/game/game_ram.c"
    "${CMAKE_SOURCE_DIR}/src/game/events.c"
    "${CMAKE_SOURCE_DIR}/src/game/ducktales_jump.c"
    "${CMAKE_SOURCE_DIR}/src/game/enhancements.c"
    "${CMAKE_SOURCE_DIR}/src/host/ducktales_sprites.c"
    "${CMAKE_SOURCE_DIR}/src/host/ducktales_host.c")
# The application (docs/architecture.md "Application"): the
# program entry with the first-run ROM import, the in-game menu and live
# settings, drawn with Dear ImGui (external/imgui) over the host's
# SDL_Renderer. Window builds only; the headless host never links it.
set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/external/imgui")
if(NOT DUCKTALES_HEADLESS)
    if(NOT EXISTS "${IMGUI_ROOT}/imgui.h")
        message(FATAL_ERROR "external/imgui is missing; run: git submodule update --init external/imgui")
    endif()
    list(APPEND _ducktales_extras
        "${CMAKE_SOURCE_DIR}/src/host/ducktales_main.c"
        "${CMAKE_SOURCE_DIR}/src/ui/app_settings.c"
        "${CMAKE_SOURCE_DIR}/src/ui/app_rom.c"
        "${CMAKE_SOURCE_DIR}/src/ui/app_ui.cpp"
        "${IMGUI_ROOT}/imgui.cpp"
        "${IMGUI_ROOT}/imgui_draw.cpp"
        "${IMGUI_ROOT}/imgui_tables.cpp"
        "${IMGUI_ROOT}/imgui_widgets.cpp"
        "${IMGUI_ROOT}/backends/imgui_impl_sdl2.cpp"
        "${IMGUI_ROOT}/backends/imgui_impl_sdlrenderer2.cpp")
endif()
if(DUCKTALES_DEV_DIAGNOSTICS)
    list(APPEND _ducktales_extras "${CMAKE_SOURCE_DIR}/src/host/ducktales_diagnostics.c")
endif()
nesrecomp_add_cycle_game(ducktales
    ROM "${DUCKTALES_ROM}"
    GAME_CONFIG "${CMAKE_SOURCE_DIR}/config/game.toml"
    ${_ducktales_options}
    HOST_EXTRAS ${_ducktales_extras})
target_include_directories(ducktales PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include")
if(NOT DUCKTALES_HEADLESS)
    # The ROM identity the first-run import checks (docs/rom.md: SHA-256 of
    # the whole file), taken from the ROM this build was generated from.
    file(SHA256 "${DUCKTALES_ROM}" _ducktales_rom_sha256)
    target_include_directories(ducktales PRIVATE "${IMGUI_ROOT}" "${IMGUI_ROOT}/backends")
    target_compile_features(ducktales PRIVATE cxx_std_17)
    target_compile_definitions(ducktales PRIVATE DUCKTALES_WINDOW=1
        "DUCKTALES_ROM_SHA256=\"${_ducktales_rom_sha256}\"" IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    # The program entry (src/host/ducktales_main.c) runs before NESRecomp's
    # own main, which is compiled under another name for this target only;
    # NESRecomp's sources stay unmodified.
    set_property(SOURCE "${NESRECOMP_ROOT}/runner/cyc/cyc_host.c" APPEND PROPERTY COMPILE_DEFINITIONS
        "$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,ducktales>:main=ducktales_host_main>")
    if(WIN32)
        target_link_libraries(ducktales PRIVATE comdlg32 shell32)
    endif()
endif()
if(DUCKTALES_DEV_DIAGNOSTICS)
    target_compile_definitions(ducktales PRIVATE DUCKTALES_DEV_DIAGNOSTICS=1)
endif()

# Unit test of the typed state decoder (pure C, no machine).
if(DUCKTALES_HEADLESS)
    add_executable(ducktales_state_test
        "${CMAKE_SOURCE_DIR}/tests/state_decode_test.c"
        "${CMAKE_SOURCE_DIR}/src/game/ducktales_state.c")
    target_include_directories(ducktales_state_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
    # Unit test of the ROM identity check and import (no window).
    add_executable(app_rom_test
        "${CMAKE_SOURCE_DIR}/tests/app_rom_test.c"
        "${CMAKE_SOURCE_DIR}/src/ui/app_rom.c")
    target_include_directories(app_rom_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(app_rom_test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(DUCKTALES_ORACLE)
    set(NESRECOMP_CYC_DIR "${NESRECOMP_ROOT}/runner/cyc")
    nesrecomp_cyc_add_oracle(ducktales_oracle)
endif()
