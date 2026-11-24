/*
 * player.c - Scrooge, decompiled from the fixed bank (docs/PLAYER.md,
 * docs/DECOMPILATION.md).
 *
 * Player_Update runs once per frame from the frame loop: it loads gravity,
 * counts the invulnerability and power-up timers down and runs the handler of
 * the current mode. Every routine below replaces the original one (decomp.c);
 * the address of each is in its comment and in ducktales_symbols.h. The
 * others call one another in C; only those original code calls have a site
 * (the table at the end).
 *
 * Where the original passes a value in a register to code whose effect
 * depends on it, the C passes it explicitly: "x" is the X register of the
 * original at that point. It matters for three things only: the slot the
 * walking code moves when it is entered without its own set-up (the push of
 * $73 with no direction held), the scratch byte $00 a sound request leaves,
 * and the routines not decompiled yet, which get the registers the original
 * gives them.
 *
 * The background probe, its snaps and the probe point $D350 (where the
 * vertical contact test can fall through to) are the C of collision.c, the
 * walls move $F0F8 and the climbing steps $F192/$F1A8 the C of objcommon.c
 * and the HP refill $F812 the C of objects/items.c.
 * Player_Update is task code (decomp.h): modes 15 and 16 (message box and
 * treasure) wait for frames, and the row change at $E8C0 drops
 * Player_Update's return address and hands off to the row scroll, which goes
 * back into the frame loop. The handlers on the way to the row change
 * (Player_Ground, Player_StartJump, Player_Air, Player_Pogo, Player_Climb,
 * Player_Stuck, Player_Carried) are task code too, since their original does
 * not return there: Player_Update's check covers them. The original jumps to
 * all of them (JMP), so no return address of theirs is on the stack. The
 * sites of $ED05, $EF62 and $E8C0 stay for the original Player_Update, which
 * replace mode runs when the call would cross VBlank.
 */
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

static void Player_Air(void);
static void Player_Walk(uint8_t x);
static uint8_t Player_Fall(void);
static void Player_StartJump(void);
static void Player_Knockback(uint8_t x);
static void Player_RowChange(Regs *r);
static void Player_Message(Regs *r);
static void Player_Treasure(Regs *r);
static void Player_Death(void);

static void Player_SetAction(uint8_t action)
{
    gObjects->action[P_] = action;
    gObjects->animFrame[P_] = 0;
    gObjects->animTimer[P_] = 0;
}

static uint8_t HitboxHeight(uint8_t box)
{
    return Game_ReadRom((uint16_t)(DT_CODE_HITBOX_HEIGHT + box));
}

static uint8_t HitboxWidth(uint8_t box)
{
    return Game_ReadRom((uint16_t)(DT_CODE_HITBOX_WIDTH + box));
}

/* How far a slot is from Scrooge as the contact tests measure it: the low
 * byte of the distance. Vertically a crouching Scrooge counts 8 px further
 * from a harmful hitbox. */
static uint8_t Player_DistanceX(uint8_t slot)
{
    uint16_t a = Obj_GetWorldX(P_), b = Obj_GetWorldX(slot);
    return (uint8_t)(a >= b ? a - b : b - a);
}

static uint8_t Player_DistanceY(uint8_t slot)
{
    uint8_t a = gObjects->y[P_], b = gObjects->y[slot];
    uint8_t d = (uint8_t)(a >= b ? a - b : b - a);
    if ((gObjects->hitbox[slot] & HITBOX_HARMFUL) && gObjects->action[P_] == PLAYER_ACTION_CROUCH) {
        d = (uint8_t)(d + 8);
    }
    return d;
}

/* $D202: the object Scrooge runs into after a horizontal move: a slot on
 * screen with a solid hitbox that he overlaps, searched from slot 15 down.
 * While he is invulnerable only blocks (kind 8) and kind $15 stop him. The
 * overlap goes to $11 and bit 0 of $10 is set; a harmful hitbox starts the
 * knock-back (mode 6), or with the power-up hurts the object ($0690).
 * Returns the slot, 0 for none (the original's carry set). */
static uint8_t Player_ContactH(void)
{
    Decomp_Enter(DT_CODE_CONTACT_H, 0, 0, 0);
    for (uint8_t x = OBJ_SLOTS - 1; x != 0; x--) {
        if (!(gObjects->flags[x] & OBJ_FLAG_ON_SCREEN) || !(gObjects->hitbox[x] & HITBOX_SOLID)) {
            continue;
        }
        uint8_t box = gObjects->hitbox[x] & HITBOX_INDEX;
        if (Player_DistanceY(x) >= HitboxHeight(box)) {
            continue;
        }
        gScratch(0) = (uint8_t)(HitboxWidth(box) + 1);
        uint8_t dx = Player_DistanceX(x);
        if (dx >= gScratch(0)) {
            continue;
        }
        gScratch(0) = dx;
        if (gPlayerInvulnerable && gObjects->kind[x] != OBJ_KIND_BLOCK && gObjects->kind[x] != OBJ_KIND_15) {
            continue;
        }
        gContactDepth = (uint8_t)(HitboxWidth(box) - dx);
        gProbeHits |= 1;
        if (gObjects->hitbox[x] & HITBOX_HARMFUL) {
            if (gPlayerPower) {
                gObjects->unk_0690[x]++;
            } else {
                gPlayerMode = PLAYER_MODE_KNOCKBACK;
            }
        }
        return x;
    }
    return 0;
}

/* $D291: the objects Scrooge overlaps after a vertical move, the same test
 * with the axes swapped, over the slots without $0600 bit 1; $00 counts
 * them. While he is invulnerable harmful ones are ignored. The first one a
 * pogo fall lands on is a bounce: feet class 1 and the object hurt ($0690).
 * Returns true for a contact (the original's carry clear). A $0690 that
 * wraps to 0 under the power-up falls through into $D350, whose return is
 * the test's. */
static bool Player_ContactV(void)
{
    Decomp_Enter(DT_CODE_CONTACT_V, 0, 0, 0);
    gScratch(0) = 0;
    gScratch(1) = 0;
    for (uint8_t x = OBJ_SLOTS - 1; x != 0; x--) {
        uint8_t flags = gObjects->flags[x];
        if (!(flags & OBJ_FLAG_ON_SCREEN) || (flags & OBJ_FLAG_NO_LANDING) || !(gObjects->hitbox[x] & HITBOX_SOLID)) {
            continue;
        }
        uint8_t box = gObjects->hitbox[x] & HITBOX_INDEX;
        uint8_t harmful = gObjects->hitbox[x] & HITBOX_HARMFUL;
        uint8_t dx = Player_DistanceX(x);
        if (dx >= HitboxWidth(box)) {
            continue;
        }
        gScratch(3) = dx;
        gScratch(2) = (uint8_t)(HitboxHeight(box) + 1);
        uint8_t dy = Player_DistanceY(x);
        if (dy >= gScratch(2)) {
            continue;
        }
        gScratch(2) = dy;
        if (gPlayerInvulnerable && harmful) {
            continue;
        }
        gScratch(0)++;
        gContactDepth = (uint8_t)(HitboxHeight(box) - dy);
        gProbeHits |= 1;
        if (gScratch(1) == 0 && (gObjects->vy[P_] & 0x80) && gObjects->action[P_] == PLAYER_ACTION_POGO &&
            (gObjects->vdir[P_] & 4)) {
            gFeetClass = 1;
            gScratch(1)++;
            if (++gObjects->unk_0690[x] != 0) {
                continue;
            }
        }
        if (!harmful) {
            continue;
        }
        if (!gPlayerPower) {
            gPlayerMode = PLAYER_MODE_KNOCKBACK;
            continue;
        }
        if (++gObjects->unk_0690[x] != 0) {
            continue;
        }
        bool carry;
        Bg_ProbePoint(x, box, &carry);
        return !carry;
    }
    return gScratch(0) != 0;
}

/* $E2FE: Select on the ground. When the last HP dot is lost and TOTAL is
 * 3,000,000 or more, TOTAL pays for a refill: its two leading digits drop by
 * 3 (leading zeros blanked again), every dot is full ($F812) and TOTAL is
 * redrawn.
 * Returns X as the original leaves it. */
static uint8_t Player_SelectRefill(void)
{
    Decomp_Enter(DT_CODE_SELECT_REFILL, 0, 0, 0);
    uint8_t x = 0;
    do {
        if (gRam[DT_ADDR_HUD_SPRITES + x] & 0x80) {
            break;
        }
        x += 4;
    } while (x != 0);
    if (gRam[DT_ADDR_HUD_SPRITES - 2 + x] == 0) {
        return x;
    }
    uint8_t p = P_C;
    gScratch(0) = SELECT_REFILL_COST;
    for (int i = 1; i >= 0; i--) {
        uint8_t digit = gTotalDigit(i);
        if (digit == DT_MONEY_TILE_BLANK) {
            digit = 0;
        }
        digit = Alu_Sbc(digit, gScratch(0), &p);
        if (!(p & P_C)) {
            digit = (uint8_t)(digit + 10);
        }
        gScratch(1 + i) = digit;
        gScratch(0) = 0;
    }
    if (!(p & P_C)) {
        return 0xFF;
    }
    gTotalDigit(0) = gScratch(1);
    gTotalDigit(1) = gScratch(2);
    for (x = 0; x < DT_TOTAL_DIGIT_COUNT && gTotalDigit(x) == 0; x++) {
        gTotalDigit(x) = DT_MONEY_TILE_BLANK;
    }
    Hud_RefillHp();
    gHudTotalDirty++;
    return x;
}

/* $EA5E: after a vertical move, test the feet (moving down or standing) or
 * the head (moving up) against the background and snap Y to the tile. Keeps
 * the facing flags and ORs the probe results into the ones of the move. */
