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
#define DT_ADDR_SCRATCH0        0x0000 /* scratch: jump vector low, sound request's saved X (verified, code) */
#define DT_ADDR_SCRATCH1        0x0001 /* scratch: jump vector high, sound request's id (verified, code) */
#define DT_ADDR_TEMP08          0x0008 /* scratch: Y before a vertical move, X before a horizontal one (code) */
#define DT_ADDR_TEMP09          0x0009 /* scratch: world X page before a horizontal move (code) */
#define DT_ADDR_PROBE_HITS      0x0010 /* background probe result, bit 0 = solid (verified, player.md) */
#define DT_ADDR_CONTACT_DEPTH   0x0011 /* overlap left by the object contact tests $D202/$D291 (code) */
#define DT_ADDR_PAD_NEW         0x0014 /* controller 1, newly pressed this frame (verified) */
#define DT_ADDR_PAD_HELD        0x0016 /* controller 1, held (verified) */
#define DT_ADDR_PAD2_HELD       0x0017 /* controller 2, held (hypothesis: read by the row drop at $E70B) */
#define DT_ADDR_MAP_GRID        0x0020 /* pointer to the current map's screen grid (verified, levels.md) */
#define DT_ADDR_STREAM_SCREEN   0x0024 /* screen index being streamed (verified) */
#define DT_ADDR_STREAM_POS      0x0026 /* stream position (verified) */
#define DT_ADDR_STREAM_DIR      0x0029 /* stream direction last used (hypothesis) */
#define DT_ADDR_MAP_ROWS        0x002A /* rows in the current map (verified) */
#define DT_ADDR_MAP_STRIDE      0x002B /* row stride of the grid, 8 (verified) */
#define DT_ADDR_CAMERA_SCREEN   0x002D /* screen index of the camera: row * 8 + page (verified) */
#define DT_ADDR_MOVE_DIR        0x0038 /* direction of the last horizontal move, pad bits (hypothesis) */
#define DT_ADDR_PUSH_COUNTER    0x003E /* frames left walking into a wall before the push pose (verified) */
#define DT_ADDR_PUSH_DIR        0x003F /* direction held when the push pose started (verified) */
#define DT_ADDR_PROBE_FIRST     0x0040 /* first background probe point to test (verified) */
#define DT_ADDR_PROBE_COUNT     0x0041 /* probe points to test, minus one (verified) */
#define DT_ADDR_PROBE_CLASS     0x0042 /* collision class per probe tested, $42 + remaining count (code) */
#define DT_ADDR_FEET_CLASS      0x0043 /* class of the middle feet probe (verified) */
#define DT_ADDR_CLIMB_CLASS     0x0045 /* class under the climbing probe (code) */
#define DT_ADDR_MOVE_SIDE       0x004B /* side the last horizontal move went: 1 right, 2 left (code) */
#define DT_ADDR_EASY_HITS       0x005F /* Easy difficulty: hits taken; only every other one costs a dot (verified) */
#define DT_ADDR_SCENE_STEP      0x006E /* message or scene step for mode $0F (hypothesis) */
#define DT_ADDR_HAZARD          0x0070 /* hazard request from special tiles (code) */
#define DT_ADDR_SLIPPERY        0x0071 /* slippery floor under Scrooge this frame (hypothesis) */
#define DT_ADDR_MOMENTUM_DIR    0x0072 /* direction of slippery-floor momentum (code) */
#define DT_ADDR_PUSH_RIGHT      0x0073 /* one-frame push of 1.5 px (hypothesis) */
#define DT_ADDR_POWER           0x0075 /* invincibility power-up running (hypothesis) */
#define DT_ADDR_POWER_FRAMES    0x0076 /* its 16-bit frame counter, low byte first (hypothesis) */
#define DT_ADDR_FINAL_ARENA     0x007B /* 5 = final arena (verified) */
#define DT_ADDR_BOSS_CHECKPOINT 0x007C /* 5 in the boss-checkpoint screen (verified) */
#define DT_ADDR_INVULNERABLE    0x007D /* frames of invulnerability after a hit (verified) */
#define DT_ADDR_RESPAWN         0x007E /* respawn/warp request, bit 7 + table index (verified) */
#define DT_ADDR_TREASURES       0x00A0 /* land treasures, bit n = land n (verified bits 0-4) */
#define DT_ADDR_PROGRESS_A1     0x00A1 /* persistent progress flags (hypothesis) */
#define DT_ADDR_KEY_ITEMS       0x00A3 /* key items, bit 1 = African Mines key (verified bit 1) */
#define DT_ADDR_LAND_REDIRECT   0x00A4 /* positive: load Transylvania, negative: Land Select (verified) */
#define DT_ADDR_POGO_CEILING    0x00A7 /* non-zero: no pogo bounce below Y $A0 (code; meaning unknown) */
#define DT_ADDR_TICK_FRAMES     0x00B0 /* frames until the HUD time drops by one (verified) */
#define DT_TICK_FRAMES_RELOAD   0x3C   /* what the timer reloads it with: 60 frames (verified) */
#define DT_ADDR_BOSS_HITS       0x00B1 /* boss hits left, 5 at the start of a fight (verified) */
#define DT_ADDR_MUSIC           0x00C5 /* last music requested through $F752 (verified) */
#define DT_ADDR_SOUND_WRITE     0x00C6 /* sound request ring: write index (verified) */
#define DT_ADDR_SOUND_RING      0x00C8 /* sound request ring: 8 entries (verified) */
#define DT_SOUND_RING_EMPTY     0x88   /* an empty ring entry (verified) */
#define DT_ADDR_BANK            0x00E7 /* PRG bank mapped at $8000 (verified) */
#define DT_ADDR_GRAVITY         0x00EF /* gravity used by $F2B5 (verified) */
#define DT_ADDR_MAP_ROW         0x00F1 /* current map row (verified) */
#define DT_ADDR_DIFFICULTY      0x00F9 /* 0 Easy, 1 Normal, 2 Difficult (verified) */
#define DT_ADDR_BOSS_FIGHT      0x00FB /* bit 7 = boss fight (verified) */
#define DT_ADDR_FRAME_COUNTER   0x0092 /* incremented by every NMI (verified) */

/* Controller bits, the layout of DT_ADDR_PAD_NEW / DT_ADDR_PAD_HELD. */
#define DT_PAD_A      0x80
#define DT_PAD_B      0x40
#define DT_PAD_SELECT 0x20
#define DT_PAD_START  0x10
#define DT_PAD_UP     0x08
#define DT_PAD_DOWN   0x04
#define DT_PAD_LEFT   0x02
#define DT_PAD_RIGHT  0x01

/* Page 1 below this is data, not stack: the master palette $0100-$011F, the
 * consumed-record bits $0120-$013F and the HUD time group $0140-$015F, which
 * the game-over box ($DEB9, 33 bytes) replaces up to its end mark at $0160
 * (verified, memory-map.md, game-state.md). */
