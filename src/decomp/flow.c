/*
 * flow.c - the game flow of the main task, decompiled from the fixed bank
 * (docs/game-state.md, docs/levels.md, docs/decompilation.md "Task code").
 *
 * The main task runs the title, Land Select, the land load and then the
 * frame loop, once per frame, until a land is left. Almost all of it waits
 * for frames, switches banks and drives the PPU, so it is task code: each
 * function below that owns a site ends either by returning or by handing off
 * to an original routine (task_yield, a fade, the PPU loaders, the object code
 * of bank 6), and the site of the address the original's JSR returns to
 * continues it. Values the original keeps on the stack across a frame (frame
 * counters, the land and row around a redraw) are pushed and pulled as it
 * does. "r" is the registers of the original at that point: they are what
 * the routines called next receive.
 *
 * Plain routines (no frame waits): the HUD timer, the HUD refreshes, the
 * per-land tables and the HUD template.
 *
 * Original routines called from here: through Decomp_Call the sprite builder
 * (bank 5 $8000), the pause text (bank 5 $B580), the spawner set-up (bank 6
 * $AE60), $C267, $C2E6, $F2C7 and $F812; handed off to: task_yield,
 * the camera $EFE2, the object spawner (bank 6 $AEA0), the fades $C279/$C282, $C197/$C1A1, $C42C, $C830, the object
 * code (bank 6 $8000), Scrooge's update $E354 and the routines of this
 * subsystem that are not decompiled yet (boss trigger, Land Select, land load,
 * scenes, ending, reset).
 */
#include "decomp/decomp.h"
#include "decomp/sound.h"

#include "ducktales/events.h"
#include "ducktales/game.h"
#include "ducktales/hud.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"

#define P_ PLAYER_SLOT

#define SFX_PAUSE       0x15
#define SFX_WARP        0x2D
#define SFX_TIME_LOW    0x36
#define MUSIC_BOSS      0x06
#define MUSIC_GAME_OVER 0x09
#define MUSIC_DEATH     0x0A

#define RESPAWN_PENDING       0x80
#define RESPAWN_LIFE          0x87 /* entries from here cost a life */
#define RESPAWN_ARENA         0x85 /* the warp into a boss arena */
#define LAND_ARENA            5
#define HAZARD_SPIKES         6 /* hazard mode that invulnerability ignores */
#define CHECKPOINT_SET        5
#define TIME_LOW_TENS         3
#define DEATH_PAUSE_FALL      0xB4 /* frames after a death */
#define DEATH_PAUSE_WARP      0x3C /* frames after a warp */
#define GAME_OVER_FRAMES      0xF0
#define LAND_EXIT_FRAMES      0xB4
#define SPLIT_ON              0x40
#define ROW_SCROLL_ADDR       0xC0
#define SCROOGE_RESPAWN       0xA0 /* $0700 of Scrooge after a respawn */
#define RESPAWN_ENTRY_LEN     9
#define NT_FILL_ADDR          0x20 /* $C203: nametables from $2000 */
#define NT_FILL_TILE          0x24
#define PPU_STRINGS_LAND      0 /* $BAB0 set drawn at every land load */
#define ROW_SCROLL_HI         0x24
#define SCROOGE_START_Y       0x40
#define SCROOGE_JUMP_VY       0x04BB /* take-off velocity, whole and fraction */
#define SCROOGE_START_SPEED   0x0180
#define SPRITE0_Y             0x2C
#define ALL_TREASURES         0x1F
#define TREASURE_TRANSYLVANIA 0x02
#define REDIRECT_LAND_SELECT  0x80
#define MAIN_TASK_STACK       0xBF
#define TITLE_SEED            0x88
#define TITLE_OBJECTS         2
#define TITLE_STRINGS         2 /* $BAB0 set of the title */
#define TITLE_CHR             6 /* $C301 set of the title */
#define TITLE_PALETTE         0x60
#define TITLE_COLOUR_TEXT     0x11
#define TITLE_COLOUR_A        0x2C
#define TITLE_COLOUR_B        0x20
#define TITLE_PAD_MOVE        (DT_PAD_SELECT | DT_PAD_LEFT | DT_PAD_RIGHT)
#define TITLE_BLINK_FRAMES    0xB4
#define SFX_CURSOR            0x2F
#define DIFFICULTY_COUNT      3

#define gScratch0        RAM_U8(DT_ADDR_SCRATCH0)
#define gStreamSteps     RAM_U8(DT_ADDR_SCRATCH4)
#define gStreamQueueEnd  RAM_U8(DT_ADDR_SCRATCH5)
#define gScreensLeft     RAM_U8(DT_ADDR_SCRATCH11)
#define gVramPending     RAM_U8(DT_ADDR_VRAM_PENDING)
#define gTileEditPending RAM_U8(DT_ADDR_TILE_EDIT_PENDING)
#define gTileEditOffset  RAM_U8(DT_ADDR_TILE_EDIT_OFFSET)
#define gStreamPage      RAM_U8(DT_ADDR_STREAM_PAGE)
#define gStreamRequest   RAM_U8(DT_ADDR_MOVE_DIR)
#define gUnk2E           RAM_U8(DT_ADDR_UNK_2E)
#define gHeldSlot        RAM_U8(DT_ADDR_HELD_SLOT)
#define gBlockOpened     RAM_U8(DT_ADDR_BLOCK_OPENED)
#define gRowChrState     RAM_U8(DT_ADDR_ROW_CHR_STATE)
#define gLandLeft        RAM_U8(DT_ADDR_LAND_LEFT)
#define gMessage         RAM_U8(DT_ADDR_MESSAGE)
#define gHazard          RAM_U8(DT_ADDR_HAZARD)
#define gHudMoneyDirty   RAM_U8(DT_ADDR_HUD_MONEY_DIRTY)
#define gHudTotalDirty   RAM_U8(DT_ADDR_HUD_TOTAL_DIRTY)
#define gScene           RAM_U8(DT_ADDR_SCENE)
#define gEnding          RAM_U8(DT_ADDR_ENDING)
#define gPogoCeiling     RAM_U8(DT_ADDR_POGO_CEILING)
#define gNoSpriteDma     RAM_U8(DT_ADDR_NO_SPRITE_DMA)
#define gSpritePhase     RAM_U8(DT_ADDR_SPR_PHASE)
#define gRowScroll       RAM_U8(DT_ADDR_ROW_SCROLL)
#define gRowScrollAddr   RAM_U8(DT_ADDR_ROW_SCROLL_ADDR)
#define gHudSplit        RAM_U8(DT_ADDR_HUD_SPLIT)
#define gBgOffBelowSplit RAM_U8(DT_ADDR_BG_OFF_BELOW_SPLIT)
#define gPrevCameraX     RAM_U8(DT_ADDR_PREV_CAMERA_X)
#define gRowScrollAddrHi RAM_U8(DT_ADDR_ROW_SCROLL_ADDR_HI)
#define gSprite0Y        RAM_U8(DT_ADDR_SPRITE0_Y)
#define gHudGate         RAM_U8(DT_ADDR_HUD_GATE)
#define gTimeDigit(i)    RAM_U8(DT_ADDR_TIME_DIGITS + 4 * (i))

static uint8_t Rom(uint16_t addr)
{
    return Game_ReadRom(addr);
}

/* A byte of the current map's screen grid (pointer at $20). */
static uint8_t Map_Cell(uint8_t index)
{
    uint16_t addr = (uint16_t)(Ram_GetU16(DT_ADDR_MAP_GRID, DT_ADDR_MAP_GRID + 1) + index);
    return addr < GAME_RAM_SIZE ? gRam[addr] : Rom(addr);
}

static void Call(Regs *r, uint16_t routine)
{
    *r = Decomp_Call(routine, r->a, r->x, r->y);
}

static void SelectBank(Regs *r, uint8_t bank)
{
    Decomp_SelectBank(bank);
    r->a = r->x = bank;
}

static void Jsr(uint16_t routine, uint16_t resume, uint8_t uses)
{
    Decomp_Handoff(routine, resume, uses);
}

