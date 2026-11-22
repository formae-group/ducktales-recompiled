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

/* ---- sprite builder (bank 5 $8000, docs/rendering.md "Sprites") ----
 * Read by the presentation compositor (host/ducktales_sprites.c) to rebuild
 * the sprite list without the console's limits; all verified by
 * tools/render_oam.py check. */
#define DT_SPR_BANK              5       /* PRG bank holding the builder's tables */
#define DT_SPR_SCRIPT_LO         0xB1C7  /* animation script pointer by type ($0620) */
#define DT_SPR_SCRIPT_HI         0xB257
#define DT_SPR_META_LO           0x9681  /* metasprite pointer by id */
#define DT_SPR_META_HI           0x9779
#define DT_SPR_OFFSET_LO         0xADE1  /* dY, dX table pointer by offset-table id (minus 2) */
#define DT_SPR_OFFSET_HI         0xAEC1
#define DT_ADDR_SPR_PHASE        0x0095  /* incremented by the builder: bit 0 slot order, bit 1 blink */
#define DT_ADDR_HUD_GATE         0x007F  /* bit 7: the HUD groups may be drawn */
#define DT_ADDR_MESSAGE          0x006C  /* non-zero: a message is shown (no HUD sprites) */
#define DT_ADDR_ENDING           0x00AC  /* non-zero: no HUD sprites (ending) */
#define DT_ADDR_PAUSE            0x00E6  /* non-zero: no HUD sprites (pause) */
#define DT_ADDR_MODE             0x0039  /* $0F: no HUD sprites, animations frozen */
#define DT_MODE_FROZEN           0x0F
#define DT_ADDR_CAMERA_X         0x00F2  /* camera X low, page in DT_ADDR_CAMERA_PAGE */
#define DT_ADDR_CAMERA_PAGE      0x00F3
#define DT_ADDR_LAND             0x00F6  /* 0-4 lands, 5 Land Select, 6+ other screens */
#define DT_LAND_HUD_TABLE_OFF    9       /* $F6 value whose top of frame keeps sprites at $0000 */
#define DT_HUD_MIN_SCROOGE_Y     0x44    /* the builder adds the HUD only with Scrooge's Y >= this */
#define DT_HUD_BAND_Y            0x30    /* objects but slot 0 lose sprites above this unless $0600 bit 4 */
#define DT_ROW_FRAME             0x0610  /* animation frame, bit 7 = loop point */
#define DT_ROW_STATUS            0x0700  /* bit 7 live, bit 5 world coordinates, bit 4 hidden */
#define DT_ROW_POS_X             0x0720  /* X: world low byte with status bit 5, else screen */
#define DT_ROW_POS_PAGE          0x0730  /* world X page */
#define DT_ADDR_HUD_GROUP_HP     0x0340  /* the three HUD sprite groups, in builder order */
#define DT_ADDR_HUD_GROUP_LIVES  0x0358
#define DT_ADDR_HUD_GROUP_TIME   0x0140

/* ---- code sites (fixed bank 7 at $C000-$FFFF) ---- */

/* The instruction right after the jump start copied DT_ADDR_JUMP_VY{,_FRAC}
 * into slot 0's velocity: the hook site of the jump experiment. It is reached
 * only by falling through from those two stores. config/game.toml declares it
 * as a [[mod_function_hook]] with a content key, so another revision fails
 * code generation instead of firing at the wrong code. */
#define DT_SITE_JUMP_START       0xE3FE
#define DT_SITE_JUMP_START_ID    "ducktales.jump-scale"