static uint8_t Player_ProbeVertical(void)
{
    Decomp_Enter(DT_CODE_PROBE_VERTICAL, 0, 0, 0);
    uint8_t vdir = gObjects->vdir[P_];
    if (gTemp08 != gObjects->y[P_]) {
        vdir = gTemp08 > gObjects->y[P_] ? 8 : 4;
    }
    gTemp08 = vdir;
    uint8_t flags = gObjects->flags[P_], hits = gProbeHits, feet = gFeetClass;
    gProbeFirst = (vdir & 4) ? PROBE_FEET : PROBE_HEAD;
    gProbeCount = 2;
    Bg_Probe(P_);
    if (gProbeHits & 1) {
        if (gTemp08 & 4) {
            Bg_SnapFeet(P_);
        } else {
            Bg_SnapHead(P_);
        }
    }
    gFeetClass |= feet;
    gProbeHits |= hits;
    gObjects->flags[P_] = flags;
    return P_;
}

/* $EA1A: the pose while airborne: pogo with B+Down (or in a bounce), the
 * push pose kept against a wall, otherwise jumping. */
static void Player_AirPose(void)
{
    if (gPlayerMode == PLAYER_MODE_CANE) {
        return;
    }
    if (gPlayerMode == PLAYER_MODE_POGO || (gPadHeld & (DT_PAD_B | DT_PAD_DOWN)) == (DT_PAD_B | DT_PAD_DOWN)) {
        if (gObjects->action[P_] == PLAYER_ACTION_POGO) {
            return;
        }
        gObjects->action[P_] = PLAYER_ACTION_POGO;
        gObjects->animTimer[P_] = 0x80;
        gObjects->animFrame[P_] = 0;
        return;
    }
    if (gObjects->action[P_] == PLAYER_ACTION_PUSH && (gPadHeld & (DT_PAD_LEFT | DT_PAD_RIGHT)) == gPlayerPushDir) {
        return;
    }
    Player_SetAction(PLAYER_ACTION_JUMP);
}

/* $E970: falling, or standing (the floor stops the fall every frame). */
static uint8_t Player_Fall(void)
{
    Decomp_Enter(DT_CODE_PLAYER_FALL, 0, 0, 0);
    gProbeHits = 0;
    gFeetClass = 0;
    gObjects->vdir[P_] = 4;
    if (gObjects->vy[P_] != FALL_SPEED_LIMIT) {
        Obj_ApplyGravity(P_);
    }
    gTemp08 = gObjects->y[P_];
    Obj_ApplyVelocityY(P_);
    if (Player_ContactV()) {
        gObjects->y[P_] -= gContactDepth;
    }
    uint8_t x = Player_ProbeVertical();
    if (gProbeHits & 1) {
        Obj_SetVelocityY(P_, 0);
        return x;
    }
    if (gPlayerMode == PLAYER_MODE_GROUND) {
        gPlayerMode = PLAYER_MODE_AIR;
    }
    Player_AirPose();
    return x;
}

/* $EA07: a jump's rise ends: no velocity left, fall from here. */
static uint8_t Player_StopRising(void)
{
    Obj_SetVelocityY(P_, 0);
    gObjects->yFrac[P_] = 0;
    return Player_Fall();
}

/* $E9BA: rising. Releasing A cuts a jump or a pogo bounce once the velocity
 * is below its hold threshold; a ceiling ends the rise. */
static uint8_t Player_Rise(void)
{
    Decomp_Enter(DT_CODE_PLAYER_RISE, 0, 0, 0);
    if (gObjects->vy[P_] & 0x80) {
        return Player_Fall();
    }
    if (gPlayerMode < PLAYER_MODE_HURT) {
        uint16_t hold = (gPlayerMode & 2) ? POGO_HOLD_MIN : JUMP_HOLD_MIN;
        if (Obj_GetVelocityY(P_) < hold && !(gPadHeld & DT_PAD_A)) {
            return Player_StopRising();
        }
    }
    gObjects->vdir[P_] = 8;
    gTemp08 = gObjects->y[P_];
    gProbeHits = 0;
    gFeetClass = 0;
    Obj_ApplyVelocityY(P_);
    if (Player_ContactV()) {
        gObjects->y[P_] += (uint8_t)(gContactDepth + 1);
    }
    Player_ProbeVertical();
    if (gProbeHits & 1) {
        return Player_StopRising();
    }
    Obj_ApplyGravity(P_);
    Player_AirPose();
    return 0;
}

/* $E593 / $E5C3: move one step right or left at the current speed, stop
 * against objects ($D202) and walls (the side probes), and note which way the
 * step went in the end. x is the slot moved (0 in every path but one). */
static uint8_t Player_FinishMove(void)
{
    uint16_t before = (uint16_t)(gTemp09 << 8 | gTemp08);
    uint16_t after = Obj_GetWorldX(P_);
    gTemp08 = (uint8_t)(before - after);
    if (before != after) {
        gPlayerMoveSide = before >= after ? 1 : 2;
    }
    return 0;
}

static uint8_t Player_ProbeSide(void)
{
    uint8_t hits = gProbeHits;
    gProbeFirst = (gPlayerMoveSide & 2) ? PROBE_LEFT : PROBE_RIGHT;
    gProbeCount = 2;
    Bg_Probe(P_);
    if (gProbeHits & 1) {
        if (gPlayerMoveSide & 1) {
            Bg_SnapRight(P_);
        } else {
            Bg_SnapLeft(P_);
        }
    }
    gProbeHits |= hits;
    return P_;
}

static uint8_t Player_MoveRight(uint8_t x)
{
    Decomp_Enter(DT_CODE_MOVE_RIGHT, 0, x, 0);
    gPlayerMoveSide = 1;
    Obj_MoveRight(x);
    gObjects->flags[P_] |= 0x40;
    if (Player_ContactH()) {
        if (gContactDepth > 0x0C) {
            gContactDepth = 0x0C;
        }
        Obj_SetWorldX(P_, (uint16_t)(Obj_GetWorldX(P_) - gContactDepth));
        Player_FinishMove();
    }
    return Player_ProbeSide();
}

static uint8_t Player_MoveLeft(uint8_t x)
{
    Decomp_Enter(DT_CODE_MOVE_LEFT, 0, x, 0);
    gPlayerMoveSide = 2;
    Obj_MoveLeft(x);
    gObjects->flags[P_] &= ~0x40;
    if (Player_ContactH()) {
        if (gContactDepth > 0x0C) {
            gContactDepth = 0x0C;
        }
        Obj_SetWorldX(P_, (uint16_t)(Obj_GetWorldX(P_) + gContactDepth));
        Player_FinishMove();
    }
    return Player_ProbeSide();
}

/* $E57F: a step in the held direction. */
static void Player_Move(uint8_t dir)
{
    gProbeHits = 0;
    gPlayerMoveDir = dir;
    gTemp08 = gObjects->worldX[P_];
    gTemp09 = gObjects->worldPage[P_];
    if (dir & DT_PAD_LEFT) {
        Player_MoveLeft(0);
    } else {
        Player_MoveRight(0);
    }
}

/* $E495: walking at the normal speed, plus the one-frame push of $73. */
static void Player_WalkNormal(uint8_t x)
{
    gPlayerMomentumDir = 0;
    Obj_SetSpeed(P_, WALK_SPEED);
    if (gPlayerPushRight) {
        gPlayerPushRight = 0;
        if (!(gPadHeld & (DT_PAD_LEFT | DT_PAD_RIGHT))) {
            Player_MoveRight(x);
            return;
        }
        uint32_t pos = (uint32_t)Obj_GetWorldX(P_) << 8 | gObjects->xFrac[P_];
        pos += WALK_SPEED;
        gObjects->xFrac[P_] = (uint8_t)pos;
        Obj_SetWorldX(P_, (uint16_t)(pos >> 8));
    }
    uint8_t dir = gPadHeld & (DT_PAD_LEFT | DT_PAD_RIGHT);
    if (dir) {
        Player_Move(dir);
    }
}

/* $E4A6: on slippery floor the speed builds up to MOMENTUM_MAX while a
 * direction is held, slides down to WALK_SPEED when released and brakes
 * faster when reversed. */
static void Player_WalkMomentum(void)
{
    gPlayerSlippery = 0;
    uint8_t dir = gPadHeld & (DT_PAD_LEFT | DT_PAD_RIGHT);
    uint16_t speed = Obj_GetSpeed(P_);
    if (gPlayerMomentumDir == 0) {
        if (!dir) {
            return;
        }
        gPlayerMomentumDir = dir;
    } else if (!dir) {
        speed -= 6;
        Obj_SetSpeed(P_, speed);
        if (speed < WALK_SPEED && speed >= 0x0100) {
            gObjects->speedFrac[P_] = 0x80;
            gPlayerMomentumDir = 0;
            return;
        }
        if (gPlayerMomentumDir & 3) {
            Player_Move(gPlayerMomentumDir & 3);
        }
        return;
    } else if (dir != gPlayerMomentumDir) {
        speed -= 0x10;
        Obj_SetSpeed(P_, speed);
        if (speed < WALK_SPEED && speed >= 0x0100) {
            gObjects->speedFrac[P_] = 0x80;
            gPlayerMomentumDir = 0;
        }
        if (gPlayerMomentumDir & 3) {
            Player_Move(gPlayerMomentumDir & 3);
        }
        gObjects->flags[P_] = (uint8_t)((gObjects->flags[P_] & ~0x40) | ((gPadHeld & DT_PAD_RIGHT) ? 0x40 : 0));
        return;
    }
    speed += 0x10;
    Obj_SetSpeed(P_, speed);
    if (speed >= MOMENTUM_MAX && speed < 0x0300) {
        gObjects->speedFrac[P_] = 0x80;
    }
    if (gPlayerMomentumDir & 3) {
        Player_Move(gPlayerMomentumDir & 3);
    }
}