static void Jump(uint16_t target, uint8_t uses)
{
    Decomp_Handoff(target, 0, uses);
}

static void Yield(uint16_t resume)
{
    Decomp_Handoff(DT_CODE_TASK_YIELD, resume, OUT_X | OUT_Y);
}

/* JSR from task code to task code in C: true when the callee returned. */
static bool Gosub(void (*routine)(Regs *), Regs *r, uint16_t resume)
{
    Decomp_PushReturn(resume);
    routine(r);
    if (Decomp_HandedOff()) {
        return false;
    }
    Decomp_PullReturn();
    return true;
}

/* The sprite list rebuilt with the NMI's sprite DMA held off. */
static void BuildSprites(Regs *r, bool clear)
{
    gNoSpriteDma++;
    if (clear) {
        Call(r, DT_CODE_OAM_CLEAR);
    }
    SelectBank(r, DT_BANK_SPRITES);
    Call(r, DT_B5_BUILD_OAM);
    gNoSpriteDma--;
}

static void SoundRequest(Regs *r, uint8_t id)
{
    Sound_Request(id, r->x);
    r->a = id;
}

static void MusicRequest(Regs *r, uint8_t id)
{
    Music_Request(id, r->x);
    r->a = id;
}

static void Copy(uint16_t dst, uint16_t src, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        gRam[dst + i] = gRam[src + i];
    }
}

static void CopyRom(uint16_t dst, uint16_t src, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        gRam[dst + i] = Rom((uint16_t)(src + i));
    }
}

/* ---- HUD ---- */

/* $E28C: the HUD time loses a second every 60 frames; at 000 the time box
 * shows the time-out word and the next frame kills Scrooge. Stops while he
 * dies or holds up a treasure. Below 30 seconds a warning sounds every 16
 * frames. */
static void Timer_Tick(Regs *r)
{
    Decomp_Enter(DT_CODE_TIMER_TICK, r->a, r->x, r->y);
    if (gPlayerMode == PLAYER_MODE_TREASURE || gPlayerMode == PLAYER_MODE_DEATH) {
        return;
    }
    if (gTimeDigit(0) == DT_TIME_TILE_UP0) {
        gPlayerMode = PLAYER_MODE_DEATH;
        gFinalArena = 0;
        return;
    }
    Decomp_RaiseEvent(EVENT_TIMER_TICK);
    if (--gTickFrames == 0) {
        gTickFrames = DT_TICK_FRAMES_RELOAD;
        uint8_t borrow = 1;
        for (int i = 2; i >= 0; i--) {
            uint8_t digit = gTimeDigit(i);
            uint8_t next = (uint8_t)(digit - borrow);
            borrow = digit < borrow;
            gTimeDigit(i) = borrow ? (uint8_t)(next + 10) : next;
        }
        gScratch0 = 0;
        r->x = 0xFC;
        if ((gTimeDigit(0) | gTimeDigit(1) | gTimeDigit(2)) == 0) {
            gTimeDigit(0) = DT_TIME_TILE_UP0;
            gTimeDigit(1) = DT_TIME_TILE_UP1;
            gTimeDigit(2) = DT_TIME_TILE_UP2;
        }
    }
    if (gTimeDigit(0) == 0 && gTimeDigit(1) < TIME_LOW_TENS && !(gSpritePhase & 0x0F)) {
        SoundRequest(r, SFX_TIME_LOW);
    }
}

/* $DE6C: queue the money for drawing (after a pickup changed it). Blank
 * digits after the first shown one become zeros. Waits while the VRAM queue
 * is still pending. */
static void Hud_RefreshMoney(Regs *r)
{
    Decomp_Enter(DT_CODE_HUD_MONEY, r->a, r->x, r->y);
    if (gVramPending) {
        return;
    }
    uint8_t first = 0;
    while (first < DT_MONEY_DIGIT_COUNT && gRam[DT_ADDR_MONEY_DIGITS + first] >= DT_MONEY_TILE_BLANK) {
        first++;
    }
    for (uint8_t i = first; i < DT_MONEY_DIGIT_COUNT; i++) {
        if (gRam[DT_ADDR_MONEY_DIGITS + i] == DT_MONEY_TILE_BLANK) {
            gRam[DT_ADDR_MONEY_DIGITS + i] = 0;
        }
    }
    Copy(DT_ADDR_VRAM_QUEUE, DT_ADDR_HUD_MONEY_PACKET, DT_HUD_MONEY_PACKET_LEN);
    gVramPending = 0xFF;
    gHudMoneyDirty = 0;
    r->a = 0;
    r->y = 0xFF;
}

/* $DEA3: queue TOTAL for drawing. */
static void Hud_RefreshTotal(Regs *r)
{
    Decomp_Enter(DT_CODE_HUD_TOTAL, r->a, r->x, r->y);
    if (gVramPending) {
        return;
    }
    gHudTotalDirty = 0;
    Copy(DT_ADDR_VRAM_QUEUE, DT_ADDR_HUD_TOTAL_PACKET, DT_HUD_TOTAL_PACKET_LEN);
    gVramPending = 0xFF;
    r->a = gRam[DT_ADDR_HUD_TOTAL_PACKET];
    r->x = 0xFF;
}

/* $D60A: the TOTAL packet and the HP dots as a new game starts them. */
static void Hud_Reset(Regs *r)
{
    Decomp_Enter(DT_CODE_HUD_TEMPLATE, r->a, r->x, r->y);
    CopyRom(DT_ADDR_HUD_TOTAL_PACKET, DT_DATA_HUD_TOTAL, DT_HUD_TOTAL_PACKET_LEN);
    CopyRom(DT_ADDR_HUD_SPRITES, DT_DATA_HUD_HP, DT_HUD_HP_TEMPLATE_LEN);
    r->a = Rom(DT_DATA_HUD_HP);
    r->y = 0xFF;
}

/* ---- lands ---- */

/* $D9F5: background colours of set (an offset of 16 per set) and the sprite
 * colours, into the palette as loaded ($0100) and as shown ($0300). */
static void Land_LoadPalette(Regs *r, uint8_t set)
{
    Decomp_Enter(DT_CODE_LAND_PALETTE, r->a, set, r->y);
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t bg = Rom((uint16_t)(DT_DATA_PALETTES + (uint8_t)(set + i)));
        uint8_t sprite = Rom((uint16_t)(DT_DATA_SPRITE_PALETTE + i));
        gRam[DT_ADDR_PALETTE_SHOWN + i] = gRam[DT_ADDR_PALETTE + i] = bg;
        gRam[DT_ADDR_PALETTE_SHOWN + 0x10 + i] = gRam[DT_ADDR_PALETTE + 0x10 + i] = sprite;
        r->a = sprite;
    }
    r->x = (uint8_t)(set + 16);
    r->y = 16;
}

/* $D9CA: the current land's music, tileset bank, start screen, row and X,
 * and its palette. */
static void Land_ApplyTables(Regs *r)
{
    Decomp_Enter(DT_CODE_LAND_TABLES, r->a, r->x, r->y);
    uint8_t land = gLand;
    MusicRequest(r, land);
    gRam[DT_ADDR_TILESET_BANK] = Rom((uint16_t)(DT_DATA_LAND_TILESET + land));
    gCameraScreen = Rom((uint16_t)(DT_DATA_LAND_SCREEN + land));
    gCameraPage = gObjects->worldPage[P_] = gStreamPage = gCameraScreen & 7;
    gObjects->worldX[P_] = Rom((uint16_t)(DT_DATA_LAND_START_X + land));
    gMapRow = Rom((uint16_t)(DT_DATA_LAND_ROW + land));
    Land_LoadPalette(r, (uint8_t)(land << 4));
}

/* ---- respawn ---- */

static bool Respawn_Advance(void);

/* $DD98: draw the next screen of the respawn area through $C830, skipping
 * cells without a screen. */
static void Respawn_DrawScreens(Regs *r)
{
    for (;;) {
        gStreamSteps = 1;
        gStreamQueueEnd = 0;
        r->y = gStreamScreen;
        r->a = Map_Cell(r->y);
        if (r->a != 0xFF) {
            Jsr(DT_CODE_STREAM_BLOCK, DT_FLOW_RESPAWN_DRAWN, OUT_A | OUT_X | OUT_Y);
            return;
        }
        if (!Respawn_Advance()) {
            return;
        }
    }
}

