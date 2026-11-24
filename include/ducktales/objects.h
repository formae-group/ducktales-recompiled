/*
 * ducktales/objects.h - the object tables at $0600-$07FF.
 *
 * The game keeps objects as a structure of arrays: each 16-byte row is one
 * attribute and the column is the object slot (0-15); slot 0 is Scrooge. The
 * overlay below names the rows in that layout, so gObjects->y[slot] is the
 * byte the game reads as LDA $0640,X. A row whose meaning is not known keeps
 * its address as its name (unk_XXXX). Field status follows
 * docs/memory-map.md: "verified" rows are named in ducktales_symbols.h with
 * that status; the rest are hypotheses.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "ducktales/ram.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBJ_SLOTS DT_OBJ_SLOTS

/* $0700 status bits (docs/rendering.md, docs/decompilation.md "Objects"). */
#define OBJ_STATUS_LIVE   0x80 /* the slot is updated and drawn (verified) */
#define OBJ_STATUS_WORLD  0x20 /* X is a world position (verified) */
#define OBJ_STATUS_HIDDEN 0x10 /* not drawn (verified) */

/* Object kinds ($0710), the index of the handler table. */
#define OBJ_KIND_NONE            0x00 /* the handler is a bare RTS (code) */
#define OBJ_KIND_HOPPER          0x01 /* enemy: walks and hops (code) */
#define OBJ_KIND_WALKER          0x02 /* enemy: walks, turns at walls (code) */
#define OBJ_KIND_THROWER         0x03 /* enemy: walks, hops, throws a shot (code) */
#define OBJ_KIND_SHOT            0x04 /* kind 3's shot (code) */
#define OBJ_KIND_WATCHER         0x05 /* enemy: stays, faces Scrooge (code) */
#define OBJ_KIND_DANGLER         0x06 /* enemy: climbs to the ceiling, drops on Scrooge (code) */
#define OBJ_KIND_SNAPPER         0x07 /* plant that catches Scrooge (code) */
#define OBJ_KIND_BLOCK           0x08 /* block: solid to other objects, launched or opened by the cane (code) */
#define OBJ_KIND_THROWN          0x09 /* a launched block (code) */
#define OBJ_KIND_DROPPER         0x0A /* block that falls when Scrooge comes near (code) */
#define OBJ_KIND_DEFEATED        0x0B /* a beaten enemy flying off (code) */
#define OBJ_KIND_CRAWLER         0x0C /* enemy: waits for Scrooge to pass, walks (code) */
#define OBJ_KIND_FLYER           0x0D /* enemy: weaving flyer (code) */
#define OBJ_KIND_SWING           0x0E /* swinging hold Scrooge grabs with Up (code) */
#define OBJ_KIND_LIFT            0x0F /* platform that rises and sinks (code) */
#define OBJ_KIND_CHARGER         0x10 /* enemy: charges at Scrooge, bounces off walls and blocks (code) */
#define OBJ_KIND_SWOOPER         0x11 /* enemy: drops to Scrooge's height and weaves (code) */
#define OBJ_KIND_BLOCK_DWELLER   0x12 /* an enemy that lives in a block (code) */
#define OBJ_KIND_PICKUP          0x13 /* an item Scrooge takes by touching it (code) */
#define OBJ_KIND_HIDDEN_ITEM     0x14 /* an item that appears when touched (code) */
#define OBJ_KIND_CHEST           0x15 /* a chest the cane opens (code) */
#define OBJ_KIND_PATROLLER       0x18 /* enemy: walks a ledge, turns at its edges and at walls (code) */
#define OBJ_KIND_POUNCER         0x19 /* enemy: flies in arcs, pounces on Scrooge (code) */
#define OBJ_KIND_PIT_JUMPER      0x1A /* enemy: jumps out of a pit with a splash (code) */
#define OBJ_KIND_BOUNCER         0x1B /* enemy: bounces towards Scrooge (code) */
#define OBJ_KIND_LEAPER          0x1C /* enemy: walks a ledge, turns and leaps with a puff of dust (code) */
#define OBJ_KIND_LEDGE_WALKER    0x1E /* enemy: walks a ledge, turns at its edges (code) */
#define OBJ_KIND_PACER           0x1F /* enemy: paces left and right (code) */
#define OBJ_KIND_BOBBER          0x20 /* enemy: bobs up and down in place (code) */
#define OBJ_KIND_ROLLER          0x23 /* something that rolls left on the floor (code) */
#define OBJ_KIND_BOULDER         0x24 /* a boulder that rolls right and pushes Scrooge (code) */
#define OBJ_KIND_CEILING_HOPPER  0x25 /* an enemy that drops from the ceiling and hops at Scrooge (code) */
#define OBJ_KIND_RISING_SHOT     0x27 /* a shot that flies up and sideways (code) */
#define OBJ_KIND_MINES_BOSS      0x28 /* the African Mines boss (code) */
#define OBJ_KIND_MOON_BOSS       0x29 /* the Moon boss (code) */
#define OBJ_KIND_HIMALAYAS_BOSS  0x2A /* the Himalayas boss (code) */
#define OBJ_KIND_DROPPING        0x2B /* something that drops and breaks on the floor (code) */
#define OBJ_KIND_TREAT_SPOUT     0x2C /* throws out four treats when Scrooge is near (code) */
#define OBJ_KIND_TREAT           0x2D /* a treat that gives an HP dot back (code) */
#define OBJ_KIND_AMAZON_BOSS     0x2E /* the Amazon boss (code) */
#define OBJ_KIND_DOOR            0x2F /* a door: warp request (code) */
#define OBJ_KIND_THIEF           0x30 /* enemy: paces, takes money on contact (code) */
#define OBJ_KIND_SHORT_THIEF     0x31 /* kind $30 with a shorter span (code) */
#define OBJ_KIND_PICKPOCKET      0x32 /* enemy: walks at Scrooge, takes money on contact (code) */
#define OBJ_KIND_TARGET          0x33 /* an object only a hit affects: it is beaten (code) */
#define OBJ_KIND_LAUNCHER        0x34 /* a launcher that throws Scrooge up (code) */
#define OBJ_KIND_TALKER          0x35 /* a character that shows a message (code) */
#define OBJ_KIND_MOVING_PLATFORM 0x39 /* a platform that carries Scrooge (code) */
#define OBJ_KIND_FINAL_STAND_IN  0x3A /* what the spawner makes of an object in the final arena (code) */
#define OBJ_KIND_KEY_SPOT        0x41 /* a spot that needs the land's key item (code) */

