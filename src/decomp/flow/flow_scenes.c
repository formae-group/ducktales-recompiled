/*
 * flow_scenes.c - the special scenes, the final trip's take-off (bank 4)
 * with the sprites frame it waits with, and the mode-15 redraw (the scene
 * set-up of message $0B that Scrooge's message box calls). It owns no
 * settings or options.
 */
#include "decomp/flow/flow_internal.h"

#define MUSIC_SCENE        0x0B
#define SCENE_NUMBER       0x0F /* $78: the scene, below its request bit */
#define SCENE_MAP          6    /* map of scene 0; the scene's map follows */
#define SCENE_RESPAWN      0x10 /* respawn entry of scene 0 */
#define SCENE_TILESET      3
#define SCENE_PALETTE      0x70
#define SCENE_OBJECT_STATE 0xA0
#define TRIP_SLOT          8    /* the plane of the take-off scene */
#define TRIP_ACTION        0x6F /* its first action; the scene ends at $74 */
#define TRIP_ACTION_END    0x74
#define TRIP_GRAVITY       0x40
#define TRIP_STATE         0xA0
#define TRIP_Y             0xC0
#define TRIP_X             0x80
#define TRIP_SPEED         2
#define TRIP_VY            0x077D /* take-off velocity, whole and fraction */
#define TRIP_FLASHES       0x10
#define TRIP_FLASH_FRAMES  4
#define TRIP_COLOUR_BRIGHT 0x16
#define TRIP_COLOUR_DARK   0x0F
#define M15_SCREEN         0x17 /* camera screen: row 2, page 7 */
#define M15_STREAM_ROW     6
#define M15_ROW            2
#define gUnk4C(i)          RAM_U8(DT_ADDR_UNK_4C + (i))

/* ---- scenes ---- */

/* $DF27: a special scene ($78 bit 7; the Launchpad ride home and the sky
 * scene): the picture fades out, the land's movement state is cleared, and
 * the scene's map is loaded with rendering off. */
void Scene(Regs *r)
{
    Decomp_Enter(DT_CODE_SCENE, r->a, r->x, r->y);
    Jsr(DT_CODE_FADE_OUT, DT_FLOW_SCENE_FADED, 0);
}

/* $DF2A */
void Scene_Faded(Regs *r)
{
    Decomp_Enter(DT_FLOW_SCENE_FADED, r->a, r->x, r->y);
    gHudSplit = 0;
    gHeldSlot = 0;
    gBossFight = 0;
    gRowScroll = 0;
    gUnk4C(0) = 0;
    gUnk4C(1) = 0;
    gUnk4C(2) = 0;
    RAM_U8(DT_ADDR_SLIPPERY) = 0;
    RAM_U8(DT_ADDR_MOMENTUM_DIR) = 0;
    RAM_U8(DT_ADDR_MOVE_SIDE) = 1;
    r->a = 1;
    Jsr(DT_CODE_SCREEN_OFF, DT_FLOW_SCENE_SCREEN_OFF, 0);
}

/* $DF45: the scene's map header, spawn tables and CHR. */
void Scene_ScreenOff(Regs *r)
{
    Decomp_Enter(DT_FLOW_SCENE_SCREEN_OFF, r->a, r->x, r->y);
    r->a = Obj_ClearSlots(&r->x);
    gRam[DT_ADDR_TILESET_BANK] = SCENE_TILESET;
    r->a = SCENE_TILESET;
    SelectBank(r, DT_BANK_OBJECTS);
    gScene &= SCENE_NUMBER;
    r->a = gScene;
    r->x = (uint8_t)(gScene + SCENE_MAP);
    Call(r, DT_CODE_MAP_HEADER);
    Call(r, DT_B6_SPAWN_TABLES_MAP);
    Jsr(DT_CODE_CHR_LOAD, DT_FLOW_SCENE_CHR, OUT_X);
}

/* $DF64: Scrooge and the camera at the scene's respawn entry. */
void Scene_Chr(Regs *r)
{
    Decomp_Enter(DT_FLOW_SCENE_CHR, r->a, r->x, r->y);
    r->a = (uint8_t)(gScene + SCENE_RESPAWN);
    if (Gosub(Run_RespawnEntry, r, DT_FLOW_SCENE_PLACED)) {
        Scene_Placed(r);
    }
}

