/*
 * objects/scenes.c - the fixed bank's scene and final-arena objects (kinds
 * $36-$38, $3A-$3D and $40): Launchpad's ride, the sky scene, the final
 * stand-in, the climber, the talker a hit wakes and the final treasure.
 */
#include "decomp/objects/objects_internal.h"

/* ---- the scene objects, kinds $36 to $38 (fixed bank) ---- */

#define RIDE_SCENE      0x82 /* the special scene its fall requests */
#define RIDE_BRAKE_X    0xB0 /* below this world X it slows down */
#define RIDE_BRAKE      4
#define RIDE_LEAVE_X    0x10
#define RIDE_BRAKE_DIG  7 /* the money digit $0326 that makes it brake */
#define FLYER_WAIT      0x3C
#define FLYER_TOP_Y     0x50
#define FLYER_STOP_X    0x70
#define FLYER_GONE_Y    0x10
#define FLYER_SCROLL    0x80
#define FLYER_NEXT_SLOT 3
#define ROPE_ACTION     4
#define ROPE_RISE       2
#define ROPE_TOP_Y      0x10

/* Kind $36 ($F869), Launchpad's ride home (scene 1; the name is a hypothesis
 * from the scene): Scrooge rides it (his Y and world X are its own) and the
 * HUD time stands still. Step 1 moves left at speed 1; while the money digit
 * $0326 is 7 it brakes by 4/256 a frame below world X $B0 and stops, going to
 * step 2, otherwise the land is left once Scrooge is left of $10. Steps 2 and
 * 3 fall with gravity, requesting scene 2 while the fall does not wrap. */
void Obj_LaunchpadRide(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_LAUNCHPAD_RIDE, 0x22, x, gObjects->kind[x]);
    gObjects->animTimer[P_] = 0;
    gTickFrames = 0;
    if ((gObjects->state[x] & 0x0F) == 0) {
        gObjects->vyFrac[x] = 0;
        gObjects->vy[x] = 0;
        gObjects->speedFrac[x] = 0;
        gObjects->speed[x] = 1;
        gObjects->state[x]++;
    }
    if (gObjects->state[x] & 0x02) {
        Obj_ApplyGravity(x);
        bool carry = (uint16_t)(gObjects->y[x] << 8 | gObjects->yFrac[x]) >=
                     (uint16_t)(gObjects->vy[x] << 8 | gObjects->vyFrac[x]);
        Obj_ApplyVelocityY(x);
        gObjects->y[P_] = gObjects->y[x];
        if (carry) {
            RAM_U8(DT_ADDR_SCENE) = RIDE_SCENE;
        }
        return;
    }
    uint8_t p = P_C;
    (void)Alu_Sbc(gObjects->xFrac[x], gObjects->speedFrac[x], &p);
    gObjects->worldX[P_] = gObjects->worldX[x] = Alu_Sbc(gObjects->worldX[x], gObjects->speed[x], &p);
    gObjects->worldPage[P_] = gObjects->worldPage[x] = Alu_Sbc(gObjects->worldPage[x], 0, &p);
    if (RAM_U8(DT_ADDR_MONEY_DIGITS + 2) != RIDE_BRAKE_DIG) {
        if (gObjects->worldX[P_] < RIDE_LEAVE_X) {
            RAM_U8(DT_ADDR_LAND_LEFT)++;
        }
        return;
    }
    if (gObjects->worldX[x] >= RIDE_BRAKE_X) {
        return;
    }
    p = P_C;
    gObjects->speedFrac[x] = Alu_Sbc(gObjects->speedFrac[x], RIDE_BRAKE, &p);
    gObjects->speed[x] = Alu_Sbc(gObjects->speed[x], 0, &p);
    if ((gObjects->speed[x] | gObjects->speedFrac[x]) == 0) {
        gObjects->state[x]++;
    }
}

