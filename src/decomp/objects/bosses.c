/*
 * objects/bosses.c - the land bosses and the boss hit: African Mines,
 * Himalayas, Moon (with its target and launcher), Amazon and Transylvania.
 */
#include "decomp/objects/objects_internal.h"

#define BOSS_COOLDOWN        0x78 /* frames before the next hit counts */
#define BOSS_ACTION_NO_PRIZE 0x84
#define PRIZE_X              0x80
#define PRIZE_Y              0xB8
#define PRIZE_STATE          0xB1
#define PRIZE_TIMER          0x3C

/* $F6D7: a hit on a boss. While its cooldown $06F0 runs the hit only counts
 * it down, and the blink (flag bit 1, and $0690) ends with it. A counted hit
 * raises EVENT_BOSS_HIT, takes one of the hits left $B1 and starts the
 * cooldown, blinking; the last one beats the boss: unless its action is $84,
 * its prize appears at the arena's middle (a hidden item in a chest, in a
 * free slot or else slot 1). True (carry) when the hit beat it; X is kept. */
bool Boss_Hit(uint8_t *x)
{
    uint8_t slot = *x;
    Decomp_Enter(DT_CODE_BOSS_HIT, 0, slot, 0);
    if (gObjects->bossCooldown[slot] != 0) {
        if (--gObjects->bossCooldown[slot] == 0) {
            gObjects->unk_0690[slot] = 0;
            gObjects->flags[slot] &= (uint8_t)~OBJ_FLAG_BLINK;
        }
        return false;
    }
    Decomp_RaiseEvent(EVENT_BOSS_HIT);
    if (--gBossHits != 0) {
        gObjects->bossCooldown[slot] = BOSS_COOLDOWN;
        gObjects->flags[slot] |= OBJ_FLAG_BLINK;
        return false;
    }
    if (gObjects->action[slot] == BOSS_ACTION_NO_PRIZE) {
        return true;
    }
    gScratch0 = slot;
    uint8_t o;
    if (!Obj_FindFreeSlot(&o)) {
        o = 1;
    }
    gObjects->y[o] = PRIZE_Y;
    gObjects->worldPage[o] = gCameraPage;
    gObjects->worldX[o] = PRIZE_X;
    gObjects->unk_0690[o] = 0;
    gObjects->animTimer[o] = 0;
    gObjects->animFrame[o] = 0;
    gObjects->hitbox[o] = 0;
    gObjects->bossCooldown[o] = 0;
    gObjects->state[o] = PRIZE_STATE;
    gObjects->flags[o] = PRIZE_STATE & 0xF0;
    gObjects->action[o] = OBJ_ACTION_CHEST;
    gObjects->kind[o] = OBJ_KIND_HIDDEN_ITEM;
    gObjects->unk_06C0[o] = PRIZE_TIMER;
    return true;
}

/* Kind $28 ($A6D3): the African Mines boss. It lands in the middle of the
 * arena (world X $80, Y $B0) and hops three times (vertical velocity $02D4,
 * sound $22), then, facing a random way (pose $2A, harmless pose $62 to
 * $63), runs a 20-step pattern of climbs up and down and walks at $0398
 * ($A8AC: direction and vertical way), stepping 4 px on the probes the step
 * expects ($A8C0). Off the expected probes after a full pattern, within 3 px
 * of the middle, it starts over. A counted hit goes to the boss hit $F6D7;
 * the last one beats it. */
