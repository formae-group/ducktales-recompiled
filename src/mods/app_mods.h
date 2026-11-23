/*
 * app_mods.h - mods: replacement tiles, palettes, music and sound effects
 * loaded from .dtmod archives and loose folders (docs/mods.md). Mods change
 * only what the window shows and what the speakers play: nothing here writes
 * to the machine, so its state, its own picture and every verified hash stay
 * the console's.
 *
 *   catalogue   every *.dtmod archive (a zip) and every folder holding a
 *               mod.toml in the mods folders, in the player's order; a mod
 *               made for another game or PRG, or an archive that cannot be
 *               read, is listed with the reason and never loaded
 *   settings    config.ini [Mods] Enabled = on|off and Order = id:on, id:off,
 *               ... (top first: earlier mods win per asset); new mods join
 *               the bottom of the list, switched on
 *   loading     once, at the first power-on: the enabled mods' tiles/,
 *               palettes/, music/ and sfx/; a change in the menu applies at
 *               the next start (app_mods_restart_needed)
 *   video       app_mods_video_apply: tiles addressed by the content hash of
 *               the 8x8 CHR tile and its palette, and NES palette colours,
 *               painted over a presented 256x240 picture
 *   audio       app_mods_audio_*: a music or effect request id with a
 *               replacement plays the file and mutes the driver's channels
 *               for it (audio-only: the APU still runs them)
 *   key dumps   app_mods_dump_dir: every tile drawn is written once as
 *               <key>.png, the template a tile replacement starts from
 *
 * Folders: --mods-dir (both hosts) or, in the window build, the data folder's
 * mods (%APPDATA%\DuckTales Recompiled\mods, or DUCKTALES_DATA_DIR\mods).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "native/native_render.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MODS_GAME "DuckTales (USA)"
#define APP_MODS_PRG_SHA256 "8ba8baed01a9fbaf1e9ff29e0c9825db1963ac2aff211d6f1f3bcfd3839e2013"
#define APP_MODS_EXTENSION ".dtmod"

typedef struct {
    char id[64];             /* mod.toml id; the file or folder name when it could not be read */
    char name[128];
    char version[32];
    char author[128];
    char description[512];
    char path[1024];         /* the archive or folder */
    bool folder;             /* a loose folder (development) */
    bool enabled;            /* the player's checkbox */
    bool loaded;             /* its assets are in use since the start */
    char error[320];         /* non-empty: refused or unreadable, never loaded */
    int tiles, palettes, music, sfx;
} AppMod;

/* ---- folders and settings ---- */

/* Adds a folder to scan, created if missing when create is set. */
void app_mods_add_folder(const char *dir, bool create);
/* The first folder: the one the menu opens. Empty when none. */
const char *app_mods_folder(void);
/* A [Mods] key of config.ini; false when the key is unknown. */
bool app_mods_setting(const char *key, const char *value);
/* Reads the [Mods] section of an ini file (the host's reader skips it). */
void app_mods_read_config(const char *path);
/* Writes the [Mods] section, header included. */
void app_mods_write_settings(FILE *f);
/* Mods on or off; off loads nothing at the next start. */
bool app_mods_enabled(void);
void app_mods_set_enabled(bool on);

/* ---- command-line options (both hosts) ---- */

/* --mods on|off, --mods-dir DIR, --mods-order ID:on|off,..., --mods-dump DIR. */
#define APP_MODS_OPTION_COUNT 4
const char *app_mods_option_name(int index);
const char *app_mods_option_help(int index);
/* True when name is one of them; *ok says whether its value was accepted. */
bool app_mods_option(const char *name, const char *value, bool *ok);

/* ---- start ---- */

/* Scans the folders and loads the enabled mods' assets, once: the running
 * PRG decides which mods are refused. Later calls do nothing. */
void app_mods_start(const uint8_t *prg, size_t prg_size);
/* The machine was powered on or a state was loaded: forget the sounds. */
void app_mods_reset(void);

/* ---- the catalogue (the menu's Mods page) ---- */

int app_mods_count(void);
const AppMod *app_mods_get(int index);
void app_mods_set_mod_enabled(int index, bool on);
void app_mods_move(int from, int to);
/* Reads the folders again; what is loaded does not change until a restart. */
void app_mods_rescan(void);
/* Enabled, the enabled mods or their order differ from what was loaded. */
bool app_mods_restart_needed(void);

/* ---- video ---- */

/* A tile or palette replacement is loaded, or a key dump is on. */
bool app_mods_video_active(void);
/* Paints the replacements over argb, the 256x240 picture drawn from video
 * with options (NULL: the console's rules). A pixel changes only where argb
 * still shows the colour that rule gives it, so a compositor's own pixels
 * stay as they are. ram (2 KiB, may be NULL) gives the land for per-land
 * palettes. */
void app_mods_video_apply(const NvVideo *video, const NvRenderOptions *options, const uint8_t *ram, uint32_t *argb);
/* Developer: from now on write every tile drawn, once, into dir as
 * <key>.png (from the player's own ROM; never distribute them). */
bool app_mods_dump_dir(const char *dir);

/* ---- audio ---- */

/* A music or effect replacement is loaded. */
bool app_mods_audio_active(void);
/* At the end of every machine frame, with its CPU RAM: the requests the
 * game queued in it start with the next frame. */
void app_mods_audio_frame_end(const uint8_t *ram);
/* APU channels (bit 0 pulse 1, 1 pulse 2, 2 triangle, 3 noise) the next
 * frame must not be heard on. */
uint8_t app_mods_audio_mute(void);
/* Adds the replacements' sound to the frame's mono samples at rate Hz. */
void app_mods_audio_mix(int16_t *samples, size_t count, int rate);

#ifdef __cplusplus
}
#endif
