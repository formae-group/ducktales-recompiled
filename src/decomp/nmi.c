/*
 * nmi.c - the NMI handler of the fixed bank ($C000-$C138), decompiled as
 * interrupt routines (docs/DECOMPILATION.md "Interrupt routines"): C that
 * ends in RTI.
 *
 * Every NMI puts the HUD split's sprite 0 into the OAM shadow; unless
 * rendering is off ($E8) it makes the PPU's updates in VBlank (sprite DMA,
 * the two VRAM queues, the palette, the scroll, the mask and the frame's
 * pattern tables); it counts the frame and the task waits; at the sprite-0
 * hit it sets the playfield's scroll below the HUD; it hands the queued
 * sound requests to the driver in bank 3 and runs its update; then it maps
 * the game's bank again. Nmi_Return ($C132) is the exit alone, for an NMI
 * whose body ran as the original.
 */
#include "decomp/decomp.h"
#include "decomp/devices.h"
#include "decomp/level.h"
#include "decomp/sound.h"
#include "decomp/system.h"

#include "ducktales/game.h"

#define gOam             (&gRam[DT_ADDR_OAM])
#define gPalette         (&gRam[DT_ADDR_PALETTE_SHOWN])
#define gTasks           (&gRam[DT_ADDR_TASKS])
#define gRing            (&gRam[DT_ADDR_SOUND_RING])
#define gSplitEnabled    RAM_U8(DT_ADDR_HUD_SPLIT)
#define gBlanked         RAM_U8(DT_ADDR_BLANKED)
#define gSprite0Y        RAM_U8(DT_ADDR_SPRITE0_Y)
#define gPpuCtrl         RAM_U8(DT_ADDR_PPU_CTRL)
#define gPpuMask         RAM_U8(DT_ADDR_PPU_MASK)
#define gNoSpriteDma     RAM_U8(DT_ADDR_NO_SPRITE_DMA)
#define gVramQueueReady  RAM_U8(DT_ADDR_VRAM_QUEUE_READY)
#define gTileEditReady   RAM_U8(DT_ADDR_TILE_EDIT_READY)
#define gPaletteRequest  RAM_U8(DT_ADDR_PALETTE_REQUEST)
#define gScrollX         RAM_U8(DT_ADDR_SCROLL_X)
#define gScrollY         RAM_U8(DT_ADDR_SCROLL_Y)
#define gScroogeY        RAM_U8(DT_ROW_Y)
#define gLand            RAM_U8(DT_ADDR_LAND)
#define gFrameCounter    RAM_U8(DT_ADDR_FRAME_COUNTER)
#define gSchedulerPass   RAM_U8(DT_ADDR_SCHEDULER_PASS)
#define gRowScroll       RAM_U8(DT_ADDR_ROW_SCROLL)
#define gRowScrollAddr   RAM_U8(DT_ADDR_ROW_SCROLL_ADDR)
#define gRowScrollAddrHi RAM_U8(DT_ADDR_ROW_SCROLL_ADDR_HI)
#define gBgOffBelowSplit RAM_U8(DT_ADDR_BG_OFF_BELOW_SPLIT)
#define gCameraX         RAM_U8(DT_ADDR_CAMERA_X)
#define gCameraPage      RAM_U8(DT_ADDR_CAMERA_PAGE)
#define gSplitScrollY    RAM_U8(DT_ADDR_SPLIT_SCROLL_Y)
#define gRingRead        RAM_U8(DT_ADDR_SOUND_RING_READ)
#define gBank            RAM_U8(DT_ADDR_BANK)

#define NMI_SAVED         5    /* the interrupt's P, then the P, A, X and Y the handler pushed */
#define PUSHED_BITS       0x30 /* B and bit 5, as PHP pushes P */
#define PULLED_BITS       0x20 /* PLP leaves B clear and bit 5 set */
#define SPRITE0_TILE      0x21
#define SPRITE0_ATTR      0x20 /* behind the background */
#define SPRITE0_X         0x08
#define CTRL_NMI_OFF      0x78 /* the shadow's increment and tables, NMI off, during the updates */
#define OAM_PAGE          0x02
#define PALETTE_ADDR      0x3F00
#define PALETTE_BYTES     32
#define CTRL_HIGH_TABLES  0x18 /* sprites and background from $1000 */
#define TABLES_FROM_Y     0x44 /* Scrooge at or below this Y on screen */
#define LAND_KEEP_TABLES  9
#define TASK_SLOTS        4
#define TASK_SLOT_BYTES   4
#define STATUS_SPRITE0    0x40
#define MASK_SPRITES_ONLY 0x10
#define CTRL_SPLIT        0x90 /* NMI on, background from $1000; the nametable is the camera page's */
#define RING_MASK         7
#define SOUND_SPEED       0xFC
#define SOUND_FADE        0xFD

/* $C132: Y, X, A and the flags the handler saved come back off the stack;
 * then RTI. */
static void Nmi_PullSaved(Regs *r)
{
    r->y = Decomp_Pull();
    r->x = Decomp_Pull();
    r->a = Decomp_Pull();
    r->p = (uint8_t)((Decomp_Pull() & ~PUSHED_BITS) | PULLED_BITS);
}

static void Nmi_Return(Regs *r)
{
    Decomp_Enter(DT_NMI_RETURN, r->a, r->x, r->y);
    Nmi_PullSaved(r);
}

/* $C025: in VBlank, with rendering off: sprite DMA, the VRAM queues and the
 * palette; then the scroll, the mask and the frame's pattern tables, from
 * $1000 for both while Scrooge is low on the screen in play. */