#define DT_ADDR_STACK_FLOOR 0x0161

/* ---- object tables: one 16-byte row per attribute, column = slot ----
 * Slot 0 is Scrooge (docs/memory-map.md "Object tables", docs/player.md). */
#define DT_OBJ_SLOTS         16
#define DT_SLOT_SCROOGE      0
#define DT_OBJ_BASE          0x0600
#define DT_ROW_FLAGS         0x0600 /* bit 6 facing right (verified); non-zero for live slots (hypothesis) */
#define DT_ROW_ACTION        0x0620 /* action/animation state (verified for slot 0) */
#define DT_ROW_ANIM_TIMER    0x0630 /* animation frame timer (hypothesis) */
#define DT_ROW_Y             0x0640 /* Y on screen (verified) */
#define DT_ROW_X             0x0650 /* X on screen (verified) */
#define DT_ROW_Y_FRAC        0x0660 /* Y fraction: the velocity fraction is subtracted from it (verified) */
#define DT_ROW_X_FRAC        0x0670 /* world X fraction (verified) */
#define DT_ROW_POSE          0x0680 /* pose variant set with crouch and landing (code) */
#define DT_ROW_TIMER         0x06B0 /* per-object countdown; Scrooge's cane recoil (code) */
#define DT_ROW_BOSS_COOLDOWN 0x06F0 /* frames before a boss can be hit again (code) */
#define DT_ROW_KIND          0x0710 /* object type the cane test reads (code, player.md) */
#define DT_ROW_SPEED_FRAC    0x0760 /* horizontal speed fraction (verified) */
#define DT_ROW_SPEED         0x0770 /* horizontal speed, whole pixels (verified) */
#define DT_ROW_VDIR          0x0780 /* Scrooge: 8 rising, 4 falling or standing (verified) */
#define DT_ROW_VY_FRAC       0x07B0 /* vertical velocity, fraction byte, up positive (verified) */
#define DT_ROW_VY            0x07C0 /* vertical velocity, whole pixels, signed, up positive (verified) */
#define DT_ROW_HITBOX        0x07D0 /* hitbox entry: bit 7 solid, bits 4-6 harmful (verified) */
#define DT_ROW_DIR           0x07E0 /* per-object direction (code) */
#define DT_ROW_RECORD        0x07F0 /* land record of the slot, $FF none (verified, levels.md) */

/* Scrooge's jump take-off velocity (8.8, up positive). Stored by the land
 * set-up code in the fixed bank and copied into slot 0's velocity when a
 * jump starts (verified, docs/architecture.md "Jump experiment"). */
#define DT_ADDR_JUMP_VY_FRAC 0x0790
#define DT_ADDR_JUMP_VY      0x07A0

/* ---- HUD (docs/memory-map.md "Other RAM") ---- */
#define DT_ADDR_TIME_DIGITS   0x0151 /* HUD time hundreds; tens at +4, ones at +8 (verified) */
#define DT_ADDR_MONEY_DIGITS  0x0324 /* 6 digit tiles, most significant first (verified) */
#define DT_MONEY_DIGIT_COUNT  6
#define DT_MONEY_TILE_BLANK   0x24   /* suppressed leading zero (verified) */
#define DT_ADDR_HUD_SPRITES   0x0340 /* HUD sprite records: Y, tile, attribute, X (verified) */
#define DT_HUD_SPRITE_RECORDS 10     /* $0340-$0367 */
#define DT_HUD_RECORD_END_Y   0xFF   /* Y that ends a group of records (verified) */
#define DT_HP_DOT_LOST        0x01   /* attribute of a lost HP dot; 0 = full (verified) */
#define DT_ADDR_LIVES         0x0361 /* extra lives, also the HUD digit tile (verified) */

/* ---- sprite builder (bank 5 $8000, docs/rendering.md "Sprites") ----
 * Read by the presentation compositor (host/ducktales_sprites.c) to rebuild
 * the sprite list without the console's limits; all verified by
 * tools/render_oam.py check. */
#define DT_SPR_BANK             5      /* PRG bank holding the builder's tables */
#define DT_SPR_SCRIPT_LO        0xB1C7 /* animation script pointer by type ($0620) */
#define DT_SPR_SCRIPT_HI        0xB257
#define DT_SPR_META_LO          0x9681 /* metasprite pointer by id */
#define DT_SPR_META_HI          0x9779
#define DT_SPR_OFFSET_LO        0xADE1 /* dY, dX table pointer by offset-table id (minus 2) */
#define DT_SPR_OFFSET_HI        0xAEC1
#define DT_ADDR_SPR_PHASE       0x0095 /* incremented by the builder: bit 0 slot order, bit 1 blink */
#define DT_ADDR_HUD_GATE        0x007F /* bit 7: the HUD groups may be drawn */
#define DT_ADDR_MESSAGE         0x006C /* non-zero: a message is shown (no HUD sprites) */
#define DT_ADDR_ENDING          0x00AC /* non-zero: no HUD sprites (ending) */
#define DT_ADDR_PAUSE           0x00E6 /* non-zero: no HUD sprites (pause) */
#define DT_ADDR_MODE            0x0039 /* $0F: no HUD sprites, animations frozen */
#define DT_MODE_FROZEN          0x0F
#define DT_ADDR_CAMERA_X        0x00F2 /* camera X low, page in DT_ADDR_CAMERA_PAGE */
#define DT_ADDR_CAMERA_PAGE     0x00F3
#define DT_ADDR_LAND            0x00F6 /* 0-4 lands, 5 Land Select, 6+ other screens */
#define DT_LAND_HUD_TABLE_OFF   9      /* $F6 value whose top of frame keeps sprites at $0000 */
#define DT_HUD_MIN_SCROOGE_Y    0x44   /* the builder adds the HUD only with Scrooge's Y >= this */
#define DT_HUD_BAND_Y           0x30   /* objects but slot 0 lose sprites above this unless $0600 bit 4 */
#define DT_ROW_FRAME            0x0610 /* animation frame, bit 7 = loop point */
#define DT_ROW_STATUS           0x0700 /* bit 7 live, bit 5 world coordinates, bit 4 hidden */
#define DT_ROW_POS_X            0x0720 /* X: world low byte with status bit 5, else screen */
#define DT_ROW_POS_PAGE         0x0730 /* world X page */
#define DT_ADDR_HUD_GROUP_HP    0x0340 /* the three HUD sprite groups, in builder order */
#define DT_ADDR_HUD_GROUP_LIVES 0x0358
#define DT_ADDR_HUD_GROUP_TIME  0x0140

/* ---- sound driver (bank 3, docs/audio.md) ----
 * Read by the mods' music and effect replacements (src/mods/ducktales/dt_mods_audio.c). */
