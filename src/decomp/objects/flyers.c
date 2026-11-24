/*
 * objects/flyers.c - enemies that fly, drop, hang from the ceiling or jump:
 * flyers, the dangler and swooper, the bobber, rising and dropping shots, the
 * ceiling hopper, the pouncer, the pit jumper and the hop walker.
 */
#include "decomp/objects/internal.h"

/* Kind $0D ($916C): a flyer that heads towards where Scrooge was and
 * weaves through 14 motion entries ($9215), 5 frames each, without
 * touching the background. Hit or off screen: beaten. */
void Obj_Flyer(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_FLYER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0)) {
        return;
    }
    if (Step(x) == 0) {
        gObjects->state[x]++;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        Obj_FaceScrooge(x);
        if (!(gObjects->dir[x] & OBJ_DIR_LEFT)) {
            gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
        }
    }
    if (gObjects->timer[x] == 0) {
        Obj_SetMotionIndex(x, Rom((uint16_t)(DT_CODE_FLYER_PATH + gObjects->unk_06C0[x])));
        if (gObjects->speed[x] & 0x80) {
            Obj_SetSpeed(x, (uint16_t)-Obj_GetSpeed(x));
        }
        if (++gObjects->unk_06C0[x] == 0x0E) {
            gObjects->unk_06C0[x] = 0;
        }
        gObjects->timer[x] = 5;
    }
    gObjects->timer[x]--;
    Obj_AddVelocityY(x);
    if (!(gObjects->dir[x] & OBJ_DIR_LEFT) ? Obj_MoveRightOff(x) : Obj_MoveLeftOff(x)) {
        Obj_Despawn(x);
        return;
    }
    if (gObjects->animTimer[x] == 0) {
        Sound_Request(SFX_FLYER, x);
    }
    (void)Obj_TouchScrooge(x);
}

/* Kind 6 ($84E5): an enemy that climbs up at 2 px a frame to the ceiling,
 * waits there while Scrooge is within $31 px, then drops at 3 px a frame
 * to Y $B0 or to Scrooge's height and climbs back ($96 frames later). Hit:
 * pose $11, beaten. */
void Obj_Dangler(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_DANGLER, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    if (gObjects->unk_0690[x] != 0) {
        gObjects->action[x] = 0x11;
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        gObjects->timer[x] = 0;
        gObjects->state[x]++;
    }
    if (!(gObjects->state[x] & 2)) {
        Obj_SetVelocityY(x, 0x0200);
        x = Obj_ClimbUp(x, 0x27, 0);
        if (!(gProbeHits & 1)) {
            return;
        }
        if (Obj_DistanceX(x) >= 0x32) {
            gObjects->timer[x] = 0;
        } else if (gObjects->timer[x] == 0) {
            gObjects->state[x]++;
        } else {
            gObjects->timer[x]--;
        }
        return;
    }
    Obj_SetVelocityY(x, 0x0300);
    Obj_AddVelocityY(x);
    if (gObjects->y[x] < 0xB0 && Obj_DistanceY(x) >= 0x18) {
        return;
    }
    gObjects->state[x]--;
    gObjects->timer[x] = 0x96;
}

/* Kind $11 ($9430): a flyer that starts within $5F px of Scrooge (pose $85
 * at once), drops at 3 px a frame to 16 px below his height, then weaves
 * through 14 motion entries ($9544; every other pass the second half slows
 * to 8 frames a step), rising again whenever it is $20 px above him. In
 * pose $85 a hit beats it. */
