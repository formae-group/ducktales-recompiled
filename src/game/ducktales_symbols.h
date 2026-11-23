/*
 * ducktales_symbols.h - every revision-specific address the port's own code
 * uses, for DuckTales (USA), PRG SHA-256 8ba8baed...e2013 (docs/rom.md).
 *
 * This is the only file that names addresses. Each entry points at the
 * document that holds its evidence; "hypothesis" marks the ones that still
 * need experiments. Nothing here is code or data from the ROM. The typed game
 * API (include/ducktales/) and the decompiled routines (src/decomp/) are
 * built on these names.
 */
#pragma once

/* ---- zero page (docs/memory-map.md, docs/game-state.md) ---- */
#define DT_ADDR_SCRATCH0         0x0000  /* scratch: jump vector low, sound request's saved X (verified, code) */
#define DT_ADDR_SCRATCH1         0x0001  /* scratch: jump vector high, sound request's id (verified, code) */
#define DT_ADDR_TEMP08           0x0008  /* scratch: Y before a vertical move, X before a horizontal one (code) */
#define DT_ADDR_TEMP09           0x0009  /* scratch: world X page before a horizontal move (code) */
#define DT_ADDR_PROBE_HITS       0x0010  /* background probe result, bit 0 = solid (verified, player.md) */
#define DT_ADDR_CONTACT_DEPTH    0x0011  /* overlap left by the object contact tests $D202/$D291 (code) */
#define DT_ADDR_PAD_NEW          0x0014  /* controller 1, newly pressed this frame (verified) */
#define DT_ADDR_PAD_HELD         0x0016  /* controller 1, held (verified) */
#define DT_ADDR_PAD2_HELD        0x0017  /* controller 2, held (hypothesis: read by the row drop at $E70B) */
#define DT_ADDR_MAP_GRID         0x0020  /* pointer to the current map's screen grid (verified, levels.md) */
#define DT_ADDR_STREAM_SCREEN    0x0024  /* screen index being streamed (verified) */
#define DT_ADDR_STREAM_POS       0x0026  /* stream position (verified) */
#define DT_ADDR_STREAM_DIR       0x0029  /* stream direction last used (hypothesis) */
#define DT_ADDR_MAP_ROWS         0x002A  /* rows in the current map (verified) */
#define DT_ADDR_MAP_STRIDE       0x002B  /* row stride of the grid, 8 (verified) */
#define DT_ADDR_CAMERA_SCREEN    0x002D  /* screen index of the camera: row * 8 + page (verified) */
#define DT_ADDR_MOVE_DIR         0x0038  /* direction of the last horizontal move, pad bits (hypothesis) */
#define DT_ADDR_PUSH_COUNTER     0x003E  /* frames left walking into a wall before the push pose (verified) */
#define DT_ADDR_PUSH_DIR         0x003F  /* direction held when the push pose started (verified) */
#define DT_ADDR_PROBE_FIRST      0x0040  /* first background probe point to test (verified) */
#define DT_ADDR_PROBE_COUNT      0x0041  /* probe points to test, minus one (verified) */
#define DT_ADDR_PROBE_CLASS      0x0042  /* collision class per probe tested, $42 + remaining count (code) */
#define DT_ADDR_FEET_CLASS       0x0043  /* class of the middle feet probe (verified) */
#define DT_ADDR_CLIMB_CLASS      0x0045  /* class under the climbing probe (code) */
#define DT_ADDR_MOVE_SIDE        0x004B  /* side the last horizontal move went: 1 right, 2 left (code) */
#define DT_ADDR_EASY_HITS        0x005F  /* Easy difficulty: hits taken; only every other one costs a dot (verified) */
#define DT_ADDR_SCENE_STEP       0x006E  /* message or scene step for mode $0F (hypothesis) */
#define DT_ADDR_HAZARD           0x0070  /* hazard request from special tiles (code) */
#define DT_ADDR_SLIPPERY         0x0071  /* slippery floor under Scrooge this frame (hypothesis) */
#define DT_ADDR_MOMENTUM_DIR     0x0072  /* direction of slippery-floor momentum (code) */
#define DT_ADDR_PUSH_RIGHT       0x0073  /* one-frame push of 1.5 px (hypothesis) */
#define DT_ADDR_POWER            0x0075  /* invincibility power-up running (hypothesis) */
#define DT_ADDR_POWER_FRAMES     0x0076  /* its 16-bit frame counter, low byte first (hypothesis) */
#define DT_ADDR_FINAL_ARENA      0x007B  /* 5 = final arena (verified) */
#define DT_ADDR_BOSS_CHECKPOINT  0x007C  /* 5 in the boss-checkpoint screen (verified) */
#define DT_ADDR_INVULNERABLE     0x007D  /* frames of invulnerability after a hit (verified) */
#define DT_ADDR_RESPAWN          0x007E  /* respawn/warp request, bit 7 + table index (verified) */
#define DT_ADDR_TREASURES        0x00A0  /* land treasures, bit n = land n (verified bits 0-4) */
#define DT_ADDR_PROGRESS_A1      0x00A1  /* persistent progress flags (hypothesis) */
#define DT_ADDR_KEY_ITEMS        0x00A3  /* key items, bit 1 = African Mines key (verified bit 1) */
#define DT_ADDR_LAND_REDIRECT    0x00A4  /* positive: load Transylvania, negative: Land Select (verified) */
#define DT_ADDR_POGO_CEILING     0x00A7  /* non-zero: no pogo bounce below Y $A0 (code; meaning unknown) */
#define DT_ADDR_TICK_FRAMES      0x00B0  /* frames until the HUD time drops by one (verified) */
#define DT_TICK_FRAMES_RELOAD    0x3C    /* what the timer reloads it with: 60 frames (verified) */
#define DT_ADDR_BOSS_HITS        0x00B1  /* boss hits left, 5 at the start of a fight (verified) */
#define DT_ADDR_MUSIC            0x00C5  /* last music requested through $F752 (verified) */
#define DT_ADDR_SOUND_WRITE      0x00C6  /* sound request ring: write index (verified) */
#define DT_ADDR_SOUND_RING       0x00C8  /* sound request ring: 8 entries (verified) */
#define DT_SOUND_RING_EMPTY      0x88    /* an empty ring entry (verified) */
#define DT_ADDR_BANK             0x00E7  /* PRG bank mapped at $8000 (verified) */
#define DT_ADDR_GRAVITY          0x00EF  /* gravity used by $F2B5 (verified) */
#define DT_ADDR_MAP_ROW          0x00F1  /* current map row (verified) */
#define DT_ADDR_DIFFICULTY       0x00F9  /* 0 Easy, 1 Normal, 2 Difficult (verified) */
#define DT_ADDR_BOSS_FIGHT       0x00FB  /* bit 7 = boss fight (verified) */
#define DT_ADDR_FRAME_COUNTER    0x0092  /* incremented by every NMI (verified) */
#define DT_ADDR_SCRATCH2         0x0002  /* scratch (code) */
#define DT_ADDR_PAD2_NEW         0x0015  /* controller 2, newly pressed (code, read_controllers) */
#define DT_ADDR_RANDOM           0x00C0  /* 32-bit shift register stepped by $C2E6 (code) */
#define DT_ADDR_BLANKED          0x00E8  /* non-zero: picture off, the NMI skips its PPU work (verified) */
#define DT_ADDR_SPLIT            0x00EA  /* sprite-0 HUD split enabled, $40 in play (verified) */
#define DT_ADDR_PPU_MASK         0x00FE  /* PPUMASK shadow (verified) */
#define DT_ADDR_PPU_CTRL         0x00FF  /* PPUCTRL shadow (verified) */
#define DT_ADDR_PALETTE_MASTER   0x0100  /* master palette, the fades' source (verified) */
#define DT_ADDR_PALETTE          0x0300  /* palette buffer the NMI uploads (verified) */
#define DT_ADDR_CHR_LIST         0x0002  /* scratch $02/$03 of $C301: the CHR list */
#define DT_ADDR_CHR_PAGES        0x0004  /* scratch of $C301: pages left, minus one */
#define DT_ADDR_CHR_SOURCE       0x0005  /* scratch $05/$06 of $C301: source page */
#define DT_ADDR_CHR_INDEX        0x0007  /* scratch of $C301: offset in the list */
#define DT_ADDR_CHR_BANK         0x0010  /* scratch of $C301: the record's PRG bank */
#define DT_ADDR_VRAM_PENDING     0x0019  /* non-zero: the NMI uploads the $0400 queue (verified) */
#define DT_ADDR_ROW_CHR_STATE    0x0050  /* row sprite-CHR stream: 0 start, bit 1 mid-record, negative done (verified) */
#define DT_ADDR_ROW_CHR_BANK     0x0051  /* its record's PRG bank (code) */
#define DT_ADDR_ROW_CHR_LIST     0x0052  /* $52/$53: its CHR list (code) */
#define DT_ADDR_ROW_CHR_SOURCE   0x0054  /* $54/$55: source page (code) */
#define DT_ADDR_ROW_CHR_PPU      0x0056  /* $56/$57: PPU address of the next packet (code) */
#define DT_ADDR_ROW_CHR_OFFSET   0x0058  /* offset of the next record in the list (code) */
#define DT_ADDR_ROW_CHR_BYTE     0x0059  /* offset in the source page (code) */
#define DT_ADDR_ROW_CHR_PAGES    0x005A  /* pages left in the record, minus one (code) */
#define DT_ADDR_TOTAL_DIGITS     0x0334  /* TOTAL money digit tiles (verified) */
#define DT_ADDR_VRAM_QUEUE       0x0400  /* VRAM update queue (verified) */