#define DT_SOUND_BANK           3      /* PRG bank of the driver and its sound table */
#define DT_SOUND_TABLE          0x8A40 /* 2-byte header pointers by request id $00-$37 (verified) */
#define DT_SOUND_IDS            0x38
#define DT_SOUND_MUSIC_IDS      0x10   /* $00-$0F music, $10-$37 effects (verified) */
#define DT_ADDR_LAST_MUSIC      0x00C5 /* last music id requested through $F752 (verified) */
#define DT_ADDR_SOUND_RING_READ 0x00C7 /* the NMI's read index into the ring (verified) */
#define DT_ADDR_SOUND_RING      0x00C8 /* 8 request slots (verified) */
#define DT_SOUND_RING_EMPTY     0x88
#define DT_ADDR_SOUND_PRIORITY  0x00D0 /* high nibble: playing effect, low: playing music (verified) */
#define DT_ADDR_SOUND_EFFECT_CH 0x00DF /* channels the effect owns: bit 0 pulse 1 .. bit 3 noise (verified) */
#define DT_SOUND_STOP_MUSIC     0xFF
#define DT_SOUND_STOP_EFFECT    0xFE
#define DT_MUSIC_GAME_OVER      0x09 /* the two songs that end by themselves (verified) */
#define DT_MUSIC_DEATH          0x0A
#define DT_MUSIC_LAND_SELECT    0x05
#define DT_MUSIC_TITLE          0x07

/* ---- code sites (fixed bank 7 at $C000-$FFFF) ---- */
/* ---- code (fixed bank 7 at $C000-$FFFF) ----
 * Routines the decompilation names or calls (docs/functions.md,
 * docs/player.md, docs/decompilation.md). config/game.toml declares a
 * [[mod_function_hook]] with a content key at every one the port hooks. */
#define DT_CODE_BG_PROBE         0xD120 /* background probes $40.. against the collision map */
#define DT_CODE_CONTACT_H        0xD202 /* Scrooge-object contact while moving horizontally */
#define DT_CODE_CONTACT_V        0xD291 /* Scrooge-object contact while moving vertically */
#define DT_CODE_SNAP_RIGHT       0xD49A /* snap world X back after a probe hit on the right */
#define DT_CODE_SNAP_LEFT        0xD4B2 /* snap world X back after a probe hit on the left */
#define DT_CODE_SNAP_HEAD        0xD4C8 /* push Y down below the tile the head probe hit */
#define DT_CODE_SNAP_FEET        0xD4D6 /* snap Y onto the tile the feet probe hit */
#define DT_CODE_SELECT_REFILL    0xE2FE /* Select on the ground (TOTAL for a lost HP dot; hypothesis) */
#define DT_CODE_PLAYER_UPDATE    0xE354 /* Scrooge's per-frame entry */
#define DT_CODE_MODE_LO          0xE396 /* mode handler table, low bytes (17 entries) */
#define DT_CODE_MODE_HI          0xE3A7 /* mode handler table, high bytes */
#define DT_CODE_PLAYER_GROUND    0xE3B8 /* mode 0 */
#define DT_CODE_JUMP_START       0xE3F2 /* copy the take-off velocity, mode 1, air handler */
#define DT_CODE_PLAYER_WALK      0xE482 /* horizontal speed and movement */
#define DT_CODE_MOVE_RIGHT       0xE593 /* move right with object and wall tests */
#define DT_CODE_MOVE_LEFT        0xE5C3 /* move left with object and wall tests */
#define DT_CODE_PLAYER_AIR       0xE63B /* mode 1 */
#define DT_CODE_GRAB_CLIMB       0xE740 /* grab a climbable tile while Up is held */
#define DT_CODE_PLAYER_POGO      0xE7EA /* mode 2 */
#define DT_CODE_PLAYER_CLIMB     0xE806 /* mode 3 */
#define DT_CODE_PLAYER_CANE      0xE8EB /* mode 4 */
#define DT_CODE_PLAYER_FALL      0xE970 /* falling or standing: gravity, move, contacts, probes */
#define DT_CODE_PLAYER_RISE      0xE9BA /* rising: jump cut, move, contacts, probes */
#define DT_CODE_PROBE_VERTICAL   0xEA5E /* feet or head probes after a vertical move */
#define DT_CODE_PLAYER_HURT      0xEAB9 /* mode 5 */
#define DT_CODE_PLAYER_KNOCKBACK 0xEB00 /* mode 6 */
#define DT_CODE_TAKE_DAMAGE      0xEB5E /* mark the last full HP dot lost */
#define DT_CODE_PLAYER_STUCK     0xEB82 /* mode 7 */
#define DT_CODE_PLAYER_PIT       0xEBA7 /* mode 8 */
#define DT_CODE_PIT_DEATH        0xEBB4 /* request the respawn that costs a life */
#define DT_CODE_PLAYER_MODE9     0xEBC9 /* mode 9 */
#define DT_CODE_PLAYER_CARRIED   0xEBD9 /* modes 10 and 11 */
#define DT_CODE_PLAYER_MODE12    0xEC2D /* mode 12 */
#define DT_CODE_PLAYER_DEATH     0xEC9B /* mode 13 */
#define DT_CODE_PLAYER_MODE14    0xECFF /* mode 14 */
#define DT_CODE_MOVE_WITH_WALLS  0xF0F8 /* move horizontally with wall probes (code) */
#define DT_CODE_CLIMB_DOWN       0xF192 /* climbing step down with probes (code) */
#define DT_CODE_CLIMB_UP         0xF1A8 /* climbing step up with probes (code) */
#define DT_CODE_MOVE_RIGHT_SPEED 0xF234 /* world X += speed (X = slot) */
#define DT_CODE_MOVE_LEFT_SPEED  0xF252 /* world X -= speed (X = slot) */
#define DT_CODE_APPLY_VY         0xF2A1 /* Y -= vertical velocity (X = slot) */
#define DT_CODE_APPLY_GRAVITY    0xF2B5 /* vertical velocity -= gravity (X = slot) */
#define DT_CODE_MUSIC_REQUEST    0xF752 /* remember the music id in $C5, then queue it */
#define DT_CODE_SOUND_REQUEST    0xF754 /* queue a sound id in the request ring */

/* ---- game flow (docs/game-state.md, docs/levels.md, docs/rendering.md;
 * decompiled in src/decomp/flow.c) ---- */
#define DT_ADDR_SCRATCH4          0x0004 /* scratch: block steps for $C830 (verified, rendering.md) */
#define DT_ADDR_SCRATCH5          0x0005 /* scratch: VRAM queue offset for $C830 (code) */
#define DT_ADDR_SCRATCH10         0x0010 /* scratch: counters and pointers of the flow code (code) */
#define DT_ADDR_SCRATCH11         0x0011 /* scratch: screens left to draw at a respawn (code) */
#define DT_ADDR_PALETTE_REQUEST   0x0018 /* the NMI uploads $0300-$031F when set (verified) */
#define DT_ADDR_VRAM_PENDING      0x0019 /* the NMI draws the $0400 queue when set (verified) */
#define DT_ADDR_TILE_EDIT_PENDING 0x001A /* the NMI draws the $0440 queue when set (verified) */
#define DT_ADDR_STREAM_PAGE       0x002C /* page of the screen being streamed (verified) */
#define DT_ADDR_PREV_CAMERA_X     0x0025 /* previous frame's $F2 (verified) */
#define DT_ADDR_UNK_2E            0x002E /* cleared every frame by the frame loop (code; meaning unknown) */
#define DT_ADDR_HELD_SLOT \
    0x004F /* slot | $80 of a marked object, $3C when it despawns; cleared at land load, respawn and scenes (code; meaning unknown) */