/* One block step drawn; false when it was the last one. */
static bool Respawn_Advance(void)
{
    if ((int8_t)--gScreensLeft < 0) {
        return false;
    }
    gStreamPos = (uint8_t)((gStreamPos + 1) & 0x3F);
    if (gStreamPos == 0) {
        gStreamScreen++;
        gStreamPage++;
    }
    return true;
}

/* $DDAB */
static void Respawn_Drawn(Regs *r)
{
    Decomp_Enter(DT_FLOW_RESPAWN_DRAWN, r->a, r->x, r->y);
    if (Respawn_Advance()) {
        Respawn_DrawScreens(r);
    }
}

/* $DD52: put the camera, the map row and Scrooge at a respawn or warp entry
 * (levels.md "Respawn and warp table") and draw the screens around him. */
static void Respawn_PlaceAt(Regs *r, uint8_t entry)
{
    Decomp_Enter(DT_CODE_RESPAWN_ENTRY, entry, r->x, r->y);
    gScratch0 = entry;
    uint16_t e = (uint16_t)(DT_DATA_RESPAWN + (uint8_t)(entry * RESPAWN_ENTRY_LEN));
    gMapRow = Rom(e);
    gCameraX = Rom((uint16_t)(e + 1));
    gCameraPage = Rom((uint16_t)(e + 2));
    gScreensLeft = Rom((uint16_t)(e + 3));
    gStreamScreen = Rom((uint16_t)(e + 4));
    gStreamPage = gStreamScreen & 7;
    gStreamPos = Rom((uint16_t)(e + 5));
    gObjects->y[P_] = Rom((uint16_t)(e + 6));
    gObjects->worldX[P_] = Rom((uint16_t)(e + 7));
    uint8_t screen = Rom((uint16_t)(e + 8));
    gObjects->worldPage[P_] = screen & 7;
    gCameraScreen = (uint8_t)((screen & 0xF8) | gCameraPage);
    Respawn_DrawScreens(r);
}

/* $DD4E: at the entry of the pending respawn request. */
static void Respawn_Place(Regs *r)
{
    Decomp_Enter(DT_CODE_RESPAWN_PLACE, r->a, r->x, r->y);
    Respawn_PlaceAt(r, gRespawn & 0x1F);
}

/* ---- death, warps and game over ---- */

static void DeathPause_Frame(Regs *r, uint8_t frames)
{
    Decomp_Push(frames);
    r->a = frames;
    BuildSprites(r, true);
    Yield(DT_FLOW_DEATH_PAUSE_NEXT);
}

/* $DD23: hold the picture for 3 seconds after a death, 1 after a warp,
 * keeping the sprites drawn. Returns A = 0. */
static void DeathPause(Regs *r)
{
    Decomp_Enter(DT_CODE_DEATH_PAUSE, r->a, r->x, r->y);
    DeathPause_Frame(r, gRespawn >= RESPAWN_LIFE ? DEATH_PAUSE_FALL : DEATH_PAUSE_WARP);
}

/* $DD42 (the frame count on the stack) */
static void DeathPause_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_PAUSE_NEXT, r->a, r->x, r->y);
    gObjects->animTimer[P_] = 0;
    uint8_t frames = (uint8_t)(Decomp_Pull() - 1);
    r->a = frames;
    if (frames) {
        DeathPause_Frame(r, frames);
    }
}

static void GameOver_Frame(Regs *r, uint8_t frames)
{
    Decomp_Push(frames);
    r->a = frames;
    BuildSprites(r, true);
    Yield(DT_FLOW_GAME_OVER_NEXT);
}

/* $DEB9: game-over music and box (in place of the time box), 4 seconds,
 * then the reset vector: a full restart at the title. */
static void GameOver(Regs *r)
{
    Decomp_Enter(DT_CODE_GAME_OVER, r->a, r->x, r->y);
    MusicRequest(r, MUSIC_GAME_OVER);
    CopyRom(DT_ADDR_HUD_GROUP_TIME, DT_DATA_GAME_OVER_SPRITES, DT_GAME_OVER_SPRITES_LEN);
    GameOver_Frame(r, GAME_OVER_FRAMES);
}

/* $DEDE (the frame count on the stack) */
static void GameOver_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_GAME_OVER_NEXT, r->a, r->x, r->y);
    uint8_t frames = (uint8_t)(Decomp_Pull() - 1);
    r->a = frames;
    if (frames) {
        GameOver_Frame(r, frames);
        return;
    }
    Jump(DT_CODE_RESET, 0);
}

static void LandExit_Frame(Regs *r, uint8_t frames)
{
    Decomp_Push(frames);
    r->a = frames;
    BuildSprites(r, false);
    Yield(DT_FLOW_LAND_EXIT_NEXT);
}

/* $DF08: a land was left: 3 seconds with the sprites drawn (none after a
 * scene), then Land Select. */
static void LandExit(Regs *r)
{
    Decomp_Enter(DT_CODE_LAND_EXIT, r->a, r->x, r->y);
    if (gScene) {
        Jump(DT_CODE_LAND_SELECT, 0);
        return;
    }
    LandExit_Frame(r, LAND_EXIT_FRAMES);
}

/* $DF1E (the frame count on the stack) */
static void LandExit_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_LAND_EXIT_NEXT, r->a, r->x, r->y);
    uint8_t frames = (uint8_t)(Decomp_Pull() - 1);
    r->a = frames;
    if (frames) {
        LandExit_Frame(r, frames);
        return;
    }
    Jump(DT_CODE_LAND_SELECT, 0);
}

static void Death_Fade(Regs *r);

/* $DC3E: a respawn or warp request ($7E bit 7). A scene request leaves the
 * land instead. An entry from $87 costs a life (game over without one); the
 * others are warps. The picture fades out, Scrooge and the camera move to the
 * entry, the screens are redrawn and the picture fades back in. */
static void DeathOrWarp(Regs *r)
{
    Decomp_Enter(DT_CODE_DEATH_OR_WARP, r->a, r->x, r->y);
    gPogoCeiling = 0;
    if (gScene) {
        LandExit(r);
        return;
    }
    if (gRespawn >= RESPAWN_LIFE) {
        MusicRequest(r, MUSIC_DEATH);
        if (gLives == 0) {
            GameOver(r);
            return;
        }
    } else {
        SoundRequest(r, SFX_WARP);
    }
    gPlayerInvulnerable = 1;
    if (Gosub(DeathPause, r, DT_FLOW_DEATH_FADE)) {
        Death_Fade(r);
    }
}

/* $DC68 */
static void Death_Fade(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_FADE, r->a, r->x, r->y);
    Jsr(DT_CODE_FADE_OUT, DT_FLOW_DEATH_BLANK, 0);
}

/* $DC6B: the picture is black: reset the land's per-life state and turn
 * rendering off for the redraw. */
static void Death_Blank(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_BLANK, r->a, r->x, r->y);
    gHeldSlot = 0;
    gBossFight = 0;
    for (int i = 0x0B; i >= 0; i--) {
        gRam[DT_ADDR_HAZARD + i] = 0;
    }
    gRowScroll = 0;
    gRowScrollAddr = ROW_SCROLL_ADDR;
    r->a = ROW_SCROLL_ADDR;
    r->x = 0xFF;
    Jsr(DT_CODE_SCREEN_OFF, DT_FLOW_DEATH_PLACE, 0);
}

static void Death_Placed(Regs *r);

/* $DC81 */
static void Death_Place(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_PLACE, r->a, r->x, r->y);
    if (Gosub(Respawn_Place, r, DT_FLOW_DEATH_PLACED)) {
        Death_Placed(r);
    }
}

/* $DC84: the land and row are kept on the stack while the row's sprite CHR
 * streams in; a death costs a life and refills the HP dots, the arena warp
 * streams the arena's CHR. */
