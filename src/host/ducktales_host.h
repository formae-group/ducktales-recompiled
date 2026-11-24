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

int ducktales_host_saved_value(DtEnh e);
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
 * (docs/NATIVE-RUNTIME.md has the measurements). On by default in the window
 * build; off in the headless build, which verification runs. The host's own
 * --extra-scanlines N overrides it for one run. */
#define DT_NO_SLOWDOWN_LINES 128u
bool ducktales_host_no_slowdown(void);
/* Applied between frames and saved with the other [Game] keys. */
void ducktales_host_set_no_slowdown(bool on);
/* Each runtime's own No slowdown: the cycle host's extra scanlines
 * (ducktales_host.c) or the native runtime's lag mode (native_player.c). */
void ducktales_host_apply_no_slowdown(bool on);

/* Decompiled code (config.ini [Game] DecompiledCode, the Settings menu), on
 * by default in the window build and off in the headless build: the native
 * player runs the routines decompiled to C in place of the recompiled ones
 * (src/decomp/decomp_native.c), with No slowdown on only, since the C does not
 * take the console's cycles and would change where the console lags. Every
 * host keeps the key; the native player applies it with No slowdown
 * (ducktales_host_apply_no_slowdown) in its window, never off screen. The
 * menu's Original mode turns both off. */
bool ducktales_host_decompiled(void);
void ducktales_host_set_decompiled(bool on);

/* config.ini [Game] SettingsVersion, written as 2 with every save. After a
 * config.ini without it (saved before Decompiled code was the default) is
 * loaded, this turns No slowdown and Decompiled code on and returns true, so
 * the caller saves the file once; a later choice, Original mode included, is
 * kept. */
bool ducktales_host_upgrade_settings(void);

/* The native player's extra voices (config.ini [Audio] MusicUnderEffects and
 * OverlapEffects, the Audio page; native/native_voices.h), both off by
 * default. NESRecomp's settings file hands the game only its [Game] keys, so
 * these two are read from the file by ducktales_host_load_audio_settings and
 * written as an [Audio] block after the [Game] keys. Every host keeps them;
 * only the native player plays them. */
bool ducktales_host_music_under_effects(void);
void ducktales_host_set_music_under_effects(bool on);
bool ducktales_host_overlap_effects(void);
void ducktales_host_set_overlap_effects(bool on);
void ducktales_host_load_audio_settings(const char *path);

/* An enhancement or sprite option (--jump-scale 150, --no-sprite-flicker off):
 * true if the name is one of them, *ok whether the value was accepted. */
bool ducktales_host_game_option(const char *name, const char *value, bool *ok);

#ifdef __cplusplus
}
#endif
