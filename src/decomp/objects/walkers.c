/*
 * objects/walkers.c - enemies that walk the floor: hoppers, walkers and
 * throwers, the charger, pacers and ledge walkers, the leaper, the bouncer
 * and the block kicker.
 */
#include "decomp/objects/internal.h"

/* Kind 1 ($8225): an enemy that walks towards where Scrooge was at 1 px a
 * frame and jumps ($0400) whenever it lands. Hit: pose $0C, beaten. */
void Obj_Hopper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_HOPPER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0x0C)) {
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        gObjects->speedFrac[x] = 0;
        gObjects->state[x]++;
        gObjects->speed[x] = 1;
        Obj_FaceScrooge(x);
    }
    bool fall = true;
    if (gObjects->state[x] & 2) {
        x = Obj_Rise(x, 0x21, 2);
        if (!(gObjects->vy[x] & 0x80)) {
            fall = false;
        } else {
            gObjects->state[x]--;
            Obj_SetVelocityY(x, 0);
        }
    }
    if (!fall || (x = Obj_Fall(x, 0x1E, 2), !(gProbeHits & 1))) {
        ResetAnim(x);
    }
    x = Enemy_Walk(x);
    if (gObjects->state[x] & 2) {
        return;
    }
    if (!(gProbeHits & 1) || Obj_GetVelocityY(x) != 0) {
        return;
    }
    gObjects->state[x]++;
    Obj_SetVelocityY(x, 0x0400);
}

/* Kind 2 ($82AF): an enemy that walks at speed 0 of the table and turns at
 * walls. Hit: pose $0C, beaten. */
void Obj_Walker(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_WALKER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        gObjects->action[x] = 0x0C;
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        gObjects->state[x]++;
        Obj_SetSpeedIndex(x, 0);
        Obj_FaceScrooge(x);
    }
    x = Obj_Fall(x, 0x1E, 2);
    if (!(gProbeHits & 1)) {
        ResetAnim(x);
    }
    x = Enemy_Walk(x);
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
    }
}

/* Kind 3 ($830E): an enemy that walks and hops like kind 1, and within $50
 * px of Scrooge stops (step 3) to throw one kind-4 shot when its animation
 * gets there; after $3C frames it walks on. Hit: beaten. */
void Obj_Thrower(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_THROWER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0)) {
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->state[x]++;
        Obj_SetSpeedIndex(x, 0);
        Obj_FaceScrooge(x);
    }
    if (Step(x) == 3) {
        uint8_t y;
        if (gObjects->unk_06C0[x] == 0 && (gObjects->animFrame[x] & 0x0F) && Obj_FindFreeSlotY(&y)) {
            gObjects->animTimer[y] = 0;
            gObjects->animFrame[y] = 0;
            gObjects->speedFrac[y] = 0;
            gObjects->hitbox[y] = 0;
            gObjects->state[y] = gObjects->state[x];
            gObjects->flags[y] = gObjects->flags[x];
            gObjects->action[y] = 0x17;
            gObjects->kind[y] = OBJ_KIND_SHOT;
            gObjects->speed[y] = 2;
            gObjects->worldX[y] = gObjects->worldX[x];
            gObjects->worldPage[y] = gObjects->worldPage[x];
            gObjects->y[y] = (uint8_t)(gObjects->y[x] - 8);
            gObjects->hitbox[y] = 0x17;
            Sound_Request(SFX_THROW, x);
            gObjects->unk_06C0[x]++;
        }
        if (--gObjects->timer[x] != 0) {
            return;
        }
        ResetAnim(x);
        gObjects->action[x] = 0x14;
        gObjects->state[x] &= 0xF1;
        return;
    }
    bool fall = true;
    if (Step(x) & 2) {
        x = Obj_Rise(x, 0x09, 2);
        if (!(gObjects->vy[x] & 0x80)) {
            fall = false;
        } else {
            gObjects->state[x]--;
            Obj_SetVelocityY(x, 0);
        }
    }
    if (!fall || (x = Obj_Fall(x, 0x06, 2), !(gProbeHits & 1))) {
        gObjects->action[x] = 0x15;
        ResetAnim(x);
    } else {
        gObjects->action[x] = 0x14;
    }
    x = (gObjects->dir[x] & OBJ_DIR_RIGHT) ? Obj_WalkRight(x, 0x00, 2) : Obj_WalkLeft(x, 0x03, 2);
    if (gObjects->state[x] & 2) {
        return;
    }
    if (Obj_GetVelocityY(x) != 0) {
        return;
    }
    if (gProbeHits & 1) {
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0x0400);
        return;
    }
    if (gObjects->unk_06C0[x] != 0 || Obj_DistanceX(x) >= 0x51) {
        return;
    }
    gObjects->animTimer[x] = 0;
    gObjects->animFrame[x] = 0x80;
    gObjects->pose[x] = 1;
    gObjects->action[x] = 0x16;
    gObjects->state[x] = (uint8_t)((gObjects->state[x] & 0xF0) | 3);
    gObjects->timer[x] = 0x3C;
}