/* Controller bits, the layout of DT_ADDR_PAD_NEW / DT_ADDR_PAD_HELD. */
#define DT_PAD_A       0x80
#define DT_PAD_B       0x40
#define DT_PAD_SELECT  0x20
#define DT_PAD_START   0x10
#define DT_PAD_UP      0x08
#define DT_PAD_DOWN    0x04
#define DT_PAD_LEFT    0x02
#define DT_PAD_RIGHT   0x01

/* Page 1 below this is data, not stack: the master palette $0100-$011F, the
 * consumed-record bits $0120-$013F and the HUD time group $0140-$015F
 * (verified, memory-map.md, game-state.md). */
#define DT_ADDR_STACK_FLOOR      0x0160

/* ---- object tables: one 16-byte row per attribute, column = slot ----
 * Slot 0 is Scrooge (docs/memory-map.md "Object tables", docs/player.md). */
#define DT_OBJ_SLOTS             16
#define DT_SLOT_SCROOGE          0
#define DT_OBJ_BASE              0x0600
#define DT_ROW_FLAGS             0x0600  /* bit 6 facing right (verified); non-zero for live slots (hypothesis) */
#define DT_ROW_ACTION            0x0620  /* action/animation state (verified for slot 0) */
#define DT_ROW_ANIM_TIMER        0x0630  /* animation frame timer (hypothesis) */
#define DT_ROW_Y                 0x0640  /* Y on screen (verified) */
#define DT_ROW_X                 0x0650  /* X on screen (verified) */
#define DT_ROW_Y_FRAC            0x0660  /* Y fraction: the velocity fraction is subtracted from it (verified) */
#define DT_ROW_X_FRAC            0x0670  /* world X fraction (verified) */
#define DT_ROW_POSE              0x0680  /* pose variant set with crouch and landing (code) */
#define DT_ROW_TIMER             0x06B0  /* per-object countdown; Scrooge's cane recoil (code) */
#define DT_ROW_BOSS_COOLDOWN     0x06F0  /* frames before a boss can be hit again (code) */
#define DT_ROW_KIND              0x0710  /* object type the cane test reads (code, player.md) */
#define DT_ROW_SPEED_FRAC        0x0760  /* horizontal speed fraction (verified) */
#define DT_ROW_SPEED             0x0770  /* horizontal speed, whole pixels (verified) */
#define DT_ROW_VDIR              0x0780  /* Scrooge: 8 rising, 4 falling or standing (verified) */
#define DT_ROW_VY_FRAC           0x07B0  /* vertical velocity, fraction byte, up positive (verified) */
#define DT_ROW_VY                0x07C0  /* vertical velocity, whole pixels, signed, up positive (verified) */
#define DT_ROW_HITBOX            0x07D0  /* hitbox entry: bit 7 solid, bits 4-6 harmful (verified) */
#define DT_ROW_DIR               0x07E0  /* per-object direction (code) */