/* $0700 bits 0-3: the handler's step (code). */
#define OBJ_STATE_STEP 0x0F

/* $0620 poses ($0620 of a pickup is its item) used by the handlers (code). */
#define OBJ_ACTION_CHEST          0x1B /* a block that holds an item */
#define OBJ_ACTION_STAY           0x22 /* a block that a hit leaves in place */
#define OBJ_ACTION_BREAK          0x2A /* debris, and a pickup jumping out */
#define OBJ_ACTION_BREAK_HIT      0x2B /* debris after a counted hit */
#define OBJ_ACTION_LAND_CHEST     0x3F /* a block whose item comes from the land's table */
#define OBJ_ITEM_FIRST            0x23 /* items $23-$29 index the take table at $8BFE */
#define OBJ_ITEM_RESPAWNS         0x29 /* gives its record back when it leaves the screen */
#define OBJ_ITEM_EXTRA_DOT        0x2B /* one more HP dot */
#define OBJ_ITEM_BIG_DIAMOND      0x61
#define OBJ_ITEM_TREASURE         0x6F /* $6F + n: treasure bit n of $A0 */
#define OBJ_ITEM_TREASURE_MESSAGE 0x74

/* $0600 flags. */
#define OBJ_FLAG_ON_SCREEN     0x80 /* set by the sprite builder; contact tests skip slots without it (code) */
#define OBJ_FLAG_FACING_RIGHT  0x40 /* (verified) */
#define OBJ_FLAG_DELETE_AT_END 0x08 /* freed when its animation ends (verified, rendering.md) */
#define OBJ_FLAG_BLINK         0x02 /* drawn every other frame (verified, rendering.md) */

/* $07E0 direction of the walking kinds: bit 0 right (code). */
#define OBJ_DIR_RIGHT 1
#define OBJ_DIR_LEFT  2

typedef struct ObjectTable {
    uint8_t flags[OBJ_SLOTS];     /* $0600 bit 6 facing right (verified); bit 2 set by special tiles (code) */
    uint8_t animFrame[OBJ_SLOTS]; /* $0610 animation frame of the action (hypothesis) */
    uint8_t action[OBJ_SLOTS];    /* $0620 action; Scrooge: PlayerAction (verified) */
    uint8_t animTimer[OBJ_SLOTS]; /* $0630 frame timer of the action (hypothesis) */
    uint8_t y[OBJ_SLOTS];         /* $0640 Y on screen (verified) */
    uint8_t x[OBJ_SLOTS];         /* $0650 X on screen (verified) */
    uint8_t yFrac[OBJ_SLOTS];     /* $0660 Y fraction (verified) */
    uint8_t xFrac[OBJ_SLOTS];     /* $0670 world X fraction (verified) */
    uint8_t pose[OBJ_SLOTS];      /* $0680 pose variant (code) */
    uint8_t unk_0690[OBJ_SLOTS];  /* $0690 incremented by contact while invincible (code) */
    uint8_t unk_06A0[OBJ_SLOTS];
    uint8_t timer[OBJ_SLOTS]; /* $06B0 countdown; Scrooge: cane recoil (code) */
    uint8_t unk_06C0[OBJ_SLOTS];
    uint8_t unk_06D0[OBJ_SLOTS];
    uint8_t unk_06E0[OBJ_SLOTS];
    uint8_t bossCooldown[OBJ_SLOTS]; /* $06F0 frames before a boss can be hit again (code) */
    uint8_t state[OBJ_SLOTS];        /* $0700 sub-state; Scrooge: death step (code) */
    uint8_t kind[OBJ_SLOTS];         /* $0710 object type read by the cane test (code) */
    uint8_t worldX[OBJ_SLOTS];       /* $0720 world X low byte (verified) */
    uint8_t worldPage[OBJ_SLOTS];    /* $0730 world X page (verified) */
    uint8_t unk_0740[OBJ_SLOTS];
    uint8_t unk_0750[OBJ_SLOTS];
    uint8_t speedFrac[OBJ_SLOTS]; /* $0760 horizontal speed fraction (verified) */
    uint8_t speed[OBJ_SLOTS];     /* $0770 horizontal speed (verified) */
    uint8_t vdir[OBJ_SLOTS];      /* $0780 Scrooge: 8 rising, 4 falling (verified) */
    uint8_t unk_0790[OBJ_SLOTS];  /* $0790 slot 0: jump take-off velocity fraction (verified) */
    uint8_t unk_07A0[OBJ_SLOTS];  /* $07A0 slot 0: jump take-off velocity (verified) */
    uint8_t vyFrac[OBJ_SLOTS];    /* $07B0 vertical velocity fraction, up positive (verified) */
    uint8_t vy[OBJ_SLOTS];        /* $07C0 vertical velocity, signed, up positive (verified) */
    uint8_t hitbox[OBJ_SLOTS];    /* $07D0 hitbox entry (verified) */
    uint8_t dir[OBJ_SLOTS];       /* $07E0 direction (code) */
    uint8_t record[OBJ_SLOTS];    /* $07F0 land record the slot was spawned from, $FF none (verified, levels.md) */
} ObjectTable;