#define DT_ADDR_ROW_CHR_STATE      0x0050 /* row sprite-CHR stream state, negative = done (verified) */
#define DT_ADDR_BLOCK_OPENED       0x005B /* a block was launched or uncovered this frame; cleared every frame (code) */
#define DT_ADDR_TILE_EDIT_OFFSET   0x005C /* write offset of the $0440 queue (verified) */
#define DT_ADDR_LAND_LEFT          0x005D /* land left: back to Land Select (verified) */
#define DT_ADDR_HUD_MONEY_DIRTY    0x0074 /* the frame loop redraws the money when set (code) */
#define DT_ADDR_SCENE              0x0078 /* special scene request: bit 7 + scene (verified) */
#define DT_ADDR_HUD_TOTAL_DIRTY    0x00AB /* the frame loop redraws TOTAL when set (code) */
#define DT_ADDR_NO_SPRITE_DMA      0x0098 /* the NMI skips sprite DMA while non-zero (verified) */
#define DT_ADDR_PPU_OFF            0x00E8 /* the NMI skips its PPU block while non-zero (verified) */
#define DT_ADDR_ROW_SCROLL         0x00E9 /* vertical row scroll running (hypothesis) */
#define DT_ADDR_HUD_SPLIT          0x00EA /* sprite-0 HUD split, $40 in play (verified) */
#define DT_ADDR_ROW_SCROLL_ADDR    0x00ED /* mid-frame $2006 low byte of the row scroll (hypothesis) */
#define DT_ADDR_ROW_SCROLL_ADDR_HI 0x00EE /* its high byte (hypothesis) */
#define DT_ADDR_SPRITE0_Y          0x00FA /* sprite-0 Y of the HUD split (verified, memory-map.md) */
#define DT_ADDR_TILESET_BANK       0x00F7 /* bank of the land's blocks and metatiles (verified) */
#define DT_ADDR_BG_OFF_BELOW_SPLIT 0x00F8 /* background off below the split (verified, code) */
#define DT_ADDR_HUD_MONEY_PACKET   0x0320 /* VRAM packet of the money: PPU address, count, digits (verified) */
#define DT_HUD_MONEY_PACKET_LEN    12
#define DT_ADDR_HUD_TOTAL_PACKET   0x0330 /* VRAM packet of TOTAL (verified) */
#define DT_HUD_TOTAL_PACKET_LEN    13
#define DT_ADDR_VRAM_QUEUE         0x0400 /* VRAM update queue (verified) */
#define DT_ADDR_PALETTE            0x0100 /* palette as loaded; $0300 is what the NMI uploads (verified) */
#define DT_ADDR_PALETTE_SHOWN      0x0300
#define DT_TIME_TILE_UP0           0x1E /* the time box's time-out word, three tiles (verified) */
#define DT_TIME_TILE_UP1           0x19
#define DT_TIME_TILE_UP2           0x24

/* Code of the flow and what it calls (bank 7 unless a bank is named). */
#define DT_CODE_RESET          0xFEE0
#define DT_CODE_TASK_YIELD     0xFFAC /* wait one frame */
#define DT_CODE_TASK_WAIT      0xFFA5 /* wait X frames */
#define DT_CODE_RTS            0xFFAB /* task_wait's RTS: the entry of task code's isolated runs */
#define DT_CODE_BANK_SELECT    0xFFDE
#define DT_CODE_BANK_TABLE     0xFFE5 /* bus-conflict table: bank n is written at $FFE5 + n */
#define DT_CODE_SCREEN_OFF     0xC197 /* $E8++, rendering off */
#define DT_CODE_SCREEN_ON      0xC1A1 /* $E8--, rendering on */
#define DT_CODE_NT_FILL        0xC203
#define DT_CODE_OAM_CLEAR      0xC267
#define DT_CODE_FADE_IN        0xC279
#define DT_CODE_FADE_OUT       0xC282
#define DT_CODE_RANDOM         0xC2E6 /* steps the generator at $C0 (code) */
#define DT_CODE_CHR_LOAD       0xC301
#define DT_CODE_ROW_CHR_STREAM 0xC42C
#define DT_CODE_DRAW_START     0xC71D
#define DT_CODE_DRAW_SCREEN    0xC78F
#define DT_CODE_MAP_HEADER     0xC7B0
#define DT_CODE_STREAM_BLOCK   0xC830
#define DT_CODE_CAMERA_UPDATE  0xEFE2
#define DT_CODE_CLEAR_OBJECTS  0xF2C7 /* clears slots 1-15 (code) */
#define DT_CODE_HP_REFILL      0xF812 /* every HP dot full, $5F = 0 (code) */
#define DT_BANK_SPRITES        5
#define DT_B5_BUILD_OAM        0x8000
#define DT_B5_PAUSE_TEXT       0xB580 /* pause or message text into the VRAM queue (code) */
#define DT_B5_PPU_STRINGS      0xBAB0 /* X = set: PPU address and data strings written to the PPU (code) */
#define DT_B5_CONSUMED_BITS    0xB760 /* the land's consumed-record bits $0120-$013F (code) */
#define DT_BANK_OBJECTS        6
#define DT_B6_OBJECTS          0x8000 /* every object's per-frame code */
#define DT_B6_SPAWN_TABLES     0xAE20 /* the land's spawn table pointers into $60-$69 (code) */
#define DT_B6_SPAWN_RESET      0xAE60
#define DT_B6_SPAWN            0xAEA0

#define DT_CODE_BOSS_TRIGGER  0xCF93
#define DT_CODE_LAND_SELECT   0xD630
#define DT_CODE_LAND_LOAD     0xD79B
#define DT_CODE_LAND_TABLES   0xD9CA
#define DT_CODE_LAND_PALETTE  0xD9F5 /* X = palette set * 16 */
#define DT_CODE_HUD_TEMPLATE  0xD60A
#define DT_CODE_FRAME_LOOP    0xDB36
#define DT_CODE_DEATH_OR_WARP 0xDC3E
#define DT_CODE_DEATH_PAUSE   0xDD23
#define DT_CODE_RESPAWN_PLACE 0xDD4E
#define DT_CODE_RESPAWN_ENTRY 0xDD52 /* A = entry */
#define DT_CODE_HUD_MONEY     0xDE6C
#define DT_CODE_HUD_TOTAL     0xDEA3
#define DT_CODE_GAME_OVER     0xDEB9
#define DT_CODE_LAND_EXIT     0xDF08
#define DT_CODE_SCENE         0xDF27
#define DT_CODE_ENDING        0xE1AF
#define DT_CODE_TIMER_TICK    0xE28C

