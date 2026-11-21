cmake_minimum_required(VERSION 3.20)
project(DuckTalesPC C CXX)

set(DUCKTALES_ROM "${CMAKE_SOURCE_DIR}/roms/DuckTales (USA).nes" CACHE FILEPATH
    "DuckTales (USA) iNES image supplied by the user")
option(DUCKTALES_HEADLESS "Build the headless verification host only (no SDL window/audio)" OFF)
option(DUCKTALES_ORACLE "Also build the TriCNES reference machine used by differential tests" OFF)

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
nesrecomp_add_cycle_game(ducktales
    ROM "${DUCKTALES_ROM}"
    GAME_CONFIG "${CMAKE_SOURCE_DIR}/config/game.toml"
    ${_ducktales_options})

if(DUCKTALES_ORACLE)
    set(NESRECOMP_CYC_DIR "${NESRECOMP_ROOT}/runner/cyc")
    nesrecomp_cyc_add_oracle(ducktales_oracle)
endif()
