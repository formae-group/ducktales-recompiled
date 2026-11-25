/*
 * fm_script.c - script mods (fm_mods.h): a loaded mod's scripts/main.lua in
 * a Lua 5.4 sandbox of its own, with the dt table the game describes
 * (FmScriptGame).
 *
 *   libraries  base without dofile and loadfile (load reads text only, print
 *              writes to the log), string without dump, table, math, utf8 and
 *              coroutine. io, os, package and debug are not even linked.
 *   dt         dt.on(event, fn) for "frame" and the game's events (a game
 *              event's handler gets a table: its name and values), dt.CANCEL,
 *              dt.state (the game's fields, read only), dt.<query>() (the
 *              game's records, read only), dt.set.<field>(v) for the fields
 *              the game declares writable, dt.tune.<name> (the game's
 *              tunables), dt.toast(text), dt.draw.text/rect, dt.store.get/set,
 *              dt.log, dt.mod, dt.frame(), dt.random (the mod's own
 *              generator), and dt.ai(kind, {before = fn, after = fn}) with
 *              dt.SKIP: the mod's own code at the turn of each of the game's
 *              objects of a kind, which it gets as a handle (fm_scripts_ai)
 *   writes     dt.set checks the value against the state when it is called.
 *              In a game event's handler it writes at once, inside the
 *              event's RAM transaction; in a "frame" handler it is queued,
 *              and the queue is applied at the frame's end, after the
 *              "frame" handlers, inside the game's write window. Every write
 *              is logged; identical repeats make one line with their count,
 *              at the next change, every SCRIPT_WRITE_FLUSH frames and at
 *              exit
 *   toast      a notice for SCRIPT_TOAST_FRAMES frames, at most one per mod
 *              every SCRIPT_TOAST_GAP frames, SCRIPT_TOAST_CHARS characters
 *              (UTF-8 code points, control characters as spaces);
 *              the game draws it on its presentation, never the machine's
 *   draw       dt.draw.text and dt.draw.rect collect a frame's drawings, at
 *              most FM_DRAW_PER_MOD a mod; at the frame's end they become
 *              what fm_draws gives the game for the next picture
 *   store      strings, numbers and booleans per mod, kept in
 *              <store_dir>/<id>.ini, at most SCRIPT_STORE_BYTES; written once
 *              a second when changed and at exit
 *   limits     a callback (the main chunk included) runs at most
 *              SCRIPT_INSTRUCTIONS VM instructions (a count hook) and a mod's
 *              state holds at most SCRIPT_MEMORY bytes (its allocator); a
 *              mod's dt.ai callbacks run at most AI_INSTRUCTIONS in all in a
 *              frame, a bound on what they add to it.
 *              pcall, xpcall, coroutine.resume and coroutine.close let a
 *              limit through: it stops the mod, not the call. The hook counts
 *              Lua instructions, so one long C library call (a pathological
 *              string pattern) is not interrupted.
 *   failure    an error or a limit stops that mod's script: its handlers are
 *              dropped, its queued writes are discarded, FmMod.script_error
 *              says why, and the game and the other mods go on.
 *   reload     once a second the script (a folder mod) or the archive is
 *              checked; when it changed, the script starts again from scratch
 *              (its store stays).
 *   files      fm_script.c: the sandbox, the limits, the events and the
 *              frame; fm_script_state.c: dt.state, dt.set and dt.<query>();
 *              fm_script_show.c: dt.toast and dt.draw; fm_script_store.c:
 *              dt.store; fm_script_internal.h: what they share
 */
#include "mods/core/fm_script_internal.h"

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

#define SCRIPT_INSTRUCTIONS 1000000
#define HOOK_STEP           1000
#define SCRIPT_MEMORY       ((size_t)16 << 20)
#define SCRIPT_EVENTS       63
#define FRAME_BIT           63
#define SCRIPT_WRITE_FLUSH  600
#define AI_INSTRUCTIONS     250000