void Obj_Swooper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_SWOOPER, 0x22, x, gObjects->kind[x]);
    if (gObjects->action[x] == 0x85 && gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        gObjects->flags[x] = 0;
        gHeldSlot = 0x3C;
        return;
    }
    if (Step(x) == 0) {
        if (gObjects->action[x] != 0x85) {
            if (Obj_DistanceX(x) >= 0x60) {
                return;
            }
            Sound_Request(SFX_SWOOP, x);
        }
        gObjects->state[x]++;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->unk_06D0[x] = 0;
        Obj_SetVelocityY(x, 0x0300);
        Obj_FaceScrooge(x);
        if (!(gObjects->dir[x] & OBJ_DIR_LEFT)) {
            gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
        }
    }
    (void)Obj_TouchScrooge(x);
    if (!(gObjects->state[x] & 2)) {
        if (gObjects->y[x] >= gObjects->y[P_] && (uint8_t)(gObjects->y[x] - gObjects->y[P_]) >= 0x10) {
            gObjects->state[x]++;
        }
        Obj_AddVelocityY(x);
        return;
    }
    if (gObjects->timer[x] == 0) {
        Obj_SetMotionIndex(x, Rom((uint16_t)(DT_CODE_SWOOPER_PATH + gObjects->unk_06C0[x])));
        if (gObjects->speed[x] & 0x80) {
            Obj_SetSpeed(x, (uint16_t)-Obj_GetSpeed(x));
        }
        gObjects->timer[x] = (gObjects->unk_06C0[x] >= 7 && (gObjects->unk_06D0[x] & 1)) ? 8 : 3;
        if (++gObjects->unk_06C0[x] == 0x0E) {
            gObjects->unk_06C0[x] = 0;
            gObjects->unk_06D0[x]++;
        }
    }
    bool tick = true;
    if (!(gObjects->state[x] & 1)) {
        if (gObjects->y[P_] >= gObjects->y[x] && (uint8_t)(gObjects->y[P_] - gObjects->y[x]) >= 0x20) {
            gObjects->state[x]++;
        } else {
            tick = false;
        }
    }
    if (tick) {
        gObjects->timer[x]--;
    }
    Obj_AddVelocityY(x);
    if (gObjects->dir[x] & OBJ_DIR_LEFT) {
        Obj_MoveLeft(x);
    } else {
        Obj_MoveRight(x);
    }
}

/* Kind $20 ($9E51): an enemy that bobs up and down at 1 px a frame, first
 * for 22 frames, then turning every 44; it faces Scrooge as it appears. A
 * hit beats it. */
void Obj_Bobber(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_BOBBER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0x0100);
        gObjects->timer[x] = 0x16;
        gObjects->state[x]++;
        Obj_FaceScrooge(x);
        if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
            gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
        } else {
            gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
        }
    }
    (void)Obj_TouchScrooge(x);
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        Obj_SubVelocityY(x);
    } else {
        Obj_AddVelocityY(x);
    }
    if (--gObjects->timer[x] == 0) {
        gObjects->dir[x] ^= 3;
        gObjects->timer[x] = 0x2C;
    }
}

/* Kind $27 ($A5BE): a shot that flies up and sideways by its velocity and
 * speed, harming Scrooge on contact; it is freed when it leaves the top of
 * the screen. */
void Obj_RisingShot(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_RISING_SHOT, 0x22, x, gObjects->kind[x]);
    (void)Obj_TouchScrooge(x);
    bool off = (uint16_t)(gObjects->y[x] << 8 | gObjects->yFrac[x]) < Obj_GetVelocityY(x);
    Obj_SubVelocityY(x);
    if (off) {
        gObjects->state[x] = 0;
        return;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        Obj_MoveRight(x);
    } else {
        Obj_MoveLeft(x);
    }
}

/* Kind $2B ($AAC8): something that drops by its velocity (probe $2D),
 * harming Scrooge on contact, and is freed when it hits the floor. */
void Obj_Dropping(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_DROPPING, 0x22, x, gObjects->kind[x]);
    x = Obj_ClimbDown(x, 0x2D, 0);
    if (gProbeHits & 1) {
        gObjects->state[x] = 0;
        gObjects->flags[x] = 0;
    }
    (void)Obj_TouchScrooge(x);
}

/* $A1D4: the ceiling hopper's walk, in its direction with probes $18 or $1B. */
static uint8_t Obj_CeilingWalk(uint8_t x)
{
    Decomp_Enter(DT_CODE_CEILING_HOPPER_WALK, 0, x, 0);
    if (gObjects->dir[x] & OBJ_DIR_LEFT) {
        return Obj_WalkLeft(x, 0x1B, 2);
    }
    return Obj_WalkRight(x, 0x18, 2);
}

static void Obj_CeilingJump(uint8_t x)
{
    gObjects->vdir[x] = 8;
    Obj_SetVelocityY(x, 0x02D4);
    gObjects->speedFrac[x] = 0;
}

static void Obj_CeilingClimb(uint8_t x)
{
    gObjects->state[x]++;
    gObjects->vdir[x] = 0;
    ResetAnim(x);
    gObjects->action[x] = 0x4C;
    Obj_SetVelocityY(x, 0x01CC);
    Obj_SetSpeed(x, 0x01CC);
}

/* Kind $25 ($A065): an enemy that waits until Scrooge is within 64 px,
 * climbs down to the floor and hops towards him (action $4D, sound $24),
 * walking while airborne. When he gets behind it after a landing, or a hop
 * runs into a wall, it climbs back up to the ceiling (action $4C) and walks
 * there. Off screen or hit, it is beaten. */