static void Death_Placed(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_PLACED, r->a, r->x, r->y);
    Call(r, DT_CODE_CLEAR_OBJECTS);
    Decomp_Push(gLand);
    Decomp_Push(gMapRow);
    if (gRespawn >= RESPAWN_LIFE) {
        gObjects->action[P_] = gObjects->animTimer[P_] = gObjects->animFrame[P_] = 0;
        Decomp_RaiseEvent(EVENT_LIFE_LOST);
        gLives--;
        Call(r, DT_CODE_HP_REFILL);
    } else if (gRespawn == RESPAWN_ARENA) {
        gMapRow = 0;
        gLand = LAND_ARENA;
        gBossFight = 0xFF;
    }
    gRowChrState = 0;
    Jsr(DT_CODE_ROW_CHR_STREAM, DT_FLOW_DEATH_ROW_CHR, 0);
}

/* $DCBD (the land and row on the stack) */
static void Death_RowChr(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_ROW_CHR, r->a, r->x, r->y);
    if (!(gRowChrState & 0x80)) {
        Jsr(DT_CODE_ROW_CHR_STREAM, DT_FLOW_DEATH_ROW_CHR, 0);
        return;
    }
    gMapRow = Decomp_Pull();
    gLand = Decomp_Pull();
    gStreamRequest = 1;
    gStreamDir = 1;
    SelectBank(r, DT_BANK_OBJECTS);
    Call(r, DT_B6_SPAWN_RESET);
    Jsr(DT_CODE_SCREEN_ON, DT_FLOW_DEATH_SHOWN, 0);
}

/* $DCD8 */
static void Death_Shown(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_SHOWN, r->a, r->x, r->y);
    Yield(DT_FLOW_DEATH_FADE_IN);
}

/* $DCDB */
static void Death_FadeIn(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_FADE_IN, r->a, r->x, r->y);
    gHudSplit = SPLIT_ON;
    r->a = SPLIT_ON;
    Jsr(DT_CODE_FADE_IN, DT_FLOW_DEATH_RESUME, 0);
}

static void Death_Done(Regs *r);

/* $DCE2: the boss music again in a boss fight; after a death the land's
 * music and its start time; then the objects spawn and a pause follows. */
static void Death_Resume(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_RESUME, r->a, r->x, r->y);
    r->y = gBossFight;
    if (gBossFight & 0x80) {
        MusicRequest(r, MUSIC_BOSS);
    } else if (gMusic == MUSIC_DEATH) {
        uint8_t land = gLand;
        r->y = (uint8_t)(land << 1);
        gTimeDigit(0) = Rom((uint16_t)(DT_DATA_LAND_TIME + r->y));
        gTimeDigit(1) = Rom((uint16_t)(DT_DATA_LAND_TIME + r->y + 1));
        gTimeDigit(2) = 0;
        gTickFrames = DT_TICK_FRAMES_RELOAD;
        MusicRequest(r, land);
    }
    SelectBank(r, DT_BANK_OBJECTS);
    Jsr(DT_B6_SPAWN, DT_FLOW_DEATH_SPAWNED, 0);
}

/* $DD14 */
static void Death_Spawned(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_SPAWNED, r->a, r->x, r->y);
    gObjects->state[P_] = SCROOGE_RESPAWN;
    r->a = SCROOGE_RESPAWN;
    if (Gosub(DeathPause, r, DT_FLOW_DEATH_DONE)) {
        Death_Done(r);
    }
}

/* $DD1C */
static void Death_Done(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_DONE, r->a, r->x, r->y);
    gPlayerInvulnerable = gPlayerMode = gRespawn = 0;
    r->a = 0;
}

/* ---- the frame loop ---- */

static void FrameLoop_End(Regs *r);
static void BossTrigger(Regs *r);
static void FrameLoop_AfterBossTrigger(Regs *r);
static void LandLoad(Regs *r);
static void FrameLoop_Yield(Regs *r);
static void FrameLoop_Requests(Regs *r);
static void FrameLoop_Sprites(Regs *r);
static void FrameLoop_AfterSpawn(Regs *r);

/* $DB36: one frame of play. The ending replaces it once started. Start
 * toggles the pause (not while a message is up), and a paused frame does
 * nothing but keep the sprites. Then Scrooge moves. */
static void FrameLoop(Regs *r)
{
    Decomp_Enter(DT_CODE_FRAME_LOOP, r->a, r->x, r->y);
    if (gEnding) {
        Jsr(DT_CODE_ENDING, DT_FLOW_AFTER_ENDING, 0);
        return;
    }
    if (gPlayerMode != DT_MODE_FROZEN) {
        if (gPadNew & DT_PAD_START) {
            SoundRequest(r, SFX_PAUSE);
            gPaused ^= 1;
            r->a = gPaused;
            Call(r, DT_CODE_OAM_CLEAR);
            SelectBank(r, DT_BANK_SPRITES);
            Call(r, DT_B5_BUILD_OAM);
            Call(r, DT_B5_PAUSE_TEXT);
        }
        if (gPaused) {
            FrameLoop_End(r);
            return;
        }
    }
    Jsr(DT_CODE_PLAYER_UPDATE, DT_FLOW_AFTER_PLAYER, OUT_X);
}

/* $DB6F: the objects move, unless a message is up. */
static void FrameLoop_AfterPlayer(Regs *r)
{
    Decomp_Enter(DT_FLOW_AFTER_PLAYER, r->a, r->x, r->y);
    if (gMessage) {
        FrameLoop_Requests(r);
        return;
    }
    SelectBank(r, DT_BANK_OBJECTS);
    Jsr(DT_B6_OBJECTS, DT_FLOW_AFTER_OBJECTS, OUT_A | OUT_X | OUT_Y);
}

/* $DB7B: the timer; a land left ends the frame loop; outside a boss fight
 * the camera follows (it maps the tileset bank to draw new blocks) and the
 * boss trigger is tested. */
static void FrameLoop_AfterObjects(Regs *r)
{
    Decomp_Enter(DT_FLOW_AFTER_OBJECTS, r->a, r->x, r->y);
    Timer_Tick(r);
    if (gLandLeft) {
        LandExit(r);
        return;
    }
    if (gBossFight & 0x80) {
        FrameLoop_Requests(r);
        return;
    }
    Jsr(DT_CODE_CAMERA_UPDATE, DT_FLOW_AFTER_CAMERA, 0);
}

/* $DB8C */
static void FrameLoop_AfterCamera(Regs *r)
{
    Decomp_Enter(DT_FLOW_AFTER_CAMERA, r->a, r->x, r->y);
    if (Gosub(BossTrigger, r, DT_FLOW_AFTER_BOSS_TRIGGER)) {
        FrameLoop_AfterBossTrigger(r);
    }
}

/* $DB8F: a land redirect loads Transylvania (positive) or returns to Land
 * Select (negative). */
static void FrameLoop_AfterBossTrigger(Regs *r)
{
    Decomp_Enter(DT_FLOW_AFTER_BOSS_TRIGGER, r->a, r->x, r->y);
    uint8_t redirect = gLandRedirect;
    if (redirect == 0) {
        FrameLoop_Requests(r);
        return;
    }
    gLandRedirect = 0;
    if (!(redirect & 0x80)) {
        Call(r, DT_CODE_CLEAR_OBJECTS);
        gLand = LAND_TRANSYLVANIA;
        r->x = LAND_TRANSYLVANIA;
        LandLoad(r);
        return;
    }
    r->a = 0;
    Jump(DT_CODE_LAND_SELECT, 0);
}

static void FrameLoop_AfterDeath(Regs *r);

/* $DBA9: a respawn or warp request. */
static void FrameLoop_Requests(Regs *r)
{
    if (gRespawn & RESPAWN_PENDING) {
        if (!Gosub(DeathOrWarp, r, DT_FLOW_AFTER_DEATH)) {
            return;
        }
    }
    FrameLoop_AfterDeath(r);
}

