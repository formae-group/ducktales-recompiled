/*
 * ducktales_host.h - what the systems layer (ducktales_host.c) shares with
 * the application UI and the program entry: the enhancements as config.ini keeps
 * them, apart from the live values (game/enhancements.h), which a
 * command-line option may override for one run, and the No slowdown setting.
 */
#pragma once
#include "game/enhancements.h"

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int  ducktales_host_saved_value(DtEnh e);
void ducktales_host_set_saved_value(DtEnh e, int value);
/* [Game] NoSpriteFlicker as config.ini keeps it (host/ducktales_sprites.h). */
bool ducktales_host_saved_sprites(void);
void ducktales_host_set_saved_sprites(bool on);
/* config.ini [Game]: one key (as the host's load_setting gets it) and all of
 * ours ("Key = value" lines, as the host's save_settings writes them). */
void ducktales_host_load_game_setting(const char *key, const char *value);
void ducktales_host_write_game_settings(FILE *f);

/* No slowdown (config.ini [Game] NoSlowdown, the Settings menu): NESRecomp's
 * extra scanlines (cyc_set_extra_scanlines), DT_NO_SLOWDOWN_LINES blank lines
 * after the picture and before each NMI, with the APU paused across them. The
 * game gets that much more CPU time per frame and the window still shows 60
 * frames a second, so frames that lag on the console finish in time
 * (docs/native-runtime.md has the measurements). On by default in the window
 * build; off in the headless build, which verification runs. The host's own
 * --extra-scanlines N overrides it for one run. */
#define DT_NO_SLOWDOWN_LINES 128u
bool ducktales_host_no_slowdown(void);
/* Applied between frames and saved with the other [Game] keys. */
void ducktales_host_set_no_slowdown(bool on);
/* Each runtime's own No slowdown: the cycle host's extra scanlines
 * (ducktales_host.c) or the native runtime's lag mode (native_window.c). */
void ducktales_host_apply_no_slowdown(bool on);

/* An enhancement or sprite option (--jump-scale 150, --no-sprite-flicker off):
 * true if the name is one of them, *ok whether the value was accepted. */
bool ducktales_host_game_option(const char *name, const char *value, bool *ok);

#ifdef __cplusplus
}
#endif