FmScriptGame fm_script_game;
Script *fm_script_scripts[FM_MAX_MODS];
int fm_script_script_count;
static bool s_registered[SCRIPT_EVENTS];
static uint8_t s_ai_registered[FM_AI_KINDS / 8];
static bool s_dirty;
uint64_t fm_script_frame;
static time_t s_checked;
Write fm_script_writes[SCRIPT_WRITES];
int fm_script_write_count;
bool fm_script_in_event;
static bool s_exit_hooked;
static const FmArg *s_args;
static int s_nargs;

Script *fm_script_of(lua_State *L)
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
    Script *s = fm_script_of(L);
    if (--s->steps > 0 && !s->stopped) {
        return;
    }
    luaL_where(L, 0);
    if (s->in_ai) {
        stop(s, "%sran more than %d instructions in dt.ai callbacks in one frame", lua_tostring(L, -1),
             AI_INSTRUCTIONS);
    } else {
        stop(s, "%sran more than %d instructions in one callback", lua_tostring(L, -1), SCRIPT_INSTRUCTIONS);
    }
    lua_pop(L, 1);
    luaL_error(L, "%s", s->why);
}

void fm_script_log_line(const Script *s, const char *what, const char *text)
{
    fm_log("Mod %s: %s%.1000s", s->id, what, text);
}

/* ---- the sandbox's replacements ---- */

static int finish_pcall(lua_State *L, int status, lua_KContext extra)
{
    if (status != LUA_OK && status != LUA_YIELD) {
        Script *s = fm_script_of(L);
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
    Script *s = fm_script_of(L);
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
    fm_script_log_line(fm_script_of(L), "", lua_tostring(L, -1));
    return 0;
}

static int event_of(const char *name)
{
    if (!strcmp(name, "frame")) {
        return FRAME_BIT;
    }
    for (int e = 0; e < fm_script_game.event_count; e++) {
        if (!strcmp(name, fm_script_game.events[e].name)) {
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
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        luaL_addstring(&b, "frame");
        for (int i = 0; i < fm_script_game.event_count; i++) {
            luaL_addstring(&b, ", ");
            luaL_addstring(&b, fm_script_game.events[i].name);
        }
        luaL_pushresult(&b);
        return luaL_error(L, "dt.on: no event \"%s\" (%s)", name, lua_tostring(L, -1));
    }
    Script *s = fm_script_of(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->handlers);
    if (lua_rawgeti(L, -1, e + 1) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_rawseti(L, -3, e + 1);
    }
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
    s->wants |= 1ull << e;
    s_dirty = true;
    return 0;
}

/* dt.ai(kind, {before = fn, after = fn}) gives the kind's objects the mod's
 * code; dt.ai(kind, nil) takes it back. */
static int sb_ai(lua_State *L)
{
    lua_Integer kind = luaL_checkinteger(L, 1);
    char err[160] = "no such kind";
    if (!fm_script_game.ai_kind || kind < 0 || kind >= FM_AI_KINDS ||
        !fm_script_game.ai_kind((long long)kind, err, sizeof err)) {
        return luaL_error(L, "dt.ai(%I): %s", kind, err);
    }
    Script *s = fm_script_of(L);
    uint8_t bit = (uint8_t)(1u << (kind & 7));
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->ai);
    if (lua_isnoneornil(L, 2)) {
        lua_pushnil(L);
        lua_rawseti(L, -2, kind);
        s->ai_kinds[kind >> 3] &= (uint8_t)~bit;
        s_dirty = true;
        return 0;
    }
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_createtable(L, 0, 2);
    int n = 0;
    lua_pushnil(L);
    while (lua_next(L, 2)) {
        const char *k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : NULL;
        if (!k || (strcmp(k, "before") && strcmp(k, "after"))) {
            return luaL_error(L, "dt.ai(%I, {...}): only before and after", kind);
        }
        if (!lua_isfunction(L, -1)) {
            return luaL_error(L, "dt.ai(%I, {%s = ...}): a function is needed", kind, k);
        }
        lua_pushvalue(L, -2);
        lua_insert(L, -2);
        lua_rawset(L, -4);
        n++;
    }
    if (!n) {
        return luaL_error(L, "dt.ai(%I, {...}): before, after or both are needed", kind);
    }
    lua_rawseti(L, -2, kind);
    s->ai_kinds[kind >> 3] |= bit;
    s_dirty = true;
    return 0;
}

static int sb_frame(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)fm_script_frame);
    return 1;
}

