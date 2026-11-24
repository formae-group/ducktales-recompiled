/*
 * flow_frame.c - the reset and the frame loop: the reset vector with its
 * VBlank waits (polling $2002), RAM, the nametables and the main task; one
 * frame of play with the pause, the boss checkpoint and the boss trigger
 * with its arena scroll; and the pause and message-box text (bank 5). It
 * owns no settings or options.
 */
#include "decomp/flow/flow_internal.h"

#define SFX_PAUSE             0x15
#define RESPAWN_PENDING       0x80
#define HAZARD_SPIKES         6 /* hazard mode that invulnerability ignores */
#define CHECKPOINT_SET        5
#define STATUS_VBLANK         0x80
#define TREASURE_TRANSYLVANIA 0x02
#define REDIRECT_LAND_SELECT  0x80
#define TEXT_PTR              0x10 /* scratch: the text's pointer */
#define TEXT_LINE_LEN         0x1D /* a line: 30 tiles */
#define TEXT_PIECE            0x20 /* from $A0 a piece of text, below it a run */
#define TEXT_PIECE_FLAGGED    5
#define TEXT_BLANKS           8
#define TEXT_BOX              4
#define TEXT_MESSAGE_BOX      9
#define gTileEditPending      RAM_U8(DT_ADDR_TILE_EDIT_PENDING)
#define gTileEditOffset       RAM_U8(DT_ADDR_TILE_EDIT_OFFSET)
#define gUnk2E                RAM_U8(DT_ADDR_UNK_2E)
#define gBlockOpened          RAM_U8(DT_ADDR_BLOCK_OPENED)
#define gMessage              RAM_U8(DT_ADDR_MESSAGE)
#define gHazard               RAM_U8(DT_ADDR_HAZARD)
#define gPaused               RAM_U8(DT_ADDR_PAUSE)
#define gBgOffBelowSplit      RAM_U8(DT_ADDR_BG_OFF_BELOW_SPLIT)

/* ---- bank 5: the pause and message box text ---- */

static void Text_Line(Regs *r);

static uint8_t Peek(uint16_t addr)
{
    return addr < GAME_RAM_SIZE ? gRam[addr] : Rom(addr);
}

/* bank 5 $B591: text Y into the VRAM queue a line a frame. A text (pointers
 * at DT_B5_TEXT_LO/HI, in ROM or RAM) is lines up to a 0: a PPU address, then
 * up to $FF a tile below $80, $80-$9F a run of the next tile (n + 1 times),
 * and from $A0 a piece of text n (low nibble): text n, but for n from 5 text
 * 8 (blanks) while its flag (DT_B5_TEXT_FLAG/MASK) is clear. */
void Text_Lines(Regs *r)
{
    Decomp_Enter(DT_B5_TEXT_LINES, r->a, r->x, r->y);
    gScratch(TEXT_PTR) = Rom((uint16_t)(DT_B5_TEXT_LO + r->y));
    gScratch(TEXT_PTR + 1) = Rom((uint16_t)(DT_B5_TEXT_HI + r->y));
    r->y = 0;
    Text_Line(r);
}

/* $B59D: the next line, then a frame. */
static void Text_Line(Regs *r)
{
    uint16_t text = Ram_GetU16(DT_ADDR_SCRATCH0 + TEXT_PTR, DT_ADDR_SCRATCH0 + TEXT_PTR + 1);
    uint8_t *q = &gRam[DT_ADDR_VRAM_QUEUE];
    uint8_t y = r->y;
    r->a = Peek((uint16_t)(text + y));
    if (!r->a) {
        return;
    }
    q[0] = r->a;
    q[1] = Peek((uint16_t)(text + ++y));
    y++;
    q[2] = TEXT_LINE_LEN;
    uint8_t x = 0;
    while ((r->a = Peek((uint16_t)(text + y))) != 0xFF) {
        if (!(r->a & 0x80)) {
            q[3 + x++] = r->a;
        } else if ((r->a &= 0x7F) < TEXT_PIECE) {
            gScratch(0) = r->a;
            r->a = Peek((uint16_t)(text + ++y));
            do {
                q[3 + x++] = r->a;
            } while (!(--gScratch(0) & 0x80));
        } else {
            gScratch(0) = y;
            uint8_t piece = r->a & 0x0F;
            if (piece >= TEXT_PIECE_FLAGGED) {
                gScratch(1) = x;
                if (!(RAM_U8(Rom((uint16_t)(DT_B5_TEXT_FLAG + piece))) & Rom((uint16_t)(DT_B5_TEXT_MASK + piece)))) {
                    piece = TEXT_BLANKS;
                }
            }
            gScratch(2) = Rom((uint16_t)(DT_B5_TEXT_LO + piece));
            gScratch(3) = Rom((uint16_t)(DT_B5_TEXT_HI + piece));
            uint16_t sub = (uint16_t)(gScratch(2) | gScratch(3) << 8);
            uint8_t k = 0;
            gScratch(1) = Peek(sub);
            do {
                r->a = Peek((uint16_t)(sub + ++k));
                q[3 + x++] = r->a;
            } while (!(--gScratch(1) & 0x80));
            y = gScratch(0);
        }
        y++;
    }
    q[3 + x] = r->a;
    gVramQueueReady = r->a;
    r->x = x;
    r->y = (uint8_t)(y + 1);
    Yield(DT_B5_FLOW_TEXT_NEXT);
}

