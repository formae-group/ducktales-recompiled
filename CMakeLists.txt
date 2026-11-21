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
# sites it alters behaviour at, settings and options. Both hosts link it.
set(_ducktales_extras
    "${CMAKE_SOURCE_DIR}/src/game/ducktales_state.c"
    "${CMAKE_SOURCE_DIR}/src/game/ducktales_jump.c"
    "${CMAKE_SOURCE_DIR}/src/host/ducktales_host.c")
if(DUCKTALES_DEV_DIAGNOSTICS)
    list(APPEND _ducktales_extras "${CMAKE_SOURCE_DIR}/src/host/ducktales_diagnostics.c")
endif()
nesrecomp_add_cycle_game(ducktales
    ROM "${DUCKTALES_ROM}"
    GAME_CONFIG "${CMAKE_SOURCE_DIR}/config/game.toml"
    ${_ducktales_options}
    HOST_EXTRAS ${_ducktales_extras})
target_include_directories(ducktales PRIVATE "${CMAKE_SOURCE_DIR}/src")
if(DUCKTALES_DEV_DIAGNOSTICS)
    target_compile_definitions(ducktales PRIVATE DUCKTALES_DEV_DIAGNOSTICS=1)
endif()

# Unit test of the typed state decoder (pure C, no machine).
if(DUCKTALES_HEADLESS)
    add_executable(ducktales_state_test
        "${CMAKE_SOURCE_DIR}/tests/state_decode_test.c"
        "${CMAKE_SOURCE_DIR}/src/game/ducktales_state.c")
    target_include_directories(ducktales_state_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
endif()

if(DUCKTALES_ORACLE)
    set(NESRECOMP_CYC_DIR "${NESRECOMP_ROOT}/runner/cyc")
    nesrecomp_cyc_add_oracle(ducktales_oracle)
endif()
