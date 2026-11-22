cmake_minimum_required(VERSION 3.20)
project(DuckTalesPC C CXX)

set(DUCKTALES_ROM "${CMAKE_SOURCE_DIR}/roms/DuckTales (USA).nes" CACHE FILEPATH
    "DuckTales (USA) iNES image supplied by the user")
option(DUCKTALES_HEADLESS "Build the headless verification host only (no SDL window/audio)" OFF)
option(DUCKTALES_ORACLE "Also build the TriCNES reference machine used by differential tests" OFF)
option(DUCKTALES_DEV_DIAGNOSTICS "Developer diagnostics (typed state log, TCP commands); never in distribution builds" OFF)
option(DUCKTALES_NATIVE "Also build ducktales_native, the native-runtime prototype (docs/native-runtime.md)" OFF)

set(NESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/external/nesrecomp")
if(NOT EXISTS "${NESRECOMP_ROOT}/runner/cyc/project.cmake")
    message(FATAL_ERROR "external/nesrecomp is missing; run: git submodule update --init external/nesrecomp")
endif()
if(NOT EXISTS "${DUCKTALES_ROM}")
    message(FATAL_ERROR "ROM not found: ${DUCKTALES_ROM}\nCopy DuckTales (USA).nes into roms/ or pass -DDUCKTALES_ROM=<path>.")
endif()

# Players need no Visual C++ redistributable: the window build links the static
# MSVC runtime (/MT). Verification builds keep CMake's default.
if(MSVC AND NOT DUCKTALES_HEADLESS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
    "${CMAKE_SOURCE_DIR}/src/game/enhancements.c"
    "${CMAKE_SOURCE_DIR}/src/host/ducktales_sprites.c"
    "${CMAKE_SOURCE_DIR}/src/host/ducktales_host.c")
# The application (docs/architecture.md "Application"): the
# program entry with the first-run ROM import, the in-game menu and live
# settings, drawn with formae-ui (external/formae-ui) on Dear ImGui
# (external/imgui) over the host's SDL_Renderer. Window builds only; the
# headless host never links it.
set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/external/imgui")
set(FORMAE_UI_ROOT "${CMAKE_SOURCE_DIR}/external/formae-ui")
if(NOT DUCKTALES_HEADLESS)
    if(NOT EXISTS "${IMGUI_ROOT}/imgui.h")
        message(FATAL_ERROR "external/imgui is missing; run: git submodule update --init external/imgui")
    endif()
    if(NOT EXISTS "${FORMAE_UI_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "external/formae-ui is missing; run: git submodule update --init external/formae-ui")
    endif()
    list(APPEND _ducktales_extras
        "${CMAKE_SOURCE_DIR}/src/host/ducktales_main.c"
        "${CMAKE_SOURCE_DIR}/src/ui/app_settings.c"
        "${CMAKE_SOURCE_DIR}/src/ui/app_present.c"
        "${CMAKE_SOURCE_DIR}/src/ui/app_rom.c"
        "${CMAKE_SOURCE_DIR}/src/ui/app_ui.cpp"
        "${IMGUI_ROOT}/backends/imgui_impl_sdl2.cpp"
        "${IMGUI_ROOT}/backends/imgui_impl_sdlrenderer2.cpp")
endif()
if(DUCKTALES_DEV_DIAGNOSTICS)
    list(APPEND _ducktales_extras "${CMAKE_SOURCE_DIR}/src/host/ducktales_diagnostics.c")
endif()
if(DUCKTALES_HEADLESS)
    list(APPEND _ducktales_extras "${CMAKE_SOURCE_DIR}/src/host/ducktales_ramhash.c")
endif()
nesrecomp_add_cycle_game(ducktales
    ROM "${DUCKTALES_ROM}"
    GAME_CONFIG "${CMAKE_SOURCE_DIR}/config/game.toml"
    ${_ducktales_options}
    HOST_EXTRAS ${_ducktales_extras})
target_include_directories(ducktales PRIVATE "${CMAKE_SOURCE_DIR}/src")
if(NOT DUCKTALES_HEADLESS)
    # The ROM identity the first-run import checks (docs/rom.md: SHA-256 of
    # the whole file), taken from the ROM this build was generated from.
    file(SHA256 "${DUCKTALES_ROM}" _ducktales_rom_sha256)
    target_compile_features(ducktales PRIVATE cxx_std_17)
    target_compile_definitions(ducktales PRIVATE DUCKTALES_WINDOW=1
        "DUCKTALES_ROM_SHA256=\"${_ducktales_rom_sha256}\"")
    # One Dear ImGui for the program and formae-ui: its core is the imgui
    # target formae-ui links, the SDL backends are compiled into ducktales.
    add_library(imgui STATIC
        "${IMGUI_ROOT}/imgui.cpp"
        "${IMGUI_ROOT}/imgui_draw.cpp"
        "${IMGUI_ROOT}/imgui_tables.cpp"
        "${IMGUI_ROOT}/imgui_widgets.cpp")
    target_include_directories(imgui PUBLIC "${IMGUI_ROOT}" "${IMGUI_ROOT}/backends")
    target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    add_subdirectory("${FORMAE_UI_ROOT}" formae-ui)
    target_link_libraries(ducktales PRIVATE imgui formae::ui)
    # The About page (src/ui/app_ui.cpp): the version (git describe) and the
    # build date when CMake configures, and the Formae logo, embedded by
    # formae_ui_embed_file so the program needs no loose file.
    execute_process(COMMAND git describe --tags --always --dirty WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _ducktales_version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT _ducktales_version)
        set(_ducktales_version "unknown")
    endif()
    string(TIMESTAMP _ducktales_build_date "%Y-%m-%d")
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/app_about.c" CONTENT [[
/* Generated by CMakeLists.txt for the About page (src/ui/app_ui.cpp). */
const char app_about_version[] = "@_ducktales_version@";
const char app_about_build_date[] = "@_ducktales_build_date@";
]] @ONLY)
    target_sources(ducktales PRIVATE "${CMAKE_BINARY_DIR}/app_about.c")
    formae_ui_embed_file(ducktales app_about_logo_png "${CMAKE_SOURCE_DIR}/assets/brand/formae.png")
    # The program entry (src/host/ducktales_main.c) runs before NESRecomp's
    # own main, which is compiled under another name for this target only;
    # NESRecomp's sources stay unmodified.
    set_property(SOURCE "${NESRECOMP_ROOT}/runner/cyc/cyc_host.c" APPEND PROPERTY COMPILE_DEFINITIONS
        "$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,ducktales>:main=ducktales_host_main>")
    if(WIN32)
        target_link_libraries(ducktales PRIVATE comdlg32 shell32)
        # The executable's icon; SDL gives every window the module's first icon.
        set(DUCKTALES_ICON "${CMAKE_SOURCE_DIR}/assets/icon/ducktales.ico")
        configure_file("${CMAKE_SOURCE_DIR}/assets/icon/ducktales.rc.in" "${CMAKE_BINARY_DIR}/ducktales.rc" @ONLY)
        target_sources(ducktales PRIVATE "${CMAKE_BINARY_DIR}/ducktales.rc")
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

