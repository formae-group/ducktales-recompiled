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
    uint8_t unk_07F0[OBJ_SLOTS];
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