/* Scrooge's jump take-off velocity (8.8, up positive). Stored by the land
 * set-up code in the fixed bank and copied into slot 0's velocity when a
 * jump starts (verified, docs/architecture.md "Jump experiment"). */
#define DT_ADDR_JUMP_VY_FRAC     0x0790
#define DT_ADDR_JUMP_VY          0x07A0

/* ---- HUD (docs/memory-map.md "Other RAM") ---- */
#define DT_ADDR_TIME_DIGITS      0x0151  /* HUD time hundreds; tens at +4, ones at +8 (verified) */
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
#define DT_ADDR_OAM              0x0200  /* OAM shadow, 64 entries of Y, tile, attribute, X */
#define DT_ADDR_SPR_SLOT         0x0096  /* slot being drawn */
#define DT_ADDR_OAM_NEXT         0x0097  /* offset of the next free OAM entry; 0 = full */
#define DT_ADDR_SPR_SCRIPT       0x0000  /* scratch $00/$01: the slot's animation script */
#define DT_ADDR_SPR_META         0x0002  /* scratch $02/$03: the frame's metasprite */
#define DT_ADDR_SPR_LEFT         0x0004  /* scratch: sprites left to place, minus one */
#define DT_ADDR_SPR_OFFSETS      0x0005  /* scratch $05/$06: the dY, dX table, minus 2 */
#define DT_ADDR_SPR_PRIORITY     0x0007  /* scratch: attribute bit 5 from $0600 bit 2 */
#define DT_ADDR_SPR_FLIP         0x0010  /* scratch: attribute bit 6 from $0600 bit 6 */
#define DT_ADDR_SPR_FLAGS        0x0011  /* scratch: 1 when flipped (offset-table step), then $0600 */
#define DT_ADDR_SPR_Y            0x0012  /* scratch: the slot's screen Y */
#define DT_ADDR_SPR_X            0x0013  /* scratch: the slot's screen X */