/* Kind 4 ($8462): a shot flying in its facing direction; it vanishes
 * against a wall. */
void Obj_Shot(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_SHOT, 0x22, x, gObjects->kind[x]);
    if (gObjects->flags[x] & OBJ_FLAG_FACING_RIGHT) {
        x = Obj_WalkRight(x, 0x24, 0);
    } else {
        x = Obj_WalkLeft(x, 0x25, 0);
    }
    (void)Obj_TouchScrooge(x);
    if (!(gProbeHits & 1)) {
        return;
    }
    gObjects->flags[x] = 0;
    gObjects->state[x] = 0;
    gObjects->record[x] = 0xFF;
}

/* Kind 5 ($8491): an enemy that stays and faces Scrooge; at the end of
 * each animation (frame $0C) it picks pose $0D or $0E by the random bytes
 * ($0E with sound $2A). Hit: pose $0F, beaten. */
void Obj_Watcher(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_WATCHER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        gObjects->action[x] = 0x0F;
        Obj_Defeat(x);
        return;
    }
    if (gObjects->animFrame[x] == 0x0C) {
        gRandom(2) = (uint8_t)(gRandom(0) + gRandom(1) + 1);
        gObjects->action[x] = (uint8_t)((gRandom(2) & 1) + 0x0D);
        if (gObjects->action[x] == 0x0E) {
            Sound_Request(SFX_WATCHER, x);
        }
        ResetAnim(x);
    }
    if (Obj_GetWorldX(x) < Obj_GetWorldX(P_)) {
        gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
    } else {
        gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
    }
}

/* Kind $0C ($90EA): an enemy that waits until Scrooge is 16 px past it to
 * the right, then walks at $0180 and turns at walls. */
void Obj_Crawler(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CRAWLER, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        uint16_t d = (uint16_t)(Obj_GetWorldX(P_) - Obj_GetWorldX(x));
        gScratch(0) = (uint8_t)d;
        if (Obj_GetWorldX(P_) < Obj_GetWorldX(x) || gScratch(0) < 0x10) {
            return;
        }
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0180);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
        gObjects->state[x]++;
    }
    (void)Obj_TouchScrooge(x);
    x = Obj_Fall(x, 0x31, 2);
    if (!(gProbeHits & 1)) {
        return;
    }
    x = (gObjects->dir[x] & OBJ_DIR_RIGHT) ? Obj_WalkRight(x, 0x34, 2) : Obj_WalkLeft(x, 0x37, 2);
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
    }
    if (gObjects->animTimer[x] == 0) {
        Sound_Request(SFX_CRAWLER, x);
    }
}

/* Kind $18 ($96D5): an enemy that walks a ledge at $0080 a frame and turns
 * back at its edges (no floor under probe $2D or $2E) and at walls. A hit
 * beats it. */
void Obj_Patroller(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_PATROLLER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0080);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
        gObjects->state[x]++;
    }
    x = Obj_Fall(x, (uint8_t)(0x2D + (gObjects->dir[x] & OBJ_DIR_RIGHT)), 0);
    if (!(gProbeHits & 1)) {
        gObjects->dir[x] ^= 3;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0x29, 0);
    } else {
        x = Obj_WalkLeft(x, 0x2B, 0);
    }
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
    }
}

/* Kind $10 ($9313): an enemy that faces Scrooge, waits 120 frames, then
 * charges at 2 px a frame. Against a wall or a block it turns, knocks the
 * block ($86F7) and bounces up ($06C0 counts the bounce, negative once it
 * falls); landing from the bounce leaves it dazed (pose $42) for 30 frames
 * before it charges again. A hit beats it. */
