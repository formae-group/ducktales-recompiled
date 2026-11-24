/*
 * fm_script_internal.h - what the script mods' files share (fm_script.c,
 * fm_script_state.c, fm_script_show.c, fm_script_store.c): a mod's script and
 * the writes it queued, the game the scripts run for, and the dt functions
 * fm_script.c puts in each script's dt table.
 */
#pragma once
#include "mods/core/fm_mods.h"

#include "lua.h"

#define SCRIPT_WRITES 256
#define STORE_KEY_MAX 64

typedef struct {
    char key[STORE_KEY_MAX + 1];
    char *value; /* "s:text" (escaped), "i:123", "n:1.5" or "b:true" */
} StoreItem;

typedef struct {
    char id[64];
    char name[128];
    char path[1024];  /* the archive or folder */
    char watch[1024]; /* the file whose change reloads the script */
    bool folder;
    long long stamp;
    lua_State *L;
    int handlers;    /* registry ref: a table of handler lists, event + 1 (the frame's at SCRIPT_EVENTS + 1) */
    uint64_t wants;  /* bit per event, FRAME_BIT for "frame" */
    uint64_t warned; /* bit per event: a dt.CANCEL it cannot take was reported */
    size_t memory;
    bool over;  /* the last growth was refused */
    long steps; /* hook steps left in this callback */
    bool stopped;
    char why[300];
    uint64_t last_toast;
    int draws; /* dt.draw calls this frame */
    bool draw_warned;
    bool store_loaded, store_dirty, store_failed;
    StoreItem *store;
    int store_count;
    size_t store_bytes;
} Script;

typedef struct {
    Script *script;
    int field;
    long long value;
} Write;

/* fm_script.c */
extern FmScriptGame s_game;
extern Script *s_scripts[FM_MAX_MODS];
extern int s_script_count;
extern uint64_t s_frame;
extern Write s_writes[SCRIPT_WRITES];
extern int s_write_count;
Script *script_of(lua_State *L);
void log_line(const Script *s, const char *what, const char *text);

/* fm_script_state.c: dt.state, dt.set and dt.<query>() */
int sb_query(lua_State *L);
int state_index(lua_State *L);
int read_only(lua_State *L);
int sb_set(lua_State *L);

/* fm_script_show.c: dt.toast and dt.draw */
extern int s_toast_frames;
int sb_toast(lua_State *L);
int sb_draw_text(lua_State *L);
int sb_draw_rect(lua_State *L);
void finish_drawing(void);

/* fm_script_store.c: dt.store */
void store_write(Script *s);
int sb_store_get(lua_State *L);
int sb_store_set(lua_State *L);