/* $DBB0: HUD redraws, and a scene request. */
static void FrameLoop_AfterDeath(Regs *r)
{
    Decomp_Enter(DT_FLOW_AFTER_DEATH, r->a, r->x, r->y);
    if (gHudMoneyDirty) {
        Hud_RefreshMoney(r);
    }
    if (gHudTotalDirty) {
        Hud_RefreshTotal(r);
    }
    if (gScene & 0x80) {
        Jsr(DT_CODE_SCENE, DT_FLOW_FRAME_SPRITES, 0);
        return;
    }
    FrameLoop_Sprites(r);
}

/* $DBC5: the sprites, then the object spawner (it maps the tileset bank to
 * edit tiles). */
static void FrameLoop_Sprites(Regs *r)
{
    Decomp_Enter(DT_FLOW_FRAME_SPRITES, r->a, r->x, r->y);
    BuildSprites(r, true);
    if (gEnding || gBgOffBelowSplit) {
        FrameLoop_Yield(r);
        return;
    }
    SelectBank(r, DT_BANK_OBJECTS);
    Jsr(DT_B6_SPAWN, DT_FLOW_AFTER_SPAWN, 0);
}

/* $DBE4: a hazard's mode change. */
static void FrameLoop_AfterSpawn(Regs *r)
{
    Decomp_Enter(DT_FLOW_AFTER_SPAWN, r->a, r->x, r->y);
    gPrevCameraX = gCameraX;
    gUnk2E = 0;
    gBlockOpened = 0;
    uint8_t hazard = gHazard;
    if (hazard) {
        gHazard = 0;
        if (hazard != HAZARD_SPIKES || gPlayerInvulnerable == 0) {
            gPlayerMode = hazard;
        }
    }
    FrameLoop_End(r);
}

/* $DC04: the boss checkpoint and the tile edits for the NMI. */
static void FrameLoop_End(Regs *r)
{
    uint8_t land = gLand;
    if (gMapRow == Rom((uint16_t)(DT_DATA_CHECKPOINT_ROW + land)) &&
        gObjects->worldPage[P_] == Rom((uint16_t)(DT_DATA_CHECKPOINT_PAGE + land))) {
        gBossCheckpoint = CHECKPOINT_SET;
    }
    if (gTileEditOffset) {
        gTileEditPending = gTileEditOffset;
        gTileEditOffset = 0;
    }
    FrameLoop_Yield(r);
}

/* $DC23: the random generator steps once a frame; then the next frame. */
static void FrameLoop_Yield(Regs *r)
{
    Call(r, DT_CODE_RANDOM);
    Yield(DT_FLOW_NEXT_FRAME);
}

/* ---- title and difficulty ---- */

static void Title_Loop(Regs *r);

/* $D4E6: the main task starts (power-on and every reset): the title. */
static void MainTask(Regs *r)
{
    Decomp_Enter(DT_CODE_MAIN_TASK, r->a, r->x, r->y);
    r->x = MAIN_TASK_STACK;
    Decomp_SetStack(MAIN_TASK_STACK);
    r->a = RAM_U8(DT_ADDR_PPU_CTRL);
    Decomp_Store(DT_PPU_CTRL, r->a);
    Yield(DT_FLOW_TITLE_START);
}

/* $D4F1: the generator's seed, the HUD packets, the title music; fade out. */
static void Title_Start(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_START, r->a, r->x, r->y);
    RAM_U8(DT_ADDR_RANDOM) = TITLE_SEED;
    Hud_Reset(r);
    MusicRequest(r, DT_MUSIC_TITLE);
    Jsr(DT_CODE_FADE_OUT, DT_FLOW_TITLE_FADED, 0);
}

/* $D500 */
static void Title_Faded(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_FADED, r->a, r->x, r->y);
    gHudSplit = 0;
    gPlayerInvulnerable = 0;
    r->a = 0;
    Yield(DT_FLOW_TITLE_BLACK);
}

/* $D509 */
static void Title_Black(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_BLACK, r->a, r->x, r->y);
    Jsr(DT_CODE_SCREEN_OFF, DT_FLOW_TITLE_SCREEN_OFF, 0);
}

/* $D50C: no scroll, both nametables cleared. */
static void Title_ScreenOff(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_SCREEN_OFF, r->a, r->x, r->y);
    RAM_U8(DT_ADDR_SCROLL_X) = 0;
    RAM_U8(DT_ADDR_SCROLL_Y) = 0;
    gCameraPage = 0;
    gCameraX = 0;
    r->a = NT_FILL_ADDR;
    r->x = NT_FILL_TILE;
    r->y = 0;
    Jsr(DT_CODE_NT_FILL, DT_FLOW_TITLE_FILLED, OUT_A | OUT_X | OUT_Y);
}

/* $D51F: the title's strings. */
static void Title_Filled(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_FILLED, r->a, r->x, r->y);
    Call(r, DT_CODE_OAM_CLEAR);
    Call(r, DT_CODE_CLEAR_OBJECTS);
    SelectBank(r, DT_BANK_SPRITES);
    r->x = TITLE_STRINGS;
    Jsr(DT_B5_PPU_STRINGS, DT_FLOW_TITLE_STRINGS, OUT_X);
}

/* $D52F: the title's CHR. */
static void Title_Strings(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_STRINGS, r->a, r->x, r->y);
    r->x = TITLE_CHR;
    Jsr(DT_CODE_CHR_LOAD, DT_FLOW_TITLE_CHR, OUT_X);
}

/* $D534: the title's palette and its two objects (Scrooge and the
 * difficulty cursor). */
static void Title_Chr(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_CHR, r->a, r->x, r->y);
    r->x = TITLE_PALETTE;
    Land_LoadPalette(r, TITLE_PALETTE);
    gRam[DT_ADDR_PALETTE_SHOWN + 0x10] = gRam[DT_ADDR_PALETTE + 0x10] = TITLE_COLOUR_TEXT;
    for (int slot = TITLE_OBJECTS - 1; slot >= 0; slot--) {
        uint16_t e = (uint16_t)(DT_DATA_TITLE_OBJECTS + 3 * slot);
        gObjects->state[slot] = 0x80;
        gObjects->flags[slot] = 0x80;
        gObjects->action[slot] = Rom(e);
        gObjects->y[slot] = Rom((uint16_t)(e + 1));
        gObjects->worldX[slot] = Rom((uint16_t)(e + 2));
        gObjects->animTimer[slot] = 0;
        gObjects->animFrame[slot] = 0;
        gObjects->worldPage[slot] = 0;
    }
    r->x = 0xFF;
    r->y = 0xFD;
    gRam[DT_ADDR_PALETTE_SHOWN + 0x1A] = gRam[DT_ADDR_PALETTE + 0x1A] = TITLE_COLOUR_A;
    gRam[DT_ADDR_PALETTE_SHOWN + 0x1E] = gRam[DT_ADDR_PALETTE + 0x1E] = TITLE_COLOUR_B;
    gRam[DT_ADDR_PALETTE_SHOWN + 0x1F] = gRam[DT_ADDR_PALETTE + 0x1F] = TITLE_COLOUR_A;
    r->a = TITLE_COLOUR_A;
    Jsr(DT_CODE_SCREEN_ON, DT_FLOW_TITLE_SHOWN, 0);
}

/* $D58B */
static void Title_Shown(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_SHOWN, r->a, r->x, r->y);
    Yield(DT_FLOW_TITLE_FADE_IN);
}

/* $D58E */
static void Title_FadeIn(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_FADE_IN, r->a, r->x, r->y);
    Jsr(DT_CODE_FADE_IN, DT_FLOW_TITLE_LIT, 0);
}

/* $D591: the lives group of the HUD as a game starts it. */
static void Title_Lit(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_LIT, r->a, r->x, r->y);
    CopyRom(DT_ADDR_HUD_GROUP_LIVES, DT_DATA_HUD_LIVES, DT_HUD_LIVES_LEN);
    r->a = Rom(DT_DATA_HUD_LIVES);
    r->y = 0xFF;
    Yield(DT_FLOW_TITLE_READY);
}

/* $D59F: Normal is preselected. */
static void Title_Ready(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_READY, r->a, r->x, r->y);
    gDifficulty = 1;
    Title_Loop(r);
}

static void Title_Frame(Regs *r);