/* $E482: horizontal movement for this frame. */
static void Player_Walk(uint8_t x)
{
    Decomp_Enter(DT_CODE_PLAYER_WALK, 0, x, 0);
    uint8_t action = gObjects->action[P_];
    if (action != PLAYER_ACTION_JUMP && action != PLAYER_ACTION_POGO && (gPlayerSlippery || gPlayerMomentumDir)) {
        Player_WalkMomentum();
        return;
    }
    Player_WalkNormal(x);
}

/* $E3D8 / $E64F: B in the push pose swings the cane. */
static void Player_StartCane(void)
{
    gObjects->action[P_] = PLAYER_ACTION_CANE;
    gObjects->animTimer[P_] = 0;
    gObjects->animFrame[P_] = 0;
    gPlayerMode = PLAYER_MODE_CANE;
}

/* Mode 0, $E3B8: standing, walking, crouching, pushing. */
static void Player_Ground(void)
{
    Decomp_Enter(DT_CODE_PLAYER_GROUND, 0, 0, 0);
    uint8_t x = Player_Fall();
    if (gPlayerMode != PLAYER_MODE_GROUND) {
        Player_Walk(x);
        return;
    }
    if (gPadNew & DT_PAD_SELECT) {
        x = Player_SelectRefill();
    }
    if (gObjects->action[P_] == PLAYER_ACTION_PUSH && (gPadNew & DT_PAD_B)) {
        Player_StartCane();
        Player_WalkNormal(x);
        return;
    }
    if (gPadNew & DT_PAD_A) {
        Player_StartJump();
        return;
    }
    uint8_t held = gPadHeld & (DT_PAD_DOWN | DT_PAD_LEFT | DT_PAD_RIGHT);
    if (!held) {
        if (gObjects->action[P_] != PLAYER_ACTION_LANDED) {
            Player_SetAction(PLAYER_ACTION_STAND);
            gPlayerPushCounter = PUSH_FRAMES;
        }
        Player_Walk(x);
        return;
    }
    if (held & DT_PAD_DOWN) {
        if (gObjects->action[P_] != PLAYER_ACTION_CROUCH) {
            gObjects->action[P_] = PLAYER_ACTION_CROUCH;
            gObjects->animTimer[P_] = 0;
            gObjects->animFrame[P_] = 0x80;
            gObjects->pose[P_] = 1;
            gPlayerPushCounter = PUSH_FRAMES;
        }
        return;
    }
    Player_Walk(x);
    if (gProbeHits & 1) {
        if (gObjects->action[P_] == PLAYER_ACTION_PUSH) {
            return;
        }
        if (--gPlayerPushCounter == 0) {
            gPlayerPushDir = gPadHeld & (DT_PAD_LEFT | DT_PAD_RIGHT);
            Player_SetAction(PLAYER_ACTION_PUSH);
            return;
        }
    } else {
        gPlayerPushCounter = PUSH_FRAMES;
    }
    if (gObjects->action[P_] == PLAYER_ACTION_WALK) {
        return;
    }
    Player_SetAction(PLAYER_ACTION_WALK);
}

/* $E3F2: take off with the land's jump velocity ($0790/$07A0). */
static void Player_StartJump(void)
{
    Decomp_Enter(DT_CODE_JUMP_START, 0, 0, 0);
    Obj_SetVelocityY(P_, Player_GetJumpVelocity());
    Decomp_RaiseEvent(EVENT_JUMP_START);
    gPlayerMode = PLAYER_MODE_AIR;
    Player_Air();
}

/* $E878: Scrooge left the screen row: in the final arena that ends the
 * fight's drop; otherwise move to the row above or below if the map has one
 * there. The move (Player_RowChange, A = row, X = 2 with Up held, Y = the
 * camera screen, $00 the stream screen) leaves Player_Update without
 * returning. */
static void Player_LeaveRow(void)
{
    if (gFinalArena && gObjects->y[P_] >= FINAL_ARENA_DROP_Y) {
        RAM_U8(DT_ADDR_SCENE_STEP) = 0x0C;
        gPlayerMode = PLAYER_MODE_MESSAGE;
        return;
    }
    if (gCameraX != 0 || !(gCameraPage & 1)) {
        return;
    }
    bool up = (gPadHeld & DT_PAD_UP) != 0;
    uint8_t p = 0;
    uint8_t step = Alu_Adc((uint8_t)(gMapStride ^ (up ? 0xFF : 0x00)), up ? 1 : 0, &p);
    uint8_t screen = Alu_Adc(step, gCameraScreen, &p);
    p = 0;
    gScratch(0) = Alu_Adc(step, gStreamScreen, &p);
    uint16_t grid = Ram_GetU16(DT_ADDR_MAP_GRID, DT_ADDR_MAP_GRID + 1);
    uint16_t cell = (uint16_t)(grid + screen);
    if ((cell < GAME_RAM_SIZE ? gRam[cell] : Game_ReadRom(cell)) == 0xFF) {
        return;
    }
    p = 0;
    uint8_t row = Alu_Adc(gMapRow, up ? 0xFF : 0x01, &p);
    if (row >= gMapRows) {
        return;
    }
    Regs r = { row, up ? 2 : 0, screen, 0 };
    Player_RowChange(&r);
}

/* $E8C0: the row change, reached from $E878 with A = the new row, X = 0 (or 2
 * with Up held), Y = the new camera screen and $00 the new stream screen. It
 * gives slot X its entry Y, drops Player_Update's return address and hands
 * off to the row scroll ($C976), which returns to a JMP into the frame loop
 * at $DBE4. */
static void Player_RowChange(Regs *r)
{
    Decomp_Enter(DT_CODE_ROW_CHANGE, r->a, r->x, r->y);
    gMapRow = r->a;
    gObjects->y[r->x] = Game_ReadRom((uint16_t)(DT_CODE_ROW_ENTRY_Y + r->x));
    gCameraScreen = r->y;
    gStreamScreen = gScratch(0);
    Decomp_Pull();
    Decomp_Pull();
    RAM_U8(DT_ADDR_HELD_SLOT) = 0;
    gObjects->flags[P_] &= 0xFC;
    r->a = gObjects->flags[P_];
    Decomp_Handoff(DT_CODE_ROW_SCROLL, DT_CODE_ROW_CHANGE_END, 0);
}

/* $E719: falling off the bottom of the screen drops into the row below. */
static void Player_DropToRowBelow(void)
{
    if ((uint8_t)(gMapRow + 1) == gMapRows) {
        return;
    }
    if (gCameraX & 0x80) {
        gCameraPage++;
        gCameraScreen++;
    }
    gCameraX = 0;
    gStreamPos = (gStreamDir & 1) ? 0x1F : 0x20;
    gPadHeld = DT_PAD_DOWN;
    Player_LeaveRow();
}

/* $E740: with Up held, grab a climbable tile (class 2) near the middle of
 * Scrooge: snap to its column and, when only one of the two probes of that
 * side found it, to its row. */
static void Player_GrabClimb(void)
{
    Decomp_Enter(DT_CODE_GRAB_CLIMB, 0, 0, 0);
    static const uint8_t COLUMN_OFFSET[4] = { 0x00, 0x00, 0x08, 0x08 };
    if (!(gPadHeld & DT_PAD_UP)) {
        return;
    }
    gProbeFirst = PROBE_CLIMB;
    gProbeCount = 3;
    Bg_Probe(P_);
    if (gProbeHits & 1) {
        return;
    }
    int i;
    for (i = 3; i >= 0; i--) {
        if (((gObjects->worldX[P_] + COLUMN_OFFSET[i]) & 0x0F) < 8 && gProbeClass(i) == 2) {
            break;
        }
    }
    if (i < 0) {
        return;
    }
    uint8_t within = gObjects->worldX[P_] & 0x0F;
    gScratch(0) = within;
    if (i >= 2) {
        Obj_SetWorldX(P_, (uint16_t)(Obj_GetWorldX(P_) + 16 - within));
        gPlayerMoveDir = 1;
    } else {
        Obj_SetWorldX(P_, (uint16_t)(Obj_GetWorldX(P_) - within));
        gPlayerMoveDir = 2;
    }
    int pair = i & 2;
    if (gProbeClass(pair) != gProbeClass(pair + 1)) {
        uint8_t row = gObjects->y[P_] & 0xF0;
        gObjects->y[P_] = (uint8_t)(((i & 1) ? (uint8_t)(row - 0x10) : row) | 8);
    }
    Player_SetAction(PLAYER_ACTION_CLIMB);
    gPlayerMode = PLAYER_MODE_CLIMB;
}

/* Mode 1, $E63B: in the air. Landing bounces on the pogo when the middle
 * feet probe stands on something solid with B held (and Down, or already
 * bouncing); otherwise Scrooge lands. */
