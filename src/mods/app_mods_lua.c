/*
 * app_mods_lua.c - script mods: a loaded mod's scripts/main.lua, run in a
 * Lua 5.4 sandbox of its own (docs/mods.md "Scripts").
 *
 *   libraries  base without dofile and loadfile (load reads text only, print
 *              writes to the log), string without dump, table, math, utf8 and
 *              coroutine. io, os, package and debug are not even linked.
 *   dt         dt.on(event, fn) for "frame" and the game events
 *              (ducktales/events.h), dt.state (read only), dt.log, dt.CANCEL,
 *              dt.mod and dt.frame(). Nothing writes the game's RAM; a handler
 *              of a cancelable event cancels it by returning dt.CANCEL.
 *   limits     a callback (the main chunk included) runs at most
 *              SCRIPT_INSTRUCTIONS VM instructions (a count hook) and a mod's
 *              state holds at most SCRIPT_MEMORY bytes (its allocator).
 *              pcall, xpcall, coroutine.resume and coroutine.close let a
 *              limit through: it stops the mod, not the call. The hook counts
 *              Lua instructions, so one long C library call (a pathological
 *              string pattern) is not interrupted.
 *   failure    an error or a limit stops that mod's script: its handlers are
 *              dropped, the Mod Menu shows why (AppMod.script_error), the game
 *              and the other mods go on.
 *   reload     once a second the script (a folder mod) or the archive is
 *              checked; when it changed, the script starts again from scratch.
 */
#include "mods/app_mods.h"
#include "mods/mods_internal.h"

#include "ducktales/events.h"
#include "ducktales/game.h"
#include "ducktales/hud.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define MAX_SCRIPTS         256
#define SCRIPT_INSTRUCTIONS 1000000
#define HOOK_STEP           1000
#define SCRIPT_MEMORY       ((size_t)16 << 20)
#define FRAME_EVENT         EVENT_COUNT

typedef struct {
    char id[64];
    char path[1024];  /* the archive or folder */
    char watch[1024]; /* the file whose change reloads the script */
    bool folder;
    long long stamp;
    lua_State *L;
    int handlers;   /* registry ref: a table of handler lists, event + 1 */
    unsigned wants; /* bit per event, FRAME_EVENT included */
    size_t memory;
    bool over;  /* the last growth was refused */
    long steps; /* hook steps left in this callback */
    bool stopped;
    char why[300];
} Script;

static Script *s_scripts[MAX_SCRIPTS];
static int s_script_count;
static bool s_registered[EVENT_COUNT];
static bool s_dirty;
static uint64_t s_frame;
static time_t s_checked;

static Script *script_of(lua_State *L)
{
    void *ud = NULL;
    lua_getallocf(L, &ud);
    return ud;
}

static void stop(Script *s, const char *fmt, ...)
{
    if (s->stopped) {
        return;
    }
    s->stopped = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->why, sizeof s->why, fmt, ap);
    va_end(ap);
}

/* A refused allocation stops the mod once the error it raises reaches a caller. */
static void stop_if_over(Script *s)
{
    if (s->over) {
        stop(s, "used more than %u MiB of memory", (unsigned)(SCRIPT_MEMORY >> 20));
    }
}

static void *script_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    Script *s = ud;
    size_t old = ptr ? osize : 0;
    if (nsize == 0) {
        free(ptr);
        s->memory -= old;
        return NULL;
    }
    if (nsize > old && (s->stopped || s->memory - old + nsize > SCRIPT_MEMORY)) {
        s->over = true;
        return NULL;
    }
    void *p = realloc(ptr, nsize);
    if (!p) {
        return NULL;
    }
    s->memory = s->memory - old + nsize;
    if (nsize > old) {
        s->over = false;
    }
    return p;
}

static void count_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    Script *s = script_of(L);
    if (--s->steps > 0 && !s->stopped) {
        return;
    }
    stop(s, "ran more than %d instructions in one callback", SCRIPT_INSTRUCTIONS);
    luaL_error(L, "%s", s->why);
}