/* $D5A3 (and $D5D1 after each frame): Start goes to Land Select; Select,
 * Left or Right moves the cursor through the three difficulties (Left
 * upward, wrapping). */
static void Title_Loop(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_LOOP, r->a, r->x, r->y);
    r->a = gPadNew & DT_PAD_START;
    if (r->a) {
        Jump(DT_CODE_LAND_SELECT, 0);
        return;
    }
    r->a = gPadNew & TITLE_PAD_MOVE;
    if (r->a) {
        r->y = r->a & DT_PAD_LEFT;
        uint8_t difficulty = (uint8_t)(gDifficulty + Rom((uint16_t)(DT_DATA_DIFFICULTY_STEP + r->y)));
        if (difficulty >= DIFFICULTY_COUNT) {
            difficulty = (uint8_t)(difficulty + Rom((uint16_t)(DT_DATA_DIFFICULTY_STEP + r->y + 1)));
        }
        gDifficulty = difficulty;
        r->y = difficulty;
        gObjects->worldX[1] = Rom((uint16_t)(DT_DATA_DIFFICULTY_X + difficulty));
        SoundRequest(r, SFX_CURSOR);
    }
    if (Gosub(Title_Frame, r, DT_FLOW_TITLE_LOOP)) {
        Title_Loop(r);
    }
}

/* $D5D7: Scrooge's idle animation picks a new variant at random every 180
 * builder frames; the sprites, and the next frame. */
static void Title_Frame(Regs *r)
{
    Decomp_Enter(DT_CODE_TITLE_FRAME, r->a, r->x, r->y);
    if (gObjects->animFrame[P_] == 0 && gObjects->animTimer[P_] == 1) {
        gObjects->animTimer[P_] = 0;
        r->a = 0;
        r->x = gSpritePhase;
        if (gSpritePhase == TITLE_BLINK_FRAMES) {
            gSpritePhase = 0;
            uint8_t *random = &gRam[DT_ADDR_RANDOM];
            random[0] = (uint8_t)(random[0] + random[1] + 1);
            gObjects->animFrame[P_] = random[0] & 1;
            r->a = random[0] & 1;
        }
    }
    Call(r, DT_CODE_OAM_CLEAR);
    SelectBank(r, DT_BANK_SPRITES);
    Call(r, DT_B5_BUILD_OAM);
    Yield(DT_FLOW_TITLE_FRAME_DONE);
}

/* $D609 */
static void Title_FrameDone(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_FRAME_DONE, r->a, r->x, r->y);
}

/* ---- boss trigger ---- */

static uint16_t Rom16(uint16_t addr)
{
    return (uint16_t)(Rom(addr) | Rom((uint16_t)(addr + 1)) << 8);
}

static void BossTrigger_Scroll(Regs *r);

/* $CF93: Scrooge reached the land's boss trigger (row, and his X page taken
 * 8 pixels ahead or behind). In Transylvania with its treasure taken and not
 * all five held, back to Land Select; otherwise the camera scrolls into the
 * arena (map 5 at row = land, or row 5 for the final arena), with the land and
 * row kept on the stack. */
static void BossTrigger(Regs *r)
{
    Decomp_Enter(DT_CODE_BOSS_TRIGGER, r->a, r->x, r->y);
    uint8_t land = gLand;
    r->y = land;
    r->a = gMapRow;
    if (gMapRow != Rom((uint16_t)(DT_DATA_BOSS_ROW + land))) {
        return;
    }
    uint8_t dir = Rom((uint16_t)(DT_DATA_BOSS_DIR + land));
    r->x = dir & DT_PAD_LEFT;
    uint16_t ahead = Rom16((uint16_t)(DT_DATA_BOSS_AHEAD + r->x));
    uint8_t carry = gObjects->worldX[P_] + (ahead & 0xFF) > 0xFF;
    r->a = (uint8_t)(gObjects->worldPage[P_] + (ahead >> 8) + carry);
    if (r->a != Rom((uint16_t)(DT_DATA_BOSS_PAGE + land))) {
        return;
    }
    uint8_t treasures = gTreasures & ALL_TREASURES;
    if (land == LAND_TRANSYLVANIA && treasures != ALL_TREASURES && (treasures & TREASURE_TRANSYLVANIA)) {
        gLandRedirect = REDIRECT_LAND_SELECT;
        r->a = REDIRECT_LAND_SELECT;
        return;
    }
    gStreamRequest = dir;
    r->a = dir;
    Call(r, DT_CODE_CLEAR_OBJECTS);
    Decomp_Push(gLand);
    Decomp_Push(gMapRow);
    if (treasures == ALL_TREASURES) {
        gMapRow = LAND_ARENA;
        gFinalArena = LAND_ARENA;
    } else {
        gMapRow = r->y;
    }
    gLand = LAND_ARENA;
    gRowChrState = 0;
    BossTrigger_Scroll(r);
}

/* $CFF4: one pixel of the scroll, Scrooge carried along; the new columns'
 * CHR streams in. */
static void BossTrigger_Scroll(Regs *r)
{
    r->y = gStreamRequest & DT_PAD_LEFT;
    uint16_t camera = (uint16_t)((gCameraX | gCameraPage << 8) + Rom16((uint16_t)(DT_DATA_BOSS_CAMERA_STEP + r->y)));
    gCameraX = (uint8_t)camera;
    gCameraPage = (uint8_t)(camera >> 8);
    uint16_t step = Rom16((uint16_t)(DT_DATA_BOSS_X_STEP + r->y));
    uint16_t frac = (uint16_t)(gObjects->xFrac[P_] + (step & 0xFF));
    gObjects->xFrac[P_] = (uint8_t)frac;
    gObjects->worldX[P_] = (uint8_t)(gObjects->worldX[P_] + (step >> 8) + (frac >> 8));
    r->a = gObjects->worldX[P_];
    Jsr(DT_CODE_ROW_CHR_STREAM, DT_FLOW_BOSS_STREAMED, 0);
}

/* $D01E (the land and row on the stack): until the camera is page-aligned,
 * the sprites and the next frame; then the land and row back, the boss music,
 * and the fight starts. */
static void BossTrigger_Streamed(Regs *r)
{
    Decomp_Enter(DT_FLOW_BOSS_STREAMED, r->a, r->x, r->y);
    if (gCameraX != 0) {
        gPrevCameraX = gCameraX;
        gUnk2E = 0;
        r->a = 0;
        Call(r, DT_CODE_OAM_CLEAR);
        SelectBank(r, DT_BANK_SPRITES);
        Call(r, DT_B5_BUILD_OAM);
        Yield(DT_FLOW_BOSS_NEXT);
        return;
    }
    gMapRow = Decomp_Pull();
    gLand = Decomp_Pull();
    MusicRequest(r, MUSIC_BOSS);
    gBossFight = 0xFF;
    RAM_U8(DT_ADDR_PALETTE_REQUEST) = 0xFF;
    r->a = 0xFF;
}

/* $D038 (the land and row on the stack) */
static void BossTrigger_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_BOSS_NEXT, r->a, r->x, r->y);
    BossTrigger_Scroll(r);
}

/* ---- land load ---- */

static void LandLoad_Done(Regs *r);

/* $D79B: load the land in $F6. The picture fades out and rendering stops for
 * the redraw: both nametables cleared, the land's tables applied, the first
 * screens drawn, the HUD strings and the land's CHR uploaded, and the row's
 * sprite CHR streamed. Then the objects' spawn tables, Scrooge at his start
 * and the HUD, and the frame loop. */
static void LandLoad(Regs *r)
{
    Decomp_Enter(DT_CODE_LAND_LOAD, r->a, r->x, r->y);
    Jsr(DT_CODE_FADE_OUT, DT_FLOW_LOAD_FADED, 0);
}

/* $D79E */
static void LandLoad_Faded(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_FADED, r->a, r->x, r->y);
    gHudSplit = 0;
    r->a = 0;
    Yield(DT_FLOW_LOAD_BLACK);
}

/* $D7A5 */
static void LandLoad_Black(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_BLACK, r->a, r->x, r->y);
    Jsr(DT_CODE_SCREEN_OFF, DT_FLOW_LOAD_SCREEN_OFF, 0);
}