void Obj_Charger(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CHARGER, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0);
        gObjects->unk_06C0[x] = 0;
        Obj_SetSpeed(x, 0x0200);
        gObjects->timer[x] = 0x78;
        Obj_FaceScrooge(x);
    }
    if (!(gObjects->state[x] & 2)) {
        if (gObjects->timer[x] != 0) {
            gObjects->timer[x]--;
            gObjects->animTimer[x] = 0;
            return;
        }
        if (gObjects->animFrame[x] != 3) {
            return;
        }
        ResetAnim(x);
        gObjects->action[x] = 0x2D;
        gObjects->hitbox[x] |= 0x80;
        gObjects->state[x]++;
    }
    uint8_t bounce = gObjects->unk_06C0[x];
    if (bounce != 0) {
        if (gObjects->animFrame[x] == 3) {
            gObjects->animTimer[x] = 0;
        }
        if (!(bounce & 0x80)) {
            x = Obj_Rise(x, 9, 2);
            if (!(gObjects->vy[x] & 0x80)) {
                return;
            }
            gObjects->unk_06C0[x] = gObjects->vy[x];
            Obj_SetVelocityY(x, 0);
            return;
        }
    }
    x = Obj_Fall(x, 6, 2);
    if (!(gProbeHits & 1)) {
        return;
    }
    if (gObjects->unk_06C0[x] & 0x80) {
        ResetAnim(x);
        gObjects->unk_06C0[x] = 0;
        gObjects->action[x] = 0x42;
        gObjects->timer[x] = 0x1E;
        gObjects->state[x]--;
        return;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0, 2);
    } else {
        x = Obj_WalkLeft(x, 3, 2);
    }
    if (gObjects->animTimer[x] == 0) {
        Sound_Request(SFX_CHARGE, x);
    }
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (!(gProbeHits & 1)) {
        return;
    }
    gObjects->dir[x] ^= 3;
    gObjects->action[x] = 0x2E;
    ResetAnim(x);
    gObjects->hitbox[x] &= 0x7F;
    uint8_t block = gContactSlot;
    if (gObjects->kind[block] == OBJ_KIND_BLOCK) {
        Obj_HitBlock(block, gProbeExitY);
    }
    Sound_Request(SFX_BOUNCE, x);
    gObjects->unk_06C0[x]++;
    Obj_SetVelocityY(x, 0x02D4);
}

/* $9C0A: a puff of dust (action $57, kind 0, freed when its animation
 * ends) 4 px below the slot, in the highest free slot; none when every slot
 * is taken. */
static void Obj_SpawnDust(uint8_t x)
{
    Decomp_Enter(DT_CODE_LEAPER_DUST, 0, x, 0);
    uint8_t y;
    if (!Obj_FindFreeSlotY(&y)) {
        return;
    }
    gObjects->state[y] = 0xA8;
    gObjects->flags[y] = 0xA8;
    ResetAnim(y);
    gObjects->kind[y] = OBJ_KIND_NONE;
    gObjects->hitbox[y] = 0;
    gObjects->action[y] = 0x57;
    gObjects->y[y] = (uint8_t)(gObjects->y[x] + 4);
    gObjects->worldX[y] = gObjects->worldX[x];
    gObjects->worldPage[y] = gObjects->worldPage[x];
}

/* Kind $1C ($9B10): an enemy that wakes when Scrooge is within 80 px, walks
 * a ledge at $0133 a frame, and after 80 frames, at a wall or with nothing
 * under it turns and leaps (4 px a frame up) with a puff of dust; landing
 * it walks again. A hit beats it (pose $56). */
void Obj_Leaper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_LEAPER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        gObjects->action[x] = 0x56;
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        if (Obj_DistanceX(x) >= 0x50) {
            return;
        }
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0133);
        gObjects->timer[x] = 0x50;
        gObjects->state[x] = (uint8_t)((gObjects->state[x] & (uint8_t)~OBJ_STATUS_HIDDEN) | 1);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
    }
    (void)Obj_TouchScrooge(x);
    if (gObjects->state[x] & 2) {
        if (!(gObjects->state[x] & 1)) {
            x = Obj_Rise(x, 0x2F, 1);
            if (!(gObjects->vy[x] & 0x80)) {
                return;
            }
            gObjects->animFrame[x]++;
            Obj_SetVelocityY(x, 0);
            gObjects->state[x]++;
            return;
        }
        x = Obj_Fall(x, 0x2D, 1);
        if (!(gProbeHits & 1)) {
            return;
        }
        ResetAnim(x);
        gObjects->action[x] = 0x55;
        gObjects->y[x]++;
        gObjects->state[x] -= 2;
        return;
    }
    x = Obj_Fall(x, (uint8_t)(0x2D + (gObjects->dir[x] & OBJ_DIR_RIGHT)), 0);
    if (gProbeHits != 0) {
        gObjects->y[x] += 2;
        if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
            x = Obj_WalkRight(x, 0x29, 0);
        } else {
            x = Obj_WalkLeft(x, 0x2B, 0);
        }
        if (!(gProbeHits & 1) && --gObjects->timer[x] != 0) {
            return;
        }
    }
    gObjects->dir[x] ^= 3;
    gObjects->timer[x] = 0x50;
    gObjects->state[x]++;
    Obj_SetVelocityY(x, 0x0400);
    gObjects->action[x] = 0x56;
    gObjects->animFrame[x] = 0;
    gObjects->animTimer[x] = 0x80;
    Sound_Request(SFX_LEAP, x);
    Obj_SpawnDust(x);
}

