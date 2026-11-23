/*
 * system.c - the system layer of the fixed bank, decompiled
 * (docs/functions.md, docs/rendering.md, docs/decompilation.md): bank
 * switching, the controllers, the PPU blanking helpers, the nametable fill,
 * the OAM park, the random generator, the CHR list upload, the scene
 * palettes and the direct string drawing of bank 5. The per-row sprite CHR
 * stream ($C42C) is decomp/level's (level.c).
 *
 * Bank switching on this UxROM board is a store of the bank number into the
 * fixed bank's bus-conflict table at $FFE5 (the table holds 0, 1, 2 ..., so
 * the byte on the bus is the bank number either way). The tables the
 * routines read (CHR lists, scene palettes, strings) are read from the ROM at
 * run time.
 */
#include "decomp/decomp.h"
#include "decomp/devices.h"

#include "ducktales/game.h"

#define gBank          RAM_U8(DT_ADDR_BANK)
#define gPpuCtrl       RAM_U8(DT_ADDR_PPU_CTRL)
#define gPpuMask       RAM_U8(DT_ADDR_PPU_MASK)
#define gBlanked       RAM_U8(DT_ADDR_PPU_OFF)
#define gSplitEnabled  RAM_U8(DT_ADDR_HUD_SPLIT)
#define gPalette       (&gRam[DT_ADDR_PALETTE_SHOWN])
#define gPaletteMaster (&gRam[DT_ADDR_PALETTE])
#define gRandom        (&gRam[DT_ADDR_RANDOM])

#define MASK_SHOWN        0x1E
#define OAM_PARKED_Y      0xF8
#define OAM_SPLIT_ENTRY   4
#define PAD_UP_DOWN       (DT_PAD_UP | DT_PAD_DOWN)
#define PAD_LEFT_RIGHT    (DT_PAD_LEFT | DT_PAD_RIGHT)
#define CHR_LIST_END      0x80
#define SCENE_SPRITE_PAL3 0x80
#define STRING_END        0xFE
#define STRINGS_TOTAL     0x0B
#define TOTAL_DIGITS      8
#define STRINGS_BANK      5

static uint16_t rom_pointer(uint16_t lo_table, uint16_t hi_table, uint8_t index)
{
    return (uint16_t)(Game_ReadRom((uint16_t)(hi_table + index)) << 8 | Game_ReadRom((uint16_t)(lo_table + index)));
}

/* $FFE0: map a bank at $8000 without recording it (the NMI's switch). */
static void Bank_SelectTemp(uint8_t bank)
{
    Decomp_Enter(DT_CODE_BANK_SELECT_TEMP, bank, 0, 0);
    Decomp_BusWrite((uint16_t)(DT_CODE_BANK_TABLE + bank), bank);
}

/* $FFDE */
static void Bank_Select(uint8_t bank)
{
    Decomp_Enter(DT_CODE_BANK_SELECT, bank, 0, 0);
    gBank = bank;
    Bank_SelectTemp(bank);
}

/* $C1AB: both controllers, the standard pad on data line 0 and an expansion
 * pad on line 1 merged into it; then the newly pressed buttons, and opposite
 * directions held together are dropped.
 *
 * The controllers latch the strobe and shift on APU cycles, which do not run
 * in an isolated call: there the original reads one unchanging bit, so its
 * cycle count (which depends on the buttons through the final masks) is not
 * the console's. Replace mode therefore gives this call to the original;
 * check mode compares the C with the original on the bits an isolated call
 * reads. */
static void Input_ReadControllers(void)
{
    Decomp_Enter(DT_CODE_READ_CONTROLLERS, 0, 0, 0);
    if (Decomp_Mode() == DECOMP_REPLACE) {
        Decomp_RunOriginal();
        return;
    }
    uint8_t *pads = &gRam[DT_ADDR_PAD_NEW];
    Decomp_BusWrite(JOYPAD1, 1);
    Decomp_BusWrite(JOYPAD1, 0);
    for (int bit = 0; bit < 8; bit++) {
        uint8_t a = Decomp_BusLoad(JOYPAD1);
        pads[0] = (uint8_t)(pads[0] << 1 | (a & 1));
        RAM_U8(DT_ADDR_SCRATCH0) = (uint8_t)(RAM_U8(DT_ADDR_SCRATCH0) << 1 | (a >> 1 & 1));
        uint8_t b = Decomp_BusLoad(JOYPAD2);
        pads[1] = (uint8_t)(pads[1] << 1 | (b & 1));
        RAM_U8(DT_ADDR_SCRATCH1) = (uint8_t)(RAM_U8(DT_ADDR_SCRATCH1) << 1 | (b >> 1 & 1));
    }
    pads[0] |= RAM_U8(DT_ADDR_SCRATCH0);
    pads[1] |= RAM_U8(DT_ADDR_SCRATCH1);
    for (int pad = 1; pad >= 0; pad--) {
        uint8_t held = pads[pad];
        pads[pad] = (uint8_t)((held ^ pads[pad + 2]) & held);
        pads[pad + 2] = held;
    }
    for (int i = 3; i >= 0; i--) {
        if ((pads[i] & PAD_UP_DOWN) == PAD_UP_DOWN || (pads[i] & PAD_LEFT_RIGHT) == PAD_LEFT_RIGHT) {
            pads[i] &= 0xF0;
        }
    }
}

