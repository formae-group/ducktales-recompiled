/*
 * flow_death.c - respawn, death and warps, the death pause, game over and
 * the land exit: the screens redrawn at the respawn entry and the frames
 * counted before the next phase. It owns no settings or options.
 */
#include "decomp/flow/flow_internal.h"

#define SFX_WARP          0x2D
#define MUSIC_GAME_OVER   0x09
#define MUSIC_DEATH       0x0A
#define RESPAWN_LIFE      0x87 /* entries from here cost a life */
#define RESPAWN_ARENA     0x85 /* the warp into a boss arena */
#define DEATH_PAUSE_FALL  0xB4 /* frames after a death */
#define DEATH_PAUSE_WARP  0x3C /* frames after a warp */
#define GAME_OVER_FRAMES  0xF0
#define LAND_EXIT_FRAMES  0xB4
#define RESPAWN_ENTRY_LEN 9
#define gScreensLeft      RAM_U8(DT_ADDR_SCRATCH11)
#define gPogoCeiling      RAM_U8(DT_ADDR_POGO_CEILING)

/* A byte of the current map's screen grid (pointer at $20). */
static uint8_t Map_Cell(uint8_t index)
{
    uint16_t addr = (uint16_t)(Ram_GetU16(DT_ADDR_MAP_GRID, DT_ADDR_MAP_GRID + 1) + index);
    return addr < GAME_RAM_SIZE ? gRam[addr] : Rom(addr);
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
void Respawn_Drawn(Regs *r)
{
    Decomp_Enter(DT_FLOW_RESPAWN_DRAWN, r->a, r->x, r->y);
    if (Respawn_Advance()) {
        Respawn_DrawScreens(r);
    }
}

/* $DD52: put the camera, the map row and Scrooge at a respawn or warp entry
 * (LEVELS.md "Respawn and warp table") and draw the screens around him. */
void Respawn_PlaceAt(Regs *r, uint8_t entry)
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
void Respawn_Place(Regs *r)
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
void DeathPause(Regs *r)
{
    Decomp_Enter(DT_CODE_DEATH_PAUSE, r->a, r->x, r->y);
    DeathPause_Frame(r, gRespawn >= RESPAWN_LIFE ? DEATH_PAUSE_FALL : DEATH_PAUSE_WARP);
}

/* $DD42 (the frame count on the stack) */
void DeathPause_Next(Regs *r)
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
void GameOver(Regs *r)
{
    Decomp_Enter(DT_CODE_GAME_OVER, r->a, r->x, r->y);
    MusicRequest(r, MUSIC_GAME_OVER);
    CopyRom(DT_ADDR_HUD_GROUP_TIME, DT_DATA_GAME_OVER_SPRITES, DT_GAME_OVER_SPRITES_LEN);
    GameOver_Frame(r, GAME_OVER_FRAMES);
}

/* $DEDE (the frame count on the stack) */
void GameOver_Next(Regs *r)
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
void LandExit(Regs *r)
{
    Decomp_Enter(DT_CODE_LAND_EXIT, r->a, r->x, r->y);
    if (gScene) {
        LandSelect(r);
        return;
    }
    LandExit_Frame(r, LAND_EXIT_FRAMES);
}

/* $DF1E (the frame count on the stack) */
void LandExit_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_LAND_EXIT_NEXT, r->a, r->x, r->y);
    uint8_t frames = (uint8_t)(Decomp_Pull() - 1);
    r->a = frames;
    if (frames) {
        LandExit_Frame(r, frames);
        return;
    }
    LandSelect(r);
}

/* $DC3E: a respawn or warp request ($7E bit 7). A scene request leaves the
 * land instead. An entry from $87 costs a life (game over without one); the
 * others are warps. The picture fades out, Scrooge and the camera move to the
 * entry, the screens are redrawn and the picture fades back in. */
void DeathOrWarp(Regs *r)
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
void Death_Fade(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_FADE, r->a, r->x, r->y);
    Jsr(DT_CODE_FADE_OUT, DT_FLOW_DEATH_BLANK, 0);
}

/* $DC6B: the picture is black: reset the land's per-life state and turn
 * rendering off for the redraw. */
void Death_Blank(Regs *r)
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

/* $DC81 */
void Death_Place(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_PLACE, r->a, r->x, r->y);
    if (Gosub(Respawn_Place, r, DT_FLOW_DEATH_PLACED)) {
        Death_Placed(r);
    }
}

/* $DC84: the land and row are kept on the stack while the row's sprite CHR
 * streams in; a death costs a life and refills the HP dots, the arena warp
 * streams the arena's CHR. */
void Death_Placed(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_PLACED, r->a, r->x, r->y);
    r->a = Obj_ClearSlots(&r->x);
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
void Death_RowChr(Regs *r)
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
void Death_Shown(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_SHOWN, r->a, r->x, r->y);
    Yield(DT_FLOW_DEATH_FADE_IN);
}

/* $DCDB */
void Death_FadeIn(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_FADE_IN, r->a, r->x, r->y);
    gHudSplit = SPLIT_ON;
    r->a = SPLIT_ON;
    Jsr(DT_CODE_FADE_IN, DT_FLOW_DEATH_RESUME, 0);
}

/* $DCE2: the boss music again in a boss fight; after a death the land's
 * music and its start time; then the objects spawn and a pause follows. */
void Death_Resume(Regs *r)
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
void Death_Spawned(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_SPAWNED, r->a, r->x, r->y);
    gObjects->state[P_] = SCROOGE_RESPAWN;
    r->a = SCROOGE_RESPAWN;
    if (Gosub(DeathPause, r, DT_FLOW_DEATH_DONE)) {
        Death_Done(r);
    }
}

/* $DD1C */
void Death_Done(Regs *r)
{
    Decomp_Enter(DT_FLOW_DEATH_DONE, r->a, r->x, r->y);
    gPlayerInvulnerable = gPlayerMode = gRespawn = 0;
    r->a = 0;
}