/* $D7A8 */
static void LandLoad_ScreenOff(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_SCREEN_OFF, r->a, r->x, r->y);
    Call(r, DT_CODE_OAM_CLEAR);
    gObjects->state[1] = 0;
    r->a = NT_FILL_ADDR;
    r->x = NT_FILL_TILE;
    r->y = 0;
    Jsr(DT_CODE_NT_FILL, DT_FLOW_LOAD_FILLED, OUT_A | OUT_X | OUT_Y);
}

/* $D7B9 */
static void LandLoad_Filled(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_FILLED, r->a, r->x, r->y);
    Land_ApplyTables(r);
    Call(r, DT_CODE_CLEAR_OBJECTS);
    Jsr(DT_CODE_DRAW_START, DT_FLOW_LOAD_DRAWN, 0);
}

/* $D7C2 */
static void LandLoad_Drawn(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_DRAWN, r->a, r->x, r->y);
    SelectBank(r, DT_BANK_SPRITES);
    r->x = PPU_STRINGS_LAND;
    Jsr(DT_B5_PPU_STRINGS, DT_FLOW_LOAD_STRINGS, OUT_X);
}

/* $D7CC */
static void LandLoad_Strings(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_STRINGS, r->a, r->x, r->y);
    r->x = gLand;
    Jsr(DT_CODE_CHR_LOAD, DT_FLOW_LOAD_CHR, OUT_X);
}

/* $D7D1 */
static void LandLoad_Chr(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_CHR, r->a, r->x, r->y);
    Jsr(DT_CODE_DRAW_START, DT_FLOW_LOAD_REDRAWN, 0);
}

/* $D7D4 */
static void LandLoad_Redrawn(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_REDRAWN, r->a, r->x, r->y);
    gRowChrState = 0;
    r->a = 0;
    Jsr(DT_CODE_ROW_CHR_STREAM, DT_FLOW_LOAD_ROW_CHR, 0);
}

/* $D7DB: until the row's sprite CHR is in; then the spawn tables, Scrooge
 * at the land's start (facing right, standing, the jump velocity and walking
 * speed of the game) and TOTAL queued for drawing. */
static void LandLoad_RowChr(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_ROW_CHR, r->a, r->x, r->y);
    r->a = gRowChrState;
    if (!(gRowChrState & 0x80)) {
        Jsr(DT_CODE_ROW_CHR_STREAM, DT_FLOW_LOAD_ROW_CHR, 0);
        return;
    }
    SelectBank(r, DT_BANK_OBJECTS);
    Call(r, DT_B6_SPAWN_TABLES);
    Call(r, DT_B6_SPAWN_RESET);
    gRowScroll = 0;
    gCameraX = 0;
    gRowScrollAddr = ROW_SCROLL_ADDR;
    gRowScrollAddrHi = ROW_SCROLL_HI;
    gObjects->state[P_] = SCROOGE_RESPAWN;
    gObjects->flags[P_] = SCROOGE_RESPAWN;
    gObjects->y[P_] = SCROOGE_START_Y;
    gObjects->animTimer[P_] = 0;
    gObjects->animFrame[P_] = 0;
    gObjects->action[P_] = 0;
    gObjects->xFrac[P_] = 0;
    gObjects->unk_0790[P_] = (uint8_t)SCROOGE_JUMP_VY;
    gObjects->unk_07A0[P_] = SCROOGE_JUMP_VY >> 8;
    gObjects->speedFrac[P_] = (uint8_t)SCROOGE_START_SPEED;
    gObjects->speed[P_] = SCROOGE_START_SPEED >> 8;
    Copy(DT_ADDR_VRAM_QUEUE, DT_ADDR_HUD_TOTAL_PACKET, DT_HUD_TOTAL_PACKET_LEN);
    r->a = gRam[DT_ADDR_HUD_TOTAL_PACKET];
    r->y = 0xFF;
    gVramPending = 0xFF;
    Jsr(DT_CODE_SCREEN_ON, DT_FLOW_LOAD_SHOWN, 0);
}

/* $D837 */
static void LandLoad_Shown(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_SHOWN, r->a, r->x, r->y);
    Yield(DT_FLOW_LOAD_FADE_IN);
}

/* $D83A: the HUD split on, and the picture fades in. */
static void LandLoad_FadeIn(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_FADE_IN, r->a, r->x, r->y);
    gSprite0Y = SPRITE0_Y;
    gHudSplit = SPLIT_ON;
    r->a = SPLIT_ON;
    Jsr(DT_CODE_FADE_IN, DT_FLOW_LOAD_DONE, 0);
}

/* $D845: the land's state starts clear, the money packet, the HP dots, the
 * HUD time group and the land's start time; slot 1 is set dead, the HUD
 * may be drawn, the consumed records are marked, and the frame loop runs. */
static void LandLoad_Done(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_DONE, r->a, r->x, r->y);
    gBossFight = 0;
    gHeldSlot = 0;
    gPlayerMode = 0;
    gLandLeft = 0;
    gPrevCameraX = 0;
    for (int i = 0; i < 16; i++) {
        gRam[DT_ADDR_HAZARD + i] = 0;
    }
    CopyRom(DT_ADDR_HUD_MONEY_PACKET, DT_DATA_HUD_MONEY, DT_HUD_MONEY_PACKET_LEN);
    r->a = Rom(DT_DATA_HUD_MONEY);
    r->x = 0xFF;
    r->y = 0xFF;
    Call(r, DT_CODE_HP_REFILL);
    CopyRom(DT_ADDR_HUD_GROUP_TIME, DT_DATA_HUD_TIME_GROUP, DT_HUD_TIME_GROUP_LEN);
    r->x = (uint8_t)(gLand << 1);
    gTimeDigit(0) = Rom((uint16_t)(DT_DATA_LAND_TIME + r->x));
    gTimeDigit(1) = Rom((uint16_t)(DT_DATA_LAND_TIME + r->x + 1));
    gTickFrames = DT_TICK_FRAMES_RELOAD;
    gObjects->state[1] = 0xFF;
    gObjects->flags[1] = 0xFF;
    gObjects->record[1] = 0xFF;
    gHudGate = 0xFF;
    gObjects->y[1] = 0;
    gObjects->worldX[1] = 0;
    gObjects->hitbox[1] = 0;
    r->y = 0xFF;
    SelectBank(r, DT_BANK_SPRITES);
    Call(r, DT_B5_CONSUMED_BITS);
    FrameLoop(r);
}

/* ---- site adapters ---- */

static void Run_LandPalette(Regs *r)
{
    Land_LoadPalette(r, r->x);
}

static void Run_RespawnEntry(Regs *r)
{
    Respawn_PlaceAt(r, r->a);
}

#define SITE(id)                           "ducktales.decomp." id
#define ROUTINE(addr, site, name, fn, out) { addr, site, name, fn, out, false, 0, false }
#define TASK(addr, site, name, fn, locals) { addr, site, name, fn, 0, true, locals, false }
#define RESUME(addr, name, fn, locals)     { addr, NULL, name, fn, 0, true, locals, true }