/* $F99C: kind $37 drops an object (kind $13, action $28) from where it is
 * into the highest free slot of 15..9, falling at 4, and waits 30, 60 or 120
 * frames for the next. With no free slot it tries again next frame. */
static void SkyFlyer_Drop(uint8_t x)
{
    for (uint8_t o = OBJ_SLOTS - 1; o > 8; o--) {
        if (gObjects->state[o] & OBJ_STATUS_LIVE) {
            continue;
        }
        gObjects->state[o] = 0xA0;
        gObjects->flags[o] = 0xA0;
        gObjects->action[o] = 0x28;
        gObjects->kind[o] = 0x13;
        gObjects->animTimer[o] = 0;
        gObjects->animFrame[o] = 0;
        gObjects->hitbox[o] = 0;
        gObjects->y[o] = gObjects->y[x];
        gObjects->worldX[o] = gObjects->worldX[x];
        gObjects->worldPage[o] = gObjects->worldPage[x];
        gObjects->vyFrac[o] = 0;
        gObjects->vy[o] = 4;
        gRandom(1) = (uint8_t)(gRandom(0) + gRandom(1) + 1);
        gObjects->unk_06C0[x] = Rom((uint16_t)(DT_CODE_SKY_FLYER_GAPS + (gRandom(1) & 7)));
        return;
    }
}

/* Kind $37 ($F8F2), in scene 2 (the sky scene): with Scrooge gone (status bit 7 clear) it requests the
 * pit death. It waits 60 frames, rises at 2 to Y $50 (the scripted camera
 * then scrolls left), moves left at speed 1 dropping objects (SkyFlyer_Drop)
 * until world X $70 of page 0, where it puts kind $38 into slot 3, then rises
 * off the top and starts over. */
void Obj_SkyFlyer(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_SKY_FLYER, 0x22, x, gObjects->kind[x]);
    if (!(gObjects->flags[P_] & 0x80)) {
        Player_PitDeath();
        return;
    }
    if ((gObjects->state[x] & 0x0F) == 0) {
        gObjects->speedFrac[x] = 0;
        gObjects->vyFrac[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->timer[x] = FLYER_WAIT;
        gObjects->speed[x] = 1;
        gObjects->vy[x] = 2;
        gObjects->state[x]++;
    }
    if (gObjects->timer[x] != 0) {
        gObjects->timer[x]--;
        return;
    }
    switch (gObjects->state[x] & 0x0F) {
        case 2: {
            if (gObjects->unk_06C0[x] == 0) {
                SkyFlyer_Drop(x);
            }
            gObjects->unk_06C0[x]--;
            Obj_MoveLeft(x);
            if (gObjects->worldPage[x] != 0 || gObjects->worldX[x] >= FLYER_STOP_X) {
                return;
            }
            gObjects->state[x]++;
            uint8_t o = FLYER_NEXT_SLOT;
            gObjects->state[o] = 0xA0;
            gObjects->flags[o] = 0xA0;
            gObjects->action[o] = 0x5A;
            gObjects->worldX[o] = 0x30;
            gObjects->y[o] = 0x80;
            gObjects->kind[o] = OBJ_KIND_SKY_ROPE;
            gObjects->hitbox[o] = 0;
            gObjects->animTimer[o] = 0;
            gObjects->animFrame[o] = 0;
            gObjects->worldPage[o] = 0;
            return;
        }
        case 3:
            Obj_SubVelocityY(x);
            if (gObjects->y[x] < FLYER_GONE_Y) {
                gObjects->state[x] = 0;
            }
            return;
        default:
            Obj_AddVelocityY(x);
            if (gObjects->y[x] < FLYER_TOP_Y) {
                return;
            }
            gObjects->state[x]++;
            RAM_U8(DT_ADDR_SCENE_SCROLL) = FLYER_SCROLL;
            return;
    }
}