/* $DF6C: the scene's palette, Scrooge's mode and the scene's objects (in
 * every other slot from 0), its music; rendering on. */
void Scene_Placed(Regs *r)
{
    Decomp_Enter(DT_FLOW_SCENE_PLACED, r->a, r->x, r->y);
    r->x = SCENE_PALETTE;
    Land_LoadPalette(r, SCENE_PALETTE);
    uint8_t scene = gScene;
    gPlayerMode = Rom((uint16_t)(DT_DATA_SCENE_MODE + scene));
    r->y = Rom((uint16_t)(DT_DATA_SCENE_OBJECTS_AT + scene));
    r->x = 0;
    for (;;) {
        uint16_t e = (uint16_t)(DT_DATA_SCENE_OBJECTS + r->y);
        r->a = Rom(e);
        if (r->a & 0x80) {
            break;
        }
        uint8_t slot = r->x;
        gObjects->action[slot] = r->a;
        gObjects->worldX[slot] = Rom((uint16_t)(e + 1));
        gObjects->worldPage[slot] = Rom((uint16_t)(e + 2));
        gObjects->y[slot] = Rom((uint16_t)(e + 3));
        gObjects->kind[slot] = Rom((uint16_t)(e + 4));
        gObjects->hitbox[slot] = Rom((uint16_t)(e + 5));
        gObjects->state[slot] = SCENE_OBJECT_STATE;
        gObjects->flags[slot] = SCENE_OBJECT_STATE;
        gObjects->animTimer[slot] = 0;
        gObjects->animFrame[slot] = 0;
        r->y = (uint8_t)(r->y + 6);
        r->x = (uint8_t)(r->x + 2);
        if (r->x == 0) {
            break;
        }
    }
    MusicRequest(r, MUSIC_SCENE);
    Jsr(DT_CODE_SCREEN_ON, DT_FLOW_SCENE_SHOWN, 0);
}

/* $DFC4 */
void Scene_Shown(Regs *r)
{
    Decomp_Enter(DT_FLOW_SCENE_SHOWN, r->a, r->x, r->y);
    Yield(DT_FLOW_SCENE_FADE_IN);
}

/* $DFC7: the HUD split on, and the picture fades in. */
void Scene_FadeIn(Regs *r)
{
    Decomp_Enter(DT_FLOW_SCENE_FADE_IN, r->a, r->x, r->y);
    gHudSplit = SPLIT_ON;
    r->a = SPLIT_ON;
    Jsr(DT_CODE_FADE_IN, DT_FLOW_SCENE_DONE, 0);
}

/* $DFCE */
void Scene_Done(Regs *r)
{
    Decomp_Enter(DT_FLOW_SCENE_DONE, r->a, r->x, r->y);
}

/* ---- the final trip's take-off ---- */

static void FinalTrip_Start(Regs *r);
static void FinalTrip_Frame(Regs *r);
static void FinalTrip_Flash(Regs *r, uint8_t flashes);

/* $F855: the sprites rebuilt, a frame, and bank 4 (the scene's) mapped
 * again. */
void SpritesFrame(Regs *r)
{
    Decomp_Enter(DT_CODE_SPRITES_FRAME, r->a, r->x, r->y);
    Call(r, DT_CODE_OAM_CLEAR);
    SelectBank(r, DT_BANK_SPRITES);
    Call(r, DT_B5_BUILD_OAM);
    Yield(DT_FLOW_SPRITES_FRAME);
}

/* $F863 */
void SpritesFrame_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_SPRITES_FRAME, r->a, r->x, r->y);
    SelectBank(r, DT_BANK_FINAL_TRIP);
}

/* Bank 4 $BE00: the plane (slot 8) takes off from the bottom of the screen
 * and flies across it once per action from $6F to $73, rising then falling,
 * leftward on odd actions; then the screen flashes 16 times. */
void FinalTrip(Regs *r)
{
    Decomp_Enter(DT_B4_FINAL_TRIP, r->a, r->x, r->y);
    RAM_U8(DT_ADDR_GRAVITY) = TRIP_GRAVITY;
    gObjects->action[TRIP_SLOT] = TRIP_ACTION;
    FinalTrip_Start(r);
}

