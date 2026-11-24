/*
 * app_ui.h - the application around the game (window builds only,
 * DUCKTALES_WINDOW). docs/ARCHITECTURE.md "Application".
 *
 *   app_present.c    the picture the window shows: HUD split, overscan,
 *                  scaling, pixel aspect; frame pacing
 *   app_settings.c   config.ini model, live display / volume / bindings, the
 *                  menu shortcut, and the reconciliation with the host's own
 *                  saves (NESRecomp's settings are private to its window)
 *   app_ui.cpp  the in-game menu (a modal loop that keeps the game
 *                  running through the host's menu tick) and the
 *                  first-run ROM screen on formae-ui (external/formae-ui)
 *                  and Dear ImGui
 *   app_rom.c   ROM identity, data folder and import
 *
 * The host reaches this through ducktales_host.c's extras callbacks; the
 * program entry (host/ducktales_main.c) calls configure / first_run / finish.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *config_path; /* --config, or NULL for the host's default (config.ini beside the program) */
    bool hidden;             /* --hidden: never open UI */
    bool uncapped;           /* --uncapped */
    bool scripted_input;     /* --window-input / --virtual-pad: never remap the pads */
    bool off_screen;         /* no player Settings are written and no player mods folder is scanned */
} AppLaunch;

/* Before the host starts: read config.ini as the host will. */
void app_ui_configure(const AppLaunch *launch);
/* After the host returned: write back what the player changed. */
void app_ui_finish(void);

/* First run: the ROM screen. 0: imported, rom_out holds the stored
 * copy; 1: the player quit; 2: no window could open. */
int app_ui_first_run(const char *expected_sha256, char *rom_out, size_t cap);

/* The host's extras callbacks (cyc_host_extras.h). */
void app_ui_event(const void *sdl_event);
void app_ui_input(uint8_t buttons[2]);
void app_ui_audio(int16_t *samples, size_t count);
void app_ui_frame_begin(void);
void app_ui_host_saved(void);
/* config.ini [Game] keys of the menu itself (app_settings.c); load returns
 * true if the key is one of them. */
bool app_ui_load_game_setting(const char *key, const char *value);
void app_ui_write_game_settings(FILE *f);
/* CycHostExtras.present: the picture the window shows (ui/app_present.h). */
const uint32_t *app_ui_present(int *width, int *height);
/* The host's game frames while the menu is open: the menu calls tick once per
 * menu frame, with run false while [Game] MenuPausesGame is on or a Reset or
 * Quit question is open (the game is paused: the host holds its sound); with
 * run true tick runs the frames that are due, with no buttons pressed, and
 * returns how many. A host that sets none (the cycle host) pauses. */
void app_ui_set_menu_tick(int (*tick)(bool run));
/* The master volume the player has set now, 0-100, or -1 before config.ini
 * is read (the native player ramps its output to it, native_output.h). */
int app_ui_volume(void);

#ifdef __cplusplus
}
#endif