void Obj_MinesBoss(uint8_t x)
{
    static const uint8_t pattern[20] = { 0x05, 0x09, 0x05, 0x09, 0x05, 0x09, 0x0A, 0x09, 0x0A, 0x09,
                                         0x0A, 0x06, 0x0A, 0x06, 0x0A, 0x06, 0x05, 0x06, 0x05, 0x06 };
    static const uint8_t expected[20] = { 0x01, 0x00, 0x01, 0x00, 0x01, 0x02, 0x00, 0x02, 0x00, 0x02,
                                          0x01, 0x00, 0x01, 0x00, 0x01, 0x02, 0x00, 0x02, 0x00, 0x02 };

    Decomp_Enter(DT_CODE_KIND_MINES_BOSS, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0 && Boss_Hit(&x)) {
        Obj_Defeat(x);
        return;
    }
    uint8_t step;
    if (Step(x) == 0) {
        if (gObjects->action[x] == 0x2A) {
            if (gObjects->animFrame[x] != 0 && gObjects->animTimer[x] == 5) {
                gObjects->action[x] = 0x62;
                ResetAnim(x);
                gObjects->hitbox[x] |= 0x80;
            }
            return;
        }
        Obj_SetVelocityY(x, 0);
        gObjects->timer[x] = 0;
        gObjects->vdir[x] = 0;
        gObjects->y[x] = 0xB0;
        gObjects->worldX[x] = 0x80;
        RAM_U8(DT_ADDR_MINES_BOSS_05E7) = 0x55;
        gObjects->state[x]++;
    }
    if (!(gObjects->state[x] & 2)) {
        if (gObjects->vdir[x] != 0) {
            x = Obj_Rise(x, 0x52, 2);
            if (gObjects->vy[x] & 0x80) {
                Obj_SetVelocityY(x, 0);
                gObjects->vdir[x]--;
            }
            return;
        }
        x = Obj_Fall(x, 0x31, 2);
        if (!(gProbeHits & 1)) {
            return;
        }
        Sound_Request(SFX_BOSS_HOP, x);
        Obj_SetVelocityY(x, 0x02D4);
        gObjects->vdir[x]++;
        if (++gObjects->timer[x] != 3) {
            return;
        }
        gObjects->state[x]++;
        gRandom(1) = (uint8_t)(gRandom(0) + gRandom(1) + 1);
        gObjects->dir[x] = (uint8_t)((gRandom(1) & 1) + 1);
        gObjects->timer[x] = gObjects->dir[x];
        gObjects->action[x] = 0x2A;
        ResetAnim(x);
        gObjects->unk_06C0[x] = 0;
        gObjects->unk_06D0[x] = 0;
        Obj_SetSpeed(x, 0x0398);
        Obj_SetVelocityY(x, 0x0398);
        step = 0;
    } else {
        if (gObjects->action[x] == 0x2A) {
            if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) {
                return;
            }
            gObjects->action[x] = 0x63;
            ResetAnim(x);
            gObjects->hitbox[x] &= 0x7F;
        }
        (void)Obj_TouchScrooge(x);
        if (gObjects->vdir[x] & 4) {
            x = Obj_ClimbDown(x, 0x31, 2);
        } else {
            x = Obj_ClimbUp(x, 0x52, 2);
        }
        uint8_t vertical = (uint8_t)((gProbeHits & 1) << 1);
        if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
            x = Obj_WalkRight(x, 0x34, 2);
        } else {
            x = Obj_WalkLeft(x, 0x37, 2);
        }
        gProbeHits = (uint8_t)(vertical + (gProbeHits & 1));
        uint8_t want = expected[gObjects->unk_06C0[x]];
        if (want != 0) {
            gProbeHits &= want;
        }
        if (gProbeHits != want) {
            if (gObjects->unk_06D0[x] == 0) {
                return;
            }
            uint8_t off = (uint8_t)(gObjects->worldX[x] - 0x80);
            if (gObjects->worldX[x] < 0x80) {
                off = (uint8_t)-off;
            }
            if (off >= 3) {
                return;
            }
            gObjects->action[x] = 0x2A;
            ResetAnim(x);
            gObjects->state[x] &= 0xF0;
            return;
        }
        if (gProbeHits == 0) {
            gObjects->y[x] = (uint8_t)(gObjects->y[x] + ((gObjects->vdir[x] & 8) ? 0xFC : 0x04));
        }
        step = (uint8_t)(gObjects->unk_06C0[x] + 1);
        if (step == 0x14) {
            gObjects->unk_06D0[x]++;
            step = 0;
        }
        gObjects->unk_06C0[x] = step;
    }
    uint8_t p = pattern[step];
    gObjects->dir[x] = (uint8_t)(p & 3);
    if (gObjects->timer[x] != 1) {
        gObjects->dir[x] ^= 3;
    }
    gObjects->vdir[x] = (uint8_t)(p & 0x0C);
}