void Obj_CeilingHopper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CEILING_HOPPER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0)) {
        return;
    }
    if (Step(x) == 0) {
        gObjects->vdir[x] = 0;
        Obj_SetSpeed(x, 0x01CC);
        Obj_FaceScrooge(x);
        gObjects->state[x]++;
    }
    if (Step(x) == 3) {
        if (gObjects->vdir[x] == 0) {
            x = Obj_ClimbUp(x, 0x21, 2);
            if ((gProbeHits & 1) || gObjects->y[x] < 0x88) {
                gObjects->vdir[x]++;
            }
            return;
        }
        x = Obj_CeilingWalk(x);
        if (gProbeHits & 1) {
            (void)Obj_ClimbUp(x, 0x21, 2);
        }
        return;
    }
    if (!(Step(x) & 2)) {
        if (Obj_DistanceX(x) >= 0x40) {
            (void)Obj_CeilingWalk(x);
            return;
        }
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0x01CC);
    }
    if (gObjects->vdir[x] == 0) {
        x = Obj_ClimbDown(x, 0x1E, 2);
        if (!(gProbeHits & 1)) {
            return;
        }
        gObjects->animTimer[x] = 0;
        gObjects->animFrame[x] = 0;
        gObjects->action[x] = 0x4D;
        Sound_Request(SFX_HOP, x);
        Obj_CeilingJump(x);
        return;
    }
    if (gObjects->vdir[x] & 8) {
        if (gObjects->action[x] == 0x4D) {
            if (gObjects->animFrame[x] != 2) {
                return;
            }
            gObjects->animTimer[x] = 0x80;
        }
        x = Obj_CeilingWalk(x);
        uint8_t hits = gProbeHits;
        x = Obj_Rise(x, 0x21, 2);
        if (!(gObjects->vy[x] & 0x80)) {
            return;
        }
        if (hits & 1) {
            Obj_CeilingClimb(x);
            return;
        }
        gObjects->vdir[x] = 4;
        Obj_SetVelocityY(x, 0);
        return;
    }
    x = Obj_Fall(x, 0x1E, 2);
    if (!(gProbeHits & 1)) {
        (void)Obj_CeilingWalk(x);
        return;
    }
    if (gObjects->action[x] != 0x4E) {
        Sound_Request(SFX_HOP, x);
        gObjects->action[x] = 0x4E;
        gObjects->animFrame[x] = 0;
        gObjects->animTimer[x] = 1;
    }
    if ((gObjects->animFrame[x] | gObjects->animTimer[x]) != 0) {
        return;
    }
    uint8_t dir = gObjects->dir[x];
    Obj_FaceScrooge(x);
    bool turned = gObjects->dir[x] != dir;
    gObjects->dir[x] = dir;
    if (turned) {
        Obj_CeilingClimb(x);
        return;
    }
    gObjects->action[x] = 0x4D;
    gObjects->animFrame[x] = 2;
    gObjects->animTimer[x] = 0;
    Obj_CeilingJump(x);
}

/* Kind $19 ($973C): an enemy that waits until Scrooge is within 12 px,
 * drops (pose $3C, then $3B) until it lands or is 40 px above him, faces
 * him and flies in arcs by a cycle of 14 motions (sound $32), its step
 * moving on once it is 80 px or more above him. Within 12 px of him it
 * pounces (vertical velocity $0280, sound $1F) and, landed, sits 120 frames
 * in pose $39 (sound $20) before flying again. Off screen or hit, it is
 * beaten. Each step's address in the jump table is left in $00/$01. */