/* Kind $1F ($9DF7): an enemy that paces 1 px a frame, right first for 22
 * frames, then turning every 32, facing its way. A hit beats it. */
void Obj_Pacer(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_PACER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        Obj_SetSpeed(x, 0x0100);
        gObjects->timer[x] = 0x16;
        gObjects->state[x]++;
        gObjects->dir[x] = OBJ_DIR_RIGHT;
    }
    (void)Obj_TouchScrooge(x);
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
        Obj_MoveRight(x);
    } else {
        gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
        Obj_MoveLeft(x);
    }
    if (--gObjects->timer[x] == 0) {
        gObjects->dir[x] ^= 3;
        gObjects->timer[x] = 0x20;
    }
}

/* Kind $1E ($9D8A): an enemy that walks a ledge at $0133 a frame, right
 * first, turning at its edges (probe $1E or $20 finds no floor) and at
 * walls. A hit beats it. */
void Obj_LedgeWalker(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_LEDGE_WALKER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0133);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
        gObjects->state[x]++;
    }
    (void)Obj_TouchScrooge(x);
    x = Obj_Fall(x, (gObjects->dir[x] & OBJ_DIR_RIGHT) ? 0x20 : 0x1E, 0);
    if (!(gProbeHits & 1)) {
        gObjects->dir[x] ^= 3;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0x19, 0);
    } else {
        x = Obj_WalkLeft(x, 0x1C, 0);
    }
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
    }
}

/* Kind $1B ($9A81): an enemy that bounces towards Scrooge, walking 1 px a
 * frame and jumping again (vertical velocity $04E5) each time it lands. Off
 * screen or hit, it is beaten. */
void Obj_Bouncer(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_BOUNCER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0)) {
        return;
    }
    if (Step(x) == 0) {
        Obj_SetSpeed(x, 0x0100);
        Obj_FaceScrooge(x);
        gObjects->state[x] |= 2;
    }
    (void)Obj_TouchScrooge(x);
    if (!(gObjects->state[x] & 2)) {
        if (gObjects->animFrame[x] != 0) {
            gObjects->animTimer[x] = 0x80;
        }
        x = Obj_Rise(x, 0x21, 2);
        if (gObjects->vy[x] & 0x80) {
            Obj_SetVelocityY(x, 0);
            gObjects->state[x]++;
        }
    } else {
        x = Obj_Fall(x, 0x1E, 2);
        if (gProbeHits & 1) {
            gObjects->state[x]--;
            ResetAnim(x);
            Obj_SetVelocityY(x, 0x04E5);
        }
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        (void)Obj_WalkRight(x, 0x18, 2);
    } else {
        (void)Obj_WalkLeft(x, 0x1B, 2);
    }
}

/* Kind $1D ($9C46): the timed enemy ($4F holds its slot). On the floor
 * (probe $4A) it waits until Scrooge is within $50 px, then walks at him at
 * $0100 (probes $44/$47). Hitting a wall, a block in its way (pose $59) is
 * kicked at frame 3 of the pose ahead of it, 2 px back, and it walks on at
 * $0200 after frame 5; another wall or object speeds it up 1 px a frame.
 * From then on it slows by $08 a frame, flipping status bit 6, and once the
 * speed runs out it turns round at $02FF. Off screen, or hit (pose $58), it
 * is beaten and $4F becomes $3C. The kick passes the Y the fall leaves
 * (gProbeExitY). */