/* Kind $2A ($A8D6): the Himalayas boss. It jumps (vertical velocity $06EE,
 * frame 2 of its pose), lands (sound $1E) and, at animation frame 7, rests
 * 30 frames before the next jump, walking at $01CC while airborne. A wall
 * hit while resting turns it (pose $65, sound $23) and starts a stomp: at
 * frame 7 of the pose (pose $64) the camera shakes 4 px every 4 frames and
 * one of two patterns ($AAA8, random) drops a row of falling objects (kind
 * $2B, pose $47) into slots 8 on, each at its frame count ($AAB6). When the
 * last one is gone it jumps again. A counted hit goes to the boss hit $F6D7;
 * the last one beats it. The spawning slot is left in $00. */
void Obj_HimalayasBoss(uint8_t x)
{
    static const uint8_t dropX[14] = { 0x70, 0xD8, 0x48, 0xA8, 0x18, 0x88, 0x00,
                                       0x68, 0x98, 0x28, 0xD8, 0x48, 0x88, 0x00 };
    static const uint8_t dropAt[14] = { 0x00, 0x08, 0x18, 0x18, 0x20, 0x30, 0x00,
                                        0x00, 0x08, 0x18, 0x18, 0x28, 0x28, 0x00 };
    static const uint8_t patterns[2] = { 0x00, 0x07 };
    static const uint8_t shakes[2] = { 0x00, 0x04 };

    Decomp_Enter(DT_CODE_KIND_HIMALAYAS_BOSS, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0 && Boss_Hit(&x)) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        ResetAnim(x);
        Obj_SetSpeed(x, 0x01CC);
        gObjects->state[x]++;
        gObjects->dir[x] = OBJ_DIR_LEFT;
        RAM_U8(DT_ADDR_HIMALAYAS_BOSS_05A0) = 0x11;
        Obj_SetVelocityY(x, 0x06EE);
    }
    if (gObjects->state[x] & 2) {
        if (gObjects->action[x] != 0x64) {
            if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 7) {
                return;
            }
            gObjects->action[x] = 0x64;
            gObjects->animFrame[x] = 0;
            gObjects->timer[x] = 0;
            gRandom(1) = (uint8_t)(gRandom(0) + gRandom(1) + 1);
            gObjects->unk_06C0[x] = patterns[gRandom(1) & 1];
            gObjects->unk_06D0[x] = 8;
        }
        for (;;) {
            gObjects->animTimer[x] = 0;
            gCameraX = shakes[(RAM_U8(DT_ADDR_SPR_PHASE) >> 2) & 1];
            uint8_t i = gObjects->unk_06C0[x];
            if (dropX[i] == 0) {
                break;
            }
            if (gObjects->timer[x] != dropAt[i]) {
                gObjects->timer[x]++;
                return;
            }
            gScratch0 = x;
            uint8_t s = gObjects->unk_06D0[x];
            gObjects->worldX[s] = dropX[i];
            gObjects->worldPage[s] = 4;
            gObjects->y[s] = 0x38;
            gObjects->state[s] = 0xA0;
            gObjects->flags[s] = 0xA0;
            gObjects->action[s] = 0x47;
            ResetAnim(s);
            gObjects->hitbox[s] = 0x10;
            gObjects->kind[s] = OBJ_KIND_DROPPING;
            Obj_SetVelocityY(s, 0x0180);
            gObjects->unk_06C0[x]++;
            gObjects->unk_06D0[x]++;
        }
        if (gObjects->state[gObjects->unk_06D0[x] - 1] != 0) {
            return;
        }
        gObjects->state[x]--;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->unk_06D0[x] = 0;
        gCameraX = 0;
        Obj_SetVelocityY(x, 0x06EE);
    }
    if (gObjects->unk_06C0[x] != 0) {
        gObjects->animTimer[x] = 0;
        if (--gObjects->unk_06C0[x] == 0) {
            gObjects->animFrame[x]++;
        }
        return;
    }
    if (gObjects->timer[x] == 0) {
        if (gObjects->animFrame[x] != 2) {
            return;
        }
        gObjects->animTimer[x] = 0;
        x = Obj_Rise(x, 0x52, 2);
        if (gObjects->vy[x] & 0x80) {
            Obj_SetVelocityY(x, 0);
            gObjects->timer[x]++;
            return;
        }
    } else {
        if (gObjects->animFrame[x] == 2) {
            gObjects->animTimer[x] = 0;
        }
        x = Obj_Fall(x, 0x31, 2);
        if (gProbeHits & 1) {
            if (gObjects->animFrame[x] != 1) {
                gObjects->animFrame[x] = 1;
            }
            if (gObjects->animTimer[x] == 0) {
                Sound_Request(SFX_BOSS_LAND, x);
            }
            if (gObjects->animTimer[x] != 7) {
                return;
            }
            gObjects->unk_06C0[x] = 0x1E;
            Obj_SetVelocityY(x, 0x06EE);
            gObjects->timer[x] = 0;
            gObjects->animFrame[x] = 0;
        }
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0x34, 2);
    } else {
        x = Obj_WalkLeft(x, 0x37, 2);
    }
    if (!(gProbeHits & 1) || gObjects->unk_06C0[x] == 0) {
        return;
    }
    gObjects->dir[x] ^= 3;
    gObjects->state[x]++;
    gObjects->action[x] = 0x65;
    ResetAnim(x);
    Sound_Request(SFX_BOSS_TURN, x);
}