/* ---- code sites (fixed bank 7 at $C000-$FFFF) ---- */
/* ---- code (fixed bank 7 at $C000-$FFFF) ----
 * Routines the decompilation names or calls (docs/functions.md,
 * docs/player.md, docs/decompilation.md). config/game.toml declares a
 * [[mod_function_hook]] with a content key at every one the port hooks. */
#define DT_CODE_BG_PROBE         0xD120  /* background probes $40.. against the collision map */
#define DT_CODE_CONTACT_H        0xD202  /* Scrooge-object contact while moving horizontally */
#define DT_CODE_CONTACT_V        0xD291  /* Scrooge-object contact while moving vertically */
#define DT_CODE_SNAP_RIGHT       0xD49A  /* snap world X back after a probe hit on the right */
#define DT_CODE_SNAP_LEFT        0xD4B2  /* snap world X back after a probe hit on the left */
#define DT_CODE_SNAP_HEAD        0xD4C8  /* push Y down below the tile the head probe hit */
#define DT_CODE_SNAP_FEET        0xD4D6  /* snap Y onto the tile the feet probe hit */
#define DT_CODE_SELECT_REFILL    0xE2FE  /* Select on the ground (TOTAL for a lost HP dot; hypothesis) */
#define DT_CODE_PLAYER_UPDATE    0xE354  /* Scrooge's per-frame entry */
#define DT_CODE_MODE_LO          0xE396  /* mode handler table, low bytes (17 entries) */
#define DT_CODE_MODE_HI          0xE3A7  /* mode handler table, high bytes */
#define DT_CODE_PLAYER_GROUND    0xE3B8  /* mode 0 */
#define DT_CODE_JUMP_START       0xE3F2  /* copy the take-off velocity, mode 1, air handler */
#define DT_CODE_PLAYER_WALK      0xE482  /* horizontal speed and movement */
#define DT_CODE_MOVE_RIGHT       0xE593  /* move right with object and wall tests */
#define DT_CODE_MOVE_LEFT        0xE5C3  /* move left with object and wall tests */
#define DT_CODE_PLAYER_AIR       0xE63B  /* mode 1 */
#define DT_CODE_GRAB_CLIMB       0xE740  /* grab a climbable tile while Up is held */
#define DT_CODE_PLAYER_POGO      0xE7EA  /* mode 2 */
#define DT_CODE_PLAYER_CLIMB     0xE806  /* mode 3 */
#define DT_CODE_PLAYER_CANE      0xE8EB  /* mode 4 */
#define DT_CODE_PLAYER_FALL      0xE970  /* falling or standing: gravity, move, contacts, probes */
#define DT_CODE_PLAYER_RISE      0xE9BA  /* rising: jump cut, move, contacts, probes */
#define DT_CODE_PROBE_VERTICAL   0xEA5E  /* feet or head probes after a vertical move */
#define DT_CODE_PLAYER_HURT      0xEAB9  /* mode 5 */
#define DT_CODE_PLAYER_KNOCKBACK 0xEB00  /* mode 6 */
#define DT_CODE_TAKE_DAMAGE      0xEB5E  /* mark the last full HP dot lost */
#define DT_CODE_PLAYER_STUCK     0xEB82  /* mode 7 */
#define DT_CODE_PLAYER_PIT       0xEBA7  /* mode 8 */
#define DT_CODE_PIT_DEATH        0xEBB4  /* request the respawn that costs a life */
#define DT_CODE_PLAYER_MODE9     0xEBC9  /* mode 9 */
#define DT_CODE_PLAYER_CARRIED   0xEBD9  /* modes 10 and 11 */
#define DT_CODE_PLAYER_MODE12    0xEC2D  /* mode 12 */
#define DT_CODE_PLAYER_DEATH     0xEC9B  /* mode 13 */
#define DT_CODE_PLAYER_MODE14    0xECFF  /* mode 14 */
#define DT_CODE_MOVE_WITH_WALLS  0xF0F8  /* move horizontally with wall probes (code) */
#define DT_CODE_CLIMB_DOWN       0xF192  /* climbing step down with probes (code) */
#define DT_CODE_CLIMB_UP         0xF1A8  /* climbing step up with probes (code) */
#define DT_CODE_MOVE_RIGHT_SPEED 0xF234  /* world X += speed (X = slot) */
#define DT_CODE_MOVE_LEFT_SPEED  0xF252  /* world X -= speed (X = slot) */
#define DT_CODE_APPLY_VY         0xF2A1  /* Y -= vertical velocity (X = slot) */
#define DT_CODE_APPLY_GRAVITY    0xF2B5  /* vertical velocity -= gravity (X = slot) */
#define DT_CODE_MUSIC_REQUEST    0xF752  /* remember the music id in $C5, then queue it */
#define DT_CODE_SOUND_REQUEST    0xF754  /* queue a sound id in the request ring */
#define DT_CODE_OAM_BUILD        0x8000  /* bank 5: the sprite builder */
#define DT_CODE_PPU_BLANK        0xC197  /* picture off, $E8 + 1 */
#define DT_CODE_PPU_UNBLANK      0xC1A1  /* picture on, $E8 - 1 */
#define DT_CODE_READ_CONTROLLERS 0xC1AB  /* read both pads into $14-$17 */
#define DT_CODE_FILL_NAMETABLE   0xC203  /* fill a nametable while blanked */
#define DT_CODE_OAM_CLEAR        0xC267  /* park the OAM shadow's entries */
#define DT_CODE_RANDOM_NEXT      0xC2E6  /* step the shift register $C0-$C3 */
#define DT_CODE_CHR_LOAD_LIST    0xC301  /* upload a CHR list while blanked */
#define DT_CODE_PALETTE_SCENE    0xF76D  /* load a scene's palettes */
#define DT_CODE_BANK_SELECT      0xFFDE  /* map bank A at $8000 and record it in $E7 */
#define DT_CODE_BANK_SELECT_TEMP 0xFFE0  /* map bank A without recording it */
#define DT_CODE_BANK_TABLE       0xFFE5  /* bus-conflict table: byte n holds n */
#define DT_CODE_CHR_STREAM_ROW   0xC42C  /* stream the row's sprite CHR, 64 bytes per call */
#define DT_CODE_DRAW_STRINGS     0xBAB0  /* bank 5: draw a string set while blanked */
#define DT_TABLE_ROW_BASE        0xC508  /* first row entry of each land in the next two tables */
#define DT_TABLE_ROW_CHR_LIST    0xC511  /* row CHR list number; bit 7 = two lists by camera page */
#define DT_TABLE_ROW_PALETTE     0xC53A  /* row sprite palette 3 number */
#define DT_TABLE_ROW_LISTS_LO    0xC563  /* row CHR list pointers */
#define DT_TABLE_ROW_LISTS_HI    0xC583
#define DT_TABLE_ROW_PALETTES    0xC6D9  /* sprite palette 3 bytes, 4 per number */
#define DT_TABLE_STRINGS_LO      0xBAFA  /* bank 5: string set pointers */
#define DT_TABLE_STRINGS_HI      0xBB06
#define DT_TABLE_CHR_LIST_LO     0xC358  /* CHR list pointers by list number */
#define DT_TABLE_CHR_LIST_HI     0xC363
#define DT_TABLE_SCENE_BG        0xE127  /* scene background palette: index of its last byte */
#define DT_TABLE_SCENE_SPR       0xE12D  /* scene sprite palette: index of its last byte, bit 7 = palette 3 too */
#define DT_TABLE_SCENE_PALETTES  0xE143  /* the scene palette bytes */
#define DT_TABLE_SCENE_SPRITE_PAL3 0xE1A3 /* sprite palette 3 by $F9, 4 bytes each */