static void Nmi_Upload(void)
{
    Ppu_SetCtrl(gPpuCtrl & CTRL_NMI_OFF);
    Ppu_ResetLatch();
    Ppu_SetMask(0);
    if (!gNoSpriteDma) {
        Decomp_BusWrite(OAM_ADDR, 0);
        Decomp_BusWrite(OAM_DMA, OAM_PAGE);
    }
    if (gVramQueueReady) {
        Vram_FlushQueue();
    }
    if (gTileEditReady) {
        Vram_FlushTileEdits();
    }
    if (gPaletteRequest) {
        gPaletteRequest = 0;
        Ppu_ResetLatch();
        Ppu_SetAddress(PALETTE_ADDR);
        for (int i = 0; i < PALETTE_BYTES; i++) {
            Ppu_Write(gPalette[i]);
        }
        Ppu_SetAddress(PALETTE_ADDR);
        Ppu_SetAddress(0);
    }
    Ppu_ResetLatch();
    Decomp_BusWrite(PPU_SCROLL, gScrollX);
    Decomp_BusWrite(PPU_SCROLL, gScrollY);
    Ppu_SetMask(gPpuMask);
    if (gSplitEnabled && gScroogeY >= TABLES_FROM_Y && gLand != LAND_KEEP_TABLES) {
        Ppu_SetCtrl(gPpuCtrl | CTRL_HIGH_TABLES);
    } else {
        Ppu_SetCtrl(gPpuCtrl);
    }
}

/* $C0AC: the frame, the scheduler's pass flag, and a frame off every waiting
 * task's count (ready at 0). */
static void Nmi_CountWaits(void)
{
    gFrameCounter++;
    gSchedulerPass = 0xFF;
    for (int slot = 0; slot < TASK_SLOTS; slot++) {
        uint8_t *task = &gTasks[slot * TASK_SLOT_BYTES];
        if (task[0] == DT_TASK_WAITING && --task[1] == 0) {
            task[0] = DT_TASK_READY;
        }
    }
}

/* $C0D2: at the sprite-0 hit below the HUD, the playfield's position: the
 * row scroll's address while one runs, the background off when asked, then
 * its scroll and the camera page's nametable. */
static void Nmi_Split(void)
{
    Decomp_BusWait(PPU_STATUS, STATUS_SPRITE0, 0);
    Decomp_BusWait(PPU_STATUS, STATUS_SPRITE0, STATUS_SPRITE0);
    if (gRowScroll) {
        Ppu_ResetLatch();
        Decomp_BusWrite(PPU_ADDR, gRowScrollAddrHi);
        Decomp_BusWrite(PPU_ADDR, gRowScrollAddr);
    }
    if (gBgOffBelowSplit) {
        Ppu_SetMask(MASK_SPRITES_ONLY);
    }
    Decomp_BusWrite(PPU_SCROLL, gCameraX);
    Decomp_BusWrite(PPU_SCROLL, gSplitScrollY);
    Ppu_SetCtrl((uint8_t)((gCameraPage & 1) | CTRL_SPLIT));
}

/* $C10B: bank 3 mapped, every queued request to the driver, then its update.
 * The first request gets Y = 0 (the task loop leaves it); after one, Y is
 * what the driver left, which only the speed and fade ids read: those give
 * the call to the original. */
static bool Nmi_Sound(void)
{
    Bank_SelectTemp(DT_SND_BANK);
    for (bool first = true;; first = false) {
        uint8_t x = gRingRead;
        uint8_t id = gRing[x];
        if (id == DT_SOUND_RING_EMPTY) {
            break;
        }
        gRing[x] = DT_SOUND_RING_EMPTY;
        gRingRead = (uint8_t)((x + 1) & RING_MASK);
        if (!first && (id == SOUND_SPEED || id == SOUND_FADE)) {
            Decomp_RunOriginal();
            return false;
        }
        Sound_Play(id, 0);
    }
    Sound_Update();
    return true;
}

/* $C000, the NMI */
static void Nmi_Handler(Regs *r)
{
    Decomp_Enter(DT_CODE_NMI, r->a, r->x, r->y);
    Decomp_Push((uint8_t)(r->p | PUSHED_BITS));
    Decomp_Push(r->a);
    Decomp_Push(r->x);
    Decomp_Push(r->y);
    if (gSplitEnabled) {
        gOam[0] = gSprite0Y;
        gOam[1] = SPRITE0_TILE;
        gOam[2] = SPRITE0_ATTR;
        gOam[3] = SPRITE0_X;
    }
    if (!gBlanked) {
        Nmi_Upload();
    }
    Nmi_CountWaits();
    if (!gBlanked && gSplitEnabled) {
        Nmi_Split();
    }
    if (!Nmi_Sound()) {
        return;
    }
    Bank_Select(gBank);
    Nmi_PullSaved(r);
}

#define NMI_REGS (OUT_A | OUT_X | OUT_Y | OUT_P)

const DecompRoutine gNmiRoutines[] = {
    { DT_CODE_NMI, "ducktales.decomp.C000", "Nmi_Handler", Nmi_Handler, NMI_REGS | OUT_HW, .interrupt = true },
    { DT_NMI_RETURN, "ducktales.decomp.C132", "Nmi_Return", Nmi_Return, NMI_REGS, true, NMI_SAVED, false,
      .interrupt = true },
};
const int gNmiRoutineCount = sizeof gNmiRoutines / sizeof gNmiRoutines[0];