/* The Moon boss back in the middle: pose $67, 30 frames facing Scrooge. */
static void Obj_MoonBossRestart(uint8_t x)
{
    gObjects->worldX[x] = 0x80;
    gObjects->unk_06C0[x]--;
    gObjects->state[x]--;
    gObjects->action[x] = 0x67;
    ResetAnim(x);
    gObjects->unk_06D0[x] = 0x1E;
}

/* Kind $29 ($AAE1): the Moon boss. It enters at world X $80 (pose $67) and
 * for 15 frames faces Scrooge, then (pose $66) walks a random way at a
 * random speed, $03C0 or $01CC, the slow one jumping first (pose $68,
 * vertical velocity $06EE). Landing from a jump it jumps again ($0548),
 * turned round; walls turn it too. After a turn, back within 4 px of the
 * middle on the floor, or landing after its second jump, it starts over
 * from the middle with 30 frames facing Scrooge. Its random steps add the
 * carry the dispatch left. A counted hit goes to the boss hit $F6D7; the
 * last one beats it. */
void Obj_MoonBoss(uint8_t x, bool carry)
{
    static const uint16_t speeds[2] = { 0x03C0, 0x01CC };

    Decomp_Enter(DT_CODE_KIND_MOON_BOSS, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        if (Boss_Hit(&x)) {
            Obj_Defeat(x);
            return;
        }
        carry = false;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        gObjects->worldX[x] = 0x80;
        RAM_U8(DT_ADDR_BOSS_05AF) = 0x44;
        gObjects->state[x]++;
        gObjects->unk_06D0[x] = 0x0F;
        gObjects->action[x] = 0x67;
    }
    if (gObjects->unk_06D0[x] != 0) {
        gObjects->unk_06D0[x]--;
        uint8_t facing = gObjects->worldX[x] >= gObjects->worldX[P_] ? 0 : OBJ_FLAG_FACING_RIGHT;
        gObjects->flags[x] = (uint8_t)((gObjects->flags[x] & (uint8_t)~OBJ_FLAG_FACING_RIGHT) | facing);
        return;
    }
    if (!(gObjects->state[x] & 2)) {
        gObjects->state[x]++;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        ResetAnim(x);
        gObjects->action[x] = 0x66;
        unsigned sum = gRandom(0) + gRandom(1) + carry;
        gRandom(3) = (uint8_t)sum;
        gObjects->dir[x] = (uint8_t)((sum & 1) + 1);
        sum = gRandom(2) + gRandom(3) + (sum >> 8);
        gRandom(1) = (uint8_t)sum;
        uint8_t slow = sum & 1;
        gObjects->timer[x] = slow;
        Obj_SetSpeed(x, speeds[slow]);
        if (slow) {
            gObjects->timer[x]++;
            Obj_SetVelocityY(x, 0x06EE);
            gObjects->action[x] = 0x68;
        }
    }
    if (gObjects->timer[x] != 0) {
        if (!(gObjects->timer[x] & 1)) {
            x = Obj_Rise(x, 0x5E, 2);
            if (gObjects->vy[x] & 0x80) {
                gObjects->timer[x]--;
                Obj_SetVelocityY(x, 0);
            }
        } else {
            x = Obj_Fall(x, 0x5B, 2);
            if (gProbeHits & 1) {
                if (gObjects->unk_06C0[x] != 0) {
                    Obj_MoonBossRestart(x);
                    return;
                }
                Obj_SetVelocityY(x, 0x0548);
                gObjects->timer[x]++;
                gObjects->unk_06C0[x]++;
                gObjects->dir[x] ^= 3;
                return;
            }
        }
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0x55, 2);
    } else {
        x = Obj_WalkLeft(x, 0x58, 2);
    }
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
        gObjects->unk_06C0[x]++;
        return;
    }
    if (gObjects->timer[x] != 0 || gObjects->unk_06C0[x] == 0) {
        return;
    }
    uint8_t off = (uint8_t)(gObjects->worldX[x] - 0x80);
    if (gObjects->worldX[x] < 0x80) {
        off = (uint8_t)-off;
    }
    if (off < 4) {
        Obj_MoonBossRestart(x);
    }
}

