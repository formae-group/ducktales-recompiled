/*
 * mods_internal.h - what the files of src/mods share (app_mods.h is the
 * interface): the archive reader, the codecs and the audio side's loader.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MOD_NAME_MAX 256

typedef struct {
    char     name[MOD_NAME_MAX];   /* '/' separated, relative to the mod's root */
    uint32_t offset;               /* archive: local header; folder: unused */
    uint32_t packed, size, crc;
    uint16_t method;
} ModEntry;

/* A mod's files: a zip read into memory, or a folder on disk. */
typedef struct {
    bool      folder;
    char      root[1024];          /* the folder, or the archive's path */
    char      prefix[MOD_NAME_MAX];/* the archive's folder holding mod.toml ("" at its root) */
    uint8_t  *data;
    size_t    data_size;
    ModEntry *entries;
    int       count;
} ModArchive;

/* ---- app_mods_archive.c ---- */
bool mod_archive_open(ModArchive *a, const char *path, bool folder, char *err, size_t cap);
void mod_archive_close(ModArchive *a);
/* The entry's index by its name under the prefix, or -1. */
int mod_archive_find(const ModArchive *a, const char *name);
/* The name under the prefix ("tiles/x.png"), or NULL for files outside it. */
const char *mod_archive_name(const ModArchive *a, int index);
/* The file's bytes (malloc'd, NUL-terminated past size), or NULL with err. */
uint8_t *mod_archive_read(const ModArchive *a, int index, size_t *size, char *err, size_t cap);
/* UTF-8 path helpers (wide calls on Windows). */
uint8_t *mod_read_file(const char *path, size_t *size);
bool mod_is_dir(const char *path);
bool mod_make_dir(const char *path);
/* Calls fn for each entry of dir (not . or ..), with its name and whether it is a folder. */
void mod_list_dir(const char *dir, void (*fn)(void *user, const char *name, bool folder), void *user);
uint32_t mod_crc32(const uint8_t *data, size_t size);

/* ---- app_mods_codec.c ---- */
/* PNG to straight-alpha 0xAARRGGBB, malloc'd; NULL when it is not a PNG. */
uint32_t *mod_png_decode(const uint8_t *data, size_t size, int *w, int *h);
bool mod_png_write(const char *path, const uint32_t *argb, int w, int h);
/* Raw deflate into exactly size bytes (malloc'd); NULL on a broken stream. */
uint8_t *mod_inflate(const uint8_t *data, size_t packed, size_t size);

/* A decoded sound being played: OGG streams from its file, WAV from its PCM. */
typedef struct ModSound ModSound;
/* Checks the file (OGG Vorbis or WAV PCM 8/16-bit, mono or stereo); NULL with err. */
ModSound *mod_sound_open(const uint8_t *file, size_t size, char *err, size_t cap);
void mod_sound_close(ModSound *s);
/* Back to the start. */
void mod_sound_rewind(ModSound *s);
/* Mono samples at the file's rate (-1..1) into out; fewer than count at the
 * end (with loop, from the loop start on). */
size_t mod_sound_read(ModSound *s, float *out, size_t count, bool loop);
int mod_sound_rate(const ModSound *s);
/* The file's own loop start in samples (OGG LOOPSTART, WAV smpl), or -1. */
long mod_sound_loop_start(const ModSound *s);

/* ---- app_mods_audio.c ---- */
/* Forgets every replacement. */
void mods_audio_clear(void);
/* The effect priorities and channel masks from the sound driver's table in
 * the player's PRG (docs/audio.md "Sound table and headers"). */
void mods_audio_read_driver(const uint8_t *prg, size_t size);
/* A replacement for music (or an effect) id; takes file (malloc'd). The
 * first one added for an id wins (mods are added top first). */
bool mods_audio_add(bool music, int id, uint8_t *file, size_t size, char *err, size_t cap);
/* An id already has a replacement. */
bool mods_audio_has(bool music, int id);

/* ---- app_mods_lua.c ---- */
/* Starts a loaded mod's scripts/main.lua; false when it failed (reported through mods_script_status). */
bool mods_lua_add(const char *id, const char *path, bool folder);

/* ---- app_mods.c ---- */
/* A mod's script stopped (why) or runs again (NULL). */
void mods_script_status(const char *id, const char *why);