/* Bank 4 $BE09: one flight starts. */
static void FinalTrip_Start(Regs *r)
{
    const int s = TRIP_SLOT;
    gObjects->state[s] = TRIP_STATE;
    gObjects->flags[s] = TRIP_STATE;
    gObjects->y[s] = TRIP_Y;
    gObjects->worldX[s] = TRIP_X;
    gObjects->worldPage[s] = 0;
    gObjects->animTimer[s] = 0;
    gObjects->animFrame[s] = 0;
    gObjects->unk_06C0[s] = 0;
    gObjects->speedFrac[s] = 0;
    gObjects->speed[s] = TRIP_SPEED;
    gObjects->vyFrac[s] = (uint8_t)TRIP_VY;
    gObjects->vy[s] = TRIP_VY >> 8;
    r->a = TRIP_VY >> 8;
    FinalTrip_Frame(r);
}

/* Bank 4 $BE3B: a frame of the flight: rising until the velocity turns
 * negative ($06C8 then counts the fall), across, and the sprites. */
static void FinalTrip_Frame(Regs *r)
{
    const int s = TRIP_SLOT;
    r->x = TRIP_SLOT;
    if (gObjects->unk_06C0[s] == 0) {
        Obj_ApplyVelocityY(s);
        Obj_ApplyGravity(s);
        if (gObjects->vy[s] & 0x80) {
            gObjects->vyFrac[s] = 0;
            gObjects->vy[s] = 0;
            gObjects->unk_06C0[s]++;
        }
    } else {
        Obj_ApplyGravity(s);
        Obj_ApplyVelocityY(s);
    }
    if (gObjects->action[s] & 1) {
        Obj_MoveLeft(s);
    } else {
        Obj_MoveRight(s);
    }
    Gosub(SpritesFrame, r, DT_B4_FLOW_TRIP_FRAME);
}

/* Bank 4 $BE73: until the plane leaves the screen; then the next action's
 * flight, and after the last the flashes. */
void FinalTrip_Next(Regs *r)
{
    Decomp_Enter(DT_B4_FLOW_TRIP_FRAME, r->a, r->x, r->y);
    const int s = TRIP_SLOT;
    r->a = gObjects->flags[s];
    if (gObjects->flags[s] & 0x80) {
        FinalTrip_Frame(r);
        return;
    }
    r->a = ++gObjects->action[s];
    if (r->a != TRIP_ACTION_END) {
        FinalTrip_Start(r);
        return;
    }
    gObjects->state[s] = 0;
    FinalTrip_Flash(r, TRIP_FLASHES);
}

/* Bank 4 $BE89 (the flashes left on the stack): background colour 0 bright
 * for 4 frames, then dark for 4. */
static void FinalTrip_Flash(Regs *r, uint8_t flashes)
{
    Decomp_Push(flashes);
    gRam[DT_ADDR_PALETTE_SHOWN + 0x10] = TRIP_COLOUR_BRIGHT;
    RAM_U8(DT_ADDR_PALETTE_REQUEST) = TRIP_COLOUR_BRIGHT;
    r->a = TRIP_COLOUR_BRIGHT;
    r->x = TRIP_FLASH_FRAMES;
    Jsr(DT_CODE_TASK_WAIT, DT_B4_FLOW_TRIP_DARK, OUT_X | OUT_Y);
}

/* Bank 4 $BE96 */
void FinalTrip_Dark(Regs *r)
{
    Decomp_Enter(DT_B4_FLOW_TRIP_DARK, r->a, r->x, r->y);
    gRam[DT_ADDR_PALETTE_SHOWN + 0x10] = TRIP_COLOUR_DARK;
    RAM_U8(DT_ADDR_PALETTE_REQUEST) = TRIP_COLOUR_DARK;
    r->a = TRIP_COLOUR_DARK;
    r->x = TRIP_FLASH_FRAMES;
    Jsr(DT_CODE_TASK_WAIT, DT_B4_FLOW_TRIP_FLASH, OUT_X | OUT_Y);
}