/* Kind $33 ($ADB9): an object that does nothing until a hit beats it. */
void Obj_Target(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_TARGET, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
    }
}

/* Kind $34 ($ADC1): a launcher. Touched (not while Scrooge is knocked back
 * or dying), it throws him into mode 12 with speed $0400 and vertical
 * velocity $0728 (sound $28) and plays its animation (frame timer 7, then
 * held at its end with $80). */
void Obj_Launcher(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_LAUNCHER, 0x22, x, gObjects->kind[x]);
    if (gPlayerMode == PLAYER_MODE_KNOCKBACK || gPlayerMode == PLAYER_MODE_DEATH) {
        return;
    }
    if (Step(x) == 0) {
        gObjects->animTimer[x] = 0;
        if (!Obj_TouchScrooge(x)) {
            return;
        }
        gPlayerMode = PLAYER_MODE_12;
        gObjects->state[x]++;
        gObjects->animTimer[x] = 7;
        Obj_SetSpeed(P_, 0x0400);
        Obj_SetVelocityY(P_, 0x0728);
        gObjects->vdir[P_] = 0;
        Sound_Request(SFX_LAUNCH, x);
    }
    if ((gObjects->animTimer[x] | gObjects->animFrame[x]) == 0) {
        gObjects->animTimer[x] = 0x80;
    }
}

/* Kind $2E ($A5DB): the Amazon boss. It starts at Y $AC walking right at
 * $0100, turning at walls. When its wait runs out it jumps (vertical
 * velocity $06B7) towards Scrooge, rises with probe $64 and falls with probe
 * $63. Landing (sound $1E) shakes the screen for 30 frames (row scroll $E9
 * on bit 2 of $95, split $C0 or $E0) and holds pogo bounces ($A7); then it
 * waits 60, 120 or 180 frames, picked by a random step that adds the carry
 * the dispatch left. A counted hit goes to the boss hit $F6D7; the last one
 * stops the shake and beats it. */