/* Kind $38 ($F9F8): what kind $37 lets down into slot 3 (a rope, as a
 * hypothesis). Once Scrooge touches it, unless he is dying (mode 13), he holds
 * on: action 4, its world X, mode 14. From then on it rises 2 px a frame with
 * him, and the land is left once it is above Y $10. */
void Obj_SkyRope(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_SKY_ROPE, 0x22, x, gObjects->kind[x]);
    if ((gObjects->state[x] & 0x0F) == 0) {
        if (!Obj_TouchScrooge(x) || gPlayerMode == PLAYER_MODE_DEATH) {
            return;
        }
        gObjects->state[x]++;
        gObjects->action[P_] = ROPE_ACTION;
        gObjects->animFrame[P_] = 0;
        gObjects->worldX[P_] = gObjects->worldX[x];
        gObjects->worldPage[P_] = gObjects->worldPage[x];
        gPlayerMode = PLAYER_MODE_14;
    }
    gObjects->animTimer[P_] = 0;
    gObjects->y[P_] = gObjects->y[x] = (uint8_t)(gObjects->y[x] - ROPE_RISE);
    if (gObjects->y[x] < ROPE_TOP_Y) {
        RAM_U8(DT_ADDR_LAND_LEFT)++;
    }
}

/* ---- kinds $3A and $3B, the final arena's stand-in (fixed bank) ---- */

#define STAND_IN_Y         0x74
#define STAND_IN_WAIT      0x78 /* frames hidden before it appears */
#define STAND_IN_SHOW      0x3C /* frames blinking before it throws */
#define STAND_IN_STAY      0xF0 /* frames it stays after the throw */
#define STAND_IN_HITBOX    0x9B
#define STAND_IN_BEATEN    0x2A /* the action of kind $3B */
#define STAND_IN_SHOT_SLOT 14
#define STAND_IN_SHOT      0x85 /* the thrown kind $11's action */
#define STAND_IN_SHOT_Y    0x58
#define STAND_IN_SHOT_BOX  0x90
#define BEATEN_FRAME_TIMER 5
#define BEATEN_STATE       0xB1
#define BEATEN_WAIT        0xB4
#define BEATEN_SCENE_STEP  0x0B /* mode 15's arena redraw */

/* Kind $3A ($FA3F): what a boss becomes in the final arena (the spawner's
 * stand-in, five hits, no prize). Hidden at Y $74, it waits 120 frames, then
 * appears at a random one of 16 world X spots ($FB54; its random step adds
 * the carry the dispatch left), blinking and facing Scrooge, for 60 frames.
 * Then it throws a kind $11 (action $85) from slot 14, $10 px to the side it
 * faces, stays 240 frames (the last 60 blinking) and hides for 60 frames
 * before the next spot. A counted hit goes to the boss hit $F6D7; the last
 * one makes it kind $3B. */
