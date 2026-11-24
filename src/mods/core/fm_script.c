/*
 * fm_script.c - script mods (fm_mods.h): a loaded mod's scripts/main.lua in
 * a Lua 5.4 sandbox of its own, with the dt table the game describes
 * (FmScriptGame).
 *
 *   libraries  base without dofile and loadfile (load reads text only, print
 *              writes to the log), string without dump, table, math, utf8 and
 *              coroutine. io, os, package and debug are not even linked.
 *   dt         dt.on(event, fn) for "frame" and the game's events, dt.CANCEL,
 *              dt.state (the game's fields, read only), dt.set.<field>(v) for
 *              the fields the game declares writable, dt.toast(text),
 *              dt.store.get/set, dt.log, dt.mod and dt.frame()
 *   writes     dt.set checks the value against the state when it is called
 *              and queues it; the queue is applied at the frame's end, after
 *              the "frame" handlers, inside the game's write window
 *   toast      a notice for SCRIPT_TOAST_FRAMES frames, at most one per mod
 *              every SCRIPT_TOAST_GAP frames, SCRIPT_TOAST_CHARS characters
 *              (UTF-8 code points, control characters as spaces);
 *              the game draws it on its presentation, never the machine's
 *   store      strings, numbers and booleans per mod, kept in
 *              <store_dir>/<id>.ini, at most SCRIPT_STORE_BYTES; written once
 *              a second when changed and at exit
 *   limits     a callback (the main chunk included) runs at most
 *              SCRIPT_INSTRUCTIONS VM instructions (a count hook) and a mod's
 *              state holds at most SCRIPT_MEMORY bytes (its allocator).
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
 */
#include "mods/core/fm_mods.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <ctype.h>
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
#define SCRIPT_WRITES       256
#define SCRIPT_TOAST_FRAMES 180
#define SCRIPT_TOAST_GAP    60
#define SCRIPT_TOAST_CHARS  64
#define SCRIPT_STORE_BYTES  (64 * 1024)
#define STORE_KEY_MAX       64

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

static FmScriptGame s_game;
static Script *s_scripts[FM_MAX_MODS];
static int s_script_count;
static bool s_registered[SCRIPT_EVENTS];
static bool s_dirty;
static uint64_t s_frame;
static time_t s_checked;
static Write s_writes[SCRIPT_WRITES];
static int s_write_count;
static char s_toast_title[128], s_toast_body[SCRIPT_TOAST_CHARS * 4 + 1];
static int s_toast_frames;
static bool s_exit_hooked;

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
    luaL_where(L, 0);
    stop(s, "%sran more than %d instructions in one callback", lua_tostring(L, -1), SCRIPT_INSTRUCTIONS);
    lua_pop(L, 1);
    luaL_error(L, "%s", s->why);
}

static void log_line(const Script *s, const char *what, const char *text)
{
    fm_log("Mod %s: %s%.1000s", s->id, what, text);
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
    log_line(script_of(L), "", lua_tostring(L, -1));
    return 0;
}

static int event_of(const char *name)
{
    if (!strcmp(name, "frame")) {
        return FRAME_BIT;
    }
    for (int e = 0; e < s_game.event_count; e++) {
        if (!strcmp(name, s_game.events[e].name)) {
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
        for (int i = 0; i < s_game.event_count; i++) {
            luaL_addstring(&b, ", ");
            luaL_addstring(&b, s_game.events[i].name);
        }
        luaL_pushresult(&b);
        return luaL_error(L, "dt.on: no event \"%s\" (%s)", name, lua_tostring(L, -1));
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
    s->wants |= 1ull << e;
    s_dirty = true;
    return 0;
}

static int sb_frame(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)s_frame);
    return 1;
}