/* Where the flow's task code resumes after a call (the address after the JSR). */
#define DT_FLOW_AFTER_ENDING       0xDB3D
#define DT_FLOW_AFTER_PLAYER       0xDB6F
#define DT_FLOW_AFTER_OBJECTS      0xDB7B
#define DT_FLOW_AFTER_CAMERA       0xDB8C
#define DT_FLOW_AFTER_BOSS_TRIGGER 0xDB8F
#define DT_FLOW_AFTER_DEATH        0xDBB0
#define DT_FLOW_FRAME_SPRITES      0xDBC5 /* also after a scene */
#define DT_FLOW_AFTER_SPAWN        0xDBE4
#define DT_FLOW_NEXT_FRAME         0xDC29
#define DT_FLOW_DEATH_FADE         0xDC68
#define DT_FLOW_DEATH_BLANK        0xDC6B
#define DT_FLOW_DEATH_PLACE        0xDC81
#define DT_FLOW_DEATH_PLACED       0xDC84
#define DT_FLOW_DEATH_ROW_CHR      0xDCBD
#define DT_FLOW_DEATH_SHOWN        0xDCD8
#define DT_FLOW_DEATH_FADE_IN      0xDCDB
#define DT_FLOW_DEATH_RESUME       0xDCE2
#define DT_FLOW_DEATH_SPAWNED      0xDD14
#define DT_FLOW_DEATH_DONE         0xDD1C
#define DT_FLOW_DEATH_PAUSE_NEXT   0xDD42
#define DT_FLOW_RESPAWN_DRAWN      0xDDAB
#define DT_FLOW_GAME_OVER_NEXT     0xDEDE
#define DT_FLOW_LAND_EXIT_NEXT     0xDF1E
#define DT_FLOW_LOAD_FADED         0xD79E
#define DT_FLOW_LOAD_BLACK         0xD7A5
#define DT_FLOW_LOAD_SCREEN_OFF    0xD7A8
#define DT_FLOW_LOAD_FILLED        0xD7B9
#define DT_FLOW_LOAD_DRAWN         0xD7C2
#define DT_FLOW_LOAD_STRINGS       0xD7CC
#define DT_FLOW_LOAD_CHR           0xD7D1
#define DT_FLOW_LOAD_REDRAWN       0xD7D4
#define DT_FLOW_LOAD_ROW_CHR       0xD7DB
#define DT_FLOW_LOAD_SHOWN         0xD837
#define DT_FLOW_LOAD_FADE_IN       0xD83A
#define DT_FLOW_LOAD_DONE          0xD845

/* Tables the flow reads from the ROM (bank 7). */
#define DT_DATA_LAND_TILESET      0xDA10 /* per land: tileset bank */
#define DT_DATA_LAND_SCREEN       0xDA19 /* per land: start screen index */
#define DT_DATA_LAND_ROW          0xDA22 /* per land: start row */
#define DT_DATA_LAND_START_X      0xDA2B /* per land: Scrooge's start X */
#define DT_DATA_PALETTES          0xDA34 /* 16 background colours per land or screen */
#define DT_DATA_SPRITE_PALETTE    0xDAC4 /* 16 sprite colours */
#define DT_DATA_HUD_MONEY         0xDAD4 /* the money packet's start value */
#define DT_DATA_HUD_TOTAL         0xDAE0 /* the TOTAL packet's start value */
#define DT_DATA_HUD_HP            0xDAED /* the HP dot records */
#define DT_HUD_HP_TEMPLATE_LEN    21
#define DT_DATA_HUD_TIME_GROUP    0xDB0F /* the HUD time group as a land starts it */
#define DT_HUD_TIME_GROUP_LEN     29
#define DT_DATA_LAND_TIME         0xDB2C /* per land: start time hundreds and tens */
#define DT_DATA_CHECKPOINT_ROW    0xDC2C /* per land: boss-checkpoint row and page */
#define DT_DATA_CHECKPOINT_PAGE   0xDC35
#define DT_DATA_RESPAWN           0xDDC1 /* 9-byte respawn and warp entries (levels.md) */
#define DT_DATA_GAME_OVER_SPRITES 0xDEE7
#define DT_GAME_OVER_SPRITES_LEN  33
/* ---- object system (bank 6, docs/decompilation.md "Objects") ----
 * The frame loop maps bank 6 and calls DT_B6_OBJECTS at $DB78, its only
 * caller (code). */
