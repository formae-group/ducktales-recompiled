/*
 * sprites.c - the sprite builder of bank 5, decompiled (docs/rendering.md
 * "Sprites", docs/decompilation.md).
 *
 * The frame loop calls Oam_Build every frame with $98 set (so the NMI does not
 * send a half-built table) after Oam_Clear parked every entry. It fills the
 * OAM shadow at $0200 from entry 1 on (entry 0 is the HUD split sprite): the
 * three HUD sprite groups when the HUD may show, then every live object slot
 * through its animation script and metasprite. The slot order flips every
 * frame ($95 bit 0), which is the game's sprite flicker: the PPU keeps the
 * first 8 sprites of a line in OAM order, so a crowded line loses a
 * different set on alternate frames.
 *
 * The animation scripts, metasprites and offset tables are read from the
 * ROM (bank 5, mapped while the builder runs) through the pointer tables
 * named in ducktales_symbols.h. Scratch bytes $00-$07 and $10-$13 hold the
 * builder's pointers and the slot's drawing state, as the original leaves
 * them.
 *
 * The frame loop yields right after the builder, and the yield saves X and
 * Y on the main task's stack, so the builder's contract includes the X and Y
 * it returns with: X is the slot loop's end ($96), Y the last index the
 * original used (s_y follows it).
 */
#include "decomp/decomp.h"

#include "ducktales/game.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"

#define gOam         (&gRam[DT_ADDR_OAM])
#define gOamNext     RAM_U8(DT_ADDR_OAM_NEXT)
#define gSprSlot     RAM_U8(DT_ADDR_SPR_SLOT)
#define gSprPhase    RAM_U8(DT_ADDR_SPR_PHASE)
#define gSpritesLeft RAM_U8(DT_ADDR_SPR_LEFT)
#define gPriorityBit RAM_U8(DT_ADDR_SPR_PRIORITY)
#define gFlipBit     RAM_U8(DT_ADDR_SPR_FLIP)
#define gSlotFlags   RAM_U8(DT_ADDR_SPR_FLAGS)
#define gSprY        RAM_U8(DT_ADDR_SPR_Y)
#define gSprX        RAM_U8(DT_ADDR_SPR_X)

#define FLAG_ON_SCREEN    0x80
#define FLAG_FLIP         0x40
#define FLAG_HUD_BAND     0x10
#define FLAG_ONE_SHOT     0x08
#define FLAG_PRIORITY     0x04
#define FLAG_BLINK        0x02
#define STATUS_LIVE       0x80
#define STATUS_WORLD      0x20
#define STATUS_HIDDEN     0x10
#define FRAME_LOOPS       0x80
#define TIMER_FROZEN      0x80
#define SLOT_SKIPPED      0xFF
#define OAM_PARKED_Y      0xF8
#define OFF_PAGE_KEEP     0x40 /* an object on another page closer than this is kept */
#define TYPE_NEVER_CULLED 0x5A

static uint8_t s_y;

static uint8_t rom_at(uint16_t ptr, uint8_t index)
{
    return Game_ReadRom((uint16_t)(ptr + index));
}

static uint16_t rom_pointer(uint16_t lo_table, uint16_t hi_table, uint8_t index)
{
    return (uint16_t)(Game_ReadRom((uint16_t)(hi_table + index)) << 8 | Game_ReadRom((uint16_t)(lo_table + index)));
}

static void free_slot(int slot)
{
    gObjects->flags[slot] = 0;
    gObjects->record[slot] = 0xFF;
}

static void delete_slot(int slot)
{
    gObjects->flags[slot] = 0;
    gObjects->state[slot] = 0;
    gObjects->record[slot] = 0xFF;
}

/* Screen X from world X; false when the slot is not drawn this frame. An
 * object on another page is deleted when it is far from the screen. */