static int sb_toast(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    Script *s = script_of(L);
    if (s->last_toast && s_frame - s->last_toast < SCRIPT_TOAST_GAP) {
        lua_pushboolean(L, 0);
        return 1;
    }
    s->last_toast = s_frame ? s_frame : 1;
    size_t n = 0;
    for (int chars = 0; text[n] && n + 1 < sizeof s_toast_body; n++) {
        unsigned char c = (unsigned char)text[n];
        if ((c & 0xC0) != 0x80 && chars++ == SCRIPT_TOAST_CHARS) {
            break;
        }
        s_toast_body[n] = c < 0x20 || c == 0x7F ? ' ' : (char)c;
    }
    s_toast_body[n] = 0;
    snprintf(s_toast_title, sizeof s_toast_title, "%s", s->name);
    s_toast_frames = SCRIPT_TOAST_FRAMES;
    log_line(s, "toast: ", s_toast_body);
    lua_pushboolean(L, 1);
    return 1;
}

static int state_index(lua_State *L)
{
    const char *k = luaL_checkstring(L, 2);
    for (int i = 0; i < s_game.field_count; i++) {
        const FmField *f = &s_game.fields[i];
        if (strcmp(k, f->name)) {
            continue;
        }
        if (!s_game.state_open || !s_game.state_open()) {
            return luaL_error(L, "dt.state.%s: the game's state is read in dt.on callbacks", k);
        }
        long long v = 0;
        if (!f->get(&v)) {
            lua_pushnil(L);
        } else if (f->boolean) {
            lua_pushboolean(L, v != 0);
        } else {
            lua_pushinteger(L, (lua_Integer)v);
        }
        return 1;
    }
    return luaL_error(L, "dt.state has no field \"%s\"", k);
}

static int read_only(lua_State *L)
{
    return luaL_error(L, "%s is read only", lua_tostring(L, lua_upvalueindex(1)));
}

/* dt.set.<field>(value): upvalue 1 is the field's index. */
static int sb_set(lua_State *L)
{
    const FmField *f = &s_game.fields[lua_tointeger(L, lua_upvalueindex(1))];
    lua_Integer v = f->boolean ? lua_toboolean(L, 1) : luaL_checkinteger(L, 1);
    if (!s_game.state_open || !s_game.state_open()) {
        return luaL_error(L, "dt.set.%s is called in dt.on callbacks", f->name);
    }
    char err[160];
    if (!f->check((long long)v, err, sizeof err)) {
        return luaL_error(L, "dt.set.%s(%I): %s", f->name, v, err);
    }
    if (s_write_count == SCRIPT_WRITES) {
        return luaL_error(L, "dt.set: more than %d writes in one frame", SCRIPT_WRITES);
    }
    s_writes[s_write_count++] = (Write){ script_of(L), (int)(f - s_game.fields), (long long)v };
    return 0;
}

/* ---- dt.store ---- */

static bool device_name(const char *id)
{
    static const char *const NAMES[] = { "con", "prn", "aux", "nul" };
    char base[64];
    size_t n = strcspn(id, ".");
    snprintf(base, sizeof base, "%.*s", (int)n, id);
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++) {
        if (!strcmp(base, NAMES[i])) {
            return true;
        }
    }
    return (!strncmp(base, "com", 3) || !strncmp(base, "lpt", 3)) && strlen(base) == 4 &&
           isdigit((unsigned char)base[3]);
}

static bool store_path(const Script *s, char *out, size_t cap)
{
    if (!s_game.store_dir || !s_game.store_dir[0] || !fm_valid_id(s->id) || device_name(s->id)) {
        return false;
    }
    snprintf(out, cap, "%s/%s.ini", s_game.store_dir, s->id);
    return true;
}

static StoreItem *store_find(Script *s, const char *key)
{
    for (int i = 0; i < s->store_count; i++) {
        if (!strcmp(s->store[i].key, key)) {
            return &s->store[i];
        }
    }
    return NULL;
}

static size_t item_bytes(const char *key, const char *value)
{
    return strlen(key) + strlen(value) + 4;
}

/* Puts key = value (value malloc'd, taken; NULL removes); false when the store would pass its cap. */
static bool store_put(Script *s, const char *key, char *value)
{
    StoreItem *it = store_find(s, key);
    size_t now = s->store_bytes - (it ? item_bytes(it->key, it->value) : 0) + (value ? item_bytes(key, value) : 0);
    if (value && now > SCRIPT_STORE_BYTES) {
        free(value);
        return false;
    }
    if (!value) {
        if (it) {
            free(it->value);
            *it = s->store[--s->store_count];
        }
    } else if (it) {
        free(it->value);
        it->value = value;
    } else {
        StoreItem *grown = realloc(s->store, (size_t)(s->store_count + 1) * sizeof *grown);
        if (!grown) {
            free(value);
            return false;
        }
        s->store = grown;
        snprintf(s->store[s->store_count].key, sizeof s->store[0].key, "%s", key);
        s->store[s->store_count++].value = value;
    }
    s->store_bytes = now;
    return true;
}

