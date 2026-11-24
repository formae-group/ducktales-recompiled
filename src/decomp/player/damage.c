/*
 * player/damage.c - Scrooge hurt, knocked back, stuck, falling into a pit,
 * carried and dying, and the modes 9, 12 and 14 (docs/PLAYER.md).
 */
#include "decomp/player/player_internal.h"

/* $EB5E without its event: on Easy only every other hit counts; the last
 * full HP dot is marked lost. */
void Player_MarkDamage(void)
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
void Player_TakeDamage(void)
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
void Player_Hurt(void)
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
void Player_Knockback(uint8_t x)
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
void Player_Stuck(void)
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
void Player_Pit(void)
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
void Player_Mode9(void)
{
    Decomp_Enter(DT_CODE_PLAYER_MODE9, 0, 0, 0);
    if (gPadNew & DT_PAD_A) {
        gPlayerMode = PLAYER_MODE_GROUND;
    }
    gObjects->animTimer[P_] = 0;
}

/* Modes 10 and 11, $EBD9: carried (hypothesis); Down drops, A jumps off in
 * mode 11, and a wall under the carrier knocks Scrooge back. */
void Player_Carried(void)
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
void Player_Mode12(void)
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
void Player_Death(void)
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
void Player_Mode14(void)
{
    Decomp_Enter(DT_CODE_PLAYER_MODE14, 0, 0, 0);
    gObjects->animTimer[P_] = 0;
}
