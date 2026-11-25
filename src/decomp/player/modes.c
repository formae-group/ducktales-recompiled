/*
 * player/modes.c - Scrooge's modes on the ground (with the Select refill),
 * in the air, on the pogo, climbing and swinging the cane, and the row change
 * when he leaves the screen's row (docs/PLAYER.md).
 */
#include "decomp/player/player_internal.h"

/* $E2FE: Select on the ground. When the last HP dot is lost and TOTAL is
 * 3,000,000 or more, TOTAL pays for a refill: its two leading digits drop by
 * 3 (leading zeros blanked again), every dot is full ($F812) and TOTAL is
 * redrawn.
 * Returns X as the original leaves it. */
uint8_t Player_SelectRefill(void)
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

/* $E3D8 / $E64F: B in the push pose swings the cane. */
static void Player_StartCane(void)
{
    gObjects->action[P_] = PLAYER_ACTION_CANE;
    gObjects->animTimer[P_] = 0;
    gObjects->animFrame[P_] = 0;
    gPlayerMode = PLAYER_MODE_CANE;
}

/* Mode 0, $E3B8: standing, walking, crouching, pushing. */
void Player_Ground(void)
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
void Player_StartJump(void)
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
void Player_RowChange(Regs *r)
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
void Player_GrabClimb(void)
{
    Decomp_Enter(DT_CODE_GRAB_CLIMB, 0, 0, 0);
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
        if (((gObjects->worldX[P_] + Game_ReadRom((uint16_t)(DT_CODE_GRAB_COLUMN_OFFSET + i))) & 0x0F) < 8 &&
            gProbeClass(i) == 2) {
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
void Player_Air(void)
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
                Obj_SetVelocityY(P_, Tune_Value(TUNE_POGO_VELOCITY, POGO_BOUNCE_VY));
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
void Player_Pogo(void)
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
void Player_Climb(void)
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
void Player_Cane(void)
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