static bool valid_key(const char *key)
{
    size_t n = strlen(key);
    if (!n || n > STORE_KEY_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!isalnum((unsigned char)key[i]) && key[i] != '_' && key[i] != '-' && key[i] != '.') {
            return false;
        }
    }
    return true;
}

static void store_load(Script *s)
{
    if (s->store_loaded) {
        return;
    }
    s->store_loaded = true;
    char path[1200];
    size_t size;
    char *text = store_path(s, path, sizeof path) ? (char *)fm_read_file(path, &size) : NULL;
    if (!text) {
        return;
    }
    int dropped = 0;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *eq = strstr(line, " = ");
        if (!eq) {
            continue;
        }
        *eq = 0;
        const char *v = eq + 3;
        if (valid_key(line) && strlen(v) >= 2 && v[1] == ':' && strchr("sinb", v[0])) {
            char *copy = malloc(strlen(v) + 1);
            if (copy) {
                strcpy(copy, v);
            }
            dropped += !copy || !store_put(s, line, copy);
        }
    }
    free(text);
    if (dropped) {
        char why[100];
        snprintf(why, sizeof why, ": %d keys past %d bytes were dropped", dropped, SCRIPT_STORE_BYTES);
        log_line(s, path, why);
    }
}

/* Writes the store to <id>.ini.tmp, then puts it in place of <id>.ini, so a
 * crash or a full disk never leaves half a file; a failure keeps it dirty
 * (tried again a second later) and is logged once. */
static void store_write(Script *s)
{
    char path[1200], tmp[1210];
    if (!s->store_dirty || !store_path(s, path, sizeof path)) {
        return;
    }
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    fm_make_dir(s_game.store_dir);
#ifdef _WIN32
    wchar_t w[1210], wt[1210];
    bool named = MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1210) &&
                 MultiByteToWideChar(CP_UTF8, 0, tmp, -1, wt, 1210);
    FILE *f = named ? _wfopen(wt, L"wb") : NULL;
#else
    FILE *f = fopen(tmp, "wb");
#endif
    bool ok = f != NULL;
    if (f) {
        fprintf(f, "; dt.store of the mod %s\n", s->id);
        for (int i = 0; i < s->store_count; i++) {
            fprintf(f, "%s = %s\n", s->store[i].key, s->store[i].value);
        }
        ok = !ferror(f);
        ok = fclose(f) == 0 && ok;
    }
#ifdef _WIN32
    ok = ok && MoveFileExW(wt, w, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok && named) {
        DeleteFileW(wt);
    }
#else
    ok = ok && rename(tmp, path) == 0;
    if (!ok) {
        remove(tmp);
    }
#endif
    s->store_dirty = !ok;
    if (!ok && !s->store_failed) {
        log_line(s, "dt.store could not be written to ", path);
    }
    s->store_failed = !ok;
}

static int sb_store_get(lua_State *L)
{
    const char *key = luaL_checkstring(L, 1);
    Script *s = script_of(L);
    store_load(s);
    StoreItem *it = store_find(s, key);
    if (!it) {
        lua_pushnil(L);
        return 1;
    }
    const char *v = it->value + 2;
    if (it->value[0] == 'i') {
        lua_pushinteger(L, (lua_Integer)strtoll(v, NULL, 10));
    } else if (it->value[0] == 'n') {
        lua_pushnumber(L, (lua_Number)strtod(v, NULL));
    } else if (it->value[0] == 'b') {
        lua_pushboolean(L, !strcmp(v, "true"));
    } else {
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        for (; *v; v++) {
            if (*v == '\\' && v[1]) {
                v++;
                luaL_addchar(&b, *v == 'n' ? '\n' : *v == 'r' ? '\r' : *v == '0' ? '\0' : *v);
            } else {
                luaL_addchar(&b, *v);
            }
        }
        luaL_pushresult(&b);
    }
    return 1;
}