void Obj_Pouncer(uint8_t x)
{
    static const uint8_t motions[14] = { 0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19,
                                         0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11 };
    static const uint16_t steps[5] = { 0x9768, 0x9787, 0x97F3, 0x97F3, 0x98C8 };

    Decomp_Enter(DT_CODE_KIND_POUNCER, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x) || gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (gObjects->action[x] != 0x39) {
        (void)Obj_TouchScrooge(x);
    }
    uint8_t step = Step(x);
    gScratch(0) = (uint8_t)steps[step];
    gScratch(1) = (uint8_t)(steps[step] >> 8);
    switch (step) {
        case 0:
            if (Obj_DistanceX(x) >= 0x0C) {
                return;
            }
            gObjects->state[x]++;
            gObjects->timer[x] = 0x1E;
            gObjects->unk_06C0[x] = 0;
            Obj_SetVelocityY(x, 0x0280);
            return;
        case 1:
            if (gObjects->timer[x] != 0) {
                if (--gObjects->timer[x] == 0) {
                    gObjects->action[x] = 0x3C;
                    ResetAnim(x);
                }
                return;
            }
            if (gObjects->action[x] == 0x3C && gObjects->animTimer[x] == 8) {
                gObjects->action[x]--;
                ResetAnim(x);
            }
            x = Obj_ClimbDown(x, 0x40, 1);
            if (!(gProbeHits & 1) && (uint8_t)(gObjects->y[P_] - gObjects->y[x]) >= 0x28) {
                return;
            }
            gObjects->state[x]++;
            Obj_FaceScrooge(x);
            if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
                gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
            }
            gObjects->action[x] = 0x3B;
            ResetAnim(x);
            return;
        case 2:
        case 3:
            if (gObjects->timer[x] == 0) {
                gObjects->vdir[x] = 0;
                Obj_SetMotionIndex(x, motions[gObjects->unk_06C0[x]]);
                if (gObjects->speed[x] & 0x80) {
                    Obj_SetSpeed(x, (uint16_t)-Obj_GetSpeed(x));
                }
                if (gObjects->vy[x] & 0x80) {
                    gObjects->vdir[x] = 8;
                    Obj_SetVelocityY(x, (uint16_t)-Obj_GetVelocityY(x));
                }
                if (++gObjects->unk_06C0[x] == 0x0E) {
                    gObjects->unk_06C0[x] = 0;
                }
                gObjects->timer[x] = 3;
            }
            if (gObjects->vdir[x] == 0) {
                x = Obj_ClimbDown(x, 0x40, 1);
            } else {
                x = Obj_ClimbUp(x, 0x42, 1);
            }
            if (gObjects->dir[x] & OBJ_DIR_LEFT) {
                x = Obj_WalkLeft(x, 0x3D, 2);
            } else {
                x = Obj_WalkRight(x, 0x3A, 2);
            }
            if (gObjects->animTimer[x] == 0) {
                Sound_Request(SFX_POUNCER_FLY, x);
            }
            if (gObjects->state[x] & 1) {
                gObjects->timer[x]--;
            } else if (gObjects->y[P_] >= gObjects->y[x] && ((gObjects->y[P_] - gObjects->y[x]) & 0xF0) >= 0x50) {
                gObjects->state[x]++;
                gObjects->timer[x]--;
            }
            if (Obj_DistanceX(x) >= 0x0C) {
                return;
            }
            gObjects->action[x] = 0x3C;
            ResetAnim(x);
            gObjects->state[x]++;
            Obj_SetVelocityY(x, 0x0280);
            Sound_Request(SFX_POUNCE, x);
            return;
        default:
            break;
    }
    if (gObjects->action[x] == 0x39) {
        if (--gObjects->timer[x] != 0) {
            return;
        }
        gObjects->action[x] = 0x3B;
        ResetAnim(x);
        gObjects->unk_06C0[x] = 0;
        gObjects->state[x] = (uint8_t)((gObjects->state[x] & 0xF0) | 2);
        gObjects->hitbox[x] &= 0x7F;
        return;
    }
    if (gObjects->action[x] == 0x3C && gObjects->animTimer[x] == 8) {
        gObjects->action[x] = 0x5C;
        ResetAnim(x);
    }
    x = Obj_ClimbDown(x, 0x40, 1);
    if (!(gProbeHits & 1)) {
        return;
    }
    gObjects->action[x] = 0x39;
    ResetAnim(x);
    gObjects->timer[x] = 0x78;
    gObjects->hitbox[x] |= 0x80;
    Sound_Request(SFX_POUNCER_LAND, x);
}

/* The splash of kind $1A: palette entries $1E/$1F of the sprites, sound
 * $21, a palette upload, and a splash (pose $37, kind 0, freed when its
 * animation ends) at Y $BC in the highest free slot, if any. */
