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
#include "decomp/system.h"

#include "ducktales/game.h"

#define gBank           RAM_U8(DT_ADDR_BANK)
#define gPpuCtrl        RAM_U8(DT_ADDR_PPU_CTRL)
#define gPpuMask        RAM_U8(DT_ADDR_PPU_MASK)
#define gBlanked        RAM_U8(DT_ADDR_PPU_OFF)
#define gSplitEnabled   RAM_U8(DT_ADDR_HUD_SPLIT)
#define gPalette        (&gRam[DT_ADDR_PALETTE_SHOWN])
#define gPaletteMaster  (&gRam[DT_ADDR_PALETTE])
#define gRandom         (&gRam[DT_ADDR_RANDOM])
#define gPaletteRequest RAM_U8(DT_ADDR_PALETTE_REQUEST)
#define gFadeStep       RAM_U8(DT_ADDR_FADE_STEP)
#define gFadeFrames     RAM_U8(DT_ADDR_FADE_FRAMES)
#define gFadeLevel      RAM_U8(DT_ADDR_FADE_LEVEL)
#define gTaskCurrent    RAM_U8(DT_ADDR_TASK_CURRENT)
#define gTaskEntry      (&gRam[DT_ADDR_TASK_ENTRY])

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
#define PALETTE_BYTES     32
#define PALETTE_BLACK     0x0F
#define FADE_FRAMES       4
#define FADE_IN_LEVEL     0x30
#define FADE_IN_STEP      0xF0 /* -0x10 */
#define FADE_OUT_STEP     0x10
#define FADE_OUT_DONE     0x50
#define TASK_STATE        0
#define TASK_WAIT         1
#define TASK_STACK        2 /* the saved S, or a new task's entry low byte */
#define TASK_ENTRY_HI     3
#define YIELD_FRAMES      1

#define TASK_BYTE(x, field) RAM_U8((uint8_t)(DT_ADDR_TASKS + (field) + (x)))

static uint16_t rom_pointer(uint16_t lo_table, uint16_t hi_table, uint8_t index)
{
    return (uint16_t)(Game_ReadRom((uint16_t)(hi_table + index)) << 8 | Game_ReadRom((uint16_t)(lo_table + index)));
}

/* $FFE0: map a bank at $8000 without recording it (the NMI's switch). */
void Bank_SelectTemp(uint8_t bank)
{
    Decomp_Enter(DT_CODE_BANK_SELECT_TEMP, bank, 0, 0);
    Decomp_BusWrite((uint16_t)(DT_CODE_BANK_TABLE + bank), bank);
}

/* $FFDE */
void Bank_Select(uint8_t bank)
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
 * in an isolated call: there the original reads one unchanging bit, and its
 * cycle count depends on the buttons through the final masks. So the C gives
 * the cycle host's measuring call the bytes those masks test, as it read them
 * (Decomp_IsolatedRead); check mode compares the C with the original on the
 * bits an isolated call reads. */