void Obj_FinalStandIn(uint8_t x, bool carry)
{
    Decomp_Enter(DT_CODE_KIND_FINAL_STAND_IN, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        if (Boss_Hit(&x)) {
            gObjects->action[x] = STAND_IN_BEATEN;
            gObjects->animTimer[x] = 0;
            gObjects->animFrame[x] = 0;
            gObjects->hitbox[x] = 0;
            gObjects->state[x] = 0xA0;
            gObjects->kind[x] = OBJ_KIND_STAND_IN_BEATEN;
            return;
        }
        carry = false;
    }
    gObjects->animTimer[x] = 0;
    if ((gObjects->state[x] & 0x0F) == 0) {
        gObjects->y[x] = STAND_IN_Y;
        gObjects->timer[x] = STAND_IN_WAIT;
        RAM_U8(DT_ADDR_TRANSYLVANIA_BOSS_05EF) = 0x44;
        gObjects->state[x]++;
    }
    if (gObjects->timer[x] != 0) {
        if (--gObjects->timer[x] != 0) {
            return;
        }
        gRandom(0) = (uint8_t)(gRandom(0) + gRandom(1) + carry);
        gObjects->worldX[x] = Rom((uint16_t)(DT_CODE_FINAL_STAND_IN_SPOTS + (gRandom(0) & 0x0F)));
        gObjects->unk_06C0[x] = STAND_IN_SHOW;
        gObjects->flags[x] |= OBJ_FLAG_BLINK;
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
        Obj_FaceScrooge(x);
        gObjects->flags[x] = (uint8_t)((gObjects->flags[x] & ~OBJ_FLAG_FACING_RIGHT) |
                                       Rom((uint16_t)(DT_CODE_FINAL_STAND_IN_FACING + (gObjects->dir[x] & 1))));
        return;
    }
    if (gObjects->unk_06C0[x] != 0) {
        if (--gObjects->unk_06C0[x] != 0) {
            return;
        }
        gObjects->flags[x] &= 0xF0;
        gObjects->animFrame[x] = 1;
        gObjects->hitbox[x] = STAND_IN_HITBOX;
        uint8_t o = STAND_IN_SHOT_SLOT;
        gObjects->y[o] = STAND_IN_SHOT_Y;
        gObjects->animTimer[o] = 0;
        gObjects->animFrame[o] = 0;
        gObjects->unk_0690[o] = 0;
        gObjects->state[o] = 0xA0;
        gObjects->flags[o] = 0xA0;
        gObjects->action[o] = STAND_IN_SHOT;
        gObjects->kind[o] = OBJ_KIND_SWOOPER;
        gObjects->hitbox[o] = STAND_IN_SHOT_BOX;
        gObjects->worldPage[o] = gObjects->worldPage[x];
        gObjects->dir[o] = gObjects->dir[x];
        gObjects->worldX[o] = (uint8_t)(gObjects->worldX[x] +
                                        Rom((uint16_t)(DT_CODE_FINAL_STAND_IN_THROW_X + (gObjects->dir[x] & 1))));
        gObjects->unk_06D0[x] = STAND_IN_STAY;
        return;
    }
    if (gObjects->unk_06D0[x] == STAND_IN_SHOW) {
        gObjects->flags[x] |= OBJ_FLAG_BLINK;
        gObjects->animFrame[x] = 0;
    }
    if (--gObjects->unk_06D0[x] != 0) {
        return;
    }
    gObjects->hitbox[x] = 0;
    gObjects->state[x] |= OBJ_STATUS_HIDDEN;
    gObjects->timer[x] = STAND_IN_SHOW;
}

/* Kind $3B ($FBC9): the beaten stand-in. Once its action shows a frame past
 * the first with the frame timer at 5, it hides (state $B1) for 180 frames,
 * then is freed and starts mode 15's arena redraw (step $0B), the background
 * below the split going off. */
void Obj_StandInBeaten(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_STAND_IN_BEATEN, 0x22, x, gObjects->kind[x]);
    if ((gObjects->state[x] & 0x0F) == 0) {
        if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != BEATEN_FRAME_TIMER) {
            return;
        }
        gObjects->state[x] = BEATEN_STATE;
        gObjects->timer[x] = BEATEN_WAIT;
    }
    if (gObjects->timer[x] != 0) {
        gObjects->timer[x]--;
        return;
    }
    gObjects->state[x] = 0;
    gPlayerMode = PLAYER_MODE_MESSAGE;
    RAM_U8(DT_ADDR_BG_OFF_BELOW_SPLIT)++;
    RAM_U8(DT_ADDR_SCENE_STEP) = BEATEN_SCENE_STEP;
}

/* ---- kinds $3C, $3D and $40 (fixed bank) ---- */

#define CLIMBER_SLOT       15 /* the climber whose arrival ends the race */
#define CLIMBER_SCREEN     2
#define CLIMBER_TOP_Y      0x90
#define SCREEN_ROWS_LAST   0xEF /* the last Y of a 240-line screen */
#define TREASURE_LOST_X    0xB0
#define TREASURE_LOST_MSG  0x0C
#define TREASURE_WAIT      0xF0
#define SFX_TREASURE_TAKEN 0x37
#define MUSIC_STOP         0xFF
#define HIT_TALKER_HITBOX  0x80
#define HIT_TALKER_SHOWN   0x40 /* message 6's bit in $A8 */