static int sb_store_set(lua_State *L)
{
    const char *key = luaL_checkstring(L, 1);
    Script *s = script_of(L);
    if (!valid_key(key)) {
        return luaL_error(L, "dt.store: a key is 1-%d characters of A-Z, a-z, 0-9, '_', '-' and '.'", STORE_KEY_MAX);
    }
    char path[1200];
    if (!store_path(s, path, sizeof path)) {
        return luaL_error(L, "dt.store: this mod has no store (its id cannot name a file)");
    }
    store_load(s);
    char *value = NULL;
    int type = lua_type(L, 2);
    if (type == LUA_TNUMBER) {
        char num[64];
        if (lua_isinteger(L, 2)) {
            snprintf(num, sizeof num, "i:%lld", (long long)lua_tointeger(L, 2));
        } else {
            snprintf(num, sizeof num, "n:%.17g", (double)lua_tonumber(L, 2));
        }
        value = malloc(strlen(num) + 1);
        if (value) {
            strcpy(value, num);
        }
    } else if (type == LUA_TBOOLEAN) {
        const char *b = lua_toboolean(L, 2) ? "b:true" : "b:false";
        value = malloc(strlen(b) + 1);
        if (value) {
            strcpy(value, b);
        }
    } else if (type == LUA_TSTRING) {
        size_t n;
        const char *t = lua_tolstring(L, 2, &n);
        if (n > SCRIPT_STORE_BYTES) {
            return luaL_error(L, "dt.store: more than %d bytes", SCRIPT_STORE_BYTES);
        }
        value = malloc(2 * n + 3);
        if (value) {
            char *o = value;
            *o++ = 's';
            *o++ = ':';
            for (size_t i = 0; i < n; i++) {
                char c = t[i];
                if (c == '\\' || c == '\n' || c == '\r' || c == '\0') {
                    *o++ = '\\';
                    c = c == '\n' ? 'n' : c == '\r' ? 'r' : c == '\0' ? '0' : c;
                }
                *o++ = c;
            }
            *o = 0;
        }
    } else if (type != LUA_TNIL) {
        return luaL_error(L, "dt.store: values are strings, numbers and booleans (got %s)", luaL_typename(L, 2));
    }
    if (type != LUA_TNIL && !value) {
        return luaL_error(L, "dt.store: out of memory");
    }
    if (!store_put(s, key, value)) {
        return luaL_error(L, "dt.store: more than %d bytes", SCRIPT_STORE_BYTES);
    }
    s->store_dirty = true;
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
    lua_pushcclosure(L, read_only, 1);
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
    set_function(L, "toast", sb_toast);
    lua_pushliteral(L, "cancel");
    lua_setfield(L, -2, "CANCEL");
    lua_pushstring(L, script_of(L)->id);
    lua_setfield(L, -2, "mod");

    lua_newtable(L);
    lua_newtable(L);
    set_function(L, "__index", state_index);
    lua_pushliteral(L, "dt.state");
    lua_pushcclosure(L, read_only, 1);
    lua_setfield(L, -2, "__newindex");
    lua_pushliteral(L, "dt.state");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "state");

    lua_newtable(L);
    for (int i = 0; i < s_game.field_count; i++) {
        if (s_game.fields[i].check && s_game.fields[i].set) {
            lua_pushinteger(L, i);
            lua_pushcclosure(L, sb_set, 1);
            lua_setfield(L, -2, s_game.fields[i].name);
        }
    }
    seal(L, "dt.set");
    lua_setfield(L, -2, "set");

    lua_newtable(L);
    set_function(L, "get", sb_store_get);
    set_function(L, "set", sb_store_set);
    seal(L, "dt.store");
    lua_setfield(L, -2, "store");
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
        if (e == FRAME_BIT) {
            lua_pushinteger(L, frame);
        } else {
            lua_pushstring(L, s_game.events[e].name);
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
    log_line(s, "script stopped: ", s->why);
    FmMod *m = fm_find(s->id);
    if (m) {
        snprintf(m->script_error, sizeof m->script_error, "%s", s->why);
    }
    s_dirty = true;
}

