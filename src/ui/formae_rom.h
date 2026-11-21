/*
 * formae_rom.h - the player's ROM: identity check, the per-user data folder
 * and the one-time import (docs/architecture.md "First run").
 *
 * The identity is the SHA-256 of the whole iNES file (docs/rom.md), compiled
 * in from the ROM the build was generated from. Import verifies a file, then
 * copies it into the data folder, where later launches find it:
 *
 *   Windows  %APPDATA%\Formae\DuckTales\DuckTales (USA).nes
 *   macOS    ~/Library/Application Support/Formae/DuckTales/...
 *   other    $XDG_DATA_HOME (or ~/.local/share)/Formae/DuckTales/...
 *
 * FORMAE_DATA_DIR overrides the folder (tests). No SDL; pure file I/O, so the
 * unit test (tests/formae_rom_test.c) runs without a window.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FORMAE_ROM_FILE_NAME "DuckTales (USA).nes"

typedef enum {
    FORMAE_ROM_OK,
    FORMAE_ROM_UNREADABLE,   /* cannot open or read the file */
    FORMAE_ROM_NOT_NES,      /* no iNES header */
    FORMAE_ROM_MISMATCH,     /* an NES image, but not this one (modified, other revision or game) */
    FORMAE_ROM_WRITE_FAILED  /* import: the copy could not be written */
} FormaeRomStatus;

void formae_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void formae_sha256_hex(const uint8_t digest[32], char out[65]);

/* Checks the file against expected_hex (64 lower-case hex digits). On a
 * mismatch actual_hex (65 bytes, may be NULL) receives the file's hash. */
FormaeRomStatus formae_rom_verify(const char *path, const char *expected_hex, char *actual_hex);

/* A player-facing sentence for a status (English). */
const char *formae_rom_status_text(FormaeRomStatus s);

/* The data folder, created if missing; false if no folder can be made. */
bool formae_data_dir(char *out, size_t cap);
/* <data folder>/DuckTales (USA).nes (it may not exist). */
bool formae_rom_stored_path(char *out, size_t cap);

/* Verifies src and copies it to formae_rom_stored_path (written to a
 * temporary file, then moved). stored (may be NULL) receives the copy's path. */
FormaeRomStatus formae_rom_import(const char *src, const char *expected_hex, char *stored, size_t cap,
                                  char *actual_hex);

/* True if the stored copy exists and still matches; out receives its path. */
bool formae_rom_stored_valid(const char *expected_hex, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