/* $C197: turn the picture off; the NMI skips its PPU work while blanked. */
static void Ppu_Blank(void)
{
    Decomp_Enter(DT_CODE_SCREEN_OFF, 0, 0, 0);
    gBlanked++;
    gPpuMask = 0;
    Ppu_SetMask(0);
}

/* $C1A1 */
static void Ppu_Unblank(void)
{
    Decomp_Enter(DT_CODE_SCREEN_ON, 0, 0, 0);
    gBlanked--;
    gPpuMask = MASK_SHOWN;
    Ppu_SetMask(MASK_SHOWN);
}

/* $C203: fill a nametable (high byte nt) with one tile and its attribute
 * table with one byte, while blanked. Below $2000 (pattern tables) the
 * attribute byte is the number of pages to fill instead. */
static void Ppu_FillNametable(uint8_t nt, uint8_t tile, uint8_t attribute)
{
    Decomp_Enter(DT_CODE_NT_FILL, nt, tile, attribute);
    RAM_U8(DT_ADDR_SCRATCH0) = nt;
    RAM_U8(DT_ADDR_SCRATCH1) = tile;
    RAM_U8(DT_ADDR_SCRATCH2) = attribute;
    Ppu_ResetLatch();
    Ppu_SetCtrl(gPpuCtrl & 0xFE);
    Ppu_SetAddress((uint16_t)(nt << 8));
    unsigned pages = nt >= 0x20 ? 4 : attribute ? attribute : 0x100;
    for (unsigned i = 0; i < pages * 0x100; i++) {
        Ppu_Write(tile);
    }
    if (nt < 0x20) {
        return;
    }
    Ppu_SetAddress((uint16_t)((uint8_t)(nt + 3) << 8 | 0xC0));
    for (int i = 0; i < 0x40; i++) {
        Ppu_Write(attribute);
    }
}

/* $C267: park every OAM entry below the screen, but the split sprite. */
static void Oam_Clear(void)
{
    Decomp_Enter(DT_CODE_OAM_CLEAR, 0, 0, 0);
    for (unsigned entry = gSplitEnabled ? OAM_SPLIT_ENTRY : 0; entry < 0x100; entry += 4) {
        RAM_U8(DT_ADDR_OAM + entry) = OAM_PARKED_Y;
    }
}

/* $C2E6: one step of the 32-bit shift register $C0-$C3 (feedback from bit 1
 * of the two low bytes); $00 keeps the first tap. */
static void Random_Next(void)
{
    Decomp_Enter(DT_CODE_RANDOM, 0, 0, 0);
    uint8_t tap = gRandom[0] & 0x02;
    RAM_U8(DT_ADDR_SCRATCH0) = tap;
    uint8_t carry = tap != (gRandom[1] & 0x02);
    for (int i = 0; i < 4; i++) {
        uint8_t out = gRandom[i] & 1;
        gRandom[i] = (uint8_t)(gRandom[i] >> 1 | carry << 7);
        carry = out;
    }
}

/* $C301: upload CHR list n to the pattern tables while blanked. A record is
 * the PRG bank (high nibble) and pages - 1 (low nibble), the source page and
 * the PPU page; a byte with bit 7 set ends the list. The source bank stays
 * selected. */
