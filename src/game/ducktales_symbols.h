/*
 * ducktales_symbols.h - every revision-specific address the port's own code
 * uses, for DuckTales (USA), PRG SHA-256 8ba8baed...e2013 (docs/rom.md).
 *
 * This is the only file that names addresses. Each entry points at the
 * document that holds its evidence; "hypothesis" marks the ones that still
 * need experiments. Nothing here is code or data from the ROM.
 */
#pragma once

/* ---- zero page (docs/memory-map.md) ---- */
#define DT_ADDR_PAD_NEW          0x0014  /* controller 1, newly pressed this frame (verified) */
#define DT_ADDR_PAD_HELD         0x0016  /* controller 1, held (verified) */
#define DT_ADDR_FRAME_COUNTER    0x0092  /* incremented by every NMI (verified) */
#define DT_ADDR_BANK             0x00E7  /* PRG bank mapped at $8000 (verified) */
#define DT_ADDR_DIFFICULTY       0x00F9  /* 0 Easy, 1 Normal, 2 Difficult (verified) */

/* Controller bits, the layout of DT_ADDR_PAD_NEW / DT_ADDR_PAD_HELD. */
#define DT_PAD_A       0x80
#define DT_PAD_B       0x40
#define DT_PAD_SELECT  0x20
#define DT_PAD_START   0x10
#define DT_PAD_UP      0x08
#define DT_PAD_DOWN    0x04
#define DT_PAD_LEFT    0x02
#define DT_PAD_RIGHT   0x01

/* ---- object tables: one 16-byte row per attribute, column = slot ----
 * Slot 0 is Scrooge (docs/memory-map.md "Object tables"). */
#define DT_OBJ_SLOTS             16
#define DT_SLOT_SCROOGE          0
#define DT_ROW_FLAGS             0x0600  /* hypothesis: non-zero for live slots */
#define DT_ROW_ACTION            0x0620  /* hypothesis: action/animation state */
#define DT_ROW_Y                 0x0640  /* Y on screen (verified) */
#define DT_ROW_X                 0x0650  /* X on screen (verified) */
#define DT_ROW_Y_FRAC            0x0660  /* Y fraction: the velocity fraction is subtracted from it (verified) */
#define DT_ROW_VY_FRAC           0x07B0  /* vertical velocity, fraction byte, up positive (verified) */
#define DT_ROW_VY                0x07C0  /* vertical velocity, whole pixels, signed, up positive (verified) */

/* Scrooge's jump take-off velocity (8.8, up positive). Stored by the land
 * set-up code in the fixed bank and copied into slot 0's velocity when a
 * jump starts (verified, docs/architecture.md "Jump experiment"). */
#define DT_ADDR_JUMP_VY_FRAC     0x0790
#define DT_ADDR_JUMP_VY          0x07A0

/* ---- HUD (docs/memory-map.md "Other RAM") ---- */
#define DT_ADDR_MONEY_DIGITS     0x0324  /* 6 digit tiles, most significant first (verified) */
#define DT_MONEY_DIGIT_COUNT     6
#define DT_MONEY_TILE_BLANK      0x24    /* suppressed leading zero (verified) */
#define DT_ADDR_HUD_SPRITES      0x0340  /* HUD sprite records: Y, tile, attribute, X (verified) */
#define DT_HUD_SPRITE_RECORDS    10      /* $0340-$0367 */
#define DT_HUD_RECORD_END_Y      0xFF    /* Y that ends a group of records (verified) */
#define DT_HP_DOT_LOST           0x01    /* attribute of a lost HP dot; 0 = full (verified) */
#define DT_ADDR_LIVES            0x0361  /* extra lives, also the HUD digit tile (verified) */

/* ---- code sites (fixed bank 7 at $C000-$FFFF) ---- */

/* The instruction right after the jump start copied DT_ADDR_JUMP_VY{,_FRAC}
 * into slot 0's velocity: the hook site of the jump experiment. It is reached
 * only by falling through from those two stores. config/game.toml declares it
 * as a [[mod_function_hook]] with a content key, so another revision fails
 * code generation instead of firing at the wrong code. */
#define DT_SITE_JUMP_START       0xE3FE
#define DT_SITE_JUMP_START_ID    "ducktales.jump-scale"