/* ---- the sandbox's replacements ---- */

static int finish_pcall(lua_State *L, int status, lua_KContext extra)
{
    if (status != LUA_OK && status != LUA_YIELD) {
        Script *s = script_of(L);
        stop_if_over(s);
        if (s->stopped) {
            return lua_error(L);
        }
        lua_pushboolean(L, 0);
        lua_pushvalue(L, -2);
        return 2;
    }
    return lua_gettop(L) - (int)extra;
}

static int sb_pcall(lua_State *L)
{
    luaL_checkany(L, 1);
    lua_pushboolean(L, 1);
    lua_insert(L, 1);
    int status = lua_pcallk(L, lua_gettop(L) - 2, LUA_MULTRET, 0, 0, finish_pcall);
    return finish_pcall(L, status, 0);
}

static int sb_xpcall(lua_State *L)
{
    int n = lua_gettop(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushboolean(L, 1);
    lua_pushvalue(L, 1);
    lua_rotate(L, 3, 2);
    int status = lua_pcallk(L, n - 2, LUA_MULTRET, 2, 2, finish_pcall);
    return finish_pcall(L, status, 2);
}

/* coroutine.resume and coroutine.close: the library's own (upvalue 1), then a limit passes through. */
static int sb_checked(lua_State *L)
{
    int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, n, LUA_MULTRET);
    Script *s = script_of(L);
    stop_if_over(s);
    if (s->stopped) {
        return luaL_error(L, "%s", s->why);
    }
    return lua_gettop(L);
}

/* load(chunk, chunkname, mode, env) with mode "t" whatever is asked: no binary chunks. */
static int sb_load(lua_State *L)
{
    bool env = lua_gettop(L) >= 4;
    lua_settop(L, 4);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_pushliteral(L, "t");
    if (env) {
        lua_pushvalue(L, 4);
    }
    lua_call(L, env ? 4 : 3, LUA_MULTRET);
    return lua_gettop(L) - 4;
}

static int sb_log(lua_State *L)
{
    int n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++) {
        if (i > 1) {
            luaL_addchar(&b, '\t');
        }
        luaL_tolstring(L, i, NULL);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    printf("[ducktales] Mod %s: %.1000s\n", script_of(L)->id, lua_tostring(L, -1));
    fflush(stdout);
    return 0;
}

static int event_of(const char *name)
{
    if (!strcmp(name, "frame")) {
        return FRAME_EVENT;
    }
    for (int e = 0; e < EVENT_COUNT; e++) {
        if (!strcmp(name, Events_Name((EventId)e))) {
            return e;
        }
    }
    return -1;
}

static int sb_on(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int e = event_of(name);
    if (e < 0) {
        return luaL_error(L,
                          "dt.on: no event \"%s\" (frame, player-damaged, life-lost, timer-tick, new-game, "
                          "jump-start, boss-hit)",
                          name);
    }
    Script *s = script_of(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->handlers);
    if (lua_rawgeti(L, -1, e + 1) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_rawseti(L, -3, e + 1);
    }
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
    s->wants |= 1u << e;
    s_dirty = true;
    return 0;
}

static int sb_frame(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)s_frame);
    return 1;
}