#define OBJ_ROW_CHECK(field, addr) \
    typedef char obj_row_check_##field[offsetof(ObjectTable, field) == (addr) - DT_OBJ_BASE ? 1 : -1]
OBJ_ROW_CHECK(flags, DT_ROW_FLAGS);
OBJ_ROW_CHECK(animFrame, DT_ROW_FRAME);
OBJ_ROW_CHECK(action, DT_ROW_ACTION);
OBJ_ROW_CHECK(animTimer, DT_ROW_ANIM_TIMER);
OBJ_ROW_CHECK(y, DT_ROW_Y);
OBJ_ROW_CHECK(x, DT_ROW_X);
OBJ_ROW_CHECK(yFrac, DT_ROW_Y_FRAC);
OBJ_ROW_CHECK(xFrac, DT_ROW_X_FRAC);
OBJ_ROW_CHECK(pose, DT_ROW_POSE);
OBJ_ROW_CHECK(timer, DT_ROW_TIMER);
OBJ_ROW_CHECK(bossCooldown, DT_ROW_BOSS_COOLDOWN);
OBJ_ROW_CHECK(state, DT_ROW_STATUS);
OBJ_ROW_CHECK(kind, DT_ROW_KIND);
OBJ_ROW_CHECK(worldX, DT_ROW_POS_X);
OBJ_ROW_CHECK(worldPage, DT_ROW_POS_PAGE);
OBJ_ROW_CHECK(speedFrac, DT_ROW_SPEED_FRAC);
OBJ_ROW_CHECK(speed, DT_ROW_SPEED);
OBJ_ROW_CHECK(vdir, DT_ROW_VDIR);
OBJ_ROW_CHECK(unk_0790, DT_ADDR_JUMP_VY_FRAC);
OBJ_ROW_CHECK(unk_07A0, DT_ADDR_JUMP_VY);
OBJ_ROW_CHECK(vyFrac, DT_ROW_VY_FRAC);
OBJ_ROW_CHECK(vy, DT_ROW_VY);
OBJ_ROW_CHECK(hitbox, DT_ROW_HITBOX);
OBJ_ROW_CHECK(dir, DT_ROW_DIR);
OBJ_ROW_CHECK(record, DT_ROW_RECORD);
typedef char obj_table_size_check[sizeof(ObjectTable) == 0x200 ? 1 : -1];

#define gObjects ((ObjectTable *)(gRam + DT_OBJ_BASE))

/* 8.8 values split over two rows (whole byte in one, fraction in another). */
static inline uint16_t Obj_GetVelocityY(int slot)
{
    return (uint16_t)(gObjects->vy[slot] << 8 | gObjects->vyFrac[slot]);
}

static inline void Obj_SetVelocityY(int slot, uint16_t v)
{
    gObjects->vy[slot] = (uint8_t)(v >> 8);
    gObjects->vyFrac[slot] = (uint8_t)v;
}

static inline uint16_t Obj_GetSpeed(int slot)
{
    return (uint16_t)(gObjects->speed[slot] << 8 | gObjects->speedFrac[slot]);
}

static inline void Obj_SetSpeed(int slot, uint16_t v)
{
    gObjects->speed[slot] = (uint8_t)(v >> 8);
    gObjects->speedFrac[slot] = (uint8_t)v;
}

static inline uint16_t Obj_GetWorldX(int slot)
{
    return (uint16_t)(gObjects->worldPage[slot] << 8 | gObjects->worldX[slot]);
}

static inline void Obj_SetWorldX(int slot, uint16_t v)
{
    gObjects->worldPage[slot] = (uint8_t)(v >> 8);
    gObjects->worldX[slot] = (uint8_t)v;
}

#ifdef __cplusplus
}
#endif