static bool place_on_screen(int slot)
{
    if (!(gObjects->state[slot] & STATUS_WORLD)) {
        gObjects->x[slot] = gObjects->worldX[slot];
        gObjects->flags[slot] |= FLAG_ON_SCREEN;
        return true;
    }
    uint16_t dx = (uint16_t)(Obj_GetWorldX(slot) - (gCameraPage << 8 | gCameraX));
    uint8_t screenX = (uint8_t)dx;
    gObjects->x[slot] = screenX;
    if (!(dx >> 8)) {
        gObjects->flags[slot] |= FLAG_ON_SCREEN;
        return !(gObjects->state[slot] & STATUS_HIDDEN);
    }
    uint8_t distance = screenX & 0x80 ? (uint8_t)-screenX : screenX;
    if (gObjects->action[slot] != TYPE_NEVER_CULLED && distance >= OFF_PAGE_KEEP) {
        delete_slot(slot);
        return false;
    }
    gObjects->x[slot] = 0;
    gObjects->flags[slot] &= ~FLAG_ON_SCREEN;
    return false;
}

/* Advance the slot's animation by one frame of time; false when a one-shot
 * animation ended and the slot was deleted. */
static bool animate(int slot, uint16_t script)
{
    if (gPlayerMode == DT_MODE_FROZEN || (gObjects->animTimer[slot] & TIMER_FROZEN)) {
        return true;
    }
    uint8_t tick = gObjects->animTimer[slot] & 0x7F;
    gObjects->animTimer[slot]++;
    s_y = 1;
    if (tick != rom_at(script, 1)) {
        return true;
    }
    s_y = 0;
    gObjects->animTimer[slot] = 0;
    uint8_t frame = gObjects->animFrame[slot] & 0x7F;
    gObjects->animFrame[slot]++;
    if (frame != rom_at(script, 0)) {
        return true;
    }
    if (gObjects->flags[slot] & FLAG_ONE_SHOT) {
        delete_slot(slot);
        return false;
    }
    gObjects->animFrame[slot] = gObjects->animFrame[slot] & FRAME_LOOPS ? gObjects->pose[slot] | FRAME_LOOPS : 0;
    return true;
}

/* A sprite edge stays on screen when adding its signed offset did not carry
 * the position past 0 or 255. */
static bool offset_fits(uint8_t base, uint8_t offset)
{
    bool carry = base + offset > 0xFF;
    return offset & 0x80 ? carry : !carry;
}

/* $80BE: one object slot into the OAM shadow. A sprite that falls off the
 * screen, or into the HUD band, is parked in the entry it was being built in
 * and does not use it. */
static void draw_slot(int slot)
{
    Decomp_Enter(DT_CODE_OAM_SLOT, 0, (uint8_t)slot, 0);
    if (!place_on_screen(slot)) {
        return;
    }
    if ((gObjects->flags[slot] & FLAG_BLINK) && !(gSprPhase & 0x02)) {
        return;
    }
    gFlipBit = gObjects->flags[slot] & FLAG_FLIP;
    gSlotFlags = gFlipBit ? 1 : 0;
    s_y = gObjects->action[slot];
    uint16_t script = rom_pointer(DT_SPR_SCRIPT_LO, DT_SPR_SCRIPT_HI, gObjects->action[slot]);
    Ram_SetU16(DT_ADDR_SPR_SCRIPT, DT_ADDR_SPR_SCRIPT + 1, script);
    if (!animate(slot, script)) {
        return;
    }
    uint16_t meta = rom_pointer(DT_SPR_META_LO, DT_SPR_META_HI,
                                rom_at(script, (uint8_t)((gObjects->animFrame[slot] & 0x7F) + 2)));
    Ram_SetU16(DT_ADDR_SPR_META, DT_ADDR_SPR_META + 1, meta);
    gSprX = gObjects->x[slot];
    gSprY = gObjects->y[slot];
    gSpritesLeft = rom_at(meta, 0);
    uint8_t table = (uint8_t)(rom_at(meta, 1) + gSlotFlags);
    gSlotFlags = gObjects->flags[slot];
    gPriorityBit = (uint8_t)((gSlotFlags & FLAG_PRIORITY) << 3);
    uint16_t offsets = (uint16_t)(rom_pointer(DT_SPR_OFFSET_LO, DT_SPR_OFFSET_HI, table) - 2);
    Ram_SetU16(DT_ADDR_SPR_OFFSETS, DT_ADDR_SPR_OFFSETS + 1, offsets);
    uint8_t entry = gOamNext;
    s_y = 1;
    if (!entry) {
        return;
    }
    uint8_t i = 1;
    do {
        uint8_t *oam = &gOam[entry];
        i++;
        oam[1] = rom_at(meta, i);
        uint8_t dy = rom_at(offsets, i);
        oam[0] = (uint8_t)(gSprY + dy);
        bool in_hud_band = gSprSlot != PLAYER_SLOT && !(gSlotFlags & FLAG_HUD_BAND) && oam[0] < DT_HUD_BAND_Y;
        bool shown = offset_fits(gSprY, dy) && !in_hud_band;
        i++;
        if (shown) {
            oam[2] = (uint8_t)((rom_at(meta, i) ^ gFlipBit) | gPriorityBit);
            uint8_t dx = rom_at(offsets, i);
            oam[3] = (uint8_t)(gSprX + dx);
            shown = offset_fits(gSprX, dx);
        }
        s_y = i;
        if (!shown) {
            oam[0] = OAM_PARKED_Y;
            continue;
        }
        entry = (uint8_t)(entry + 4);
        gOamNext = entry;
        if (!entry) {
            return;
        }
    } while ((int8_t)--gSpritesLeft >= 0);
}

