/*
 * ducktales/ram.h - the game's 2 KiB of CPU RAM ($0000-$07FF) as the port's
 * code reads and writes it.
 *
 * Every typed accessor of the game API (objects.h, player.h, hud.h, game.h)
 * is an lvalue in gRam, the RAM image of the transaction that is open. Code
 * that runs at a hook site, in a frame callback or in a decompiled routine
 * opens one, works on the image like the game's own code works on RAM, and
 * the transaction stores the bytes it changed back into the machine as CPU
 * stores would (cyc_mod_poke, through the compiled-code write watch). Outside
 * a transaction gRam is NULL.
 *
 *   GameRam_Begin / GameRam_End   a transaction on the running machine; a
 *                                 nested Begin joins the one already open
 *   GameRam_Enter / GameRam_Leave a private image (a decompiled routine in
 *                                 progress, src/decomp/decomp.c); nothing is
 *                                 stored when it is left
 *
 * Addresses come from game/ducktales_symbols.h, the only file that names them.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "game/ducktales_symbols.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_RAM_SIZE 0x800

extern uint8_t *gRam;

#define RAM_U8(addr) (gRam[(addr)])

static inline uint16_t Ram_GetU16(uint16_t lo, uint16_t hi)
{
    return (uint16_t)(gRam[lo] | gRam[hi] << 8);
}

static inline void Ram_SetU16(uint16_t lo, uint16_t hi, uint16_t v)
{
    gRam[lo] = (uint8_t)v;
    gRam[hi] = (uint8_t)(v >> 8);
}

void GameRam_Begin(void);
void GameRam_End(void);
void GameRam_Enter(uint8_t *image);
void GameRam_Leave(void);
bool GameRam_Open(void);

/* A byte of ROM or I/O as the CPU would read it now, with the bank mapped at
 * $8000 (no side effects; I/O reads 0). For data tables the game reads at
 * run time, such as the map grid at ($20). */
uint8_t Game_ReadRom(uint16_t addr);
/* A write to the mapper as decompiled code makes it (Decomp_BusWrite): from
 * then on Game_ReadRom reads $8000-$BFFF from the bank it selects, until
 * Game_ResetRomView. */
void Game_MapperWrite(uint16_t addr, uint8_t value);
void Game_ResetRomView(void);
/* The bank of the view (-1: as mapped), to put it back after a nested run. */
int Game_RomView(void);
void Game_SetRomView(int bank);

#ifdef __cplusplus
}
#endif