/* dt.random: a xorshift64* generator of the mod's own, seeded from its id
 * (FNV-1a) when the script starts, so a run draws the same numbers every
 * time and the game's generator ($C0) is never touched. */
static uint64_t random_seed(uint64_t h, const char *text)
{
    for (; *text; text++) {
        h = (h ^ (uint8_t)*text) * 0x100000001B3ull;
    }
    return h ? h : 1;
}

/* dt.random(n): 1 to n; dt.random(m, n): m to n (called through the table's __call). */
static int sb_random(lua_State *L)
{
    lua_Integer lo = 1, hi;
    if (lua_gettop(L) == 3) {
        lo = luaL_checkinteger(L, 2);
        hi = luaL_checkinteger(L, 3);
    } else if (lua_gettop(L) == 2) {
        hi = luaL_checkinteger(L, 2);
    } else {
        return luaL_error(L, "dt.random(n) or dt.random(m, n)");
    }
    if (hi < lo) {
        return luaL_error(L, "dt.random(%I, %I): the range is empty", lo, hi);
    }
    Script *s = fm_script_of(L);
    uint64_t x = s->random;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    s->random = x;
    x *= 0x2545F4914F6CDD1Dull;
    uint64_t span = (uint64_t)hi - (uint64_t)lo + 1;
    lua_pushinteger(L, (lua_Integer)((uint64_t)lo + (span ? x % span : x)));
    return 1;
}

/* dt.random.seed(n): the same n gives the same numbers again. */
static int sb_random_seed(lua_State *L)
{
    char text[32];
    snprintf(text, sizeof text, "%lld", (long long)luaL_checkinteger(L, 1));
    fm_script_of(L)->random = random_seed(0xCBF29CE484222325ull, text);
    return 0;
}

/* ---- the state ---- */

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

/* Makes the table at -1 read only; name goes in the error. */
static void seal(lua_State *L, const char *name)
{
    lua_newtable(L);
    lua_pushstring(L, name);
    lua_pushcclosure(L, fm_script_read_only, 1);
    lua_setfield(L, -2, "__newindex");
    lua_pushstring(L, name);
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
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
    set_function(L, "toast", fm_script_sb_toast);
    set_function(L, "ai", sb_ai);
    lua_pushliteral(L, "cancel");
    lua_setfield(L, -2, "CANCEL");
    lua_pushliteral(L, "skip");
    lua_setfield(L, -2, "SKIP");
    lua_pushstring(L, fm_script_of(L)->id);
    lua_setfield(L, -2, "mod");

    fm_script_of(L)->random = random_seed(0xCBF29CE484222325ull, fm_script_of(L)->id);
    lua_newtable(L);
    set_function(L, "seed", sb_random_seed);
    lua_newtable(L);
    set_function(L, "__call", sb_random);
    lua_pushliteral(L, "dt.random");
    lua_pushcclosure(L, fm_script_read_only, 1);
    lua_setfield(L, -2, "__newindex");
    lua_pushliteral(L, "dt.random");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "random");

    lua_newtable(L);
    lua_newtable(L);
    set_function(L, "__index", fm_script_state_index);
    lua_pushliteral(L, "dt.state");
    lua_pushcclosure(L, fm_script_read_only, 1);
    lua_setfield(L, -2, "__newindex");
    lua_pushliteral(L, "dt.state");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "state");

    lua_newtable(L);
    for (int i = 0; i < fm_script_game.field_count; i++) {
        if (fm_script_game.fields[i].check && fm_script_game.fields[i].set) {
            lua_pushinteger(L, i);
            lua_pushcclosure(L, fm_script_sb_set, 1);
            lua_setfield(L, -2, fm_script_game.fields[i].name);
        }
    }
    seal(L, "dt.set");
    lua_setfield(L, -2, "set");

    lua_newtable(L);
    lua_newtable(L);
    set_function(L, "__index", fm_script_tune_index);
    set_function(L, "__newindex", fm_script_tune_newindex);
    lua_pushliteral(L, "dt.tune");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "tune");

    lua_newtable(L);
    set_function(L, "get", fm_script_sb_store_get);
    set_function(L, "set", fm_script_sb_store_set);
    seal(L, "dt.store");
    lua_setfield(L, -2, "store");

    lua_newtable(L);
    set_function(L, "text", fm_script_sb_draw_text);
    set_function(L, "rect", fm_script_sb_draw_rect);
    seal(L, "dt.draw");
    lua_setfield(L, -2, "draw");

    for (int i = 0; i < fm_script_game.query_count; i++) {
        lua_pushinteger(L, i);
        lua_pushcclosure(L, fm_script_sb_query, 1);
        lua_setfield(L, -2, fm_script_game.queries[i].name);
        if (fm_script_game.queries[i].create) {
            lua_pushinteger(L, i);
            lua_pushcclosure(L, fm_script_sb_create, 1);
            lua_setfield(L, -2, fm_script_game.queries[i].create);
        }
    }
    for (int i = 0; i < fm_script_game.call_count; i++) {
        const FmCall *c = &fm_script_game.calls[i];
        if (!c->table) {
            lua_pushinteger(L, i);
            lua_pushcclosure(L, fm_script_sb_call, 1);
            lua_setfield(L, -2, c->name);
            continue;
        }
        if (lua_getfield(L, -1, c->table) != LUA_TTABLE) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, c->table);
        }
        lua_pushinteger(L, i);
        lua_pushcclosure(L, fm_script_sb_call, 1);
        lua_setfield(L, -2, c->name);
        lua_pop(L, 1);
    }
    lua_setfield(L, -2, "dt");
    lua_pop(L, 1);

    lua_newtable(L);
    fm_script_of(L)->handlers = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    fm_script_of(L)->ai = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

