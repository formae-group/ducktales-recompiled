/*
 * fm_nes.h - the mod core's NES module (fm_nes.c): tile keys, the table of
 * replacement tiles, .pal palettes and tile templates. Game-agnostic for NES
 * games; the game paints with it (src/mods/ducktales/dt_mods.c).
 */
#pragma once
#include "mods/core/fm_mods.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FM_NES_ANY_PALETTE 0xFFFFFFFFu

/* A replacement tile: art (straight-alpha 0xAARRGGBB, w x h) for a CHR hash and palette. */
typedef struct {
    uint64_t hash;
    uint32_t palette; /* FM_NES_ANY_PALETTE: any */
    int w, h;
    uint32_t *argb;
} FmNesTile;

/* The key of a tile's 16 CHR bytes. */
uint64_t fm_nes_tile_hash(const uint8_t *chr);
/* A tile file's name as its key: false when it is not <hash>.png or <hash>_<palette>.png. */
bool fm_nes_tile_key(const char *name, uint64_t *hash, uint32_t *palette);
/* Takes art->argb; false (and frees it) when the key is taken or memory is short. */
bool fm_nes_tile_insert(FmNesTile *art);
const FmNesTile *fm_nes_tile_find(uint64_t hash, uint32_t palette);
size_t fm_nes_tile_count(void);

/* A .pal file as 512 ARGB colours (malloc'd); base fills the emphasis rows of a 64-entry file. NULL when the size is wrong. */
uint32_t *fm_nes_pal_read(const uint8_t *data, size_t size, const uint32_t *base);

/* Tile templates: every tile and palette painted is written once as dir/<hash>_<palette>.png. */
bool fm_nes_dump_to(const char *dir);
bool fm_nes_dumping(void);
/* sprite: colour 0 is transparent; base: the 64 NES colours as ARGB. */
void fm_nes_dump_tile(const uint8_t *chr, bool sprite, uint64_t hash, uint32_t palette, const uint32_t *base);

#ifdef __cplusplus
}
#endif
