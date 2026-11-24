/*
 * player/move.c - Scrooge's moves: falling and rising with the vertical
 * probes, the air pose, and the horizontal steps with their wall probes,
 * plain or with momentum (docs/PLAYER.md).
 */
#include "decomp/player/player_internal.h"

/* $EA5E: after a vertical move, test the feet (moving down or standing) or
 * the head (moving up) against the background and snap Y to the tile. Keeps
 * the facing flags and ORs the probe results into the ones of the move. */
uint8_t Player_ProbeVertical(void)
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
uint8_t Player_Fall(void)
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
uint8_t Player_Rise(void)
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

uint8_t Player_MoveRight(uint8_t x)
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

uint8_t Player_MoveLeft(uint8_t x)
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
void Player_WalkNormal(uint8_t x)
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
void Player_Walk(uint8_t x)
{
    Decomp_Enter(DT_CODE_PLAYER_WALK, 0, x, 0);
    uint8_t action = gObjects->action[P_];
    if (action != PLAYER_ACTION_JUMP && action != PLAYER_ACTION_POGO && (gPlayerSlippery || gPlayerMomentumDir)) {
        Player_WalkMomentum();
        return;
    }
    Player_WalkNormal(x);
}