/* Kind $3C ($FB68): a climber on the final climb (Transylvania, types $72
 * and $73). Its height is a screen ($06C0, from 2) and a Y ($06B0) that it
 * climbs 1 px a frame, wrapping from 0 to $EF of the screen above; it is
 * drawn only on the map row that screen is. Once the climber of slot 15 is
 * on screen 0 above Y $90, every climber stops and slot 15 keeps that Y in
 * its $06D0, which kind $40 reads (a Y of 0 there still stops them). */
void Obj_Climber(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CLIMBER, 0x22, x, gObjects->kind[x]);
    if ((gObjects->state[x] & 0x0F) == 0) {
        gObjects->unk_06D0[x] = 0;
        gObjects->timer[x] = gObjects->y[x];
        gObjects->unk_06C0[x] = CLIMBER_SCREEN;
        gObjects->state[x]++;
    }
    uint8_t s = CLIMBER_SLOT;
    if (gObjects->unk_06C0[s] == 0 && gObjects->timer[s] < CLIMBER_TOP_Y) {
        gObjects->unk_06D0[s] = gObjects->timer[s];
    } else if (gObjects->timer[x] == 0) {
        gObjects->timer[x] = SCREEN_ROWS_LAST;
        gObjects->unk_06C0[x]--;
    } else {
        gObjects->timer[x]--;
    }
    if (gObjects->unk_06C0[x] != gMapRow) {
        gObjects->state[x] |= OBJ_STATUS_HIDDEN;
        return;
    }
    gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
    gObjects->y[x] = gObjects->timer[x];
}

/* Kind $3D ($FBFF): a character a hit wakes (Himalayas, type $6D). The hit
 * sets $A2 and makes it solid (hitbox $80); it falls to the floor, jumps
 * (vertical velocity $054B) walking left at 1 px a frame and, landing, is
 * freed and shows its message (mode 15, the message id from its record),
 * marked shown (message 6) and consumed. */
void Obj_HitTalker(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_HIT_TALKER, 0x22, x, gObjects->kind[x]);
    if ((gObjects->state[x] & 0x0F) == 0) {
        if (gObjects->unk_0690[x] == 0) {
            gObjects->animTimer[x] = 0;
            return;
        }
        RAM_U8(DT_ADDR_HIMALAYAS_OPEN)++;
        gObjects->vyFrac[x] = 0;
        gObjects->vy[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->hitbox[x] = HIT_TALKER_HITBOX;
        gObjects->state[x]++;
    }
    if (!(gObjects->state[x] & 0x02)) {
        x = Obj_Fall(x, 0x2D, 1);
        if (gProbeHits & 1) {
            gObjects->speed[x] = 1;
            gObjects->speedFrac[x] = 0;
            Obj_SetVelocityY(x, 0x054B);
            gObjects->state[x]++;
        }
        gObjects->animTimer[x] = 0;
        return;
    }
    if (gObjects->unk_06C0[x] == 0) {
        x = Obj_Rise(x, 0x2F, 1);
        if (gObjects->vy[x] & 0x80) {
            gObjects->unk_06C0[x]++;
            Obj_SetVelocityY(x, 0);
        }
    } else {
        x = Obj_Fall(x, 0x2D, 1);
        if (gProbeHits & 1) {
            gObjects->kind[x] = OBJ_KIND_NONE;
            gPlayerMode = PLAYER_MODE_MESSAGE;
            gMessagesShown(0) |= HIT_TALKER_SHOWN;
            gSceneStep = gObjects->timer[x];
            gMessageSlot = x;
            Obj_MarkConsumed(x);
        }
    }
    x = Obj_WalkLeft(x, 0x2B, 1);
    gObjects->animTimer[x] = 0;
}