static int state_index(lua_State *L)
{
    const char *k = luaL_checkstring(L, 2);
    if (!GameRam_Open()) {
        return luaL_error(L, "dt.state.%s: the game's state is read in dt.on callbacks", k);
    }
    uint8_t max = 0;
    uint32_t money = 0;
    if (!strcmp(k, "land")) {
        lua_pushinteger(L, gLand);
    } else if (!strcmp(k, "lives")) {
        lua_pushinteger(L, gLives);
    } else if (!strcmp(k, "hp")) {
        lua_pushinteger(L, Hud_GetHp(NULL));
    } else if (!strcmp(k, "hp_max")) {
        Hud_GetHp(&max);
        lua_pushinteger(L, max);
    } else if (!strcmp(k, "money")) {
        if (Hud_GetMoney(&money)) {
            lua_pushinteger(L, money);
        } else {
            lua_pushnil(L);
        }
    } else if (!strcmp(k, "time")) {
        lua_pushinteger(L, Hud_GetTime());
    } else if (!strcmp(k, "difficulty")) {
        lua_pushinteger(L, gDifficulty);
    } else if (!strcmp(k, "paused")) {
        lua_pushboolean(L, gPaused != 0);
    } else if (!strcmp(k, "boss_hits")) {
        lua_pushinteger(L, gBossHits);
    } else if (!strcmp(k, "treasures")) {
        lua_pushinteger(L, gTreasures);
    } else if (!strcmp(k, "key_items")) {
        lua_pushinteger(L, gKeyItems);
    } else if (!strcmp(k, "pad")) {
        lua_pushinteger(L, gPadHeld);
    } else if (!strcmp(k, "pad_new")) {
        lua_pushinteger(L, gPadNew);
    } else if (!strcmp(k, "music")) {
        lua_pushinteger(L, gMusic);
    } else if (!strcmp(k, "mode")) {
        lua_pushinteger(L, gPlayerMode);
    } else if (!strcmp(k, "invulnerable")) {
        lua_pushinteger(L, gPlayerInvulnerable);
    } else if (!strcmp(k, "x")) {
        lua_pushinteger(L, Obj_GetWorldX(PLAYER_SLOT));
    } else if (!strcmp(k, "y")) {
        lua_pushinteger(L, gObjects->y[PLAYER_SLOT]);
    } else {
        return luaL_error(L, "dt.state has no field \"%s\"", k);
    }
    return 1;
}

static int state_newindex(lua_State *L)
{
    return luaL_error(L, "dt.state is read only");
}

static void set_function(lua_State *L, const char *name, lua_CFunction fn)
{
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, name);
}

/* table at -1: field name becomes fn with the old value as upvalue 1. */
static void wrap_function(lua_State *L, const char *name, lua_CFunction fn)
{
    lua_getfield(L, -1, name);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

static int setup(lua_State *L)
{
    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base },       { LUA_STRLIBNAME, luaopen_string }, { LUA_TABLIBNAME, luaopen_table },
        { LUA_MATHLIBNAME, luaopen_math }, { LUA_UTF8LIBNAME, luaopen_utf8 },  { LUA_COLIBNAME, luaopen_coroutine },
    };
    for (size_t i = 0; i < sizeof libs / sizeof libs[0]; i++) {
        luaL_requiref(L, libs[i].name, libs[i].func, 1);
        lua_pop(L, 1);
    }
    lua_pushglobaltable(L);
    lua_pushnil(L);
    lua_setfield(L, -2, "dofile");
    lua_pushnil(L);
    lua_setfield(L, -2, "loadfile");
    wrap_function(L, "load", sb_load);
    set_function(L, "pcall", sb_pcall);
    set_function(L, "xpcall", sb_xpcall);
    set_function(L, "print", sb_log);
    lua_getfield(L, -1, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);
    lua_getfield(L, -1, LUA_COLIBNAME);
    wrap_function(L, "resume", sb_checked);
    wrap_function(L, "close", sb_checked);
    lua_pop(L, 1);

    lua_newtable(L);
    set_function(L, "on", sb_on);
    set_function(L, "log", sb_log);
    set_function(L, "frame", sb_frame);
    lua_pushliteral(L, "cancel");
    lua_setfield(L, -2, "CANCEL");
    lua_pushstring(L, script_of(L)->id);
    lua_setfield(L, -2, "mod");
    lua_newtable(L);
    lua_newtable(L);
    set_function(L, "__index", state_index);
    set_function(L, "__newindex", state_newindex);
    lua_pushliteral(L, "dt.state");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "state");
    lua_setfield(L, -2, "dt");
    lua_pop(L, 1);

    lua_newtable(L);
    script_of(L)->handlers = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