# The native-runtime prototype (docs/native-runtime.md): the same generated C
# as the ducktales target, linked against src/native/ instead of NESRecomp's
# cycle host, PPU and APU. Only the CPU's instruction sequences (cpu6502.c)
# and its instruction templates for uncompiled code (cpu6502_interp.c) come
# from NESRecomp.
if(DUCKTALES_NATIVE)
    include("${CMAKE_CURRENT_BINARY_DIR}/cycle-ducktales/sources.cmake")
    set(_nes_cyc "${NESRECOMP_ROOT}/runner/cyc")
    add_executable(ducktales_native
        "${CMAKE_SOURCE_DIR}/src/native/native_main.c"
        "${CMAKE_SOURCE_DIR}/src/native/native_bus.c"
        "${CMAKE_SOURCE_DIR}/src/native/native_video.c"
        "${_nes_cyc}/cpu6502.c"
        "${_nes_cyc}/cpu6502_interp.c"
        ${CYC_PROJECT_SOURCES})
    target_include_directories(ducktales_native PRIVATE "${CMAKE_SOURCE_DIR}/src" "${_nes_cyc}"
        "${NESRECOMP_ROOT}/runner/include")
    target_compile_definitions(ducktales_native PRIVATE NESRECOMP_NATIVE_INTERVALS=0
        NESRECOMP_HEADROOM_COUNTERS=0 NESRECOMP_NATIVE_PRG_GUARDS=0 _CRT_SECURE_NO_WARNINGS)
    target_compile_features(ducktales_native PRIVATE c_std_11)
    if(MSVC)
        target_compile_options(ducktales_native PRIVATE /utf-8)
    endif()
endif()

if(DUCKTALES_ORACLE)
    set(NESRECOMP_CYC_DIR "${NESRECOMP_ROOT}/runner/cyc")
    nesrecomp_cyc_add_oracle(ducktales_oracle)
endif()

# The release package (CONTRIBUTING.md "Releasing"): `cpack -C Release` in
# build/windows writes DuckTalesRecompiled-<version>-win64.zip with the
# executable, SDL2, the player README and every licence the binaries carry
# (docs/licensing.md). No ROM, generated source or test tool is installed.
if(NOT DUCKTALES_HEADLESS)
    configure_file("${CMAKE_SOURCE_DIR}/packaging/README.txt.in" "${CMAKE_BINARY_DIR}/package/README.txt" @ONLY NEWLINE_STYLE CRLF)
    install(TARGETS ducktales RUNTIME DESTINATION .)
    install(FILES "$<TARGET_FILE:SDL2::SDL2>" "${CMAKE_BINARY_DIR}/package/README.txt" DESTINATION .)
    install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION . RENAME LICENSE.txt)
    install(FILES "${NESRECOMP_ROOT}/LICENSE" DESTINATION licenses RENAME NESRecomp-LICENSE.txt)
    install(FILES "${NESRECOMP_ROOT}/PERMISSIONS.md" DESTINATION licenses RENAME NESRecomp-PERMISSIONS.md)
    install(FILES "${NESRECOMP_ROOT}/runner/cyc/vendor/emu2413/LICENSE" DESTINATION licenses RENAME emu2413-LICENSE.txt)
    install(FILES "${NESRECOMP_ROOT}/runner/external/SDL2/COPYING.txt" DESTINATION licenses RENAME SDL2-COPYING.txt)
    install(FILES "${NESRECOMP_ROOT}/runner/external/SDL2/README-SDL.txt" DESTINATION licenses RENAME SDL2-README-SDL.txt)
    install(FILES "${IMGUI_ROOT}/LICENSE.txt" DESTINATION licenses RENAME imgui-LICENSE.txt)
    install(FILES "${FORMAE_UI_ROOT}/LICENSE" DESTINATION licenses RENAME formae-ui-LICENSE.txt)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_NAME DuckTalesRecompiled)
    set(CPACK_PACKAGE_VERSION "${_ducktales_version}")
    set(CPACK_PACKAGE_FILE_NAME "DuckTalesRecompiled-${_ducktales_version}-win64")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    include(CPack)
endif()