static void Chr_LoadList(uint8_t n)
{
    Decomp_Enter(DT_CODE_CHR_LOAD, 0, n, 0);
    Ppu_SetCtrl(gPpuCtrl & 0xFE);
    uint16_t list = rom_pointer(DT_TABLE_CHR_LIST_LO, DT_TABLE_CHR_LIST_HI, n);
    Ram_SetU16(DT_ADDR_CHR_LOAD_LIST, DT_ADDR_CHR_LOAD_LIST + 1, list);
    uint8_t i = 0;
    do {
        uint8_t head = Game_ReadRom((uint16_t)(list + i));
        if (head & CHR_LIST_END) {
            return;
        }
        RAM_U8(DT_ADDR_CHR_LOAD_BANK) = head >> 4;
        RAM_U8(DT_ADDR_CHR_LOAD_PAGES) = head & 0x0F;
        i++;
        Ram_SetU16(DT_ADDR_CHR_LOAD_SOURCE, DT_ADDR_CHR_LOAD_SOURCE + 1,
                   (uint16_t)(Game_ReadRom((uint16_t)(list + i)) << 8));
        i++;
        Ppu_ResetLatch();
        Ppu_SetAddress((uint16_t)(Game_ReadRom((uint16_t)(list + i)) << 8));
        RAM_U8(DT_ADDR_CHR_LOAD_INDEX) = i;
        Bank_Select(RAM_U8(DT_ADDR_CHR_LOAD_BANK));
        do {
            uint16_t page = Ram_GetU16(DT_ADDR_CHR_LOAD_SOURCE, DT_ADDR_CHR_LOAD_SOURCE + 1);
            for (int b = 0; b < 0x100; b++) {
                Ppu_Write(Game_ReadRom((uint16_t)(page + b)));
            }
            RAM_U8(DT_ADDR_CHR_LOAD_SOURCE + 1)++;
        } while ((int8_t)--RAM_U8(DT_ADDR_CHR_LOAD_PAGES) >= 0);
        i = (uint8_t)(RAM_U8(DT_ADDR_CHR_LOAD_INDEX) + 1);
    } while (i);
}

static uint8_t string_byte(uint8_t y)
{
    return Game_ReadRom((uint16_t)(Ram_GetU16(DT_ADDR_SCRATCH0, DT_ADDR_SCRATCH1) + y));
}

static uint8_t string_next(uint8_t y)
{
    if (!++y) {
        RAM_U8(DT_ADDR_SCRATCH1)++;
    }
    return y;
}

/* bank 5 $BAB0: draw string set n straight to the nametables while blanked:
 * records of a PPU address (high byte 0 ends the set) and tiles up to $FE.
 * The TOTAL set also writes the eight TOTAL digit tiles after its last
 * record. */
static void Text_DrawStrings(uint8_t set)
{
    Decomp_Enter(DT_CODE_DRAW_STRINGS, 0, set, 0);
    Ram_SetU16(DT_ADDR_SCRATCH0, DT_ADDR_SCRATCH1, rom_pointer(DT_TABLE_STRINGS_LO, DT_TABLE_STRINGS_HI, set));
    uint8_t y = 0;
    for (;;) {
        Ppu_ResetLatch();
        uint8_t high = string_byte(y);
        if (!high) {
            break;
        }
        Decomp_BusWrite(PPU_ADDR, high);
        y = string_next(y);
        Decomp_BusWrite(PPU_ADDR, string_byte(y));
        y = string_next(y);
        for (uint8_t tile; (tile = string_byte(y)) != STRING_END; y = string_next(y)) {
            Ppu_Write(tile);
        }
        y = string_next(y);
    }
    if (set != STRINGS_TOTAL) {
        return;
    }
    for (int i = 0; i < TOTAL_DIGITS; i++) {
        Ppu_Write(gRam[DT_ADDR_TOTAL_DIGITS + i]);
    }
}

/* $F76D: both palette halves of scene y into the buffer and the master copy
 * (two 16-byte entries of one table, read backwards from their last byte);
 * bit 7 of the sprite index also loads sprite palette 3 by $F9. */
