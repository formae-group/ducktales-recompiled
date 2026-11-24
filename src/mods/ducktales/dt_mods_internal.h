/*
 * dt_mods_internal.h - what dt_mods.c and dt_mods_audio.c share: the sound
 * replacements' loader.
 */
#pragma once
#include "mods/core/fm_mods.h"

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