/* ---- running ---- */

/* (event, frame) -> whether a handler returned dt.CANCEL; in protected mode. */
static int dispatch(lua_State *L)
{
    Script *s = script_of(L);
    int e = (int)lua_tointeger(L, 1);
    lua_Integer frame = lua_tointeger(L, 2);
    lua_settop(L, 0);
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->handlers);
    lua_rawgeti(L, 1, e + 1);
    lua_Integer n = lua_istable(L, 2) ? (lua_Integer)lua_rawlen(L, 2) : 0;
    bool cancel = false;
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, 2, i);
        if (e == FRAME_EVENT) {
            lua_pushinteger(L, frame);
        } else {
            lua_pushstring(L, Events_Name((EventId)e));
        }
        s->steps = SCRIPT_INSTRUCTIONS / HOOK_STEP;
        lua_call(L, 1, 1);
        const char *r = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
        cancel |= r && !strcmp(r, "cancel");
        lua_pop(L, 1);
    }
    lua_pushboolean(L, cancel);
    return 1;
}

static void report(Script *s)
{
    printf("[ducktales] Mod %s: script stopped: %s\n", s->id, s->why);
    fflush(stdout);
    mods_script_status(s->id, s->why);
    s_dirty = true;
}

static bool run(Script *s, int e)
{
    lua_State *L = s->L;
    lua_pushcfunction(L, dispatch);
    lua_pushinteger(L, e);
    lua_pushinteger(L, (lua_Integer)s_frame);
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        stop_if_over(s);
        const char *msg = lua_tostring(L, -1);
        stop(s, "%s", msg ? msg : "an error without a message");
        lua_pop(L, 1);
        report(s);
        return false;
    }
    bool cancel = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return cancel;
}

static EventResult on_event(EventId id, void *user)
{
    (void)user;
    bool cancel = false;
    for (int i = 0; i < s_script_count; i++) {
        Script *s = s_scripts[i];
        if (s->L && !s->stopped && (s->wants & 1u << id)) {
            cancel |= run(s, id);
        }
    }
    return cancel ? EVENT_CANCEL : EVENT_CONTINUE;
}

static void close_script(Script *s)
{
    if (!s->L) {
        return;
    }
    s->stopped = true;
    s->steps = 0;
    lua_close(s->L);
    s->L = NULL;
    s->wants = 0;
    s_dirty = true;
}

/* Closes stopped scripts and keeps a game event's hook on exactly while a script handles it. */
static void sync_events(void)
{
    s_dirty = false;
    unsigned wants = 0;
    for (int i = 0; i < s_script_count; i++) {
        if (s_scripts[i]->stopped) {
            close_script(s_scripts[i]);
        }
        wants |= s_scripts[i]->wants;
    }
    s_dirty = false;
    for (int e = 0; e < EVENT_COUNT; e++) {
        bool want = (wants & 1u << e) != 0;
        if (want == s_registered[e]) {
            continue;
        }
        if (want) {
            Events_On((EventId)e, on_event, NULL);
        } else {
            Events_Off((EventId)e, on_event, NULL);
        }
        s_registered[e] = want;
    }
}

static long long file_stamp(const char *path)
{
#ifdef _WIN32
    wchar_t w[1024];
    struct _stat64 st;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024) || _wstat64(w, &st)) {
        return -1;
    }
#else
    struct stat st;
    if (stat(path, &st)) {
        return -1;
    }
#endif
    return (long long)st.st_mtime * 1000003 + (long long)st.st_size;
}