static void Palette_LoadScene(uint8_t scene)
{
    Decomp_Enter(DT_CODE_PALETTE_SCENE, 0, 0, scene);
    RAM_U8(DT_ADDR_SCRATCH0) = Game_ReadRom((uint16_t)(DT_TABLE_SCENE_BG + scene));
    RAM_U8(DT_ADDR_SCRATCH2) = Game_ReadRom((uint16_t)(DT_TABLE_SCENE_SPR + scene));
    RAM_U8(DT_ADDR_SCRATCH1) = RAM_U8(DT_ADDR_SCRATCH2) & 0x7F;
    for (int i = 15; i >= 0; i--) {
        uint8_t bg = Game_ReadRom((uint16_t)(DT_TABLE_SCENE_PALETTES + RAM_U8(DT_ADDR_SCRATCH0)));
        gPalette[i] = bg;
        gPaletteMaster[i] = bg;
        uint8_t spr = Game_ReadRom((uint16_t)(DT_TABLE_SCENE_PALETTES + RAM_U8(DT_ADDR_SCRATCH1)));
        gPalette[0x10 + i] = spr;
        gPaletteMaster[0x10 + i] = spr;
        RAM_U8(DT_ADDR_SCRATCH0)--;
        RAM_U8(DT_ADDR_SCRATCH1)--;
    }
    gPalette[0x10] = gPalette[0];
    gPaletteMaster[0x10] = gPalette[0];
    if (!(RAM_U8(DT_ADDR_SCRATCH2) & SCENE_SPRITE_PAL3)) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        uint8_t color = Game_ReadRom((uint16_t)(DT_TABLE_SCENE_SPRITE_PAL3 + (uint8_t)(gDifficulty * 4 + i)));
        gPalette[0x1C + i] = color;
        gPaletteMaster[0x1C + i] = color;
    }
}

/* Bank_Select and Bank_SelectTemp return with the bank in A and X. */
static void Run_BankSelect(Regs *r)
{
    Bank_Select(r->a);
    r->x = r->a;
    r->p = Flags_NZ(r->p, r->x);
}

static void Run_BankSelectTemp(Regs *r)
{
    Bank_SelectTemp(r->a);
    r->x = r->a;
    r->p = Flags_NZ(r->p, r->x);
}

static void Run_ReadControllers(Regs *r)
{
    (void)r;
    Input_ReadControllers();
}

static void Run_PpuBlank(Regs *r)
{
    (void)r;
    Ppu_Blank();
}

static void Run_PpuUnblank(Regs *r)
{
    (void)r;
    Ppu_Unblank();
}

static void Run_FillNametable(Regs *r)
{
    Ppu_FillNametable(r->a, r->x, r->y);
}

static void Run_OamClear(Regs *r)
{
    (void)r;
    Oam_Clear();
}

/* Random_Next leaves its loop's X (4) and Y (0): the frame loop yields right
 * after it, and the yield keeps X and Y on the stack. */
static void Run_RandomNext(Regs *r)
{
    Random_Next();
    r->x = 4;
    r->y = 0;
}

static void Run_ChrLoadList(Regs *r)
{
    Chr_LoadList(r->x);
}

static void Run_PaletteLoadScene(Regs *r)
{
    Palette_LoadScene(r->y);
}

static void Run_TextDrawStrings(Regs *r)
{
    Text_DrawStrings(r->x);
}

const DecompRoutine gSystemRoutines[] = {
    { DT_CODE_BANK_SELECT, "ducktales.decomp.FFDE", "Bank_Select", Run_BankSelect, OUT_A | OUT_X | OUT_P | OUT_HW },
    { DT_CODE_BANK_SELECT_TEMP, "ducktales.decomp.FFE0", "Bank_SelectTemp", Run_BankSelectTemp,
      OUT_A | OUT_X | OUT_P | OUT_HW },
    { DT_CODE_READ_CONTROLLERS, "ducktales.decomp.C1AB", "Input_ReadControllers", Run_ReadControllers, OUT_HW },
    { DT_CODE_SCREEN_OFF, "ducktales.decomp.C197", "Ppu_Blank", Run_PpuBlank, OUT_HW },
    { DT_CODE_SCREEN_ON, "ducktales.decomp.C1A1", "Ppu_Unblank", Run_PpuUnblank, OUT_HW },
    { DT_CODE_NT_FILL, "ducktales.decomp.C203", "Ppu_FillNametable", Run_FillNametable, OUT_HW },
    { DT_CODE_OAM_CLEAR, "ducktales.decomp.C267", "Oam_Clear", Run_OamClear, 0 },
    { DT_CODE_RANDOM, "ducktales.decomp.C2E6", "Random_Next", Run_RandomNext, OUT_X | OUT_Y },
    { DT_CODE_CHR_LOAD, "ducktales.decomp.C301", "Chr_LoadList", Run_ChrLoadList, OUT_HW },
    { DT_CODE_PALETTE_SCENE, "ducktales.decomp.F76D", "Palette_LoadScene", Run_PaletteLoadScene, 0 },
    { DT_CODE_DRAW_STRINGS, "ducktales.decomp.5.BAB0", "Text_DrawStrings", Run_TextDrawStrings, OUT_HW,
      .bank = STRINGS_BANK },
};
const int gSystemRoutineCount = sizeof gSystemRoutines / sizeof gSystemRoutines[0];