static void Player_Air(void)
{
    Decomp_Enter(DT_CODE_PLAYER_AIR, 0, 0, 0);
    gPlayerPushCounter = PUSH_FRAMES;
    uint8_t x = Player_Rise();
    if (gObjects->action[P_] == PLAYER_ACTION_PUSH && (gPadNew & DT_PAD_B)) {
        Player_StartCane();
        return;
    }
    if ((gObjects->vdir[P_] & 4) && (gProbeHits & 1)) {
        bool bounce = false;
        if (gObjects->action[P_] != PLAYER_ACTION_PUSH && gPlayerMode != PLAYER_MODE_CANE) {
            bounce = (gFeetClass & 1) && (gPlayerMode == PLAYER_MODE_POGO || (gPadHeld & DT_PAD_DOWN)) &&
                     (gPadHeld & DT_PAD_B) && (!RAM_U8(DT_ADDR_POGO_CEILING) || gObjects->y[P_] < POGO_CEILING_Y);
            if (bounce) {
                Sound_Request(SFX_POGO_BOUNCE, x);
                Obj_SetVelocityY(P_, POGO_BOUNCE_VY);
                gObjects->action[P_] = PLAYER_ACTION_POGO;
                gObjects->animTimer[P_] = 0;
                gObjects->pose[P_] = 2;
                gObjects->animFrame[P_] = 0x81;
                gPlayerMode = PLAYER_MODE_POGO;
            } else {
                Sound_Request(SFX_LAND, x);
                gObjects->action[P_] = PLAYER_ACTION_LANDED;
                gObjects->animTimer[P_] = 0;
                gObjects->animFrame[P_] = 0x80;
                gObjects->pose[P_] = 1;
            }
        }
        if (!bounce) {
            gPlayerMode = PLAYER_MODE_GROUND;
        }
    }
    Player_Walk(x);
    if (gObjects->action[P_] == PLAYER_ACTION_PUSH && !(gProbeHits & 1)) {
        Player_SetAction(PLAYER_ACTION_JUMP);
    }
    Player_GrabClimb();
    if ((gObjects->vdir[P_] & 4) && !(gPad2Held & DT_PAD_UP) && gObjects->y[P_] >= ROW_BOTTOM_Y) {
        Player_DropToRowBelow();
    }
}

/* Mode 2, $E7EA: a pogo bounce is the air mode with B standing in for A, so
 * B alone keeps the bounce at full height. */
static void Player_Pogo(void)
{
    Decomp_Enter(DT_CODE_PLAYER_POGO, 0, 0, 0);
    gPlayerPushCounter = PUSH_FRAMES;
    gScratch(0) = (uint8_t)(gPadHeld << 1) & DT_PAD_A;
    gPadHeld |= gScratch(0);
    if (!(gPadHeld & DT_PAD_A)) {
        gPlayerMode = PLAYER_MODE_AIR;
    }
    Player_Air();
}

/* $E86D */
static void Player_LetGo(void)
{
    gPlayerMode = PLAYER_MODE_GROUND;
    Obj_SetVelocityY(P_, 0);
}

/* Mode 3, $E806: climbing. Up/Down step along the rope at the walking speed;
 * A with neither lets go; leaving the rope's tiles drops off; the top or
 * bottom of the screen changes the row. */
static void Player_Climb(void)
{
    Decomp_Enter(DT_CODE_PLAYER_CLIMB, 0, 0, 0);
    gPlayerPushCounter = PUSH_FRAMES;
    uint8_t dir = gPadHeld & (DT_PAD_UP | DT_PAD_DOWN);
    if (!dir) {
        if (gPadNew & DT_PAD_A) {
            Player_LetGo();
            return;
        }
        gObjects->animTimer[P_] |= 0x80;
        return;
    }
    gObjects->animTimer[P_] &= 0x7F;
    if (gObjects->animTimer[P_] == 0) {
        Sound_Request(SFX_CLIMB, dir);
    }
    gObjects->vyFrac[P_] = gObjects->speedFrac[P_];
    gObjects->vy[P_] = gObjects->speed[P_];
    if (dir != DT_PAD_UP) {
        (void)Obj_ClimbDown(P_, 0x10, 3);
        if (gClimbClass != 2) {
            Player_LetGo();
        } else if (gObjects->y[P_] >= ROW_BOTTOM_Y) {
            Player_LeaveRow();
        }
        return;
    }
    (void)Obj_ClimbUp(P_, 0x14, 3);
    if (gClimbClass != 2) {
        Player_LetGo();
    } else if (gObjects->y[P_] < ROW_TOP_Y_CLIMBING) {
        Player_LeaveRow();
    }
}

/* Mode 4, $E8EB: the cane swing. On animation frame 2 the swing tests the
 * object in front; at frame 6 it returns to the ground (in the push pose if
 * the push direction is still held). A connecting swing recoils for
 * CANE_HIT_FRAMES. */
static void Player_Cane(void)
{
    Decomp_Enter(DT_CODE_PLAYER_CANE, 0, 0, 0);
    uint8_t x = Player_Rise();
    if (gObjects->action[P_] == PLAYER_ACTION_CANE_HIT) {
        if (gObjects->animTimer[P_] == 0 && gObjects->animFrame[P_] == 0 && --gObjects->timer[P_] == 0) {
            gPlayerMode = PLAYER_MODE_GROUND;
            gPlayerPushCounter = PUSH_FRAMES;
        }
        return;
    }
    if (gObjects->animFrame[P_] == 6) {
        if (gPadHeld & gPlayerPushDir) {
            Player_SetAction(PLAYER_ACTION_PUSH);
            gPlayerMode = PLAYER_MODE_GROUND;
            return;
        }
        gPlayerMode = PLAYER_MODE_GROUND;
        gPlayerPushCounter = PUSH_FRAMES;
    }
    if (gObjects->animTimer[P_] != 0 || gObjects->animFrame[P_] != 2) {
        return;
    }
    Sound_Request(SFX_CANE_SWING, x);
    uint8_t slot = Player_ContactH();
    if (slot) {
        uint8_t kind = gObjects->kind[slot];
        if (kind == OBJ_KIND_BLOCK || kind == OBJ_KIND_15 || kind == OBJ_KIND_CHEST_HEALTH) {
            return;
        }
    }
    gObjects->animTimer[P_] = 0;
    gObjects->animFrame[P_] = 0;
    gObjects->timer[P_] = CANE_HIT_FRAMES;
    gObjects->action[P_] = PLAYER_ACTION_CANE_HIT;
    Sound_Request(SFX_CANE_HIT, slot);
}

/* $EB5E without its event: on Easy only every other hit counts; the last
 * full HP dot is marked lost. */
static void Player_MarkDamage(void)
{
    Decomp_Enter(DT_CODE_TAKE_DAMAGE, 0, 0, 0);
    if (gDifficulty == DIFFICULTY_EASY && (++gPlayerEasyHits & 1)) {
        return;
    }
    uint8_t y = 0;
    do {
        if ((HUD_RECORD(0)[y] & 0x80) || HUD_RECORD(0)[y + 2] != 0) {
            break;
        }
        y = (uint8_t)(y + 4);
    } while (y != 0);
    gRam[DT_ADDR_HUD_SPRITES - 2 + y] = DT_HP_DOT_LOST;
}

/* $EB5E: a hit costs an HP dot (EVENT_PLAYER_DAMAGED can cancel it). */
static void Player_TakeDamage(void)
{
    if (Decomp_RaiseEvent(EVENT_PLAYER_DAMAGED) == EVENT_CANCEL) {
        return;
    }
    Player_MarkDamage();
}

static bool Player_OutOfHp(void)
{
    return HUD_RECORD(0)[2] != 0;
}

/* $EB4F: the first HP dot is gone: death. */
static void Player_Die(void)
{
    gPlayerMode = PLAYER_MODE_DEATH;
    gObjects->state[P_] &= 0xF0;
    Player_Death();
}

/* $EADC: the hurt pose drifts one step against the facing, which is kept. */
static void Player_HurtMove(uint8_t x)
{
    gObjects->action[P_] = PLAYER_ACTION_HURT;
    if (gObjects->flags[P_] & 0x40) {
        Player_MoveLeft(x);
        gObjects->flags[P_] |= 0x40;
    } else {
        Player_MoveRight(x);
        gObjects->flags[P_] &= ~0x40;
    }
}

/* Mode 5, $EAB9: hurt, waiting for the invulnerability timer; when it is
 * already over, the hit is taken now. */
static void Player_Hurt(void)
{
    Decomp_Enter(DT_CODE_PLAYER_HURT, 0, 0, 0);
    uint8_t timer = gPlayerInvulnerable;
    if (timer == 0) {
        gPlayerMode = PLAYER_MODE_GROUND;
        Obj_SetSpeed(P_, WALK_SPEED);
        Player_TakeDamage();
        if (Player_OutOfHp()) {
            Player_Die();
        }
        return;
    }
    if (timer >= 8) {
        return;
    }
    Player_HurtMove(Player_Fall());
}

/* Mode 6, $EB00: knocked back by a harmful object. The first frame plays the
 * hurt sound, throws Scrooge up, starts the invulnerability and takes the
 * hit; then he drifts back under gravity until KNOCKBACK_END. */
static void Player_Knockback(uint8_t x)
{
    Decomp_Enter(DT_CODE_PLAYER_KNOCKBACK, 0, x, 0);
    if (gPlayerInvulnerable == 0) {
        Sound_Request(SFX_HURT, x);
        Obj_SetVelocityY(P_, KNOCKBACK_VY);
        gObjects->animTimer[P_] = 0;
        gObjects->animFrame[P_] = 0;
        gObjects->speedFrac[P_] = 0;
        gPlayerInvulnerable = KNOCKBACK_FRAMES;
        Player_TakeDamage();
        if (Player_OutOfHp()) {
            Player_Die();
            return;
        }
    }
    x = (gObjects->vy[P_] & 0x80) ? Player_Fall() : Player_Rise();
    if (gPlayerInvulnerable == KNOCKBACK_END) {
        gObjects->speedFrac[P_] = 0x80;
        gPlayerMode = PLAYER_MODE_GROUND;
    }
    gObjects->action[P_] = PLAYER_ACTION_HURT;
    Player_HurtMove(x);
}