void Obj_AmazonBoss(uint8_t x, bool carry)
{
    static const uint8_t waits[8] = { 0x3C, 0x3C, 0x78, 0x78, 0x78, 0xB4, 0xB4, 0xB4 };
    static const uint8_t splits[2] = { 0xC0, 0xE0 };

    Decomp_Enter(DT_CODE_KIND_AMAZON_BOSS, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        if (Boss_Hit(&x)) {
            gRowScroll = 0;
            gRowScrollAddr = 0xC0;
            Obj_Defeat(x);
            return;
        }
        carry = false;
    }
    if (Step(x) == 0) {
        gObjects->unk_06C0[x] = 0;
        gObjects->y[x] = 0xAC;
        gObjects->timer[x] = 0x1E;
        Obj_SetSpeed(x, 0x0100);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
        RAM_U8(DT_ADDR_BOSS_05AF) = 0x44;
        gObjects->state[x]++;
    }
    if (gObjects->timer[x] == 0) {
        if (!(gObjects->state[x] & 2)) {
            x = Obj_Rise(x, 0x64, 0);
            if (gObjects->vy[x] & 0x80) {
                gObjects->state[x]++;
                Obj_SetVelocityY(x, 0);
            }
        } else {
            bool shaking = gObjects->unk_06C0[x] != 0;
            if (!shaking) {
                x = Obj_Fall(x, 0x63, 0);
                if (gProbeHits & 1) {
                    Sound_Request(SFX_BOSS_LAND, x);
                    gPogoCeiling++;
                    gObjects->unk_06C0[x] = 0x1E;
                    shaking = true;
                }
            }
            if (shaking) {
                if (--gObjects->unk_06C0[x] != 0) {
                    gRowScroll = (uint8_t)((gSprPhase >> 2) & 1);
                    gRowScrollAddr = splits[gRowScroll];
                    return;
                }
                gObjects->state[x]--;
                gRandom(0) = (uint8_t)(gRandom(0) + gRandom(3) + carry);
                gObjects->timer[x] = waits[gRandom(0) & 7];
                gRowScrollAddr = 0xC0;
                gRowScroll = 0;
                gPogoCeiling = 0;
                return;
            }
        }
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0x61, 0);
    } else {
        x = Obj_WalkLeft(x, 0x62, 0);
    }
    if (gObjects->timer[x] == 0) {
        return;
    }
    if (--gObjects->timer[x] == 0) {
        Obj_SetVelocityY(x, 0x06B7);
        Obj_FaceScrooge(x);
    }
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
    }
}

/* The Transylvania boss rises again: step + 1, pose $2A, vertical velocity
 * $0380. */
static void Obj_TransylvaniaBossRise(uint8_t x)
{
    gObjects->state[x]++;
    gObjects->action[x] = 0x2A;
    ResetAnim(x);
    Obj_SetVelocityY(x, 0x0380);
}

/* Kind $26 ($A335): the Transylvania boss, by step through $A585/$A58A
 * (steps 0-4; the bytes past it are other tables).
 * Step 0 waits 60 frames, then rises. Step 1 turns to pose $5E at frame 1,
 * timer 5 of pose $2A and picks a height ($A58F) with a random step; there
 * it picks a random world X ($A597), a wait of 60 or 150 frames and a way.
 * Step 2 flies the 14-entry weave of $A5A0 (an entry every 3 frames, its
 * speed made positive), walls turning it, until the wait is over and it is
 * within 4 px of that X. Step 3 drops to the floor (probe $1E) and faces
 * Scrooge in pose $2A. Step 4 turns to pose $5D at frame 1, timer 5 for 15
 * frames, then at frame 2 throws three shots (kind $27, slots 8-10, sound
 * $1C) 16 px ahead, and 30 frames later rises again. A counted hit goes to
 * the boss hit $F6D7; the last one beats it. */