static void copy_hud_group(uint16_t group, uint8_t *entry)
{
    for (int i = 0; i < 0x100 && gRam[group + i] != DT_HUD_RECORD_END_Y; i++) {
        gOam[(*entry)++] = gRam[group + i];
    }
}

static bool hud_shown(void)
{
    return (RAM_U8(DT_ADDR_HUD_GATE) & 0x80) && !RAM_U8(DT_ADDR_MESSAGE) && !RAM_U8(DT_ADDR_ENDING) && !gPaused &&
           gPlayerMode != DT_MODE_FROZEN && gObjects->y[PLAYER_SLOT] >= DT_HUD_MIN_SCROOGE_Y;
}

static void visit_slot(int slot)
{
    if (!(gObjects->state[slot] & STATUS_LIVE)) {
        free_slot(slot);
        return;
    }
    if (gObjects->flags[slot] != SLOT_SKIPPED) {
        draw_slot(slot);
    }
}

/* bank 5 $8000; returns the Y it leaves. */
static uint8_t Oam_Build(void)
{
    Decomp_Enter(DT_CODE_OAM_BUILD, 0, 0, 0);
    gObjects->flags[PLAYER_SLOT] &= (uint8_t)~(FLAG_BLINK | 0x01);
    if (gPlayerInvulnerable | gPlayerPower) {
        gObjects->flags[PLAYER_SLOT] |= FLAG_BLINK;
    }
    uint8_t entry = 4;
    gOamNext = entry;
    if (hud_shown()) {
        copy_hud_group(DT_ADDR_HUD_GROUP_HP, &entry);
        copy_hud_group(DT_ADDR_HUD_GROUP_LIVES, &entry);
        copy_hud_group(DT_ADDR_HUD_GROUP_TIME, &entry);
        gOamNext = entry;
    }
    s_y = entry;
    gSprPhase++;
    if (!(gSprPhase & 1)) {
        for (gSprSlot = 0; gSprSlot != OBJ_SLOTS; gSprSlot++) {
            visit_slot(gSprSlot);
        }
    } else {
        for (gSprSlot = OBJ_SLOTS - 1; !(gSprSlot & 0x80); gSprSlot--) {
            visit_slot(gSprSlot);
        }
    }
    return s_y;
}

static void Run_OamBuild(Regs *r)
{
    r->y = Oam_Build();
    r->x = gSprSlot;
}

static void Run_DrawSlot(Regs *r)
{
    draw_slot(r->x);
}

const DecompRoutine gSpriteRoutines[] = {
    { DT_CODE_OAM_BUILD, "ducktales.decomp.5.8000", "Oam_Build", Run_OamBuild, OUT_X | OUT_Y, .bank = DT_SPR_BANK },
    { DT_CODE_OAM_SLOT, NULL, "Oam_DrawSlot", Run_DrawSlot, 0, .bank = DT_SPR_BANK },
};
const int gSpriteRoutineCount = sizeof gSpriteRoutines / sizeof gSpriteRoutines[0];