static const char *event_name(int e)
{
    return e == FRAME_BIT ? "frame" : s_game.events[e].name;
}

/* Runs the script's handlers of event e: true when one returned dt.CANCEL
 * and e can be cancelled. */
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
        size_t n = strlen(s->why);
        snprintf(s->why + n, sizeof s->why - n, " (in a \"%s\" handler)", event_name(e));
        lua_pop(L, 1);
        report(s);
        return false;
    }
    bool cancel = lua_toboolean(L, -1);
    lua_pop(L, 1);
    bool cancelable = e != FRAME_BIT && s_game.events[e].cancelable;
    if (cancel && !cancelable && !(s->warned & 1ull << e)) {
        s->warned |= 1ull << e;
        char text[160];
        snprintf(text, sizeof text,
                 "a \"%s\" handler returned dt.CANCEL, which that event ignores: it cannot be cancelled",
                 event_name(e));
        log_line(s, "", text);
    }
    return cancel && cancelable;
}

static bool live(const Script *s, int bit)
{
    return s->L && !s->stopped && (s->wants & 1ull << bit);
}

bool fm_scripts_event(int event)
{
    bool cancel = false;
    for (int i = 0; i < s_script_count; i++) {
        if (event >= 0 && event < s_game.event_count && live(s_scripts[i], event)) {
            cancel |= run(s_scripts[i], event);
        }
    }
    return cancel;
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

/* Closes stopped scripts and keeps a game event's site on exactly while a script handles it. */
static void sync_events(void)
{
    uint64_t wants = 0;
    for (int i = 0; i < s_script_count; i++) {
        if (s_scripts[i]->stopped) {
            close_script(s_scripts[i]);
        }
        wants |= s_scripts[i]->wants;
    }
    s_dirty = false;
    for (int e = 0; e < s_game.event_count; e++) {
        bool want = (wants & 1ull << e) != 0;
        if (want != s_registered[e] && s_game.listen) {
            s_game.listen(e, want);
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
}

void fm_scripts_configure(const FmScriptGame *game)
{
    s_game = *game;
    if (s_game.event_count > SCRIPT_EVENTS) {
        s_game.event_count = SCRIPT_EVENTS;
    }
}

bool fm_script_add(const FmMod *mod)
{
    if (s_script_count == FM_MAX_MODS) {
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
    s_scripts[s_script_count++] = s;
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
    return s_script_count;
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
            log_line(s, "script reloaded", "");
            FmMod *m = fm_find(s->id);
            if (m) {
                m->script_error[0] = 0;
            }
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
    for (int i = 0; i < s_script_count; i++) {
        store_write(s_scripts[i]);
    }
}

void fm_scripts_frame(void)
{
    if (!s_script_count) {
        return;
    }
    s_frame++;
    if (s_toast_frames > 0) {
        s_toast_frames--;
    }
    if (s_dirty) {
        sync_events();
    }
    bool frame = s_write_count > 0;
    for (int i = 0; i < s_script_count; i++) {
        frame |= live(s_scripts[i], FRAME_BIT);
    }
    if (frame && s_game.begin_frame && s_game.end_frame) {
        s_game.begin_frame();
        for (int i = 0; i < s_script_count; i++) {
            if (live(s_scripts[i], FRAME_BIT)) {
                run(s_scripts[i], FRAME_BIT);
            }
        }
        for (int i = 0; i < s_write_count; i++) {
            const Write *w = &s_writes[i];
            const FmField *f = &s_game.fields[w->field];
            char err[160];
            if (w->script->stopped) {
                continue;
            }
            if (f->check(w->value, err, sizeof err)) {
                f->set(w->value);
            } else {
                char text[260];
                snprintf(text, sizeof text, "dt.set.%s(%lld) dropped at the frame's end: %s", f->name, w->value, err);
                log_line(w->script, "", text);
            }
        }
        s_game.end_frame();
    }
    s_write_count = 0;
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

bool fm_toast(char *title, size_t title_cap, char *body, size_t body_cap)
{
    if (s_toast_frames <= 0) {
        return false;
    }
    snprintf(title, title_cap, "%s", s_toast_title);
    snprintf(body, body_cap, "%s", s_toast_body);
    return true;
}