void Obj_TransylvaniaBoss(uint8_t x)
{
    static const uint8_t heights[4] = { 0x68, 0x48, 0x88, 0x68 };
    static const uint8_t waits[2] = { 0x3C, 0x96 };
    static const uint8_t ways[2] = { OBJ_DIR_RIGHT, OBJ_DIR_LEFT };
    static const uint8_t targets[8] = { 0x30, 0x50, 0x80, 0xB0, 0xD0, 0x30, 0x80, 0xD0 };
    static const uint8_t weave[14] = { 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
                                       0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
    static const uint8_t ahead[2] = { 0xF0, 0x10 };
    static const uint8_t facing[2] = { 0, OBJ_FLAG_FACING_RIGHT };
    static const uint16_t shotVy[3] = { 0x0000, 0x016A, 0x0200 };
    static const uint16_t shotSpeed[3] = { 0x0200, 0x016A, 0x0000 };

    Decomp_Enter(DT_CODE_KIND_TRANSYLVANIA_BOSS, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0 && Boss_Hit(&x)) {
        Obj_Defeat(x);
        return;
    }
    uint8_t step = Step(x);
    gScratch0 = Rom((uint16_t)(DT_CODE_TRANSYLVANIA_BOSS_STEP_LO + step));
    gScratch1 = Rom((uint16_t)(DT_CODE_TRANSYLVANIA_BOSS_STEP_HI + step));
    switch (step) {
        case 0:
            RAM_U8(DT_ADDR_TRANSYLVANIA_BOSS_05EF) = 0x44;
            gObjects->animTimer[x] = 0;
            if (++gObjects->timer[x] == 0x3C) {
                Obj_TransylvaniaBossRise(x);
            }
            return;
        case 1:
            if (gObjects->action[x] == 0x2A) {
                if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) {
                    return;
                }
                gObjects->action[x] = 0x5E;
                ResetAnim(x);
                gRandom(1) = (uint8_t)(gRandom(0) + gRandom(2) + 1);
                gObjects->timer[x] = heights[gRandom(1) & 3];
                return;
            }
            Obj_SubVelocityY(x);
            if (gObjects->y[x] >= gObjects->timer[x]) {
                return;
            }
            gRandom(2) = (uint8_t)(gRandom(1) + gRandom(3));
            {
                uint8_t i = gRandom(2) & 7;
                gObjects->unk_06C0[x] = targets[i];
                gObjects->timer[x] = waits[i & 1];
                gObjects->unk_06D0[x] = 0;
                gObjects->unk_06E0[x] = 0;
                gObjects->dir[x] = ways[i & 1];
            }
            gObjects->state[x]++;
            return;
        case 2: {
            if (gObjects->timer[x] != 0) {
                gObjects->timer[x]--;
            }
            if (gObjects->unk_06D0[x] == 0) {
                Obj_SetMotionIndex(x, weave[gObjects->unk_06E0[x]]);
                if (gObjects->speed[x] & 0x80) {
                    Obj_SetSpeed(x, (uint16_t)-Obj_GetSpeed(x));
                }
                if (++gObjects->unk_06E0[x] == 0x0E) {
                    gObjects->unk_06E0[x] = 0;
                }
                gObjects->unk_06D0[x] = 3;
            }
            gObjects->unk_06D0[x]--;
            uint16_t y = (uint16_t)((gObjects->y[x] << 8 | gObjects->yFrac[x]) + Obj_GetVelocityY(x));
            gObjects->yFrac[x] = (uint8_t)y;
            gObjects->y[x] = (uint8_t)(y >> 8);
            if (gObjects->dir[x] & OBJ_DIR_LEFT) {
                x = Obj_WalkLeft(x, 0x1B, 2);
            } else {
                x = Obj_WalkRight(x, 0x18, 2);
            }
            if (gObjects->timer[x] == 0 && AbsDiff(gObjects->worldX[x], gObjects->unk_06C0[x]) < 4) {
                gObjects->state[x]++;
                Obj_SetVelocityY(x, 0x0380);
            }
            if (gObjects->unk_06D0[x] == 0 && (gProbeHits & 1)) {
                gObjects->dir[x] ^= 3;
            }
            return;
        }
        case 3:
            x = Obj_ClimbDown(x, 0x1E, 2);
            if (!(gProbeHits & 1)) {
                return;
            }
            gObjects->action[x] = 0x2A;
            ResetAnim(x);
            gObjects->state[x]++;
            Obj_FaceScrooge(x);
            gObjects->flags[x] = (uint8_t)((gObjects->flags[x] & (uint8_t)~OBJ_FLAG_FACING_RIGHT) |
                                           facing[gObjects->dir[x] & 1]);
            return;
        case 4:
            if (gObjects->action[x] == 0x2A) {
                if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) {
                    return;
                }
                gObjects->action[x] = 0x5D;
                ResetAnim(x);
                gObjects->unk_06C0[x] = 0;
                gObjects->timer[x] = 0x0F;
                return;
            }
            if (gObjects->timer[x] != 0) {
                gObjects->timer[x]--;
                gObjects->animTimer[x] = 0;
                return;
            }
            if (gObjects->unk_06C0[x] != 0) {
                gObjects->state[x] &= 0xF0;
                Obj_TransylvaniaBossRise(x);
                return;
            }
            if (gObjects->animFrame[x] != 2) {
                return;
            }
            gScratch0 = (uint8_t)(gObjects->worldX[x] + ahead[gObjects->dir[x] & 1]);
            for (int i = 2; i >= 0; i--) {
                uint8_t s = (uint8_t)(8 + i);
                gObjects->state[s] = 0xA0;
                gObjects->flags[s] = 0xA0;
                gObjects->animTimer[s] = 0;
                gObjects->animFrame[s] = 0;
                gObjects->action[s] = 0x5F;
                gObjects->y[s] = gObjects->y[x];
                gObjects->worldPage[s] = gObjects->worldPage[x];
                gObjects->worldX[s] = gScratch0;
                gObjects->dir[s] = gObjects->dir[x];
                Obj_SetVelocityY(s, shotVy[i]);
                Obj_SetSpeed(s, shotSpeed[i]);
                gObjects->kind[s] = OBJ_KIND_RISING_SHOT;
                gObjects->hitbox[s] = 0x1D;
            }
            Sound_Request(SFX_BOSS_SHOTS, x);
            gObjects->timer[x] = 0x1E;
            gObjects->unk_06C0[x]++;
            return;
    }
}