/* Mode 7, $EB82: stuck in snow; a jump frees him. */
static void Player_Stuck(void)
{
    Decomp_Enter(DT_CODE_PLAYER_STUCK, 0, 0, 0);
    if (gObjects->action[P_] != PLAYER_ACTION_STUCK) {
        Player_SetAction(PLAYER_ACTION_STUCK);
        gObjects->y[P_] += STUCK_SINK;
    }
    if (gPadNew & DT_PAD_A) {
        Player_StartJump();
    }
}

/* $EBB4: fell into a pit: request the respawn that costs a life. */
void Player_PitDeath(void)
{
    Decomp_Enter(DT_CODE_PIT_DEATH, 0, 0, 0);
    uint8_t p = 0;
    gRespawn = Alu_Adc(Alu_Adc(gLand, 0x87, &p), gBossCheckpoint, &p);
    gPlayerMode = PLAYER_MODE_GROUND;
    gPlayerHazard = 0;
    gObjects->state[P_] = 0;
    gPlayerEasyHits = 0;
}

/* Mode 8, $EBA7: falling; below PIT_Y it is a pit death. */
static void Player_Pit(void)
{
    Decomp_Enter(DT_CODE_PLAYER_PIT, 0, 0, 0);
    uint8_t x = Player_Fall();
    if (gObjects->y[P_] < PIT_Y) {
        Player_Walk(x);
        return;
    }
    Player_PitDeath();
}

/* Mode 9, $EBC9: waits for A. */
static void Player_Mode9(void)
{
    Decomp_Enter(DT_CODE_PLAYER_MODE9, 0, 0, 0);
    if (gPadNew & DT_PAD_A) {
        gPlayerMode = PLAYER_MODE_GROUND;
    }
    gObjects->animTimer[P_] = 0;
}

/* Modes 10 and 11, $EBD9: carried (hypothesis); Down drops, A jumps off in
 * mode 11, and a wall under the carrier knocks Scrooge back. */
static void Player_Carried(void)
{
    Decomp_Enter(DT_CODE_PLAYER_CARRIED, 0, 0, 0);
    gPlayerMoveDir = 2;
    gPlayerMoveSide = 2;
    gObjects->state[P_] = (gPadHeld & DT_PAD_DOWN) ? 0 : 0xA0;
    if (gPlayerMode != PLAYER_MODE_CARRIED_A && !(gPadHeld & DT_PAD_DOWN) && gPadNew == DT_PAD_A) {
        Player_StartJump();
        return;
    }
    if (gObjects->state[P_] == 0) {
        return;
    }
    gProbeFirst = PROBE_LEFT;
    gProbeCount = 2;
    Bg_Probe(P_);
    if (!(gProbeHits & 1)) {
        return;
    }
    gObjects->y[P_] += 8;
    gPlayerMode = PLAYER_MODE_KNOCKBACK;
    Player_Knockback(P_);
}

/* Mode 12, $EC2D (not studied): a scripted fall and drift to the right. */
static void Player_Mode12(void)
{
    Decomp_Enter(DT_CODE_PLAYER_MODE12, 0, 0, 0);
    if (gObjects->vdir[P_] != 0) {
        gObjects->state[P_] = 0xE0;
        Obj_ApplyGravity(P_);
        Obj_ApplyVelocityY(P_);
    } else if (gObjects->y[P_] >= 0x18) {
        Obj_ApplyVelocityY(P_);
        Obj_ApplyGravity(P_);
    }
    gPlayerMoveDir = 1;
    gPlayerMoveSide = 1;
    (void)Obj_WalkRight(P_, 0, 2);
    if (gObjects->vdir[P_] == 0 && gObjects->worldPage[P_] == 7) {
        gObjects->vdir[P_]++;
        Obj_SetVelocityY(P_, 0);
    }
    if (gProbeHits & 1) {
        gPlayerMode = PLAYER_MODE_GROUND;
        Obj_SetSpeed(P_, WALK_SPEED);
    }
    Player_SetAction(PLAYER_ACTION_JUMP);
}

/* Mode 13, $EC9B: the death jump: up, then down off the screen, drifting
 * right; off the bottom it is the pit death request. */
static void Player_Death(void)
{
    Decomp_Enter(DT_CODE_PLAYER_DEATH, 0, 0, 0);
    if ((gObjects->state[P_] & 0x0F) == 0) {
        gObjects->vdir[P_] = 0;
        gObjects->animFrame[P_] = 0;
        gObjects->action[P_] = PLAYER_ACTION_HURT;
        gObjects->dir[P_] = 1;
        Obj_SetVelocityY(P_, DEATH_VY);
        Obj_SetSpeed(P_, 0x0080);
        gObjects->state[P_]++;
    }
    if (gObjects->vdir[P_] == 0) {
        Obj_ApplyVelocityY(P_);
        Obj_ApplyGravity(P_);
        if (gObjects->vy[P_] & 0x80) {
            gObjects->vdir[P_]++;
            Obj_SetVelocityY(P_, 0);
            return;
        }
    } else {
        Obj_ApplyGravity(P_);
        Obj_ApplyVelocityY(P_);
        if (gObjects->y[P_] >= ROW_BOTTOM_Y) {
            Player_PitDeath();
            return;
        }
    }
    Obj_MoveRight(P_);
}

/* Mode 14, $ECFF */
static void Player_Mode14(void)
{
    Decomp_Enter(DT_CODE_PLAYER_MODE14, 0, 0, 0);
    gObjects->animTimer[P_] = 0;
}

/* $E354: once per frame from the frame loop. */
static void Player_Update(Regs *r)
{
    Decomp_Enter(DT_CODE_PLAYER_UPDATE, r->a, r->x, r->y);
    gGravity = PLAYER_GRAVITY;
    if (gPlayerInvulnerable != 0 && !(gPlayerInvulnerable & 0x80)) {
        gPlayerInvulnerable--;
    }
    if (gPlayerPower != 0 && !(gPlayerPower & 0x80)) {
        if (gMusic != MUSIC_POWER) {
            Music_Request(MUSIC_POWER, r->x);
        }
        uint8_t p = P_C;
        RAM_U8(DT_ADDR_POWER_FRAMES) = Alu_Sbc(RAM_U8(DT_ADDR_POWER_FRAMES), 1, &p);
        RAM_U8(DT_ADDR_POWER_FRAMES + 1) = Alu_Sbc(RAM_U8(DT_ADDR_POWER_FRAMES + 1), 0, &p);
        if (Player_GetPowerFrames() == 0) {
            gPlayerPower = 0;
            Music_Request(gLand, r->x);
        }
    }
    uint8_t mode = gPlayerMode;
    gScratch(0) = Game_ReadRom((uint16_t)(DT_CODE_MODE_LO + mode));
    gScratch(1) = Game_ReadRom((uint16_t)(DT_CODE_MODE_HI + mode));
    r->a = gScratch(1);
    r->x = mode;
    switch (mode) {
        case PLAYER_MODE_GROUND:
            Player_Ground();
            break;
        case PLAYER_MODE_AIR:
            Player_Air();
            break;
        case PLAYER_MODE_POGO:
            Player_Pogo();
            break;
        case PLAYER_MODE_CLIMB:
            Player_Climb();
            break;
        case PLAYER_MODE_CANE:
            Player_Cane();
            break;
        case PLAYER_MODE_HURT:
            Player_Hurt();
            break;
        case PLAYER_MODE_KNOCKBACK:
            Player_Knockback(mode);
            break;
        case PLAYER_MODE_STUCK:
            Player_Stuck();
            break;
        case PLAYER_MODE_PIT:
            Player_Pit();
            break;
        case PLAYER_MODE_9:
            Player_Mode9();
            break;
        case PLAYER_MODE_CARRIED_A:
        case PLAYER_MODE_CARRIED_B:
            Player_Carried();
            break;
        case PLAYER_MODE_12:
            Player_Mode12();
            break;
        case PLAYER_MODE_DEATH:
            Player_Death();
            break;
        case PLAYER_MODE_14:
            Player_Mode14();
            break;
        case PLAYER_MODE_MESSAGE:
            Player_Message(r);
            break;
        case PLAYER_MODE_TREASURE:
            Player_Treasure(r);
            break;
    }
}

/* ---- modes 15 and 16: task code (decomp.h), reached at their own sites ---- */

#define gMessageId       RAM_U8(DT_ADDR_SCENE_STEP)
#define gMessageNext     RAM_U8(DT_ADDR_MESSAGE) /* offset of the next letter; 0 once the text is done */
#define gMessageText     DT_ADDR_MESSAGE_TEXT
#define gMessageAt       RAM_U8(DT_ADDR_MESSAGE_AT)
#define gMessageLine     RAM_U8(DT_ADDR_MESSAGE_LINE)
#define gMessageSlot     RAM_U8(DT_ADDR_MESSAGE_SLOT)
#define gLineEntry       RAM_U8(DT_ADDR_SCRATCH10) /* queue offset of the line's entry */
#define gLineCount       RAM_U8(DT_ADDR_SCRATCH11) /* the entry's byte count - 1 */
#define gVramPending     RAM_U8(DT_ADDR_VRAM_PENDING)
#define gQueue(i)        RAM_U8(DT_ADDR_VRAM_QUEUE + (i))
#define gMoneyDigit(i)   RAM_U8(DT_ADDR_MONEY_DIGITS + (i))
#define gTimeDigit(i)    RAM_U8(DT_ADDR_TIME_DIGITS + 4 * (i))
#define gCollision(i)    RAM_U8(DT_ADDR_COLLISION_MAP + (i))
#define gLaunchpadRides  RAM_U8(DT_ADDR_LAUNCHPAD_RIDES)
#define gBgOffBelowSplit RAM_U8(DT_ADDR_BG_OFF_BELOW_SPLIT)
#define gHudMoneyDirty   RAM_U8(DT_ADDR_HUD_MONEY_DIRTY)
#define gLandLeft        RAM_U8(DT_ADDR_LAND_LEFT)
#define gSpritePhase     RAM_U8(DT_ADDR_SPR_PHASE)
#define gScene           RAM_U8(DT_ADDR_SCENE)

