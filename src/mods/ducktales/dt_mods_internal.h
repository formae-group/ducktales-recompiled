/*
 * dt_mods_internal.h - what DuckTales' mods files share: the sound
 * replacements' loader (dt_mods_audio.c), the game's description for the
 * core (dt_mods_script.c) and the palettes the loaded mods use (dt_mods.c,
 * painted by dt_mods_picture.c).
 */
#pragma once
#include "mods/core/fm_mods.h"

#define LANDS 5

/* The palettes loaded (dt_mods.c), and the one used for each land and,
 * at LANDS, everywhere else: the first mod's that has one wins. */
extern int dt_mods_palette_count;
extern const uint32_t *dt_mods_palette_for[LANDS + 1];

/* Tells the core about this game and its scripts, once (dt_mods_script.c). */
void dt_mods_configure(void);

/* Forgets every replacement. */
void mods_audio_clear(void);
/* The effect priorities and channel masks from the sound driver's table in
 * the player's PRG (docs/AUDIO.md "Sound table and headers"). */
void mods_audio_read_driver(const uint8_t *prg, size_t size);
/* A replacement for music (or an effect) id; takes file (malloc'd). The
 * first one added for an id wins (mods are added top first). */
bool mods_audio_add(bool music, int id, uint8_t *file, size_t size, char *err, size_t cap);
/* An id already has a replacement. */
bool mods_audio_has(bool music, int id);