static void Input_ReadControllers(void)
{
    Decomp_Enter(DT_CODE_READ_CONTROLLERS, 0, 0, 0);
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
        Decomp_IsolatedRead(DT_CODE_READ_CONTROLLERS + 0x3E, (uint16_t)(DT_ADDR_PAD_NEW + i), pads[i]);
        Decomp_IsolatedRead(DT_CODE_READ_CONTROLLERS + 0x46, (uint16_t)(DT_ADDR_PAD_NEW + i), pads[i]);
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
void Oam_Clear(void)
{
    Decomp_Enter(DT_CODE_OAM_CLEAR, 0, 0, 0);
    for (unsigned entry = gSplitEnabled ? OAM_SPLIT_ENTRY : 0; entry < 0x100; entry += 4) {
        RAM_U8(DT_ADDR_OAM + entry) = OAM_PARKED_Y;
    }
}

/* ---- the fades: task code (docs/decompilation.md "Task code") ---- */

/* $C28D: the shown palette is the loaded one darkened by the fade level
 * (colours that would go below 0 are black); then the step's frames, the
 * count kept on the stack across task_yield. X keeps the step. */
static void Fade_Step(Regs *r)
{
    for (int i = 0; i < PALETTE_BYTES; i++) {
        gPalette[i] = gPaletteMaster[i];
    }
    for (int i = PALETTE_BYTES - 1; i >= 0; i--) {
        uint8_t colour = (uint8_t)(gPalette[i] - gFadeLevel);
        gPalette[i] = colour & 0x80 ? PALETTE_BLACK : colour;
    }
    r->y = 0xFF;
    gPaletteRequest = r->y;
    r->a = gFadeFrames;
    Decomp_Push(r->a);
    Decomp_Handoff(DT_CODE_TASK_YIELD, DT_CODE_FADE_WAIT, OUT_X | OUT_Y);
}

static void Fade_Start(Regs *r, uint8_t level, uint8_t step)
{
    gFadeLevel = level;
    gFadeStep = step;
    gFadeFrames = FADE_FRAMES;
    r->x = step;
    Fade_Step(r);
}

/* $C279: from black to the loaded palette in four steps. */
static void Palette_FadeIn(Regs *r)
{
    Decomp_Enter(DT_CODE_FADE_IN, r->a, r->x, r->y);
    Oam_Clear();
    Fade_Start(r, FADE_IN_LEVEL, FADE_IN_STEP);
}

/* $C27C: the fade in with the sprites kept (the ending's screens). */
static void Palette_FadeInKeep(Regs *r)
{
    Decomp_Enter(DT_CODE_FADE_IN_KEEP, r->a, r->x, r->y);
    Fade_Start(r, FADE_IN_LEVEL, FADE_IN_STEP);
}

/* $C282: from the loaded palette to black in four steps. */
static void Palette_FadeOut(Regs *r)
{
    Decomp_Enter(DT_CODE_FADE_OUT, r->a, r->x, r->y);
    Fade_Start(r, FADE_OUT_STEP, FADE_OUT_STEP);
}

/* $C2B2 (the frame count on the stack): wait out the step, then the next
 * level; the fade ends when the level reaches $50 (out) or turns negative
 * (in). A and the flags are those of the last CMP or LDA. */
static void Palette_FadeWait(Regs *r)
{
    Decomp_Enter(DT_CODE_FADE_WAIT, r->a, r->x, r->y);
    uint8_t frames = (uint8_t)(Decomp_Pull() - 1);
    r->a = frames;
    if (frames) {
        Decomp_Push(frames);
        Decomp_Handoff(DT_CODE_TASK_YIELD, DT_CODE_FADE_WAIT, OUT_X | OUT_Y);
        return;
    }
    uint8_t p = (uint8_t)(r->p & ~P_C);
    gFadeLevel = Alu_Adc(gFadeLevel, gFadeStep, &p);
    r->a = gFadeLevel;
    p = Flags_Cmp(p, r->a, FADE_OUT_DONE);
    if (r->a != FADE_OUT_DONE) {
        p = Flags_NZ(p, r->a);
        if (!(r->a & 0x80)) {
            Fade_Step(r);
            return;
        }
    }
    r->p = p;
}

/* ---- the task primitives (docs/functions.md); task_yield and task_wait are
 * task code ---- */

/* $FFA1: X = A * 4, the offset of task slot A in $80-$8F; A keeps it. */
static void Task_SlotOffset(Regs *r)
{
    Decomp_Enter(DT_CODE_TASK_SLOT, r->a, r->x, r->y);
    r->p = (uint8_t)((r->p & ~P_C) | (r->a >> 6 & P_C));
    r->a = (uint8_t)(r->a << 2);
    r->x = r->a;
    r->p = Flags_NZ(r->p, r->x);
}

/* $FF9F: the running task's slot offset. */
static void Task_CurrentSlot(Regs *r)
{
    Decomp_Enter(DT_CODE_TASK_CURRENT, r->a, r->x, r->y);
    r->a = gTaskCurrent;
    Task_SlotOffset(r);
}

/* $FF7D (task_create, A = slot): the scheduler starts the slot at the entry
 * in $93/$94. */
static void Task_Create(Regs *r)
{
    Decomp_Enter(DT_CODE_TASK_CREATE, r->a, r->x, r->y);
    Task_SlotOffset(r);
    TASK_BYTE(r->x, TASK_STACK) = gTaskEntry[0];
    TASK_BYTE(r->x, TASK_ENTRY_HI) = gTaskEntry[1];
    r->a = DT_TASK_NEW;
    TASK_BYTE(r->x, TASK_STATE) = r->a;
    r->p = Flags_NZ(r->p, r->a);
}

/* $FFAC (task_yield): the running task waits a frame. X and Y go on its
 * stack and its S into its slot, and the scheduler runs the next task; it
 * returns into this one once the NMI has counted the wait out. */
static void Task_Yield(Regs *r)
{
    Decomp_Enter(DT_CODE_TASK_YIELD, r->a, r->x, r->y);
    gTaskEntry[0] = YIELD_FRAMES;
    Decomp_Push(r->x);
    Decomp_Push(r->y);
    Task_CurrentSlot(r);
    TASK_BYTE(r->x, TASK_WAIT) = gTaskEntry[0];
    TASK_BYTE(r->x, TASK_STATE) = DT_TASK_WAITING;
    r->a = r->y = r->x;
    r->x = Decomp_Stack();
    r->p = Flags_NZ(r->p, r->x);
    TASK_BYTE(r->y, TASK_STACK) = r->x;
    Decomp_Handoff(DT_CODE_SCHEDULER, 0, 0);
}

/* $FFA5 (task_wait): task_yield X times (256 for X = 0). */
static void Task_Wait(Regs *r)
{
    Decomp_Enter(DT_CODE_TASK_WAIT, r->a, r->x, r->y);
    Decomp_PushReturn(DT_TASK_WAIT_NEXT);
    Task_Yield(r);
}

/* $FFA8, where task_yield returns into task_wait with X and Y back. */
static void Task_WaitNext(Regs *r)
{
    Decomp_Enter(DT_TASK_WAIT_NEXT, r->a, r->x, r->y);
    r->x--;
    r->p = Flags_NZ(r->p, r->x);
    if (r->x) {
        Task_Wait(r);
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

/* The fades return with A, X, Y and the flags as their RTS leaves them. */
#define FADE_RETURNS (OUT_A | OUT_X | OUT_Y | OUT_P)

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
    { DT_CODE_FADE_IN, "ducktales.decomp.C279", "Palette_FadeIn", Palette_FadeIn, FADE_RETURNS, true, 0, false },
    { DT_CODE_FADE_IN_KEEP, "ducktales.decomp.C27C", "Palette_FadeInKeep", Palette_FadeInKeep, FADE_RETURNS, true, 0,
      false },
    { DT_CODE_FADE_OUT, "ducktales.decomp.C282", "Palette_FadeOut", Palette_FadeOut, FADE_RETURNS, true, 0, false },
    { DT_CODE_FADE_WAIT, NULL, "Palette_FadeWait", Palette_FadeWait, FADE_RETURNS, true, 1, true },
    { DT_CODE_CHR_LOAD, "ducktales.decomp.C301", "Chr_LoadList", Run_ChrLoadList, OUT_HW },
    { DT_CODE_PALETTE_SCENE, "ducktales.decomp.F76D", "Palette_LoadScene", Run_PaletteLoadScene, 0 },
    { DT_CODE_DRAW_STRINGS, "ducktales.decomp.5.BAB0", "Text_DrawStrings", Run_TextDrawStrings, OUT_HW,
      .bank = STRINGS_BANK },
    { DT_CODE_TASK_SLOT, NULL, "Task_SlotOffset", Task_SlotOffset, OUT_X },
    { DT_CODE_TASK_CURRENT, NULL, "Task_CurrentSlot", Task_CurrentSlot, OUT_X },
    { DT_CODE_TASK_CREATE, "ducktales.decomp.FF7D", "Task_Create", Task_Create, 0 },
    { DT_CODE_TASK_YIELD, "ducktales.decomp.FFAC", "Task_Yield", Task_Yield, 0, true, 0, false },
    { DT_CODE_TASK_WAIT, "ducktales.decomp.FFA5", "Task_Wait", Task_Wait, 0, true, 0, false },
    { DT_TASK_WAIT_NEXT, NULL, "Task_WaitNext", Task_WaitNext, OUT_A | OUT_X | OUT_Y | OUT_P, true, 0, true },
};
const int gSystemRoutineCount = sizeof gSystemRoutines / sizeof gSystemRoutines[0];