/* ---- running ---- */

/* (event, frame) -> whether a handler returned dt.CANCEL; in protected mode. */
static int dispatch(lua_State *L)
{
    Script *s = fm_script_of(L);
    int e = (int)lua_tointeger(L, 1);
    lua_Integer frame = lua_tointeger(L, 2);
    lua_settop(L, 0);
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->handlers);
    lua_rawgeti(L, 1, e + 1);
    lua_Integer n = lua_istable(L, 2) ? (lua_Integer)lua_rawlen(L, 2) : 0;
    bool cancel = false;
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, 2, i);
        if (e == FRAME_BIT) {
            lua_pushinteger(L, frame);
        } else {
            lua_createtable(L, 0, s_nargs + 1);
            lua_pushstring(L, fm_script_game.events[e].name);
            lua_setfield(L, -2, "name");
            for (int a = 0; a < s_nargs; a++) {
                lua_pushinteger(L, (lua_Integer)s_args[a].value);
                lua_setfield(L, -2, s_args[a].name);
            }
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
    fm_script_log_line(s, "script stopped: ", s->why);
    fm_set_script_error(s->id, s->path, s->why);
    s_dirty = true;
}

static const char *event_name(int e)
{
    return e == FRAME_BIT ? "frame" : fm_script_game.events[e].name;
}

/* A callback's error (at the top of the stack) or limit stops the script;
 * where says which callback, as " (in ...)" ends the reason. */
static void fail(Script *s, const char *where)
{
    stop_if_over(s);
    const char *msg = lua_tostring(s->L, -1);
    stop(s, "%s", msg ? msg : "an error without a message");
    size_t n = strlen(s->why);
    snprintf(s->why + n, sizeof s->why - n, " (in %s)", where);
    lua_pop(s->L, 1);
    report(s);
}

/* Runs the script's handlers of event e: true when one returned dt.CANCEL
 * and e can be cancelled. */