/* bank 5 $B618 */
void Text_LineNext(Regs *r)
{
    Decomp_Enter(DT_B5_FLOW_TEXT_NEXT, r->a, r->x, r->y);
    Text_Line(r);
}

/* bank 5 $B580: the box (text 4), then the message box's frame in mode 15,
 * else the pause text for $E6 (0: the HUD back, 1: PAUSE). */
void PauseText(Regs *r)
{
    Decomp_Enter(DT_B5_PAUSE_TEXT, r->a, r->x, r->y);
    r->y = TEXT_BOX;
    if (Gosub(Text_Lines, r, DT_B5_FLOW_PAUSE_BOX)) {
        PauseText_Framed(r);
    }
}

/* bank 5 $B585 */
void PauseText_Framed(Regs *r)
{
    Decomp_Enter(DT_B5_FLOW_PAUSE_BOX, r->a, r->x, r->y);
    r->a = gPlayerMode;
    r->y = gPlayerMode == DT_MODE_FROZEN ? TEXT_MESSAGE_BOX : gPaused;
    Text_Lines(r);
}

/* ---- the frame loop ---- */

static void FrameLoop_End(Regs *r);
static void FrameLoop_Yield(Regs *r);
static void FrameLoop_Requests(Regs *r);

/* $DB36: one frame of play. The ending replaces it once started. Start
 * toggles the pause (not while a message is up), and a paused frame does
 * nothing but keep the sprites. Then Scrooge moves. */
void FrameLoop(Regs *r)
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
            if (!Gosub(PauseText, r, DT_FLOW_PAUSED)) {
                return;
            }
        }
        FrameLoop_Paused(r);
        return;
    }
    Jsr(DT_CODE_PLAYER_UPDATE, DT_FLOW_AFTER_PLAYER, OUT_X);
}

/* $DB65: a paused frame only keeps the sprites. */
void FrameLoop_Paused(Regs *r)
{
    Decomp_Enter(DT_FLOW_PAUSED, r->a, r->x, r->y);
    r->a = gPaused;
    if (gPaused) {
        FrameLoop_End(r);
        return;
    }
    Jsr(DT_CODE_PLAYER_UPDATE, DT_FLOW_AFTER_PLAYER, OUT_X);
}

/* $DB6F: the objects move, unless a message is up. */
void FrameLoop_AfterPlayer(Regs *r)
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
void FrameLoop_AfterObjects(Regs *r)
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
void FrameLoop_AfterCamera(Regs *r)
{
    Decomp_Enter(DT_FLOW_AFTER_CAMERA, r->a, r->x, r->y);
    if (Gosub(BossTrigger, r, DT_FLOW_AFTER_BOSS_TRIGGER)) {
        FrameLoop_AfterBossTrigger(r);
    }
}

/* $DB8F: a land redirect loads Transylvania (positive) or returns to Land
 * Select (negative). */
void FrameLoop_AfterBossTrigger(Regs *r)
{
    Decomp_Enter(DT_FLOW_AFTER_BOSS_TRIGGER, r->a, r->x, r->y);
    uint8_t redirect = gLandRedirect;
    if (redirect == 0) {
        FrameLoop_Requests(r);
        return;
    }
    gLandRedirect = 0;
    if (!(redirect & 0x80)) {
        r->a = Obj_ClearSlots(&r->x);
        gLand = LAND_TRANSYLVANIA;
        r->x = LAND_TRANSYLVANIA;
        LandLoad(r);
        return;
    }
    r->a = 0;
    Jump(DT_CODE_LAND_SELECT, 0);
}

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
void FrameLoop_AfterDeath(Regs *r)
{
    Decomp_Enter(DT_FLOW_AFTER_DEATH, r->a, r->x, r->y);
    if (gHudMoneyDirty) {
        Hud_RefreshMoney(r);
    }
    if (gHudTotalDirty) {
        Hud_RefreshTotal(r);
    }
    if (gScene & 0x80) {
        if (!Gosub(Scene, r, DT_FLOW_FRAME_SPRITES)) {
            return;
        }
    }
    FrameLoop_Sprites(r);
}