#define BANK_TILE_OFF   1
#define BANK_TILE       2
#define BANK_MESSAGES   4
#define TILE_ENTRY_HI   0x12 /* the queue entry of CHR tile $12D: PPU $12D0, 16 bytes */
#define TILE_ENTRY_LO   0xD0
#define TILE_BYTES      0x10
#define QUEUE_END       0xFF
#define BOX_ROW_HI      0x20
#define TILE_BLANK      0x24
#define TILE_TEXT_END   0x2E /* the mark after the last letter */
#define TEXT_END        0xAE
#define TEXT_LINE_END   0xFF
#define LINE_LETTERS    0x10
#define BOX_LINES       3
#define LETTER_FRAMES   3
#define SFX_LETTER      0x2E
#define SFX_TIME_BONUS  0x15
#define MUSIC_NONE      0xFF
#define MUSIC_FRAMES    0 /* task_wait counts 256 frames */
#define LEAVE_FRAMES    0x3C
#define PAY_COST        3 /* taken from the second money digit */
#define SCENE_LAUNCHPAD 0x81

#define MESSAGE_KEY_CHECK 0x03 /* the African Mines key not taken: Transylvania next (hypothesis) */
#define MESSAGE_BLOCK     0x06
#define MESSAGE_PAY       0x0A
#define MESSAGE_SCENE     0x0B
#define MESSAGE_LIFE      0x0C
#define MESSAGE_LAUNCHPAD 0x0F

#define BLOCK_STATE  0xA0
#define BLOCK_ACTION 0x1B
#define BLOCK_Y      0xB8
#define BLOCK_X      0x30
#define BLOCK_TIMER  0x0B
#define BLOCK_CELL   0x21 /* the two collision cells the block's draw clears */
#define BLOCK_CELL_2 0x29
#define BLOCK_TILE   0
#define LIFT_STATE   0xE0 /* bits kept */
#define LIFT_Y       0x80
#define LIFT_X       0x80

/* bank_select: the C reads the bank's ROM too, so its view follows. */
static void Task_SelectBank(Regs *r, uint8_t bank)
{
    Decomp_SelectBank(bank);
    Game_MapperWrite((uint16_t)(DT_CODE_BANK_TABLE + bank), bank);
    r->a = r->x = bank;
}

static void Task_Wait(uint16_t resume)
{
    Decomp_Handoff(DT_CODE_TASK_WAIT, resume, OUT_X | OUT_Y);
}

static void Task_Yield(uint16_t resume)
{
    Decomp_Handoff(DT_CODE_TASK_YIELD, resume, OUT_X | OUT_Y);
}

/* JSR from task code to task code in C: true when the callee returned. */
static bool Task_Gosub(void (*routine)(Regs *), Regs *r, uint16_t resume)
{
    Decomp_PushReturn(resume);
    routine(r);
    if (Decomp_HandedOff()) {
        return false;
    }
    Decomp_PullReturn();
    return true;
}

/* Queue CHR tile $12D from the bank's copy at tile; the NMI uploads it. */
static void Message_QueueTile(Regs *r, uint8_t bank, uint16_t tile)
{
    Task_SelectBank(r, bank);
    gQueue(0) = TILE_ENTRY_HI;
    gQueue(1) = TILE_ENTRY_LO;
    gQueue(2) = TILE_BYTES - 1;
    for (int i = TILE_BYTES - 1; i >= 0; i--) {
        gQueue(3 + i) = r->a = Game_ReadRom((uint16_t)(tile + i));
    }
    r->x = 0xFF;
    gQueue(3 + TILE_BYTES) = r->x;
    gVramPending = r->x;
}

/* $EF20: end the line's queue entry at x and count it. */
static void Message_EndEntry(Regs *r, uint8_t extra)
{
    gQueue(r->x) = QUEUE_END;
    r->x = gLineEntry;
    r->a = (uint8_t)(gLineCount + extra);
    gQueue(2 + r->x) = r->a;
    gVramPending = r->a = 0xFF;
}

/* $EEAE: write the box's lines from gMessageLine on, a letter every 3 frames
 * with a blip (spaces are free). A line ends at 16 letters or at $FF, and
 * its entry is padded with spaces; the text ends at $AE with the end mark.
 * The box holds 3 lines: the page ends with text left (gMessageNext) and
 * the wait scrolls it. letter: resume at the next letter, x the queue. */
static void Message_Write(Regs *r, bool letter)
{
    for (;;) {
        if (!letter) {
            r->y = gMessageLine;
            gMessageAt = gLineEntry = r->x = Game_ReadRom((uint16_t)(DT_CODE_MESSAGE_LINE_AT + r->y));
            gLineCount = 0xFF;
            gQueue(r->x) = BOX_ROW_HI;
            gQueue(r->x + 1) = r->a = Game_ReadRom((uint16_t)(DT_CODE_MESSAGE_LINE_LO + r->y));
        }
        letter = false;
        for (;;) {
            r->y = gMessageNext;
            r->a = Game_ReadRom((uint16_t)(Ram_GetU16(gMessageText, gMessageText + 1) + r->y));
            if (r->a == TEXT_END) {
                gQueue(r->x + 3) = TILE_TEXT_END;
                gQueue(r->x + 4) = QUEUE_END;
                r->x = gLineEntry;
                gQueue(2 + r->x) = (uint8_t)(gLineCount + 1);
                gVramPending = 0xFF;
                gMessageNext = r->a = 0;
                return;
            }
            if (r->a == TEXT_LINE_END) {
                gMessageNext++;
                break;
            }
            gQueue(r->x + 3) = r->a;
            r->x++;
            gLineCount++;
            gMessageNext++;
            if (r->a == TILE_BLANK) {
                continue;
            }
            gMessageAt = r->x;
            r->a = gLineCount;
            if (r->a == LINE_LETTERS) {
                break;
            }
            r->a = SFX_LETTER;
            Sound_Request(SFX_LETTER, r->x);
            r->x = (uint8_t)(r->x + 3);
            Message_EndEntry(r, 0);
            r->x = LETTER_FRAMES;
            Task_Wait(DT_MESSAGE_LETTER_NEXT);
            return;
        }
        r->x = (uint8_t)(r->x + 3);
        for (;;) {
            gMessageAt = r->x;
            r->y = gMessageLine;
            r->a = Game_ReadRom((uint16_t)(DT_CODE_MESSAGE_LINE_END + r->y));
            if (r->a == gMessageAt) {
                break;
            }
            gQueue(r->x) = r->a = TILE_BLANK;
            gLineCount++;
            if (++r->x == 0) {
                break;
            }
        }
        Message_EndEntry(r, 0);
        r->a = gMessageLine;
        if (r->a == BOX_LINES - 1) {
            return;
        }
        gMessageLine++;
    }
}

static void Message_Text(Regs *r)
{
    Decomp_Enter(DT_CODE_MESSAGE_TEXT, r->a, r->x, r->y);
    Message_Write(r, false);
}

static void Message_LetterNext(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_LETTER_NEXT, r->a, r->x, r->y);
    r->x = gMessageAt;
    Message_Write(r, true);
}

/* $EE8B: lines 2 and 3 move up, the last one is cleared. */
static void Message_Scroll(Regs *r)
{
    Decomp_Enter(DT_CODE_MESSAGE_SCROLL, r->a, r->x, r->y);
    for (int i = LINE_LETTERS; i >= 0; i--) {
        gQueue(3 + i) = gQueue(0x17 + i);
        gQueue(0x17 + i) = gQueue(0x2B + i);
        gQueue(0x2B + i) = r->a = TILE_BLANK;
    }
    r->x = 0xFF;
    gVramPending = r->x;
    Task_Yield(DT_MESSAGE_SCROLLED);
}

static void Message_Scrolled(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_SCROLLED, r->a, r->x, r->y);
    gMessageLine = BOX_LINES - 1;
    gMessageAt = r->a = 0x28;
    Message_Text(r);
}

/* $EE22: closing message $0A with A pays 30,000 (2 digits, blanks as 0)
 * when money has it, and makes the speaker's slot a lift. */
static void Message_Pay(Regs *r)
{
    r->a = gPadNew & DT_PAD_B;
    if (r->a) {
        return;
    }
    gScratch(0) = PAY_COST;
    uint8_t p = P_C;
    for (int x = 1; x >= 0; x--) {
        uint8_t d = gMoneyDigit(x) == TILE_BLANK ? 0 : gMoneyDigit(x);
        d = Alu_Sbc(d, gScratch(0), &p);
        if (!(p & P_C)) {
            d = Alu_Adc(d, 10, &p);
            p &= (uint8_t)~P_C;
        }
        gScratch(1 + x) = d;
        gScratch(0) = r->a = 0;
    }
    r->x = 0xFF;
    if (!(p & P_C)) {
        return;
    }
    for (int x = 1; x >= 0; x--) {
        gMoneyDigit(x) = r->a = gScratch(1 + x);
    }
    for (r->x = 0; (r->a = gMoneyDigit(r->x)) == 0;) {
        gMoneyDigit(r->x) = r->a = TILE_BLANK;
        if (++r->x == DT_MONEY_DIGIT_COUNT) {
            break;
        }
    }
    gHudMoneyDirty++;
    r->x = gMessageSlot;
    gObjects->kind[r->x] = OBJ_KIND_LIFT;
    gObjects->state[r->x] &= LIFT_STATE;
    gObjects->y[r->x] = LIFT_Y;
    gObjects->worldX[r->x] = LIFT_X;
    gObjects->hitbox[r->x] = r->a = (uint8_t)(HITBOX_SOLID | gObjects->hitbox[r->x]);
}