static bool run(Script *s, int e)
{
    lua_State *L = s->L;
    lua_pushcfunction(L, dispatch);
    lua_pushinteger(L, e);
    lua_pushinteger(L, (lua_Integer)fm_script_frame);
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        char where[96];
        snprintf(where, sizeof where, "a \"%s\" handler", event_name(e));
        fail(s, where);
        return false;
    }
    bool cancel = lua_toboolean(L, -1);
    lua_pop(L, 1);
    bool cancelable = e != FRAME_BIT && fm_script_game.events[e].cancelable;
    if (cancel && !cancelable && !(s->warned & 1ull << e)) {
        s->warned |= 1ull << e;
        char text[160];
        snprintf(text, sizeof text,
                 "a \"%s\" handler returned dt.CANCEL, which that event ignores: it cannot be cancelled",
                 event_name(e));
        fm_script_log_line(s, "", text);
    }
    return cancel && cancelable;
}

static bool live(const Script *s, int bit)
{
    return s->L && !s->stopped && (s->wants & 1ull << bit);
}

bool fm_scripts_event(int event, const FmArg *args, int nargs)
{
    bool cancel = false;
    s_args = args;
    s_nargs = nargs;
    fm_script_in_event = true;
    for (int i = 0; i < fm_script_script_count; i++) {
        if (event >= 0 && event < fm_script_game.event_count && live(fm_script_scripts[i], event)) {
            cancel |= run(fm_script_scripts[i], event);
        }
    }
    fm_script_in_event = false;
    s_nargs = 0;
    return cancel;
}

/* (kind, after, handle) -> whether the kind's before callback returned
 * dt.SKIP; in protected mode. The callbacks share the frame's AI_INSTRUCTIONS. */
static int ai_dispatch(lua_State *L)
{
    Script *s = fm_script_of(L);
    bool after = lua_toboolean(L, 2);
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->ai);
    lua_rawgeti(L, -1, lua_tointeger(L, 1));
    if (lua_getfield(L, -1, after ? "after" : "before") != LUA_TFUNCTION) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushvalue(L, 3);
    s->steps = s->ai_steps;
    s->in_ai = true;
    lua_call(L, 1, 1);
    s->in_ai = false;
    s->ai_steps = s->steps;
    lua_pushboolean(L, !after && lua_type(L, -1) == LUA_TSTRING && !strcmp(lua_tostring(L, -1), "skip"));
    return 1;
}

static bool ai_live(const Script *s, int kind)
{
    return s->L && !s->stopped && (s->ai_kinds[kind >> 3] & 1u << (kind & 7));
}

bool fm_scripts_ai(int kind, bool after, const long long *values)
{
    if (kind < 0 || kind >= FM_AI_KINDS) {
        return false;
    }
    const FmQuery *q = &fm_script_game.queries[fm_script_game.ai_query];
    bool skip = false;
    fm_script_in_event = true;
    for (int i = 0; i < fm_script_script_count; i++) {
        Script *s = fm_script_scripts[i];
        if (!ai_live(s, kind)) {
            continue;
        }
        if (s->ai_frame != fm_script_frame) {
            s->ai_frame = fm_script_frame;
            s->ai_steps = AI_INSTRUCTIONS / HOOK_STEP;
        }
        lua_State *L = s->L;
        lua_pushcfunction(L, ai_dispatch);
        lua_pushinteger(L, kind);
        lua_pushboolean(L, after);
        fm_script_push_handle(L, fm_script_game.ai_query, values);
        if (lua_pcall(L, 3, 1, 0) != LUA_OK) {
            s->in_ai = false;
            char where[96];
            snprintf(where, sizeof where, "dt.ai(%d)'s %s callback", kind, after ? "after" : "before");
            fail(s, where);
            continue;
        }
        bool skipped = lua_toboolean(L, -1);
        lua_pop(L, 1);
        if (skipped) {
            char name[64], text[96];
            q->describe(values, name, sizeof name);
            snprintf(text, sizeof text, "dt.ai(%d): %s skipped", kind, name);
            fm_script_log_write(s, text, 1, text, text);
            skip = true;
        }
    }
    fm_script_in_event = false;
    return skip;
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
    fm_script_flush_writes(s);
    s->write_logs = 0;
    for (int t = 0; t < fm_script_game.tune_count; t++) {
        if (s->tuned & 1u << t) {
            char text[160];
            snprintf(text, sizeof text, "dt.tune.%s = nil (the script ended)", fm_script_game.tunes[t].name);
            fm_script_log_line(s, "", text);
        }
    }
    s->tuned = 0;
    fm_script_sync_tunes();
    s->wants = 0;
    memset(s->ai_kinds, 0, sizeof s->ai_kinds);
    s_dirty = true;
}