/* Kind $40 ($FD9E): the prize at the top of the final climb (Transylvania,
 * type $71). Touched by Scrooge, unless he is dying, it holds him (mode
 * 14), stops the music, plays sound $37 and, with the HUD time stopped, 240
 * frames later starts the ending ($AC). Once the climber of slot 15 has
 * arrived it slides right instead and, at world X $B0, is freed and shows
 * message $0C (mode 15), unless Scrooge is dying. */
void Obj_FinalTreasure(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_FINAL_TREASURE, 0x22, x, gObjects->kind[x]);
    if ((gObjects->state[x] & 0x0F) == 0) {
        if (gObjects->unk_06D0[CLIMBER_SLOT] != 0 && ++gObjects->worldX[x] >= TREASURE_LOST_X) {
            gObjects->state[x] = 0;
            if (gPlayerMode == PLAYER_MODE_DEATH) {
                return;
            }
            gSceneStep = TREASURE_LOST_MSG;
            gPlayerMode = PLAYER_MODE_MESSAGE;
            return;
        }
        if (!Obj_TouchScrooge(x) || gPlayerMode == PLAYER_MODE_DEATH) {
            return;
        }
        gObjects->state[x]++;
        Music_Request(MUSIC_STOP, x);
        Sound_Request(SFX_TREASURE_TAKEN, x);
        gObjects->timer[x] = TREASURE_WAIT;
        gPlayerMode = PLAYER_MODE_14;
    }
    gTickFrames = 0;
    if (--gObjects->timer[x] == 0) {
        RAM_U8(DT_ADDR_ENDING)++;
    }
}

/* ---- adapters ---- */

static void Run_LaunchpadRide(Regs *r)
{
    Obj_LaunchpadRide(r->x);
}

static void Run_SkyFlyer(Regs *r)
{
    Obj_SkyFlyer(r->x);
}

static void Run_SkyRope(Regs *r)
{
    Obj_SkyRope(r->x);
}

static void Run_FinalStandIn(Regs *r)
{
    Obj_FinalStandIn(r->x, r->p & P_C);
}

static void Run_StandInBeaten(Regs *r)
{
    Obj_StandInBeaten(r->x);
}

static void Run_Climber(Regs *r)
{
    Obj_Climber(r->x);
}

static void Run_HitTalker(Regs *r)
{
    Obj_HitTalker(r->x);
}

static void Run_FinalTreasure(Regs *r)
{
    Obj_FinalTreasure(r->x);
}

const DecompRoutine gObjSceneRoutines[] = {
    { DT_CODE_KIND_LAUNCHPAD_RIDE, NULL, "Obj_LaunchpadRide", Run_LaunchpadRide, 0 },
    { DT_CODE_KIND_SKY_FLYER, NULL, "Obj_SkyFlyer", Run_SkyFlyer, 0 },
    { DT_CODE_KIND_SKY_ROPE, NULL, "Obj_SkyRope", Run_SkyRope, 0 },
    { DT_CODE_KIND_FINAL_STAND_IN, NULL, "Obj_FinalStandIn", Run_FinalStandIn, 0 },
    { DT_CODE_KIND_STAND_IN_BEATEN, NULL, "Obj_StandInBeaten", Run_StandInBeaten, 0 },
    { DT_CODE_KIND_CLIMBER, NULL, "Obj_Climber", Run_Climber, 0 },
    { DT_CODE_KIND_HIT_TALKER, NULL, "Obj_HitTalker", Run_HitTalker, 0 },
    { DT_CODE_KIND_FINAL_TREASURE, NULL, "Obj_FinalTreasure", Run_FinalTreasure, 0 },
};
const int gObjSceneRoutineCount = sizeof gObjSceneRoutines / sizeof gObjSceneRoutines[0];
