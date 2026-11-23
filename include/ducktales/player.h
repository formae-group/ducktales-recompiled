/*
 * ducktales/player.h - Scrooge (docs/player.md).
 *
 * Scrooge is object slot PLAYER_SLOT; his per-object values are that column
 * of gObjects (objects.h). The values the game keeps for him outside the
 * object tables are zero-page variables, named here. The ROM has one player,
 * so these are one set; the decompiled routines (src/decomp/player.c) reach
 * them only through these names, which is the one place a second player
 * would give its own set.
 */
#pragma once
#include <stdint.h>

#include "ducktales/objects.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLAYER_SLOT DT_SLOT_SCROOGE

/* $39, the index of the mode table at $E387 (verified unless marked). */
typedef enum PlayerMode {
    PLAYER_MODE_GROUND = 0,     /* stand, walk, crouch, push */
    PLAYER_MODE_AIR = 1,        /* jump or fall */
    PLAYER_MODE_POGO = 2,       /* pogo bounce */
    PLAYER_MODE_CLIMB = 3,      /* rope or vine (code) */
    PLAYER_MODE_CANE = 4,       /* cane swing */
    PLAYER_MODE_HURT = 5,       /* waits for the invulnerability timer (code; trigger not found) */
    PLAYER_MODE_KNOCKBACK = 6,  /* knocked back by a harmful object */
    PLAYER_MODE_STUCK = 7,      /* stuck in snow, freed by a jump */
    PLAYER_MODE_PIT = 8,        /* (hypothesis) falls until the pit death request */
    PLAYER_MODE_9 = 9,          /* (not studied) */
    PLAYER_MODE_CARRIED_A = 10, /* (not studied) */
    PLAYER_MODE_CARRIED_B = 11, /* (not studied) */
    PLAYER_MODE_12 = 12,        /* (not studied) */
    PLAYER_MODE_DEATH = 13,     /* death jump */
    PLAYER_MODE_14 = 14,        /* (not studied) */
    PLAYER_MODE_MESSAGE = 15,   /* holding up an item or a message (hypothesis) */
    PLAYER_MODE_TREASURE = 16,  /* holding up a land treasure */
} PlayerMode;

/* gObjects->action[PLAYER_SLOT] (verified unless marked). */
typedef enum PlayerAction {
    PLAYER_ACTION_STAND = 0,
    PLAYER_ACTION_WALK = 1,
    PLAYER_ACTION_JUMP = 2,
    PLAYER_ACTION_CROUCH = 3, /* (code) */
    PLAYER_ACTION_CLIMB = 4,  /* (code) */
    PLAYER_ACTION_HURT = 5,
    PLAYER_ACTION_POGO = 6,
    PLAYER_ACTION_PUSH = 7,
    PLAYER_ACTION_LANDED = 8,
    PLAYER_ACTION_CANE = 9,
    PLAYER_ACTION_STUCK = 0x2C,    /* (code) */
    PLAYER_ACTION_CANE_HIT = 0x41, /* (code) */
} PlayerAction;

#define gPlayerMode         RAM_U8(DT_ADDR_MODE)
#define gPlayerInvulnerable RAM_U8(DT_ADDR_INVULNERABLE)
#define gPlayerPushCounter  RAM_U8(DT_ADDR_PUSH_COUNTER)
#define gPlayerPushDir      RAM_U8(DT_ADDR_PUSH_DIR)
#define gPlayerEasyHits     RAM_U8(DT_ADDR_EASY_HITS)
#define gPlayerSlippery     RAM_U8(DT_ADDR_SLIPPERY) /* hypothesis */
#define gPlayerMomentumDir  RAM_U8(DT_ADDR_MOMENTUM_DIR)
#define gPlayerPushRight    RAM_U8(DT_ADDR_PUSH_RIGHT) /* hypothesis */
#define gPlayerPower        RAM_U8(DT_ADDR_POWER)      /* hypothesis: invincibility */
#define gPlayerMoveDir      RAM_U8(DT_ADDR_MOVE_DIR)
#define gPlayerMoveSide     RAM_U8(DT_ADDR_MOVE_SIDE)
#define gPlayerHazard       RAM_U8(DT_ADDR_HAZARD)

/* The take-off velocity a jump copies into Scrooge's velocity (8.8). */
static inline uint16_t Player_GetJumpVelocity(void)
{
    return Ram_GetU16(DT_ADDR_JUMP_VY_FRAC, DT_ADDR_JUMP_VY);
}

static inline void Player_SetJumpVelocity(uint16_t v)
{
    Ram_SetU16(DT_ADDR_JUMP_VY_FRAC, DT_ADDR_JUMP_VY, v);
}

static inline uint16_t Player_GetPowerFrames(void)
{
    return Ram_GetU16(DT_ADDR_POWER_FRAMES, DT_ADDR_POWER_FRAMES + 1);
}

#ifdef __cplusplus
}
#endif