/* A fresh state running scripts/main.lua; false with the reason. */
static bool start_script(Script *s, char *err, size_t cap)
{
    ModArchive a;
    if (!mod_archive_open(&a, s->path, s->folder, err, cap)) {
        return false;
    }
    if (s->folder) {
        snprintf(s->watch, sizeof s->watch, "%s/%sscripts/main.lua", a.root, a.prefix);
    } else {
        snprintf(s->watch, sizeof s->watch, "%s", s->path);
    }
    s->stamp = file_stamp(s->watch);
    int at = mod_archive_find(&a, "scripts/main.lua");
    size_t size = 0;
    uint8_t *text = at < 0 ? NULL : mod_archive_read(&a, at, &size, err, cap);
    mod_archive_close(&a);
    if (at < 0) {
        snprintf(err, cap, "scripts/main.lua is missing.");
        return false;
    }
    if (!text) {
        return false;
    }
    s->memory = 0;
    s->over = false;
    s->stopped = false;
    s->why[0] = 0;
    s->wants = 0;
    s->steps = SCRIPT_INSTRUCTIONS / HOOK_STEP;
    lua_State *L = lua_newstate(script_alloc, s);
    if (!L) {
        free(text);
        snprintf(err, cap, "Out of memory.");
        return false;
    }
    s->L = L;
    lua_sethook(L, count_hook, LUA_MASKCOUNT, HOOK_STEP);
    lua_pushcfunction(L, setup);
    int status = lua_pcall(L, 0, 0, 0);
    if (status == LUA_OK) {
        status = luaL_loadbufferx(L, (const char *)text, size, "=scripts/main.lua", "t");
    }
    if (status == LUA_OK) {
        status = lua_pcall(L, 0, 0, 0);
    }
    free(text);
    if (status != LUA_OK) {
        stop_if_over(s);
        const char *msg = lua_tostring(L, -1);
        snprintf(err, cap, "%s", s->stopped ? s->why : msg ? msg : "an error without a message");
        close_script(s);
        return false;
    }
    s_dirty = true;
    return true;
}

bool mods_lua_add(const char *id, const char *path, bool folder)
{
    if (s_script_count == MAX_SCRIPTS) {
        return false;
    }
    Script *s = calloc(1, sizeof *s);
    if (!s) {
        return false;
    }
    snprintf(s->id, sizeof s->id, "%s", id);
    snprintf(s->path, sizeof s->path, "%s", path);
    s->folder = folder;
    s_scripts[s_script_count++] = s;
    char err[300];
    bool ok = start_script(s, err, sizeof err);
    if (!ok) {
        snprintf(s->why, sizeof s->why, "%s", err);
        report(s);
    }
    sync_events();
    return ok;
}

static void reload_changed(void)
{
    for (int i = 0; i < s_script_count; i++) {
        Script *s = s_scripts[i];
        long long stamp = file_stamp(s->watch);
        if (stamp == s->stamp) {
            continue;
        }
        close_script(s);
        char err[300];
        if (start_script(s, err, sizeof err)) {
            printf("[ducktales] Mod %s: script reloaded\n", s->id);
            fflush(stdout);
            mods_script_status(s->id, NULL);
        } else {
            s->stamp = stamp;
            s->stopped = true;
            snprintf(s->why, sizeof s->why, "%s", err);
            report(s);
        }
    }
}

void app_mods_scripts_frame(void)
{
    if (!s_script_count) {
        return;
    }
    s_frame++;
    if (s_dirty) {
        sync_events();
    }
    bool frame = false;
    for (int i = 0; i < s_script_count; i++) {
        frame |= s_scripts[i]->L && !s_scripts[i]->stopped && (s_scripts[i]->wants & 1u << FRAME_EVENT);
    }
    if (frame) {
        GameRam_Begin();
        for (int i = 0; i < s_script_count; i++) {
            Script *s = s_scripts[i];
            if (s->L && !s->stopped && (s->wants & 1u << FRAME_EVENT)) {
                run(s, FRAME_EVENT);
            }
        }
        GameRam_End();
    }
    time_t now = time(NULL);
    if (now != s_checked) {
        s_checked = now;
        reload_changed();
    }
    if (s_dirty) {
        sync_events();
    }
}