/* Closes stopped scripts and keeps a game event's site on exactly while a script handles it. */
static void sync_events(void)
{
    uint64_t wants = 0;
    uint8_t kinds[FM_AI_KINDS / 8] = { 0 };
    for (int i = 0; i < fm_script_script_count; i++) {
        if (fm_script_scripts[i]->stopped) {
            close_script(fm_script_scripts[i]);
        }
        wants |= fm_script_scripts[i]->wants;
        for (int b = 0; b < FM_AI_KINDS / 8; b++) {
            kinds[b] |= fm_script_scripts[i]->ai_kinds[b];
        }
    }
    for (int k = 0; k < FM_AI_KINDS; k++) {
        bool want = kinds[k >> 3] & 1u << (k & 7), had = s_ai_registered[k >> 3] & 1u << (k & 7);
        if (want != had && fm_script_game.ai_listen) {
            fm_script_game.ai_listen(k, want);
            s_ai_registered[k >> 3] ^= (uint8_t)(1u << (k & 7));
        }
    }
    s_dirty = false;
    for (int e = 0; e < fm_script_game.event_count; e++) {
        bool want = (wants & 1ull << e) != 0;
        if (want != s_registered[e] && fm_script_game.listen) {
            fm_script_game.listen(e, want);
            s_registered[e] = want;
        }
    }
}

static long long file_stamp(const char *path)
{
#ifdef _WIN32
    wchar_t w[1024];
    WIN32_FILE_ATTRIBUTE_DATA st;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024) || !GetFileAttributesExW(w, GetFileExInfoStandard, &st)) {
        return -1;
    }
    long long written = (long long)st.ftLastWriteTime.dwHighDateTime << 32 | st.ftLastWriteTime.dwLowDateTime;
    return written * 31 + ((long long)st.nFileSizeHigh << 32 | st.nFileSizeLow);
#else
    struct stat st;
    if (stat(path, &st)) {
        return -1;
    }
    return (long long)st.st_mtime * 1000003 + (long long)st.st_size;
#endif
}

/* A fresh state running scripts/main.lua; false with the reason. */
static bool start_script(Script *s, char *err, size_t cap)
{
    FmArchive a;
    if (!fm_archive_open(&a, s->path, s->folder, err, cap)) {
        return false;
    }
    if (s->folder) {
        snprintf(s->watch, sizeof s->watch, "%s/%sscripts/main.lua", a.root, a.prefix);
    } else {
        snprintf(s->watch, sizeof s->watch, "%s", s->path);
    }
    s->stamp = file_stamp(s->watch);
    int at = fm_archive_find(&a, "scripts/main.lua");
    size_t size = 0;
    uint8_t *text = at < 0 ? NULL : fm_archive_read(&a, at, &size, err, cap);
    fm_archive_close(&a);
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
    s->warned = 0;
    memset(s->ai_kinds, 0, sizeof s->ai_kinds);
    s->in_ai = false;
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
        const char *chunk = fm_skip_bom((const char *)text);
        status = luaL_loadbufferx(L, chunk, size - (size_t)(chunk - (const char *)text), "=scripts/main.lua", "t");
    }
    if (status == LUA_OK) {
        status = lua_pcall(L, 0, 0, 0);
    }
    free(text);
    if (status != LUA_OK) {
        stop_if_over(s);
        const char *msg = lua_tostring(L, -1);
        snprintf(err, cap, "%s (when the script started)",
                 s->stopped ? s->why
                 : msg      ? msg
                            : "an error without a message");
        close_script(s);
        return false;
    }
    s_dirty = true;
    return true;
}

static void flush_at_exit(void)
{
    fm_scripts_flush();
    for (int i = 0; i < fm_script_script_count; i++) {
        fm_script_flush_writes(fm_script_scripts[i]);
    }
}

