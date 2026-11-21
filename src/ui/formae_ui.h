/*
 * formae_ui.h - the Formae application around the game (window builds only,
 * DUCKTALES_WINDOW). docs/architecture.md "Formae application".
 *
 *   formae_app.c   config.ini model, live display / volume / bindings, the
 *                  menu shortcut, and the reconciliation with the host's own
 *                  saves (NESRecomp's settings are private to its window)
 *   formae_ui.cpp  Dear ImGui: the Formae look, the in-game menu (a modal
 *                  pause loop) and the first-run ROM screen
 *   formae_rom.c   ROM identity, data folder and import
 *
 * The host reaches this through ducktales_host.c's extras callbacks; the
 * program entry (host/ducktales_main.c) calls configure / first_run / finish.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *config_path;   /* --config, or NULL for the host's default (config.ini beside the program) */
    bool hidden;               /* --hidden: never open UI */
    bool uncapped;             /* --uncapped */
    bool scripted_input;       /* --window-input / --virtual-pad: never remap the pads */
} FormaeLaunch;

/* Before the host starts: read config.ini as the host will. */
void formae_ui_configure(const FormaeLaunch *launch);
/* After the host returned: write back what the player changed. */
void formae_ui_finish(void);

/* First run: the Formae ROM screen. 0: imported, rom_out holds the stored
 * copy; 1: the player quit; 2: no window could open. */
int formae_ui_first_run(const char *expected_sha256, char *rom_out, size_t cap);

/* The host's extras callbacks (cyc_host_extras.h). */
void formae_ui_event(const void *sdl_event);
void formae_ui_input(uint8_t buttons[2]);
void formae_ui_audio(int16_t *samples, size_t count);
void formae_ui_frame_begin(void);
void formae_ui_host_saved(void);

#ifdef __cplusplus
}
#endif