void Obj_BlockKicker(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_BLOCK_KICKER, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x) || gObjects->unk_0690[x] != 0) {
        if (OnScreen(x)) {
            gObjects->action[x] = 0x58;
        }
        gHeldSlot = 0x3C;
        Obj_Defeat(x);
        return;
    }
    x = Obj_Fall(x, 0x4A, 2);
    if (!(gProbeHits & 1)) {
        return;
    }
    if (Step(x) == 0) {
        gObjects->animTimer[x] = 0;
        if (Obj_DistanceX(x) >= 0x50) {
            return;
        }
        Obj_SetVelocityY(x, 0);
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        Obj_SetSpeed(x, 0x0100);
        Obj_FaceScrooge(x);
        gObjects->state[x]++;
        return;
    }
    if (gObjects->state[x] & 2) {
        if (gObjects->animFrame[x] == 5) {
            gObjects->state[x]--;
            gObjects->action[x]--;
            ResetAnim(x);
            gObjects->speed[x] = 2;
            gObjects->unk_06C0[x]++;
            return;
        }
        if (gObjects->timer[x] == 0 || gObjects->animFrame[x] != 3) {
            return;
        }
        gPlayerPushDir = gObjects->dir[x];
        Obj_KickBlock(gObjects->timer[x], gProbeExitY);
        gObjects->timer[x] = 0;
        return;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0x44, 2);
    } else {
        x = Obj_WalkLeft(x, 0x47, 2);
    }
    if (gObjects->unk_06C0[x] != 0) {
        if (gObjects->timer[x] != 0) {
            return;
        }
        gObjects->state[x] ^= 0x40;
        uint16_t speed = (uint16_t)(Obj_GetSpeed(x) - 8);
        Obj_SetSpeed(x, speed);
        if (!(speed & 0x8000)) {
            return;
        }
        gObjects->timer[x] = 0xFF;
        Obj_SetSpeed(x, 0x02FF);
        gObjects->dir[x] ^= 3;
        return;
    }
    if (!(gProbeHits & 1)) {
        return;
    }
    if (Obj_HitOthers(x)) {
        gObjects->action[x] = 0x58;
        uint8_t o = gContactDepth;
        if (gObjects->kind[o] == OBJ_KIND_BLOCK) {
            gObjects->timer[x] = o;
            gObjects->state[x]++;
            gObjects->action[x]++;
            gObjects->unk_06C0[x]++;
            ResetAnim(x);
            Obj_SetWorldX(x, (uint16_t)(Obj_GetWorldX(x) + ((gObjects->dir[x] & OBJ_DIR_LEFT) ? 2 : 0xFFFE)));
            return;
        }
    }
    gObjects->speed[x]++;
    gObjects->unk_06C0[x]++;
}

/* ---- adapters ---- */

static void Run_Hopper(Regs *r)
{
    Obj_Hopper(r->x);
}

static void Run_Walker(Regs *r)
{
    Obj_Walker(r->x);
}

static void Run_Thrower(Regs *r)
{
    Obj_Thrower(r->x);
}

static void Run_Shot(Regs *r)
{
    Obj_Shot(r->x);
}

static void Run_Watcher(Regs *r)
{
    Obj_Watcher(r->x);
}

static void Run_Crawler(Regs *r)
{
    Obj_Crawler(r->x);
}

static void Run_Patroller(Regs *r)
{
    Obj_Patroller(r->x);
}

static void Run_Charger(Regs *r)
{
    Obj_Charger(r->x);
}

static void Run_Leaper(Regs *r)
{
    Obj_Leaper(r->x);
}

static void Run_SpawnDust(Regs *r)
{
    Obj_SpawnDust(r->x);
}

static void Run_Pacer(Regs *r)
{
    Obj_Pacer(r->x);
}

static void Run_LedgeWalker(Regs *r)
{
    Obj_LedgeWalker(r->x);
}

static void Run_Bouncer(Regs *r)
{
    Obj_Bouncer(r->x);
}

static void Run_BlockKicker(Regs *r)
{
    Obj_BlockKicker(r->x);
}

const DecompRoutine gObjWalkerRoutines[] = {
    { DT_CODE_KIND_HOPPER, NULL, "Obj_Hopper", Run_Hopper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_WALKER, NULL, "Obj_Walker", Run_Walker, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_THROWER, NULL, "Obj_Thrower", Run_Thrower, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_SHOT, NULL, "Obj_Shot", Run_Shot, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_WATCHER, NULL, "Obj_Watcher", Run_Watcher, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CRAWLER, NULL, "Obj_Crawler", Run_Crawler, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_BLOCK_KICKER, NULL, "Obj_BlockKicker", Run_BlockKicker, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_BOUNCER, NULL, "Obj_Bouncer", Run_Bouncer, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_LEDGE_WALKER, NULL, "Obj_LedgeWalker", Run_LedgeWalker, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_PACER, NULL, "Obj_Pacer", Run_Pacer, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_LEAPER, NULL, "Obj_Leaper", Run_Leaper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_LEAPER_DUST, NULL, "Obj_SpawnDust", Run_SpawnDust, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CHARGER, NULL, "Obj_Charger", Run_Charger, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_PATROLLER, NULL, "Obj_Patroller", Run_Patroller, 0, .bank = DT_BANK_OBJECTS },
};
const int gObjWalkerRoutineCount = sizeof gObjWalkerRoutines / sizeof gObjWalkerRoutines[0];