void fm_scripts_configure(const FmScriptGame *game)
{
    fm_script_game = *game;
    if (fm_script_game.event_count > SCRIPT_EVENTS) {
        fm_script_game.event_count = SCRIPT_EVENTS;
    }
    if (fm_script_game.tune_count > SCRIPT_TUNES) {
        fm_script_game.tune_count = SCRIPT_TUNES;
    }
}

bool fm_script_add(const FmMod *mod)
{
    if (fm_script_script_count == FM_MAX_MODS) {
        return false;
    }
    Script *s = calloc(1, sizeof *s);
    if (!s) {
        return false;
    }
    snprintf(s->id, sizeof s->id, "%s", mod->id);
    snprintf(s->name, sizeof s->name, "%s", mod->name);
    snprintf(s->path, sizeof s->path, "%s", mod->path);
    s->folder = mod->folder;
    fm_script_scripts[fm_script_script_count++] = s;
    if (!s_exit_hooked) {
        s_exit_hooked = true;
        atexit(flush_at_exit);
    }
    char err[300];
    bool ok = start_script(s, err, sizeof err);
    if (!ok) {
        snprintf(s->why, sizeof s->why, "%s", err);
        s->stopped = true;
        report(s);
    }
    sync_events();
    return ok;
}

int fm_script_count(void)
{
    return fm_script_script_count;
}

static void reload_changed(void)
{
    for (int i = 0; i < fm_script_script_count; i++) {
        Script *s = fm_script_scripts[i];
        long long stamp = file_stamp(s->watch);
        if (stamp == s->stamp) {
            continue;
        }
        close_script(s);
        char err[300];
        if (start_script(s, err, sizeof err)) {
            fm_script_log_line(s, "script reloaded", "");
            fm_set_script_error(s->id, s->path, "");
        } else {
            s->stamp = stamp;
            s->stopped = true;
            snprintf(s->why, sizeof s->why, "%s", err);
            report(s);
        }
    }
}

void fm_scripts_flush(void)
{
    for (int i = 0; i < fm_script_script_count; i++) {
        fm_script_store_write(fm_script_scripts[i]);
    }
}

void fm_scripts_frame(void)
{
    if (!fm_script_script_count) {
        return;
    }
    fm_script_frame++;
    if (fm_script_toast_frames > 0) {
        fm_script_toast_frames--;
    }
    if (s_dirty) {
        sync_events();
    }
    bool frame = fm_script_write_count > 0;
    for (int i = 0; i < fm_script_script_count; i++) {
        frame |= live(fm_script_scripts[i], FRAME_BIT);
    }
    if (frame && fm_script_game.begin_frame && fm_script_game.end_frame) {
        fm_script_game.begin_frame();
        for (int i = 0; i < fm_script_script_count; i++) {
            if (live(fm_script_scripts[i], FRAME_BIT)) {
                run(fm_script_scripts[i], FRAME_BIT);
            }
        }
        for (int i = 0; i < fm_script_write_count; i++) {
            const Write *w = &fm_script_writes[i];
            const FmField *f = &fm_script_game.fields[w->field];
            char err[160];
            if (w->script->stopped) {
                continue;
            }
            if (f->check(w->value, err, sizeof err)) {
                fm_script_apply(w->script, f, w->value);
            } else {
                char text[260];
                snprintf(text, sizeof text, "dt.set.%s(%lld) dropped at the frame's end: %s", f->name, w->value, err);
                fm_script_log_line(w->script, "", text);
            }
        }
        fm_script_game.end_frame();
    }
    fm_script_write_count = 0;
    fm_script_finish_drawing();
    for (int i = 0; i < fm_script_script_count && fm_script_frame % SCRIPT_WRITE_FLUSH == 0; i++) {
        fm_script_flush_writes(fm_script_scripts[i]);
    }
    time_t now = time(NULL);
    if (now != s_checked) {
        s_checked = now;
        reload_changed();
        fm_scripts_flush();
    }
    if (s_dirty) {
        sync_events();
    }
}