#define DT_ADDR_OBJ_CURSOR        0x004A /* slot whose handler runs (code) */
#define DT_CODE_OBJ_HANDLER_LO    0x802C /* handler address by kind ($0710), low bytes, 67 entries (code) */
#define DT_CODE_OBJ_HANDLER_HI    0x806F /* high bytes */
#define DT_ADDR_CONTACT_SLOT      0x0012 /* block slot a horizontal block test found, 0 none (code) */
#define DT_ADDR_CONSUMED          0x0120 /* consumed-record bits of the land (verified, game-state.md) */
#define DT_CODE_OBJ_WALK_LEFT     0xF13B /* move left with wall probes and blocks (code) */
#define DT_CODE_OBJ_FALL          0xF1BE /* gravity, move down, land on tiles or blocks (code) */
#define DT_CODE_OBJ_RISE          0xF1F9 /* move up, gravity, bump on tiles or blocks (code) */
#define DT_CODE_SPEED_TABLE       0xF330 /* 8.8 speeds by index, read by $F312 (code) */
#define DT_CODE_OBJ_TOUCH_SCROOGE 0xF436 /* contact between a slot and Scrooge; knock-back (code) */
#define DT_CODE_HITBOX_HEIGHT     0xF498 /* half-height by hitbox entry (code) */
#define DT_CODE_HITBOX_WIDTH      0xF4B8 /* half-width by hitbox entry (code) */
#define DT_CODE_OBJ_FACE_SCROOGE  0xF6BD /* $07E0 = direction towards Scrooge (code) */
#define DT_CODE_OBJ_FIND_FREE     0xF7BE /* highest free slot 15..1 (code) */
#define DT_CODE_OBJ_DISTANCE_X    0xF7DA /* |Scrooge's world X - the slot's|, low byte (code) */
#define DT_CODE_OBJ_MARK_CONSUMED 0xF7F0 /* set the slot's record bit in $0120 (verified, game-state.md) */
#define DT_CODE_OBJ_EDIT_TILE     0xF506 /* edit the metatile at the slot (rendering.md) */
#define DT_CODE_BIT_TABLE         0xF80A /* 1 << n, n = 0-7 (code) */
#define DT_CODE_LAND_BIT          0xD8C7 /* bit by land or message id (code) */
#define DT_ADDR_MESSAGE_SLOT      0x007A /* slot of the character whose message shows (code) */
#define DT_ADDR_LAUNCHPAD_RIDES   0x00A6 /* lands where the Launchpad ride was taken (hypothesis, game-state.md) */
#define DT_ADDR_MESSAGES_SHOWN    0x00A8 /* bit per message id already shown (hypothesis, game-state.md) */
#define DT_ADDR_EXTRA_DOT_TAKEN   0x00AE /* the extra HP dot was taken in the Himalayas (code) */
#define DT_ADDR_RANDOM            0x00C0 /* four random bytes, stepped by $C2E6 (code) */
/* Handlers by kind (bank 6 unless noted) and their tables. */
#define DT_CODE_HIT_HEIGHT           0x814A /* $80C1: half-heights by the thrower's hitbox (code) */
#define DT_CODE_HIT_WIDTH            0x816A /* $80C1: half-widths (code) */
#define DT_CODE_MOTION_VY            0x81A5 /* $818A: vertical velocity by index, 8.8 */
#define DT_CODE_MOTION_SPEED         0x81E5 /* $818A: speed by index, 8.8 */
#define DT_CODE_SNAPPER_HOLD_Y       0x8661 /* kind 7: Scrooge's Y offset when caught, by pose */
#define DT_CODE_SNAPPER_HOLD_POSE    0x8663 /* kind 7: Scrooge's pose when caught */
#define DT_CODE_SNAPPER_HITBOX       0x8665 /* kind 7: hitbox by animation frame */
#define DT_CODE_KIND_BLOCK           0x8668 /* kind 8 */
#define DT_CODE_KIND_THROWN          0x8722 /* kind 9 */
#define DT_CODE_THROWN_STEP_LO       0x88FC /* kind 9's code by step, low bytes (9 entries) */
#define DT_CODE_THROWN_STEP_HI       0x8905
#define DT_CODE_BLOCK_LAUNCH         0x890E /* launch velocity, speed by pose - $1A, 4 bytes each */
#define DT_CODE_KIND_DEFEATED        0x897B /* kind $0B */
#define DT_CODE_KIND_PICKUP          0x8A59 /* kind $13 */
#define DT_CODE_ITEM_TAKE_LO         0x8BFE /* what taking items $23-$29 does, low bytes */
#define DT_CODE_ITEM_TAKE_HI         0x8C05
#define DT_CODE_ITEM_SOUND           0x8C0C /* their sounds */
#define DT_CODE_KIND_HIDDEN_ITEM     0x8C45 /* kind $14 */
#define DT_CODE_KIND_CHEST           0x8D19 /* kind $15 */
#define DT_CODE_ITEM_BY_INDEX        0x8FB8 /* item by $06B0 */
#define DT_CODE_CHEST_ITEM           0x8FDA /* chest: 0 = no item shown, by land * 4 + $06B0 */
#define DT_CODE_CHEST_RISE           0x9002 /* chest: Y offset when opened */
#define DT_CODE_ITEM_BY_LAND         0x9016 /* item of a pose-$3F block by land * 3 + $06B0 */
#define DT_CODE_DROP_BY_KIND         0x9025 /* item a beaten enemy may drop, by kind */
#define DT_CODE_KIND_DOOR            0x9065 /* kind $2F */
#define DT_CODE_FLYER_PATH           0x9215 /* kind $0D: motion index by step, 14 entries */
#define DT_CODE_SWING_PATH           0x92ED /* kind $0E: motion index by step, bit 7 = long step, 36 entries */
#define DT_CODE_SWING_FRAMES         0x9311 /* kind $0E: frames per step, short and long */
#define DT_CODE_SWOOPER_PATH         0x9544 /* kind $11: motion index by step, 14 entries */
#define DT_CODE_KIND_LIFT            0x9093 /* kind $0F */
#define DT_CODE_KIND_TALKER          0xFDF5 /* kind $35 (fixed bank) */
#define DT_CODE_KIND_HOPPER          0x8225 /* kind 1 */
#define DT_CODE_KIND_WALKER          0x82AF /* kind 2 */
#define DT_CODE_KIND_THROWER         0x830E /* kind 3 */
#define DT_CODE_KIND_SHOT            0x8462 /* kind 4 */
#define DT_CODE_KIND_WATCHER         0x8491 /* kind 5 */
#define DT_CODE_KIND_DANGLER         0x84E5 /* kind 6 */
#define DT_CODE_KIND_SNAPPER         0x8567 /* kind 7 */
#define DT_CODE_KIND_DROPPER         0x8845 /* kind $0A */
#define DT_CODE_KIND_CRAWLER         0x90EA /* kind $0C */
#define DT_CODE_KIND_FLYER           0x916C /* kind $0D */
#define DT_CODE_KIND_SWING           0x9225 /* kind $0E */
#define DT_CODE_KIND_SWOOPER         0x9430 /* kind $11 */
#define DT_CODE_OBJ_FIND_FREE_Y      0x80B3 /* highest free slot 15..1 in Y */
#define DT_CODE_OBJ_HIT_OTHERS       0x80C1 /* the slot a thrown slot hits */
#define DT_CODE_OBJ_SET_MOTION       0x818A /* vertical velocity and speed by index */
#define DT_CODE_ENEMY_WALK           0x82F9 /* walk with probes $18 or $1B */
#define DT_CODE_OBJ_LAUNCH_BLOCK     0x86B5 /* a block starts flying (kind 9) */
#define DT_CODE_OBJ_HIT_BLOCK        0x86F7 /* a counted blow on a block */
#define DT_CODE_OBJ_RESOLVE_BLOCK    0x870D /* open, leave or shatter a block */
#define DT_CODE_THROWN_FALL          0x88BC /* kind 9: fall with probe $2D */
#define DT_CODE_THROWN_WALK          0x88C8 /* kind 9: walk with probes $29 or $2B */
#define DT_CODE_OBJ_BREAK            0x88DF /* pose $1C, then $88E4 */
#define DT_CODE_OBJ_SHATTER          0x88E4 /* the debris animation */
#define DT_CODE_OBJ_DEFEAT           0x8932 /* an enemy is beaten */
#define DT_CODE_OBJ_OPEN_BLOCK       0x8A11 /* an opened block or chest picks its item */
#define DT_CODE_OBJ_BECOME_PICKUP    0x8A32 /* become a pickup now */
#define DT_CODE_PICKUP_TAKE          0x8B21 /* Scrooge takes a pickup */
#define DT_CODE_PICKUP_EXTRA_DOT     0x8B38 /* item $2B: one more HP dot */
#define DT_CODE_HUD_RESTORE_DOT      0x8B82 /* the first lost HP dot becomes full */
#define DT_CODE_MONEY_ADD            0x8BBF /* add A to money digit Y */
#define DT_CODE_PICKUP_TREASURE      0x8C13 /* a land treasure */
#define DT_CODE_OBJ_SHOW_ITEM        0x8CAC /* the slot shows item A */
#define DT_CODE_OBJ_BECOME_BLOCK     0x8CE1 /* the slot becomes a block */
#define DT_CODE_OBJ_DESPAWN          0xF179 /* free the slot (fixed bank) */
#define DT_CODE_OBJ_SET_SPEED        0xF312 /* speed by index (fixed bank) */
#define DT_CODE_OBJ_TOUCH_BLOCK_H    0xF332 /* the block a slot overlaps, horizontal (fixed bank) */
#define DT_CODE_OBJ_TOUCH_BLOCK_V    0xF3B7 /* the same, vertical (fixed bank) */
#define DT_CODE_OBJ_DISTANCE_Y       0xF7CC /* |Scrooge's Y - the slot's| (fixed bank) */
#define DT_CODE_OBJ_DIFFICULT_ACTION 0xF822 /* actions on Difficult (fixed bank) */
#define DT_CODE_MONEY_MAX            0xF83F /* money capped at 9,999,990 (fixed bank) */

