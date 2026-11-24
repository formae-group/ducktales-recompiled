/*
 * player/contact.c - Scrooge's contact tests with the objects after a
 * horizontal ($D202) or vertical ($D291) move (docs/PLAYER.md).
 */
#include "decomp/player/player_internal.h"

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
uint8_t Player_ContactH(void)
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
bool Player_ContactV(void)
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