/* ---- adapters ---- */

static void Run_MinesBoss(Regs *r)
{
    Obj_MinesBoss(r->x);
}

static void Run_HimalayasBoss(Regs *r)
{
    Obj_HimalayasBoss(r->x);
}

static void Run_MoonBoss(Regs *r)
{
    Obj_MoonBoss(r->x, r->p & P_C);
}

static void Run_Target(Regs *r)
{
    Obj_Target(r->x);
}

static void Run_Launcher(Regs *r)
{
    Obj_Launcher(r->x);
}

static void Run_AmazonBoss(Regs *r)
{
    Obj_AmazonBoss(r->x, r->p & P_C);
}

static void Run_TransylvaniaBoss(Regs *r)
{
    Obj_TransylvaniaBoss(r->x);
}

static void Run_BossHit(Regs *r)
{
    SetCarry(r, Boss_Hit(&r->x));
}

const DecompRoutine gObjBossRoutines[] = {
    { DT_CODE_KIND_TRANSYLVANIA_BOSS, NULL, "Obj_TransylvaniaBoss", Run_TransylvaniaBoss, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_AMAZON_BOSS, NULL, "Obj_AmazonBoss", Run_AmazonBoss, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_LAUNCHER, NULL, "Obj_Launcher", Run_Launcher, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_TARGET, NULL, "Obj_Target", Run_Target, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_MOON_BOSS, NULL, "Obj_MoonBoss", Run_MoonBoss, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_HIMALAYAS_BOSS, NULL, "Obj_HimalayasBoss", Run_HimalayasBoss, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_MINES_BOSS, NULL, "Obj_MinesBoss", Run_MinesBoss, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_BOSS_HIT, "ducktales.decomp.F6D7", "Boss_Hit", Run_BossHit, OUT_X | OUT_C },
};
const int gObjBossRoutineCount = sizeof gObjBossRoutines / sizeof gObjBossRoutines[0];