/* Bank 4 $BEA2: the next flash; after the last, $5E is cleared. */
void FinalTrip_Flashed(Regs *r)
{
    Decomp_Enter(DT_B4_FLOW_TRIP_FLASH, r->a, r->x, r->y);
    uint8_t flashes = (uint8_t)(Decomp_Pull() - 1);
    r->a = flashes;
    if (flashes) {
        FinalTrip_Flash(r, flashes);
        return;
    }
    gSelectMessage = 0;
}

/* ---- mode 15 redraw ---- */

static void Mode15_Stream(Regs *r);

/* $DFED: called by Scrooge's mode 15 ($ED0B) when $6E is $0B. The objects
 * go, the camera moves to screen $17 (row 2, page 7) and map row 6 is
 * streamed in a block a frame with Transylvania's blocks, with the arena's
 * row CHR (land 5) streamed before and after; then row 2 and its objects. */
void Mode15_Redraw(Regs *r)
{
    Decomp_Enter(DT_CODE_MESSAGE_SCENE, r->a, r->x, r->y);
    r->a = Obj_ClearSlots(&r->x);
    gStreamPos = 0;
    gRowChrState = 0;
    gStreamScreen = M15_SCREEN;
    gCameraScreen = M15_SCREEN;
    gStreamPage = M15_SCREEN & 7;
    gObjects->worldPage[P_] = M15_SCREEN & 7;
    gCameraPage = M15_SCREEN & 7;
    gLand = LAND_ARENA;
    gMapRow = M15_STREAM_ROW;
    r->a = M15_STREAM_ROW;
    Jsr(DT_CODE_ROW_CHR_STREAM, DT_FLOW_M15_ROW_CHR, 0);
}

/* $E010 */
void Mode15_RowChr(Regs *r)
{
    Decomp_Enter(DT_FLOW_M15_ROW_CHR, r->a, r->x, r->y);
    gLand = LAND_TRANSYLVANIA;
    r->a = LAND_TRANSYLVANIA;
    Mode15_Stream(r);
}

/* $E014: 64 blocks, one a frame; then the row CHR until it is in. */
static void Mode15_Stream(Regs *r)
{
    r->a = gStreamPos;
    if (gStreamPos == ENDING_STREAM_STEPS) {
        gLand = LAND_ARENA;
        r->a = LAND_ARENA;
        Jsr(DT_CODE_ROW_CHR_STREAM, DT_FLOW_M15_ARENA_CHR, 0);
        return;
    }
    gStreamSteps = 1;
    gStreamQueueEnd = 0;
    r->a = 0;
    Jsr(DT_CODE_STREAM_BLOCK, DT_FLOW_M15_STREAMED, OUT_A | OUT_X | OUT_Y);
}

/* $E025 */
void Mode15_Streamed(Regs *r)
{
    Decomp_Enter(DT_FLOW_M15_STREAMED, r->a, r->x, r->y);
    gStreamPos++;
    Yield(DT_FLOW_M15_NEXT);
}

/* $E02A */
void Mode15_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_M15_NEXT, r->a, r->x, r->y);
    Mode15_Stream(r);
}

/* $E034: once the row CHR is in, row 2 with its objects, and the palette
 * uploaded. */
void Mode15_ArenaChr(Regs *r)
{
    Decomp_Enter(DT_FLOW_M15_ARENA_CHR, r->a, r->x, r->y);
    gLand = LAND_TRANSYLVANIA;
    r->a = gRowChrState;
    if (!(gRowChrState & 0x80)) {
        Yield(DT_FLOW_M15_NEXT);
        return;
    }
    gMapRow = M15_ROW;
    SelectBank(r, DT_BANK_OBJECTS);
    Call(r, DT_B6_SPAWN_RESET);
    gBossFight = 0;
    r->a = 0;
    Jsr(DT_B6_SPAWN, DT_FLOW_M15_SPAWNED, 0);
}

/* $E04F */
void Mode15_Spawned(Regs *r)
{
    Decomp_Enter(DT_FLOW_M15_SPAWNED, r->a, r->x, r->y);
    RAM_U8(DT_ADDR_PALETTE_REQUEST) = 0xFF;
    r->a = 0xFF;
}