/* ---- sound driver of bank 3 (docs/audio.md "Driver RAM") ---- */
#define DT_SND_BANK               3       /* PRG bank of the driver and its data */
#define DT_ADDR_SND_PRIORITY      0x00D0  /* high nibble playing effect, low nibble playing music (verified) */
#define DT_ADDR_SND_OLD_MASK      0x00D1  /* previous effect channel mask while an effect starts */
#define DT_ADDR_SND_HEADER        0x00D2  /* $D2/$D3: header being started */
#define DT_ADDR_SND_BUSY          0x00D4  /* set while a request runs; the update skips the channels */
#define DT_ADDR_SND_COUNT         0x00D5  /* request scratch: channel counter 4..1 */
#define DT_ADDR_SND_REG           0x00D6  /* request scratch: APU register offset */
#define DT_ADDR_SND_SPEED         0x00D7  /* sequencer ticks per frame, 0 or 1 = normal (verified) */
#define DT_ADDR_SND_FADE          0x00D8  /* global fade: bits 0-6 frames per step, bit 7 out (verified) */
#define DT_ADDR_SND_FADE_STEP     0x00D9  /* fade step 1-16 */
#define DT_ADDR_SND_FRAME         0x00DA  /* driver frame counter */
#define DT_ADDR_SND_APU_REG       0x00DB  /* APU register offset of the channel being updated */
#define DT_ADDR_SND_RECORD        0x00DC  /* $DC/$DD: record being updated ($DC alone: its offset in a request) */
#define DT_ADDR_SND_CHANNEL       0x00DE  /* channel countdown: 4 pulse 1, 3 pulse 2, 2 triangle, 1 noise */
#define DT_ADDR_SND_EFFECT_MASK   0x00DF  /* channels the effect owns, rotated during an update (verified) */
#define DT_ADDR_SND_EFFECT_STREAM 0x00E0  /* $E0/$E1: effect stream (verified) */
#define DT_ADDR_SND_EFFECT_WAIT   0x00E2  /* frames until the effect stream is read again (verified) */
#define DT_ADDR_SND_EFFECT_LOOP   0x00E3  /* effect loop counter */
#define DT_ADDR_SND_TEMP          0x00E4  /* $E4/$E5: scratch (volume, voice offset, pointers) */
#define DT_ADDR_SND_CHANNELS      0x0370  /* four 31-byte channel records */
#define DT_ADDR_SND_INSTRUMENTS   0x03EC  /* $03EC/$03ED: the song's instrument table */
#define DT_SND_RECORD_SIZE        0x1F
#define DT_CODE_SOUND_UPDATE      0x8000  /* bank 3: the per-frame update (JMP $823D) */
#define DT_CODE_SOUND_PLAY        0x8003  /* bank 3: the request handler */
#define DT_TABLE_SOUNDS           0x8A40  /* bank 3: header pointer by id */
#define DT_TABLE_NOTE_LENGTHS     0x896B  /* bank 3: units by note length bits */
#define DT_TABLE_DOTTED_LENGTHS   0x8973  /* bank 3: the same, dotted */
#define DT_TABLE_PERIODS          0x897B  /* bank 3: 96 NTSC periods */
#define DT_TABLE_MUSIC_COMMANDS   0x87BB  /* bank 3: music command handlers */
#define DT_TABLE_EFFECT_COMMANDS  0x85C3  /* bank 3: effect command handlers */