/* $EDCF: closing message $06 puts a block in the speaker's slot, clears two
 * collision cells and draws the block. */
static void Message_PlaceBlock(Regs *r)
{
    r->x = gMessageSlot;
    gObjects->state[r->x] = BLOCK_STATE;
    gObjects->kind[r->x] = OBJ_KIND_BLOCK;
    gObjects->action[r->x] = BLOCK_ACTION;
    gObjects->y[r->x] = BLOCK_Y;
    gObjects->worldX[r->x] = BLOCK_X;
    gObjects->animFrame[r->x] = 0;
    gObjects->animTimer[r->x] = 0;
    gObjects->unk_0690[r->x] = 0;
    gCollision(BLOCK_CELL) = 0;
    gCollision(BLOCK_CELL_2) = 0;
    gObjects->timer[r->x] = r->a = BLOCK_TIMER;
    r->y = BLOCK_TILE;
    Decomp_Handoff(DT_CODE_BLOCK_DRAW, 0, OUT_Y);
}

/* $ED60: A or B ends the page: the next one scrolls in, or the box closes
 * (the mode back to 0, the pause text cleared, the tile restored). */
static void Message_Wait(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_WAIT, r->a, r->x, r->y);
    r->a = gPadNew & (DT_PAD_A | DT_PAD_B);
    if (!r->a) {
        Task_Yield(DT_MESSAGE_WAIT_NEXT);
        return;
    }
    r->a = gMessageNext;
    if (r->a) {
        if (Task_Gosub(Message_Scroll, r, DT_MESSAGE_PAGED)) {
            Task_Yield(DT_MESSAGE_WAIT_NEXT);
        }
        return;
    }
    Decomp_Push(r->a = gPadNew);
    gPlayerMode = r->a = PLAYER_MODE_GROUND;
    Task_SelectBank(r, DT_BANK_SPRITES);
    Decomp_Handoff(DT_B5_PAUSE_TEXT, DT_MESSAGE_CLEARED, OUT_X | OUT_Y);
}

static void Message_Cleared(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_CLEARED, r->a, r->x, r->y);
    Message_QueueTile(r, BANK_TILE_OFF, DT_B1_MESSAGE_TILE_OFF);
    Task_Yield(DT_MESSAGE_CLOSED);
}

static void Message_WaitNext(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_WAIT_NEXT, r->a, r->x, r->y);
    Message_Wait(r);
}

static void Message_Paged(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_PAGED, r->a, r->x, r->y);
    Task_Yield(DT_MESSAGE_WAIT_NEXT);
}

/* $EDAA: the box is gone; some messages do something as it closes. */
static void Message_Closed(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_CLOSED, r->a, r->x, r->y);
    gPadNew = r->a = Decomp_Pull();
    r->a = gMessageId;
    switch (r->a) {
        case MESSAGE_PAY:
            Message_Pay(r);
            return;
        case MESSAGE_LIFE:
            Player_PitDeath();
            return;
        case MESSAGE_BLOCK:
            Message_PlaceBlock(r);
            return;
        case MESSAGE_LAUNCHPAD:
            r->a = gPadNew & DT_PAD_B;
            if (!r->a) {
                r->y = gLand;
                gLaunchpadRides = r->a = gLaunchpadRides | Game_ReadRom((uint16_t)(DT_CODE_LAND_BIT + r->y));
                gScene = r->a = SCENE_LAUNCHPAD;
            }
            return;
        case MESSAGE_KEY_CHECK:
            r->a = gKeyItems & 2;
            if (!r->a) {
                gLandRedirect++;
            }
            return;
        case MESSAGE_SCENE:
            gBgOffBelowSplit = r->a = 0;
            return;
        default:
            return;
    }
}

/* $ED33: the sprites are rebuilt and the box's frame drawn (the pause text
 * of mode 15, which waits for frames). */
static void Message_Shown(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_SHOWN, r->a, r->x, r->y);
    r->a = 0;
    gMessageNext = gMessageAt = gMessageLine = 0;
    Task_SelectBank(r, DT_BANK_SPRITES);
    Oam_Clear();
    r->y = Oam_Build();
    r->x = RAM_U8(DT_ADDR_SPR_SLOT);
    Decomp_Handoff(DT_B5_PAUSE_TEXT, DT_MESSAGE_DRAWN, OUT_X | OUT_Y);
}

/* bank 5 $B76A (bank 5 mapped): the land's records whose progress flag is
 * set get their consumed bit. Per land a list (its first index at
 * DT_B5_PROGRESS_FIRST): the count - 1, then per record the flag's address in
 * page 0, its mask and the record's bit number in $0120. $B760 clears the bits
 * first and runs into it. */
void Records_MarkTaken(Regs *r)
{
    Decomp_Enter(DT_B5_PROGRESS_BITS, r->a, r->x, r->y);
    r->x = Game_ReadRom((uint16_t)(DT_B5_PROGRESS_FIRST + gLand));
    gScratch(0) = Game_ReadRom((uint16_t)(DT_B5_PROGRESS_FLAG + r->x));
    r->x++;
    do {
        r->y = Game_ReadRom((uint16_t)(DT_B5_PROGRESS_FLAG + r->x));
        r->a = RAM_U8(r->y) & Game_ReadRom((uint16_t)(DT_B5_PROGRESS_MASK + r->x));
        if (r->a) {
            uint8_t bit = Game_ReadRom((uint16_t)(DT_B5_PROGRESS_RECORD + r->x));
            gScratch(1) = (uint8_t)(1 << (bit & 7));
            r->y = bit >> 3;
            r->a = RAM_U8(DT_ADDR_CONSUMED + r->y) |= gScratch(1);
        }
        r->x++;
        gScratch(0)--;
    } while (!(gScratch(0) & 0x80));
}

/* $ED49: the text of the box's frame is queued; the text begins. */
static void Message_Drawn(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_DRAWN, r->a, r->x, r->y);
    Records_MarkTaken(r);
    Task_SelectBank(r, BANK_MESSAGES);
    r->x = gMessageId;
    RAM_U8(gMessageText) = Game_ReadRom((uint16_t)(DT_B4_MESSAGE_LO + r->x));
    RAM_U8(gMessageText + 1) = r->a = Game_ReadRom((uint16_t)(DT_B4_MESSAGE_HI + r->x));
    if (Task_Gosub(Message_Text, r, DT_MESSAGE_WAIT)) {
        Message_Wait(r);
    }
}

static void Message_Opened(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_OPENED, r->a, r->x, r->y);
    Message_QueueTile(r, BANK_TILE, DT_B2_MESSAGE_TILE);
    Task_Yield(DT_MESSAGE_SHOWN);
}

/* Mode 15, $ED05: a message box. The frame loop waits while it shows:
 * the box's tile is swapped in, the text is written page by page, each
 * page waits for A or B, and then the tile goes back and the message's
 * effect runs. */
static void Player_Message(Regs *r)
{
    Decomp_Enter(DT_CODE_PLAYER_MESSAGE, r->a, r->x, r->y);
    r->a = gMessageId;
    if (r->a == MESSAGE_SCENE) {
        Decomp_Handoff(DT_CODE_MESSAGE_SCENE, DT_MESSAGE_OPENED, OUT_X | OUT_Y);
        return;
    }
    Message_Opened(r);
}

/* $EF70: every other frame a time unit turns into money (digit 3 + 1,
 * capped at 9,999,990); with no time left the land is left 60 frames on. */
static void Treasure_Tick(Regs *r)
{
    r->a = gSpritePhase & 1;
    if (!r->a) {
        return;
    }
    gObjects->animTimer[P_] = r->a = 0;
    gTimeDigit(2) = 0;
    r->a = gTimeDigit(0) | gTimeDigit(1);
    if (!r->a) {
        r->x = LEAVE_FRAMES;
        Task_Wait(DT_TREASURE_DONE);
        return;
    }
    gScratch(0) = 1;
    uint8_t p = P_C;
    for (int i = 1; i >= 0; i--) {
        uint8_t d = Alu_Sbc(gTimeDigit(i), gScratch(0), &p);
        if (!(p & P_C)) {
            d = (uint8_t)(d + 10);
        }
        gTimeDigit(i) = d;
        gScratch(0) = 0;
    }
    gScratch(0) = 1;
    p = 0;
    for (int x = 3; x >= 0; x--) {
        uint8_t d = Alu_Adc(gMoneyDigit(x), gScratch(0), &p);
        if (d == TILE_BLANK) {
            p = 0;
        } else {
            if (d > TILE_BLANK) {
                d = (uint8_t)(d - TILE_BLANK);
            }
            p = d >= 10 ? P_C : 0;
            if (p) {
                d = (uint8_t)(d - 10);
            }
        }
        gMoneyDigit(x) = d;
        gScratch(0) = 0;
    }
    r->x = 0xFF;
    if (p) {
        Money_Max();
    }
    r->a = SFX_TIME_BONUS;
    Sound_Request(SFX_TIME_BONUS, r->x);
    gHudMoneyDirty++;
}

/* Mode 16, $EF62: the treasure is held up. The music plays for 256 frames
 * first (the frame loop waits), then stops, and the time bonus runs. */
static void Player_Treasure(Regs *r)
{
    Decomp_Enter(DT_CODE_PLAYER_TREASURE, r->a, r->x, r->y);
    r->a = gMusic;
    if (!(r->a & 0x80)) {
        r->x = MUSIC_FRAMES;
        Task_Wait(DT_TREASURE_WAITED);
        return;
    }
    Treasure_Tick(r);
}