const DecompRoutine gFlowRoutines[] = {
    ROUTINE(DT_CODE_TIMER_TICK, NULL, "Timer_Tick", Timer_Tick, 0),
    ROUTINE(DT_CODE_HUD_MONEY, NULL, "Hud_RefreshMoney", Hud_RefreshMoney, 0),
    ROUTINE(DT_CODE_HUD_TOTAL, NULL, "Hud_RefreshTotal", Hud_RefreshTotal, 0),
    ROUTINE(DT_CODE_HUD_TEMPLATE, SITE("D60A"), "Hud_Reset", Hud_Reset, 0),
    ROUTINE(DT_CODE_LAND_TABLES, SITE("D9CA"), "Land_ApplyTables", Land_ApplyTables, OUT_X | OUT_Y),
    ROUTINE(DT_CODE_LAND_PALETTE, SITE("D9F5"), "Land_LoadPalette", Run_LandPalette, OUT_X | OUT_Y),
    TASK(DT_CODE_FRAME_LOOP, SITE("DB36"), "FrameLoop", FrameLoop, 0),
    RESUME(DT_FLOW_AFTER_PLAYER, "FrameLoop_AfterPlayer", FrameLoop_AfterPlayer, 0),
    RESUME(DT_FLOW_AFTER_OBJECTS, "FrameLoop_AfterObjects", FrameLoop_AfterObjects, 0),
    RESUME(DT_FLOW_AFTER_CAMERA, "FrameLoop_AfterCamera", FrameLoop_AfterCamera, 0),
    RESUME(DT_FLOW_AFTER_BOSS_TRIGGER, "FrameLoop_AfterBossTrigger", FrameLoop_AfterBossTrigger, 0),
    RESUME(DT_FLOW_AFTER_DEATH, "FrameLoop_AfterDeath", FrameLoop_AfterDeath, 0),
    RESUME(DT_FLOW_FRAME_SPRITES, "FrameLoop_Sprites", FrameLoop_Sprites, 0),
    RESUME(DT_FLOW_AFTER_SPAWN, "FrameLoop_AfterSpawn", FrameLoop_AfterSpawn, 0),
    TASK(DT_CODE_DEATH_OR_WARP, NULL, "DeathOrWarp", DeathOrWarp, 0),
    RESUME(DT_FLOW_DEATH_FADE, "Death_Fade", Death_Fade, 0),
    RESUME(DT_FLOW_DEATH_BLANK, "Death_Blank", Death_Blank, 0),
    RESUME(DT_FLOW_DEATH_PLACE, "Death_Place", Death_Place, 0),
    RESUME(DT_FLOW_DEATH_PLACED, "Death_Placed", Death_Placed, 0),
    RESUME(DT_FLOW_DEATH_ROW_CHR, "Death_RowChr", Death_RowChr, 2),
    RESUME(DT_FLOW_DEATH_SHOWN, "Death_Shown", Death_Shown, 0),
    RESUME(DT_FLOW_DEATH_FADE_IN, "Death_FadeIn", Death_FadeIn, 0),
    RESUME(DT_FLOW_DEATH_RESUME, "Death_Resume", Death_Resume, 0),
    RESUME(DT_FLOW_DEATH_SPAWNED, "Death_Spawned", Death_Spawned, 0),
    RESUME(DT_FLOW_DEATH_DONE, "Death_Done", Death_Done, 0),
    TASK(DT_CODE_DEATH_PAUSE, NULL, "DeathPause", DeathPause, 0),
    RESUME(DT_FLOW_DEATH_PAUSE_NEXT, "DeathPause_Next", DeathPause_Next, 1),
    TASK(DT_CODE_RESPAWN_PLACE, NULL, "Respawn_Place", Respawn_Place, 0),
    TASK(DT_CODE_RESPAWN_ENTRY, SITE("DD52"), "Respawn_PlaceAt", Run_RespawnEntry, 0),
    RESUME(DT_FLOW_RESPAWN_DRAWN, "Respawn_Drawn", Respawn_Drawn, 0),
    TASK(DT_CODE_GAME_OVER, NULL, "GameOver", GameOver, 0),
    RESUME(DT_FLOW_GAME_OVER_NEXT, "GameOver_Next", GameOver_Next, 1),
    TASK(DT_CODE_LAND_EXIT, NULL, "LandExit", LandExit, 0),
    RESUME(DT_FLOW_LAND_EXIT_NEXT, "LandExit_Next", LandExit_Next, 1),
    TASK(DT_CODE_MAIN_TASK, SITE("D4E6"), "MainTask", MainTask, 0),
    RESUME(DT_FLOW_TITLE_START, "Title_Start", Title_Start, 0),
    RESUME(DT_FLOW_TITLE_FADED, "Title_Faded", Title_Faded, 0),
    RESUME(DT_FLOW_TITLE_BLACK, "Title_Black", Title_Black, 0),
    RESUME(DT_FLOW_TITLE_SCREEN_OFF, "Title_ScreenOff", Title_ScreenOff, 0),
    RESUME(DT_FLOW_TITLE_FILLED, "Title_Filled", Title_Filled, 0),
    RESUME(DT_FLOW_TITLE_STRINGS, "Title_Strings", Title_Strings, 0),
    RESUME(DT_FLOW_TITLE_CHR, "Title_Chr", Title_Chr, 0),
    RESUME(DT_FLOW_TITLE_SHOWN, "Title_Shown", Title_Shown, 0),
    RESUME(DT_FLOW_TITLE_FADE_IN, "Title_FadeIn", Title_FadeIn, 0),
    RESUME(DT_FLOW_TITLE_LIT, "Title_Lit", Title_Lit, 0),
    RESUME(DT_FLOW_TITLE_READY, "Title_Ready", Title_Ready, 0),
    RESUME(DT_FLOW_TITLE_LOOP, "Title_Loop", Title_Loop, 0),
    TASK(DT_CODE_TITLE_FRAME, NULL, "Title_Frame", Title_Frame, 0),
    RESUME(DT_FLOW_TITLE_FRAME_DONE, "Title_FrameDone", Title_FrameDone, 0),
    TASK(DT_CODE_BOSS_TRIGGER, NULL, "BossTrigger", BossTrigger, 0),
    RESUME(DT_FLOW_BOSS_STREAMED, "BossTrigger_Streamed", BossTrigger_Streamed, 2),
    RESUME(DT_FLOW_BOSS_NEXT, "BossTrigger_Next", BossTrigger_Next, 2),
    TASK(DT_CODE_LAND_LOAD, SITE("D79B"), "LandLoad", LandLoad, 0),
    RESUME(DT_FLOW_LOAD_FADED, "LandLoad_Faded", LandLoad_Faded, 0),
    RESUME(DT_FLOW_LOAD_BLACK, "LandLoad_Black", LandLoad_Black, 0),
    RESUME(DT_FLOW_LOAD_SCREEN_OFF, "LandLoad_ScreenOff", LandLoad_ScreenOff, 0),
    RESUME(DT_FLOW_LOAD_FILLED, "LandLoad_Filled", LandLoad_Filled, 0),
    RESUME(DT_FLOW_LOAD_DRAWN, "LandLoad_Drawn", LandLoad_Drawn, 0),
    RESUME(DT_FLOW_LOAD_STRINGS, "LandLoad_Strings", LandLoad_Strings, 0),
    RESUME(DT_FLOW_LOAD_CHR, "LandLoad_Chr", LandLoad_Chr, 0),
    RESUME(DT_FLOW_LOAD_REDRAWN, "LandLoad_Redrawn", LandLoad_Redrawn, 0),
    RESUME(DT_FLOW_LOAD_ROW_CHR, "LandLoad_RowChr", LandLoad_RowChr, 0),
    RESUME(DT_FLOW_LOAD_SHOWN, "LandLoad_Shown", LandLoad_Shown, 0),
    RESUME(DT_FLOW_LOAD_FADE_IN, "LandLoad_FadeIn", LandLoad_FadeIn, 0),
    RESUME(DT_FLOW_LOAD_DONE, "LandLoad_Done", LandLoad_Done, 0),
};
const int gFlowRoutineCount = sizeof gFlowRoutines / sizeof gFlowRoutines[0];

#define STOP(addr, id) { addr, "ducktales.decomp.stop." id }

/* The routines the flow hands off to that have a decompiled C of their own
 * ($C42C, $C71D, $C830, $EFE2 in level.c, $E354 in player.c, $8000 bank 6 in
 * objects.c) stop the isolated runs at their own sites. */
const DecompStop gFlowStops[] = {
    STOP(DT_CODE_TASK_YIELD, "FFAC"),  STOP(DT_CODE_RESET, "FEE0"), STOP(DT_B6_SPAWN, "AEA0.6"),
    STOP(DT_CODE_LAND_SELECT, "D630"), STOP(DT_CODE_SCENE, "DF27"), STOP(DT_CODE_ENDING, "E1AF"),
    STOP(DT_CODE_ROW_SCROLL, "C976"),
};
const int gFlowStopCount = sizeof gFlowStops / sizeof gFlowStops[0];