/* ---- hook sites of the event system (src/game/events.c) ----
 * config/game.toml declares each with a length/crc32 content key, so another
 * revision fails code generation instead of firing at the wrong code. */

/* Entry of the damage routine (functions.md `damage`): marks the last full
 * HP dot lost (every other hit on Easy). Reached only by JSR, from $EAD0 and
 * $EB22; the instruction before it is a JMP, so nothing falls into it. */
#define DT_SITE_DAMAGE    0xEB5E
#define DT_SITE_DAMAGE_ID "ducktales.event.player-damaged"

/* Inside the HUD timer (`timer_tick`, $E28C): the decrement of the
 * frames-per-tick counter $B0, after the time-out check. */
#define DT_SITE_TIMER_TICK    0xE2A6
#define DT_SITE_TIMER_TICK_ID "ducktales.event.timer-tick"

/* In `death_or_warp` ($DC3E): the only instruction in the ROM that decrements
 * the lives counter $0361, reached after a death when lives are not 0. */
#define DT_SITE_LIFE_LOST    0xDCB0
#define DT_SITE_LIFE_LOST_ID "ducktales.event.life-lost"

/* In the title set-up of the main task ($D4E6): the first instruction after
 * the loop that copies the 13-byte HUD template into $0358-$0364, which sets
 * the lives counter $0361 to 2. Runs after every power-on, reset and game
 * over (game over jumps to the reset vector). */
#define DT_SITE_NEW_GAME    0xD59C
#define DT_SITE_NEW_GAME_ID "ducktales.event.new-game"

/* The instruction right after the jump start copied DT_ADDR_JUMP_VY{,_FRAC}
 * into slot 0's velocity. It is reached only by falling through from those
 * two stores. */
#define DT_SITE_JUMP_START    0xE3FE
#define DT_SITE_JUMP_START_ID "ducktales.event.jump-start"

/* In the boss hit routine ($F6D7, called by the boss objects of bank 6 and
 * by $FA44): the decrement of the boss hits left $B1, reached only when the
 * boss's cooldown $06F0,X is 0, i.e. for a hit that counts
 * (docs/decompilation.md "Events"). */
#define DT_SITE_BOSS_HIT    0xF6DD
#define DT_SITE_BOSS_HIT_ID "ducktales.event.boss-hit"

/* Levels: map and screen decoding, column streaming, the VRAM queues, the
 * camera and the background probe (docs/levels.md, docs/rendering.md,
 * src/decomp/level.c and collision.c). */
#define DT_ADDR_SCRATCH2         0x0002
#define DT_ADDR_SCRATCH3         0x0003 /* also the buffer offset of the block quadrant being decoded (code) */
#define DT_ADDR_STEPS            0x0004 /* stream steps left; quadrant of a probed point (code) */
#define DT_ADDR_QUEUE_END        0x0005 /* end of the $0400 queue being written; cell of a probed point (code) */
#define DT_ADDR_PROBE_X          0x0006 /* world X low of the last probed point (code) */
#define DT_ADDR_PROBE_Y          0x0007 /* Y of the last probed point (code) */
#define DT_ADDR_SCRATCH0F        0x000F
#define DT_ADDR_TARGET_X         0x0010 /* camera target X / block cell row * 8 + column (code) */
#define DT_ADDR_TARGET_PAGE      0x0011 /* camera target page / steps left of a start draw (code) */
#define DT_ADDR_TARGET_SCREEN    0x0012 /* camera target screen index (code) */
#define DT_ADDR_VRAM_QUEUE_READY 0x0019 /* non-zero: the NMI uploads the $0400 queue (verified) */
#define DT_ADDR_TILE_EDIT_READY  0x001A /* non-zero: the NMI uploads the $0440 queue (verified) */
#define DT_ADDR_SCREEN_DATA      0x0022 /* pointer to the screen's 72 bytes in bank 4 (verified) */
#define DT_ADDR_CAMERA_X_PREV    0x0025 /* camera X low of the previous frame (verified) */
#define DT_ADDR_BLOCK_ATTR       0x0027 /* attribute byte of the decoded block (verified) */
#define DT_ADDR_BLOCK_CLASSES    0x0028 /* collision byte of the decoded block (verified) */
#define DT_ADDR_STREAM_PAGE      0x002C /* page of the streamed screen, $24 & 7 (verified) */
#define DT_ADDR_CAMERA_DELTA     0x002E /* camera movement in pixels (code) */
#define DT_ADDR_SCENE            0x0078 /* scene number; bit 1 = camera moved by script (hypothesis) */
#define DT_ADDR_SCENE_SCROLL     0x0079 /* bit 7: the scripted camera scrolls left (hypothesis) */
#define DT_ADDR_HIMALAYAS_OPEN   0x00A2 /* non-zero: the Himalayas substitution is drawn (hypothesis) */
#define DT_ADDR_BLANKED          0x00E8 /* non-zero: rendering is off, queues are uploaded at once (verified) */
#define DT_ADDR_ROW_SCROLL       0x00E9 /* non-zero during the vertical row scroll (code) */
#define DT_ADDR_TILESET_BANK     0x00F7 /* PRG bank of blocks and metatiles (verified) */
#define DT_ADDR_TILE_BUFFER      0x03F0 /* 4x4 tiles of the decoded block (verified) */
#define DT_ADDR_VRAM_QUEUE       0x0400 /* VRAM update queue: PPU address high, low, count - 1, bytes (verified) */
#define DT_ADDR_TILE_EDIT_QUEUE  0x0440 /* tile edit queue, same format (verified) */
#define DT_ADDR_ATTR_SHADOW      0x0500 /* attribute tables of both nametables (verified) */
#define DT_ADDR_COLLISION_MAP    0x0580 /* collision class bytes of both nametables (verified) */