static void Treasure_Waited(Regs *r)
{
    Decomp_Enter(DT_TREASURE_WAITED, r->a, r->x, r->y);
    r->a = MUSIC_NONE;
    Music_Request(MUSIC_NONE, r->x);
    Treasure_Tick(r);
}

static void Treasure_Done(Regs *r)
{
    Decomp_Enter(DT_TREASURE_DONE, r->a, r->x, r->y);
    gLandLeft++;
}

/* ---- site adapters ---- */

static void Run_Update(Regs *r)
{
    Player_Update(r);
}

static void Run_Ground(Regs *r)
{
    (void)r;
    Player_Ground();
}

static void Run_StartJump(Regs *r)
{
    (void)r;
    Player_StartJump();
}

static void Run_Walk(Regs *r)
{
    Player_Walk(r->x);
}

static void Run_MoveRight(Regs *r)
{
    r->x = Player_MoveRight(r->x);
}

static void Run_MoveLeft(Regs *r)
{
    r->x = Player_MoveLeft(r->x);
}

static void Run_Air(Regs *r)
{
    (void)r;
    Player_Air();
}

static void Run_GrabClimb(Regs *r)
{
    (void)r;
    Player_GrabClimb();
}

static void Run_Pogo(Regs *r)
{
    (void)r;
    Player_Pogo();
}

static void Run_Climb(Regs *r)
{
    (void)r;
    Player_Climb();
}

static void Run_Cane(Regs *r)
{
    (void)r;
    Player_Cane();
}

static void Run_Fall(Regs *r)
{
    r->x = Player_Fall();
}

static void Run_Rise(Regs *r)
{
    r->x = Player_Rise();
}

static void Run_ProbeVertical(Regs *r)
{
    r->x = Player_ProbeVertical();
}

static void Run_Hurt(Regs *r)
{
    (void)r;
    Player_Hurt();
}

static void Run_Knockback(Regs *r)
{
    Player_Knockback(r->x);
}

static void Run_TakeDamage(Regs *r)
{
    (void)r;
    Player_MarkDamage();
}

static void Run_Stuck(Regs *r)
{
    (void)r;
    Player_Stuck();
}

static void Run_Pit(Regs *r)
{
    (void)r;
    Player_Pit();
}

static void Run_PitDeath(Regs *r)
{
    (void)r;
    Player_PitDeath();
}

static void Run_Mode9(Regs *r)
{
    (void)r;
    Player_Mode9();
}

static void Run_Carried(Regs *r)
{
    (void)r;
    Player_Carried();
}

static void Run_Mode12(Regs *r)
{
    (void)r;
    Player_Mode12();
}

static void Run_Death(Regs *r)
{
    (void)r;
    Player_Death();
}

static void Run_Mode14(Regs *r)
{
    (void)r;
    Player_Mode14();
}

static void Run_ContactH(Regs *r)
{
    r->x = Player_ContactH();
    r->p = (uint8_t)((r->p & ~P_C) | (r->x ? 0 : P_C));
}

static void Run_ContactV(Regs *r)
{
    r->p = (uint8_t)((r->p & ~P_C) | (Player_ContactV() ? 0 : P_C));
}

static void Run_SelectRefill(Regs *r)
{
    r->x = Player_SelectRefill();
}

/* Sites only where original code calls a routine: the frame loop calls
 * Player_Update, the original modes 15 and 16 jump to Player_PitDeath, and
 * the original $E878 falls into Player_RowChange. The
 * damage routine keeps its site for replace mode's measuring call (decomp.c);
 * the damage event runs at its own [[mod_function_hook]] there (declared
 * first, so it fires first), and the adapter is the routine without it. */
const DecompRoutine gPlayerRoutines[] = {
    { DT_CODE_PLAYER_UPDATE, "ducktales.decomp.E354", "Player_Update", Run_Update, 0, true, 0, false },
    { DT_CODE_PLAYER_GROUND, NULL, "Player_Ground", Run_Ground, 0, true, 0, false },
    { DT_CODE_JUMP_START, NULL, "Player_StartJump", Run_StartJump, 0, true, 0, false },
    { DT_CODE_PLAYER_WALK, NULL, "Player_Walk", Run_Walk, 0 },
    { DT_CODE_MOVE_RIGHT, NULL, "Player_MoveRight", Run_MoveRight, OUT_X },
    { DT_CODE_MOVE_LEFT, NULL, "Player_MoveLeft", Run_MoveLeft, OUT_X },
    { DT_CODE_PLAYER_AIR, NULL, "Player_Air", Run_Air, 0, true, 0, false },
    { DT_CODE_GRAB_CLIMB, NULL, "Player_GrabClimb", Run_GrabClimb, 0 },
    { DT_CODE_PLAYER_POGO, NULL, "Player_Pogo", Run_Pogo, 0, true, 0, false },
    { DT_CODE_PLAYER_CLIMB, NULL, "Player_Climb", Run_Climb, 0, true, 0, false },
    { DT_CODE_PLAYER_CANE, NULL, "Player_Cane", Run_Cane, 0 },
    { DT_CODE_PLAYER_FALL, NULL, "Player_Fall", Run_Fall, OUT_X },
    { DT_CODE_PLAYER_RISE, NULL, "Player_Rise", Run_Rise, OUT_X },
    { DT_CODE_PROBE_VERTICAL, NULL, "Player_ProbeVertical", Run_ProbeVertical, OUT_X },
    { DT_CODE_PLAYER_HURT, NULL, "Player_Hurt", Run_Hurt, 0 },
    { DT_CODE_PLAYER_KNOCKBACK, NULL, "Player_Knockback", Run_Knockback, 0 },
    { DT_CODE_TAKE_DAMAGE, "ducktales.decomp.EB5E", "Player_TakeDamage", Run_TakeDamage, 0 },
    { DT_CODE_PLAYER_STUCK, NULL, "Player_Stuck", Run_Stuck, 0, true, 0, false },
    { DT_CODE_PLAYER_PIT, NULL, "Player_Pit", Run_Pit, 0 },
    { DT_CODE_PIT_DEATH, "ducktales.decomp.EBB4", "Player_PitDeath", Run_PitDeath, 0 },
    { DT_CODE_PLAYER_MODE9, NULL, "Player_Mode9", Run_Mode9, 0 },
    { DT_CODE_PLAYER_CARRIED, NULL, "Player_Carried", Run_Carried, 0, true, 0, false },
    { DT_CODE_PLAYER_MODE12, NULL, "Player_Mode12", Run_Mode12, 0 },
    { DT_CODE_PLAYER_DEATH, NULL, "Player_Death", Run_Death, 0 },
    { DT_CODE_PLAYER_MODE14, NULL, "Player_Mode14", Run_Mode14, 0 },
    { DT_CODE_CONTACT_H, NULL, "Player_ContactH", Run_ContactH, OUT_X | OUT_C },
    { DT_CODE_CONTACT_V, NULL, "Player_ContactV", Run_ContactV, OUT_C },
    { DT_CODE_SELECT_REFILL, NULL, "Player_SelectRefill", Run_SelectRefill, OUT_X },
    { DT_CODE_ROW_CHANGE, "ducktales.decomp.E8C0", "Player_RowChange", Player_RowChange, 0, true, 0, false },
    { DT_CODE_PLAYER_MESSAGE, "ducktales.decomp.ED05", "Player_Message", Player_Message, 0, true, 0, false },
    { DT_MESSAGE_OPENED, NULL, "Message_Opened", Message_Opened, 0, true, 0, true },
    { DT_MESSAGE_SHOWN, NULL, "Message_Shown", Message_Shown, 0, true, 0, true },
    { DT_MESSAGE_DRAWN, NULL, "Message_Drawn", Message_Drawn, 0, true, 0, true },
    { DT_MESSAGE_WAIT, NULL, "Message_Wait", Message_Wait, 0, true, 0, true },
    { DT_MESSAGE_WAIT_NEXT, NULL, "Message_WaitNext", Message_WaitNext, 0, true, 0, true },
    { DT_MESSAGE_PAGED, NULL, "Message_Paged", Message_Paged, 0, true, 0, true },
    { DT_MESSAGE_CLEARED, NULL, "Message_Cleared", Message_Cleared, 0, true, 1, true },
    { DT_MESSAGE_CLOSED, NULL, "Message_Closed", Message_Closed, 0, true, 1, true },
    { DT_CODE_MESSAGE_SCROLL, NULL, "Message_Scroll", Message_Scroll, 0, true, 0, false },
    { DT_MESSAGE_SCROLLED, NULL, "Message_Scrolled", Message_Scrolled, OUT_X | OUT_Y, true, 0, true },
    { DT_CODE_MESSAGE_TEXT, NULL, "Message_Text", Message_Text, OUT_X | OUT_Y, true, 0, false },
    { DT_MESSAGE_LETTER_NEXT, NULL, "Message_LetterNext", Message_LetterNext, OUT_X | OUT_Y, true, 0, true },
    { DT_CODE_PLAYER_TREASURE, "ducktales.decomp.EF62", "Player_Treasure", Player_Treasure, 0, true, 0, false },
    { DT_TREASURE_WAITED, NULL, "Treasure_Waited", Treasure_Waited, 0, true, 0, true },
    { DT_TREASURE_DONE, NULL, "Treasure_Done", Treasure_Done, 0, true, 0, true },
    { DT_B5_PROGRESS_BITS, NULL, "Records_MarkTaken", Records_MarkTaken, OUT_A | OUT_X | OUT_Y,
      .bank = DT_BANK_SPRITES },
};
const int gPlayerRoutineCount = sizeof gPlayerRoutines / sizeof gPlayerRoutines[0];
