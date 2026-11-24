/*
 * player/player_internal.h - what the files of src/decomp/player/ share: the
 * constants and RAM names of Scrooge's code, the small helpers and the
 * routines one file calls in another. Not for code outside player/.
 */
#pragma once

#include "decomp/decomp.h"
#include "decomp/collision.h"
#include "decomp/motion.h"
#include "decomp/objcommon.h"
#include "decomp/player.h"
#include "decomp/sound.h"
#include "decomp/system.h"
#include "ducktales/events.h"
#include "ducktales/game.h"
#include "ducktales/hud.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"
#include "ducktales/tune.h"

#define P_ PLAYER_SLOT

#define PLAYER_GRAVITY     0x33
#define PUSH_FRAMES        0x10
#define JUMP_HOLD_MIN      0x042D /* below this a released A ends a jump's rise */
#define POGO_HOLD_MIN      0x05D2 /* the same for a pogo bounce */
#define POGO_BOUNCE_VY     0x05A8
#define KNOCKBACK_VY       0x0169
#define KNOCKBACK_FRAMES   0x46 /* invulnerability after a hit */
#define KNOCKBACK_END      0x3C /* the knock-back ends 10 frames after the hit */
#define DEATH_VY           0x0400
#define WALK_SPEED         0x0180
#define MOMENTUM_MAX       0x0280
#define FALL_SPEED_LIMIT   0xF4 /* gravity stops while the velocity's whole byte is this */
#define PIT_Y              0xD0
#define ROW_BOTTOM_Y       0xE0
#define ROW_TOP_Y_CLIMBING 0x39
#define FINAL_ARENA_DROP_Y 0xC0
#define POGO_CEILING_Y     0xA0
#define CANE_HIT_FRAMES    5
#define STUCK_SINK         4
#define SELECT_REFILL_COST 3 /* taken from TOTAL's two leading digits */

#define OBJ_FLAG_ON_SCREEN  0x80
#define OBJ_FLAG_NO_LANDING 0x02 /* skipped by the vertical contact test (code) */
#define OBJ_KIND_BLOCK      0x08
#define OBJ_KIND_15         0x15
#define HITBOX_SOLID        0x80
#define HITBOX_INDEX        0x7F
#define HITBOX_HARMFUL      0x70

#define PROBE_RIGHT 0
#define PROBE_LEFT  3
#define PROBE_FEET  6
#define PROBE_HEAD  9
#define PROBE_CLIMB 12

#define gProbeHits     RAM_U8(DT_ADDR_PROBE_HITS)
#define gContactDepth  RAM_U8(DT_ADDR_CONTACT_DEPTH)
#define gProbeFirst    RAM_U8(DT_ADDR_PROBE_FIRST)
#define gProbeCount    RAM_U8(DT_ADDR_PROBE_COUNT)
#define gProbeClass(i) RAM_U8(DT_ADDR_PROBE_CLASS + (i))
#define gFeetClass     RAM_U8(DT_ADDR_FEET_CLASS)
#define gClimbClass    RAM_U8(DT_ADDR_CLIMB_CLASS)
#define gTemp08        RAM_U8(DT_ADDR_TEMP08)
#define gTemp09        RAM_U8(DT_ADDR_TEMP09)
#define gScratch(i)    RAM_U8(DT_ADDR_SCRATCH0 + (i))
#define gHudTotalDirty RAM_U8(DT_ADDR_HUD_TOTAL_DIRTY)
#define gTotalDigit(i) RAM_U8(DT_ADDR_TOTAL_DIGITS + (i))

/* A tunable at the point its site replaces the game's value: the tuned value
 * in replace mode, the game's otherwise (in off and check mode the site
 * stores the tuned value on the machine, so check compares the C with an
 * original that has the game's). */
static inline uint16_t Tune_Value(TuneId id, uint16_t original)
{
    int32_t v = Tune_Get(id);
    return v != TUNE_UNSET && Decomp_Mode() == DECOMP_REPLACE ? (uint16_t)v : original;
}

static inline void Player_SetAction(uint8_t action)
{
    gObjects->action[P_] = action;
    gObjects->animFrame[P_] = 0;
    gObjects->animTimer[P_] = 0;
}

uint8_t Player_ContactH(void);
bool Player_ContactV(void);
uint8_t Player_SelectRefill(void);
uint8_t Player_ProbeVertical(void);
uint8_t Player_Fall(void);
uint8_t Player_Rise(void);
uint8_t Player_MoveRight(uint8_t x);
uint8_t Player_MoveLeft(uint8_t x);
void Player_WalkNormal(uint8_t x);
void Player_Walk(uint8_t x);
void Player_Ground(void);
void Player_StartJump(void);
void Player_RowChange(Regs *r);
void Player_GrabClimb(void);
void Player_Air(void);
void Player_Pogo(void);
void Player_Climb(void);
void Player_Cane(void);
void Player_MarkDamage(void);
void Player_TakeDamage(void);
void Player_Hurt(void);
void Player_Knockback(uint8_t x);
void Player_Stuck(void);
void Player_Pit(void);
void Player_Mode9(void);
void Player_Carried(void);
void Player_Mode12(void);
void Player_Death(void);
void Player_Mode14(void);
void Message_Text(Regs *r);
void Message_LetterNext(Regs *r);
void Message_Scroll(Regs *r);
void Message_Scrolled(Regs *r);
void Message_Wait(Regs *r);
void Message_Cleared(Regs *r);
void Message_WaitNext(Regs *r);
void Message_Paged(Regs *r);
void Message_Closed(Regs *r);
void Message_Shown(Regs *r);
void Message_Drawn(Regs *r);
void Message_Opened(Regs *r);
void Player_Message(Regs *r);
void Player_Treasure(Regs *r);
void Treasure_Waited(Regs *r);
void Treasure_Done(Regs *r);
