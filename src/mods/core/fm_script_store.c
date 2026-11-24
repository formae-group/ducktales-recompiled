/*
 * fm_script_store.c - dt.store.get and dt.store.set (fm_mods.h): strings,
 * numbers and booleans per mod, kept in <store_dir>/<id>.ini, at most
 * SCRIPT_STORE_BYTES, keys of 1-STORE_KEY_MAX characters. fm_script.c
 * writes it once a second when changed and at exit.
 */
#include "mods/core/fm_script_internal.h"

#include "lauxlib.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define SCRIPT_STORE_BYTES (64 * 1024)

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
    if (!fm_script_game.store_dir || !fm_script_game.store_dir[0] || !fm_valid_id(s->id) || device_name(s->id)) {
        return false;
    }
    snprintf(out, cap, "%s/%s.ini", fm_script_game.store_dir, s->id);
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
        fm_script_log_line(s, path, why);
    }
}

/* Writes the store to <id>.ini.tmp, then puts it in place of <id>.ini, so a
 * crash or a full disk never leaves half a file; a failure keeps it dirty
 * (tried again a second later) and is logged once. */
void fm_script_store_write(Script *s)
{
    char path[1200], tmp[1210];
    if (!s->store_dirty || !store_path(s, path, sizeof path)) {
        return;
    }
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    fm_make_dir(fm_script_game.store_dir);
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
        fm_script_log_line(s, "dt.store could not be written to ", path);
    }
    s->store_failed = !ok;
}

int fm_script_sb_store_get(lua_State *L)
{
    const char *key = luaL_checkstring(L, 1);
    Script *s = fm_script_of(L);
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

int fm_script_sb_store_set(lua_State *L)
{
    const char *key = luaL_checkstring(L, 1);
    Script *s = fm_script_of(L);
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