static void Obj_PitSplash(uint8_t x, uint8_t c2, uint8_t c3)
{
    gPaletteShown(0x1E) = c2;
    gPaletteShown(0x1F) = c3;
    Sound_Request(SFX_SPLASH, x);
    gPaletteUpload = 0xFF;
    uint8_t y = 0x0F;
    while (gObjects->state[y] & OBJ_STATUS_LIVE) {
        if (--y == 0) {
            return;
        }
    }
    gObjects->state[y] = 0xA8;
    gObjects->flags[y] = 0xA8;
    gObjects->y[y] = 0xBC;
    gObjects->worldX[y] = gObjects->worldX[x];
    gObjects->worldPage[y] = gObjects->worldPage[x];
    gObjects->action[y] = 0x37;
    ResetAnim(y);
    gObjects->hitbox[y] = 0;
    gObjects->kind[y] = OBJ_KIND_NONE;
}

/* $9A1E: the splash as the jumper leaves the pit. */
static void Obj_PitSplashOut(uint8_t x)
{
    Decomp_Enter(DT_CODE_PIT_SPLASH_OUT, 0, x, 0);
    Obj_PitSplash(x, 0x19, 0x27);
}

/* $9A2A: the splash as it falls back in. */
static void Obj_PitSplashIn(uint8_t x)
{
    Decomp_Enter(DT_CODE_PIT_SPLASH_IN, 0, x, 0);
    Obj_PitSplash(x, 0x21, 0x17);
}

/* Kind $1A ($9956): an enemy that, once Scrooge is within 32 px, jumps out
 * of a pit from the bottom of the screen (Y $EF) at the velocity its
 * variant ($06B0) gives, falls back, and jumps again 120 frames after it
 * passes Y $F0, with a splash at the frame counts ($06D0) its variant
 * gives. It harms him (hitbox $91) only on screen above Y $D0. A hit beats
 * it. */
void Obj_PitJumper(uint8_t x)
{
    static const uint16_t jumps[3] = { 0x0652, 0x07A5, 0x07A5 };
    static const uint8_t splashOut[3] = { 0x08, 0x07, 0x00 };
    static const uint8_t splashIn[3] = { 0x2A, 0x37, 0x00 };

    Decomp_Enter(DT_CODE_KIND_PIT_JUMPER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    gObjects->hitbox[x] = 0;
    if (gObjects->y[x] != 0 && gObjects->y[x] < 0xD0) {
        gObjects->hitbox[x] = 0x91;
    }
    if (Step(x) == 0) {
        if (Obj_DistanceX(x) >= 0x20) {
            return;
        }
        gObjects->vdir[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->unk_06D0[x] = 0;
        gObjects->y[x] = 0xEF;
        Obj_SetVelocityY(x, jumps[gObjects->timer[x]]);
        gObjects->animTimer[x] = 0x80;
        gObjects->animFrame[x] = 0;
        gObjects->state[x]++;
    }
    gObjects->unk_06D0[x]++;
    uint8_t variant = gObjects->timer[x];
    if (gObjects->vdir[x] == 0) {
        Obj_ApplyVelocityY(x);
        Obj_ApplyGravity(x);
        if (gObjects->vy[x] & 0x80) {
            gObjects->vdir[x]++;
            Obj_SetVelocityY(x, 0);
            gObjects->animFrame[x] = 1;
        }
        if (splashOut[variant] != 0 && splashOut[variant] == gObjects->unk_06D0[x]) {
            Obj_PitSplashOut(x);
        }
        return;
    }
    if (gObjects->unk_06C0[x] == 0) {
        Obj_ApplyGravity(x);
        Obj_ApplyVelocityY(x);
        if (gObjects->y[x] >= 0xF0) {
            gObjects->yFrac[x] = 0;
            gObjects->y[x] = 0;
            gObjects->unk_06C0[x] = 0x78;
        }
    } else if (--gObjects->unk_06C0[x] == 0) {
        gObjects->state[x] &= 0xF0;
        return;
    }
    if (splashIn[variant] != 0 && splashIn[variant] == gObjects->unk_06D0[x]) {
        Obj_PitSplashIn(x);
    }
}

/* $9FC5: kind $22 walks the way of $07E0 bit 0 (set: probes $39 with the
 * left mover, clear: probes $36 with the right one) and turns at a wall. */
static uint8_t Obj_HopWalkerWalk(uint8_t x)
{
    Decomp_Enter(DT_CODE_HOP_WALKER_WALK, 0, x, 0);
    if (gObjects->dir[x] & 1) {
        x = Obj_WalkLeft(x, 0x39, 0);
    } else {
        x = Obj_WalkRight(x, 0x36, 0);
    }
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
    }
    return x;
}

/* Kind $22 ($9F3D): with Scrooge within $32 px it starts at $0180 with $07E0
 * = 1 and a vertical velocity of $02D4; until then its animation stays at
 * timer 0, facing left. Its first step walks; unless that turned it, it
 * jumps (probe $53) and, past the top, falls (probe $31, sound $27 on each
 * new animation frame) and walks on once on the floor. A turn on the first
 * step holds the animation as before. It always tests contact with
 * Scrooge. */
void Obj_HopWalker(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_HOP_WALKER, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        if (Obj_DistanceX(x) >= 0x32) {
            gObjects->animTimer[x] = 0;
            gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
            return;
        }
        Obj_SetVelocityY(x, 0x02D4);
        Obj_SetSpeed(x, 0x0180);
        gObjects->dir[x] = 1;
        gObjects->state[x]++;
    }
    (void)Obj_TouchScrooge(x);
    switch (Step(x)) {
        case 2:
            x = Obj_Rise(x, 0x53, 0);
            if (!(gObjects->vy[x] & 0x80)) {
                return;
            }
            Obj_SetVelocityY(x, 0);
            gObjects->state[x]++;
            return;
        case 3:
            if (gObjects->animTimer[x] == 0) {
                Sound_Request(SFX_ROLL, x);
            }
            x = Obj_Fall(x, 0x31, 2);
            if (gProbeHits & 1) {
                (void)Obj_HopWalkerWalk(x);
            }
            return;
        default: {
            uint8_t dir = gObjects->dir[x];
            x = Obj_HopWalkerWalk(x);
            if (dir == gObjects->dir[x]) {
                gObjects->state[x]++;
                return;
            }
            gObjects->animTimer[x] = 0;
            gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
            return;
        }
    }
}