/* $DBC5: the sprites, then the object spawner (it maps the tileset bank to
 * edit tiles). */
void FrameLoop_Sprites(Regs *r)
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
void FrameLoop_AfterSpawn(Regs *r)
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

/* $DC29 */
void FrameLoop_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_NEXT_FRAME, r->a, r->x, r->y);
    FrameLoop(r);
}

static void Reset_Ready(Regs *r);

/* $FEE0: the reset vector turns rendering off and resets S. Four VBlanks
 * pass before RAM is initialized. */
void Reset(Regs *r)
{
    Decomp_Enter(DT_CODE_RESET, r->a, r->x, r->y);
    r->p = (uint8_t)((r->p | P_I) & ~P_D);
    Decomp_Store(PPU_CTRL, 0);
    Decomp_Store(PPU_MASK, 0);
    r->x = 0xFF;
    Decomp_SetStack(r->x);
    r->y = 4;
    r->p = Flags_NZ(r->p, r->y);
    Jump(DT_RESET_POLL, OUT_X | OUT_Y | OUT_P);
}

/* $FEEE: Y times, VBlank (the frame ends in the poll)... */
void Reset_WaitVblank(Regs *r)
{
    Decomp_Enter(DT_RESET_POLL, r->a, r->x, r->y);
    Decomp_BusWait(PPU_STATUS, STATUS_VBLANK, STATUS_VBLANK);
    Jump(DT_RESET_VBLANK_END, OUT_Y);
}

/* $FEF3: ...and its end (the pre-render line); then the rest of the reset. */
void Reset_WaitVblankEnd(Regs *r)
{
    Decomp_Enter(DT_RESET_VBLANK_END, r->a, r->x, r->y);
    Decomp_BusWait(PPU_STATUS, STATUS_VBLANK, 0);
    if (--r->y) {
        Jump(DT_RESET_POLL, OUT_Y);
        return;
    }
    Reset_Ready(r);
}

/* $FEFB: clear all eight pages (the original's pointer $00/$01 ends at
 * $0700), park the sprites and fill the first nametable with blank tiles. */
static void Reset_Ready(Regs *r)
{
    Decomp_KeepStackRam();
    memset(gRam, 0, GAME_RAM_SIZE);
    gScratch(1) = GAME_RAM_SIZE / 0x100 - 1;
    Oam_Clear();
    r->a = 0x20;
    r->x = DT_MONEY_TILE_BLANK;
    r->y = 0;
    Jsr(DT_CODE_NT_FILL, DT_RESET_NT0_READY, OUT_A | OUT_X | OUT_Y);
}

/* $FF1B: the second nametable. */
void Reset_SecondNametable(Regs *r)
{
    Decomp_Enter(DT_RESET_NT0_READY, r->a, r->x, r->y);
    r->a = 0x24;
    r->x = DT_MONEY_TILE_BLANK;
    r->y = 0;
    Jsr(DT_CODE_NT_FILL, DT_RESET_NT1_READY, OUT_A | OUT_X | OUT_Y);
}

/* $FF24: slot 0 starts at the title. */
void Reset_CreateMainTask(Regs *r)
{
    Decomp_Enter(DT_RESET_NT1_READY, r->a, r->x, r->y);
    Ram_SetU16(DT_ADDR_TASK_ENTRY, DT_ADDR_TASK_ENTRY + 1, DT_CODE_MAIN_TASK);
    r->a = 0;
    Jsr(DT_CODE_TASK_CREATE, DT_RESET_CREATED, OUT_A);
}

/* $FF31: the control byte for the main task to turn on NMIs and the
 * background pattern table, then enter the scheduler. */
void Reset_StartScheduler(Regs *r)
{
    Decomp_Enter(DT_RESET_CREATED, r->a, r->x, r->y);
    r->a = 0x90;
    gRam[DT_ADDR_PPU_CTRL] = r->a;
    Jump(DT_CODE_SCHEDULER, 0);
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
void BossTrigger(Regs *r)
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
    r->a = Obj_ClearSlots(&r->x);
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
void BossTrigger_Streamed(Regs *r)
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
void BossTrigger_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_BOSS_NEXT, r->a, r->x, r->y);
    BossTrigger_Scroll(r);
}
