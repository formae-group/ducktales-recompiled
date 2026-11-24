/*
 * flow_table.c - the game flow's routine table (gFlowRoutines): each
 * routine's address, site, contract and C, with the adapters for the two
 * whose C takes an argument of its own. tools/flow_sites.py writes the
 * config/game.toml sites from it. It owns no settings or options.
 */
#include "decomp/flow/flow_internal.h"

/* ---- site adapters ---- */

static void Run_LandPalette(Regs *r)
{
    Land_LoadPalette(r, r->x);
}

void Run_RespawnEntry(Regs *r)
{
    Respawn_PlaceAt(r, r->a);
}

#define SITE(id)                                "ducktales.decomp." id
#define ROUTINE(addr, site, name, fn, out)      { addr, site, name, fn, out, false, 0, false }
#define TASK(addr, site, name, fn, locals)      { addr, site, name, fn, 0, true, locals, false }
#define RESUME(addr, name, fn, locals)          { addr, NULL, name, fn, 0, true, locals, true }
#define TASK_POLL(addr, site, name, fn)         { addr, site, name, fn, 0, true, 0, false, 0, false, false, true }
#define RESUME_POLL(addr, name, fn, locals)     { addr, NULL, name, fn, 0, true, locals, true, 0, false, false, true }
#define ROUTINE_IN(bank, addr, name, fn, out)   { addr, NULL, name, fn, out, false, 0, false, bank }
#define TASK_AT(bank, addr, site, name, fn)     { addr, site, name, fn, 0, true, 0, false, bank }
#define TASK_IN(bank, addr, name, fn)           { addr, NULL, name, fn, 0, true, 0, false, bank }
#define RESUME_IN(bank, addr, name, fn, locals) { addr, NULL, name, fn, 0, true, locals, true, bank }