/* ---- hook sites of the event system (src/game/events.c) ----
 * config/game.toml declares each with a length/crc32 content key, so another
 * revision fails code generation instead of firing at the wrong code. */

/* Entry of the damage routine (functions.md `damage`): marks the last full
 * HP dot lost (every other hit on Easy). Reached only by JSR, from $EAD0 and
 * $EB22; the instruction before it is a JMP, so nothing falls into it. */
#define DT_SITE_DAMAGE            0xEB5E
#define DT_SITE_DAMAGE_ID         "ducktales.event.player-damaged"

/* Inside the HUD timer (`timer_tick`, $E28C): the decrement of the
 * frames-per-tick counter $B0, after the time-out check. */
#define DT_SITE_TIMER_TICK        0xE2A6
#define DT_SITE_TIMER_TICK_ID     "ducktales.event.timer-tick"

/* In `death_or_warp` ($DC3E): the only instruction in the ROM that decrements
 * the lives counter $0361, reached after a death when lives are not 0. */
#define DT_SITE_LIFE_LOST         0xDCB0
#define DT_SITE_LIFE_LOST_ID      "ducktales.event.life-lost"

/* In the title set-up of the main task ($D4E6): the first instruction after
 * the loop that copies the 13-byte HUD template into $0358-$0364, which sets
 * the lives counter $0361 to 2. Runs after every power-on, reset and game
 * over (game over jumps to the reset vector). */
#define DT_SITE_NEW_GAME          0xD59C
#define DT_SITE_NEW_GAME_ID       "ducktales.event.new-game"

/* The instruction right after the jump start copied DT_ADDR_JUMP_VY{,_FRAC}
 * into slot 0's velocity. It is reached only by falling through from those
 * two stores. */
#define DT_SITE_JUMP_START        0xE3FE
#define DT_SITE_JUMP_START_ID     "ducktales.event.jump-start"

/* In the boss hit routine ($F6D7, called by the boss objects of bank 6 and
 * by $FA44): the decrement of the boss hits left $B1, reached only when the
 * boss's cooldown $06F0,X is 0, i.e. for a hit that counts
 * (docs/decompilation.md "Events"). */
#define DT_SITE_BOSS_HIT          0xF6DD
#define DT_SITE_BOSS_HIT_ID       "ducktales.event.boss-hit"

/* The RTI of the NMI handler ($C000), its only exit: replace mode spends
 * there the cycles a replaced routine still owed when the NMI came
 * (docs/decompilation.md "Timing in replace mode"). */
#define DT_SITE_NMI_EXIT          0xC138
#define DT_SITE_NMI_EXIT_ID       "ducktales.decomp.nmi-exit"
