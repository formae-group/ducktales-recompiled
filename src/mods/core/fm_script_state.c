/*
 * fm_script_state.c - the game's state in script mods (fm_mods.h): dt.state
 * reads the fields the game describes (FmField), dt.set.<field>(v) checks the
 * value against the state when it is called and queues it for fm_script.c to
 * apply at the frame's end, and dt.<query>() gives the game's records
 * (FmQuery), read only. All of them work in dt.on callbacks only.
 */
#include "mods/core/fm_script_internal.h"

#include "lauxlib.h"

#include <string.h>

static void push_record(lua_State *L, const FmQuery *q, const long long *values)
{
    lua_createtable(L, 0, q->field_count);
    for (int f = 0; f < q->field_count; f++) {
        lua_pushinteger(L, (lua_Integer)values[f]);
        lua_setfield(L, -2, q->fields[f]);
    }
}

/* dt.<query>(): upvalue 1 is the query's index. */
int sb_query(lua_State *L)
{
    const FmQuery *q = &s_game.queries[lua_tointeger(L, lua_upvalueindex(1))];
    if (!s_game.state_open || !s_game.state_open()) {
        return luaL_error(L, "dt.%s: the game's state is read in dt.on callbacks", q->name);
    }
    long long values[FM_QUERY_VALUES];
    int n = q->read(values);
    if (!q->list) {
        push_record(L, q, values);
        return 1;
    }
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        push_record(L, q, &values[i * q->field_count]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int state_index(lua_State *L)
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

int read_only(lua_State *L)
{
    return luaL_error(L, "%s is read only", lua_tostring(L, lua_upvalueindex(1)));
}

/* dt.set.<field>(value): upvalue 1 is the field's index. */
int sb_set(lua_State *L)
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