/* ---- adapters ---- */

static void Run_Dangler(Regs *r)
{
    Obj_Dangler(r->x);
}

static void Run_Flyer(Regs *r)
{
    Obj_Flyer(r->x);
}

static void Run_Bobber(Regs *r)
{
    Obj_Bobber(r->x);
}

static void Run_RisingShot(Regs *r)
{
    Obj_RisingShot(r->x);
}

static void Run_Dropping(Regs *r)
{
    Obj_Dropping(r->x);
}

static void Run_CeilingHopper(Regs *r)
{
    Obj_CeilingHopper(r->x);
}

static void Run_CeilingWalk(Regs *r)
{
    Obj_CeilingWalk(r->x);
}

static void Run_Pouncer(Regs *r)
{
    Obj_Pouncer(r->x);
}

static void Run_PitJumper(Regs *r)
{
    Obj_PitJumper(r->x);
}

static void Run_PitSplashOut(Regs *r)
{
    Obj_PitSplashOut(r->x);
}

static void Run_PitSplashIn(Regs *r)
{
    Obj_PitSplashIn(r->x);
}

static void Run_HopWalker(Regs *r)
{
    Obj_HopWalker(r->x);
}

static void Run_HopWalkerWalk(Regs *r)
{
    Obj_HopWalkerWalk(r->x);
}

static void Run_Swooper(Regs *r)
{
    Obj_Swooper(r->x);
}

const DecompRoutine gObjFlyerRoutines[] = {
    { DT_CODE_KIND_DANGLER, NULL, "Obj_Dangler", Run_Dangler, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_FLYER, NULL, "Obj_Flyer", Run_Flyer, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_SWOOPER, NULL, "Obj_Swooper", Run_Swooper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_HOP_WALKER, NULL, "Obj_HopWalker", Run_HopWalker, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_HOP_WALKER_WALK, NULL, "Obj_HopWalkerWalk", Run_HopWalkerWalk, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_PIT_JUMPER, NULL, "Obj_PitJumper", Run_PitJumper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PIT_SPLASH_OUT, NULL, "Obj_PitSplashOut", Run_PitSplashOut, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PIT_SPLASH_IN, NULL, "Obj_PitSplashIn", Run_PitSplashIn, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_POUNCER, NULL, "Obj_Pouncer", Run_Pouncer, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CEILING_HOPPER, NULL, "Obj_CeilingHopper", Run_CeilingHopper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_CEILING_HOPPER_WALK, NULL, "Obj_CeilingWalk", Run_CeilingWalk, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_DROPPING, NULL, "Obj_Dropping", Run_Dropping, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_RISING_SHOT, NULL, "Obj_RisingShot", Run_RisingShot, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_BOBBER, NULL, "Obj_Bobber", Run_Bobber, 0, .bank = DT_BANK_OBJECTS },
};
const int gObjFlyerRoutineCount = sizeof gObjFlyerRoutines / sizeof gObjFlyerRoutines[0];