const DecompRoutine gFlowRoutines[] = {
    ROUTINE(DT_CODE_TIMER_TICK, NULL, "Timer_Tick", Timer_Tick, 0),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_ENDING_OBJECTS, "EndingObjects", EndingObjects, 0),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_END_OBJ_PLACE, "EndingObjects_Place", EndingObjects_Place, 0),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_END_OBJ_UPDATE, "EndingObjects_Update", EndingObjects_Update, 0),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_END_OBJ_HOP, "EndingObjects_Hop", EndingObjects_Hop, 0),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_END_OBJ_WALK, "EndingObjects_Walk", EndingObjects_Walk, 0),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_END_OBJ_WAIT, "EndingObjects_Wait", EndingObjects_Wait, 0),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_END_OBJ_EXIT, "EndingObjects_Exit", EndingObjects_Exit, 0),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_END_OBJ_DONE, "EndingObjects_Done", EndingObjects_Done, 0),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_MESSAGE, "Select_Message", Select_Message, OUT_A | OUT_X | OUT_Y),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_SELECT_MESSAGE, "Select_ResultMessage", Select_ResultMessage,
               OUT_A | OUT_X | OUT_Y),
    ROUTINE_IN(DT_BANK_SPRITES, DT_B5_CONSUMED_BITS, "Records_Consumed", Records_Consumed, OUT_A | OUT_X | OUT_Y),
    ROUTINE(DT_CODE_HUD_MONEY, NULL, "Hud_RefreshMoney", Hud_RefreshMoney, 0),
    ROUTINE(DT_CODE_HUD_TOTAL, NULL, "Hud_RefreshTotal", Hud_RefreshTotal, 0),
    ROUTINE(DT_CODE_TOTAL_MAX, NULL, "Total_Max", Total_Max, OUT_A | OUT_Y),
    ROUTINE(DT_CODE_HUD_TEMPLATE, SITE("D60A"), "Hud_Reset", Hud_Reset, 0),
    ROUTINE(DT_CODE_LAND_TABLES, SITE("D9CA"), "Land_ApplyTables", Land_ApplyTables, OUT_X | OUT_Y),
    ROUTINE(DT_CODE_LAND_PALETTE, SITE("D9F5"), "Land_LoadPalette", Run_LandPalette, OUT_X | OUT_Y),
    TASK(DT_CODE_FRAME_LOOP, SITE("DB36"), "FrameLoop", FrameLoop, 0),
    RESUME(DT_FLOW_NEXT_FRAME, "FrameLoop_Next", FrameLoop_Next, 0),
    RESUME(DT_FLOW_PAUSED, "FrameLoop_Paused", FrameLoop_Paused, 0),
    RESUME(DT_FLOW_AFTER_PLAYER, "FrameLoop_AfterPlayer", FrameLoop_AfterPlayer, 0),
    TASK_AT(DT_BANK_SPRITES, DT_B5_PAUSE_TEXT, SITE("5.B580"), "PauseText", PauseText),
    RESUME_IN(DT_BANK_SPRITES, DT_B5_FLOW_PAUSE_BOX, "PauseText_Framed", PauseText_Framed, 0),
    TASK_IN(DT_BANK_SPRITES, DT_B5_TEXT_LINES, "Text_Lines", Text_Lines),
    RESUME_IN(DT_BANK_SPRITES, DT_B5_FLOW_TEXT_NEXT, "Text_LineNext", Text_LineNext, 0),
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
    TASK(DT_CODE_LAND_SELECT, SITE("D630"), "LandSelect", LandSelect, 0),
    RESUME(DT_FLOW_SELECT_FADED, "LandSelect_Faded", LandSelect_Faded, 0),
    RESUME(DT_FLOW_SELECT_SCREEN_OFF, "LandSelect_ScreenOff", LandSelect_ScreenOff, 0),
    RESUME(DT_FLOW_SELECT_CHR, "LandSelect_Chr", LandSelect_Chr, 1),
    RESUME(DT_FLOW_SELECT_DRAWN, "LandSelect_Drawn", LandSelect_Drawn, 1),
    RESUME(DT_FLOW_SELECT_SHOWN, "LandSelect_Shown", LandSelect_Shown, 0),
    RESUME(DT_FLOW_SELECT_FADE_IN, "LandSelect_FadeIn", LandSelect_FadeIn, 0),
    RESUME(DT_FLOW_SELECT_LIT, "LandSelect_Lit", LandSelect_Lit, 0),
    RESUME(DT_FLOW_SELECT_WAITED, "LandSelect_Waited", LandSelect_Waited, 0),
    RESUME(DT_FLOW_SELECT_BANKED, "LandSelect_Banked", LandSelect_Banked, 0),
    RESUME(DT_FLOW_SELECT_FINAL_WAIT, "LandSelect_FinalWait", LandSelect_FinalWait, 0),
    RESUME(DT_FLOW_SELECT_FINAL_SCENE, "LandSelect_FinalScene", LandSelect_FinalScene, 0),
    RESUME(DT_FLOW_SELECT_FINAL_READ, "LandSelect_FinalRead", LandSelect_FinalRead, 0),
    RESUME(DT_FLOW_SELECT_RESULT_READ, "LandSelect_ResultRead", LandSelect_ResultRead, 0),
    RESUME(DT_FLOW_SELECT_CURSOR, "LandSelect_Cursor", LandSelect_Cursor, 0),
    RESUME(DT_FLOW_SELECT_NEXT, "LandSelect_Next", LandSelect_Next, 0),
    TASK(DT_CODE_SCENE, NULL, "Scene", Scene, 0),
    RESUME(DT_FLOW_SCENE_FADED, "Scene_Faded", Scene_Faded, 0),
    RESUME(DT_FLOW_SCENE_SCREEN_OFF, "Scene_ScreenOff", Scene_ScreenOff, 0),
    RESUME(DT_FLOW_SCENE_CHR, "Scene_Chr", Scene_Chr, 0),
    RESUME(DT_FLOW_SCENE_PLACED, "Scene_Placed", Scene_Placed, 0),
    RESUME(DT_FLOW_SCENE_SHOWN, "Scene_Shown", Scene_Shown, 0),
    RESUME(DT_FLOW_SCENE_FADE_IN, "Scene_FadeIn", Scene_FadeIn, 0),
    RESUME(DT_FLOW_SCENE_DONE, "Scene_Done", Scene_Done, 0),
    TASK(DT_CODE_SPRITES_FRAME, NULL, "SpritesFrame", SpritesFrame, 0),
    RESUME(DT_FLOW_SPRITES_FRAME, "SpritesFrame_Next", SpritesFrame_Next, 0),
    TASK_IN(DT_BANK_FINAL_TRIP, DT_B4_FINAL_TRIP, "FinalTrip", FinalTrip),
    RESUME_IN(DT_BANK_FINAL_TRIP, DT_B4_FLOW_TRIP_FRAME, "FinalTrip_Next", FinalTrip_Next, 0),
    RESUME_IN(DT_BANK_FINAL_TRIP, DT_B4_FLOW_TRIP_DARK, "FinalTrip_Dark", FinalTrip_Dark, 1),
    RESUME_IN(DT_BANK_FINAL_TRIP, DT_B4_FLOW_TRIP_FLASH, "FinalTrip_Flashed", FinalTrip_Flashed, 1),
    TASK(DT_CODE_MESSAGE_SCENE, SITE("DFED"), "Mode15_Redraw", Mode15_Redraw, 0),
    RESUME(DT_FLOW_M15_ROW_CHR, "Mode15_RowChr", Mode15_RowChr, 0),
    RESUME(DT_FLOW_M15_STREAMED, "Mode15_Streamed", Mode15_Streamed, 0),
    RESUME(DT_FLOW_M15_NEXT, "Mode15_Next", Mode15_Next, 0),
    RESUME(DT_FLOW_M15_ARENA_CHR, "Mode15_ArenaChr", Mode15_ArenaChr, 0),
    RESUME(DT_FLOW_M15_SPAWNED, "Mode15_Spawned", Mode15_Spawned, 0),
    TASK(DT_CODE_RESET, SITE("FEE0"), "Reset", Reset, 0),
    TASK_POLL(DT_RESET_POLL, SITE("FEEE"), "Reset_WaitVblank", Reset_WaitVblank),
    TASK_POLL(DT_RESET_VBLANK_END, SITE("FEF3"), "Reset_WaitVblankEnd", Reset_WaitVblankEnd),
    RESUME(DT_RESET_NT0_READY, "Reset_SecondNametable", Reset_SecondNametable, 0),
    RESUME(DT_RESET_NT1_READY, "Reset_CreateMainTask", Reset_CreateMainTask, 0),
    RESUME(DT_RESET_CREATED, "Reset_StartScheduler", Reset_StartScheduler, 0),
    TASK(DT_CODE_ENDING, SITE("E1AF"), "Ending", Ending, 0),
    TASK(DT_CODE_ENDING_BANK, NULL, "Ending_BankTransfer", Ending_BankTransfer, 0),
    RESUME(DT_FLOW_ENDING_BANKED, "Ending_Banked", Ending_Banked, 0),
    RESUME(DT_FLOW_ENDING_WAITED, "Ending_Waited", Ending_Waited, 0),
    RESUME_POLL(DT_FLOW_END_SPLIT_WAIT, "Ending_Split", Ending_Split, 0),
    RESUME(DT_FLOW_END_SECOND, "Ending_Second", Ending_Second, 1),
    RESUME(DT_FLOW_END_OVER, "Ending_OverNext", Ending_OverNext, 0),
    RESUME(DT_FLOW_ENDING_SHOWN, "Ending_Shown", Ending_Shown, 0),
    TASK(DT_CODE_ENDING_SCREEN, NULL, "EndingScreen", EndingScreen, 0),
    RESUME(DT_FLOW_END_FADED, "EndingScreen_Faded", EndingScreen_Faded, 0),
    RESUME(DT_FLOW_END_BLACK, "EndingScreen_Black", EndingScreen_Black, 0),
    RESUME(DT_FLOW_END_SCREEN_OFF, "EndingScreen_ScreenOff", EndingScreen_ScreenOff, 0),
    RESUME(DT_FLOW_END_FILLED, "EndingScreen_Filled", EndingScreen_Filled, 0),
    RESUME(DT_FLOW_END_PALETTE, "EndingScreen_Palette", EndingScreen_Palette, 0),
    RESUME(DT_FLOW_END_STREAMED, "EndingScreen_Streamed", EndingScreen_Streamed, 1),
    RESUME(DT_FLOW_END_STRINGS, "EndingScreen_NextString", EndingScreen_NextString, 1),
    RESUME(DT_FLOW_END_CHR, "EndingScreen_Chr", EndingScreen_Chr, 0),
    RESUME(DT_FLOW_END_SHOWN, "EndingScreen_Shown", EndingScreen_Shown, 0),
    RESUME(DT_FLOW_END_NEXT, "EndingScreen_Next", EndingScreen_Next, 0),
    RESUME(DT_FLOW_END_LIT, "EndingScreen_Lit", EndingScreen_Lit, 0),
    TASK(DT_CODE_BANK_TRANSFER, NULL, "BankTransfer", BankTransfer, 0),
    RESUME(DT_FLOW_BANK_NEXT, "BankTransfer_Next", BankTransfer_Next, 0),
    TASK(DT_CODE_WAIT_A, NULL, "WaitA", WaitA, 0),
    RESUME(DT_FLOW_WAIT_A_NEXT, "WaitA_Next", WaitA_Next, 0),
};
const int gFlowRoutineCount = sizeof gFlowRoutines / sizeof gFlowRoutines[0];

/* Every routine the flow hands off to has a decompiled C of its own ($C42C,
 * $C71D, $C78F, $C830, $C976, $D068, $EFE2 and bank 6 $AEA0 in level.c, $E354
 * in player.c, $8000 bank 6 in objects/dispatch.c, the fades, the PPU and CHR loaders
 * and bank 5 $BAB0 in system.c, and task_yield, task_wait and the scheduler
 * there): the isolated runs stop at their own sites. */