#define DT_PPU_ADDR          0x2006
#define DT_PPU_DATA          0x2007
#define DT_SCREEN_BANK       4      /* PRG bank of the screens (verified) */
#define DT_SCREEN_BASE       0x8000 /* screen id * 72 from here (verified) */
#define DT_SCREEN_NINTH_BITS 0x40   /* offset of the 9th block bits in a screen (verified) */
#define DT_BLOCK_BASE        0xB300 /* block * 4 from here in the tileset bank (verified) */
#define DT_METATILE_TILES    0xBB00 /* four 256-byte tables: top-left, top-right, bottom-left, bottom-right */
#define DT_METATILE_ATTR     0xBF00 /* palette bits 0-1, collision class bits 4-5 (verified) */

#define DT_CODE_MAP_ROWS         0xCE57 /* rows per map */
#define DT_CODE_MAP_STRIDE       0xCE61 /* row stride per map */
#define DT_CODE_MAP_GRID_LO      0xCF7F /* grid pointer per map, low bytes */
#define DT_CODE_MAP_GRID_HI      0xCF89
#define DT_CODE_NINTH_BIT_MASK   0xCE20 /* bit of block column n in the 9th-bit byte */
#define DT_CODE_QUADRANT_OFFSET  0xCE28 /* buffer offset of quadrant n in $03F0 */
#define DT_CODE_QUEUE_TILE_LO    0xCCAD /* PPU address low of block row n */
#define DT_CODE_QUEUE_TILE_HI    0xCCB5 /* PPU address high of block row n */
#define DT_CODE_QUEUE_QUADRANT   0xC96E /* queue offset of quadrant n's top tiles */
#define DT_CODE_STREAM_STEP      0xC96A /* step of $26 and $24: right, right x2, left, left x2 */
#define DT_CODE_START_STEPS      0xC786 /* start draw: steps - 1, first $26, screen offset (3 tables of 3) */
#define DT_CODE_START_FIRST_POS  0xC789
#define DT_CODE_START_SCREEN     0xC78C
#define DT_CODE_CAMERA_PAGE_STEP 0xF0E2 /* camera target page carry: 0 right, $FF left (index $38 & 2) */
#define DT_CODE_SCRIPT_PUSH      0xF0E5 /* scripted camera: push of 24-bit world X at the screen edges */
#define DT_CODE_PROBE_DX         0xD433 /* probe point X offsets */
#define DT_CODE_PROBE_DY         0xD3CE /* probe point Y offsets */
#define DT_CODE_PROBE_PAGE_BIT   0xD498 /* collision map half of nametable n */
#define DT_CODE_SPECIAL_LO       0xD1E2 /* handlers of class-3 metatiles by id >> 4, low bytes */
#define DT_CODE_SPECIAL_HI       0xD1F2
#define DT_CODE_BANK_TABLE       0xFFE5 /* bus-conflict table of bank_select $FFDE */

#define DT_CODE_MAP_HEADER            0xC7B0
#define DT_CODE_DRAW_START            0xC71D
#define DT_CODE_DRAW_SCREEN           0xC78F
#define DT_CODE_STREAM_BLOCKS         0xC830
#define DT_CODE_DECODE_BLOCK          0xCCBD
#define DT_CODE_METATILE_AT           0xCDE2
#define DT_CODE_SCREEN_POINTER        0xCE2C
#define DT_CODE_VRAM_FLUSH            0xC139
#define DT_CODE_TILE_EDIT_FLUSH       0xC15E
#define DT_CODE_CAMERA_UPDATE         0xEFE2
#define DT_CODE_SPECIAL_SOLID         0xD162 /* class-3 handlers of the background probe */
#define DT_CODE_SPECIAL_MARK          0xD16B
#define DT_CODE_SPECIAL_HAZARD6       0xD176
#define DT_CODE_SPECIAL_HAZARD7       0xD199
#define DT_CODE_SPECIAL_SLIPPERY      0xD1B1
#define DT_CODE_SPECIAL_PIT           0xD1BF
#define DT_CODE_SPECIAL_MARK_SLIPPERY 0xD1D1
#define DT_CODE_BOSS_TRIGGER          0xCF93
#define DT_CODE_BOSS_ROW              0xD04D /* boss trigger: row by land */
#define DT_CODE_BOSS_PAGE             0xD052 /* boss trigger: Scrooge's X page by land */
#define DT_CODE_BOSS_SCROLL           0xD057 /* boss trigger: scroll direction by land (move-dir bits) */
#define DT_CODE_BOSS_LOOK_AHEAD       0xD064 /* 16-bit offset added to Scrooge's X by direction & 2 */
#define DT_CODE_TILE_EDIT             0xF574
#define DT_CODE_TILE_EDIT_METATILE    0xF653 /* replacement metatile by land * 4 + kind */
#define DT_CODE_TILE_EDIT_HI          0xF67F /* PPU address high by tile row / 32 px */
#define DT_CODE_TILE_EDIT_LO          0xF687 /* PPU address low by tile row / 32 px */
#define DT_CODE_TILE_EDIT_HALF        0xF68F /* PPU address low of the metatile in its block, by quadrant */
#define DT_CODE_QUADRANT_KEEP         0xF693 /* mask keeping the other three quadrants of a byte */
#define DT_ADDR_TILE_EDIT_END         0x005C /* end of the tile edit queue being written (verified) */
#define DT_ROW_TILE_Y                 0x0740 /* Y of the tile an object edits (code) */
#define DT_CODE_ROW_CHR_STREAM        0xC42C
#define DT_CODE_ROW_CHR_BASE          0xC508 /* first entry of each land in the row tables */
#define DT_CODE_ROW_CHR_LIST          0xC511 /* CHR list per row; bit 7: two lists chosen by camera page */
#define DT_CODE_ROW_PALETTE           0xC53A /* sprite palette 3 per row */
#define DT_CODE_CHR_LIST_LO           0xC563 /* CHR list pointers */
#define DT_CODE_CHR_LIST_HI           0xC583
#define DT_CODE_SPRITE_PALETTES       0xC6D9 /* 4-byte sprite palettes */
#define DT_ADDR_CHR_STREAM 0x0050 /* row CHR stream: 0 start, 1 next record, 2 in a record, negative done (verified) */
#define DT_ADDR_CHR_BANK   0x0051 /* bank of the record being streamed (code) */
#define DT_ADDR_CHR_LIST   0x0052 /* pointer to the CHR list (code) */
#define DT_ADDR_CHR_SOURCE 0x0054 /* pointer to the source page (code) */
#define DT_ADDR_CHR_TARGET 0x0056 /* PPU address of the next 64 bytes, low byte first (code) */
#define DT_ADDR_CHR_RECORD 0x0058 /* offset of the next record in the list (code) */
#define DT_ADDR_CHR_OFFSET 0x0059 /* offset in the source page (code) */
#define DT_ADDR_CHR_PAGES  0x005A /* pages left in the record, minus one (code) */
