/*
 * fm_script_state.c - the game's state in script mods (fm_mods.h): dt.state
 * reads the fields the game describes (FmField), dt.set.<field>(v) checks the
 * value against the state when it is called, and dt.<query>() gives the
 * game's records (FmQuery), read only. All of them work in dt.on callbacks
 * only. In a game event's handler dt.set writes at once, inside the event's
 * RAM transaction; in a "frame" handler it queues the write for fm_script.c
 * to apply at the frame's end. Every write is logged: the first one to a
 * target and every change of its value get a line, and identical repeats
 * are counted in one line with the next change, every SCRIPT_WRITE_FLUSH
 * frames (fm_script.c) and at exit.
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
int fm_script_sb_query(lua_State *L)
{
    const FmQuery *q = &fm_script_game.queries[lua_tointeger(L, lua_upvalueindex(1))];
    if (!fm_script_game.state_open || !fm_script_game.state_open()) {
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

int fm_script_state_index(lua_State *L)
{
    const char *k = luaL_checkstring(L, 2);
    for (int i = 0; i < fm_script_game.field_count; i++) {
        const FmField *f = &fm_script_game.fields[i];
        if (strcmp(k, f->name)) {
            continue;
        }
        if (!fm_script_game.state_open || !fm_script_game.state_open()) {
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

int fm_script_read_only(lua_State *L)
{
    return luaL_error(L, "%s is read only", lua_tostring(L, lua_upvalueindex(1)));
}

/* dt.set.<field>(value): upvalue 1 is the field's index. */
int fm_script_sb_set(lua_State *L)
{
    const FmField *f = &fm_script_game.fields[lua_tointeger(L, lua_upvalueindex(1))];
    lua_Integer v = f->boolean ? lua_toboolean(L, 1) : luaL_checkinteger(L, 1);
    if (!fm_script_game.state_open || !fm_script_game.state_open()) {
        return luaL_error(L, "dt.set.%s is called in dt.on callbacks", f->name);
    }
    char err[160];
    if (!f->check((long long)v, err, sizeof err)) {
        return luaL_error(L, "dt.set.%s(%I): %s", f->name, v, err);
    }
    if (fm_script_in_event) {
        fm_script_apply(fm_script_of(L), f, (long long)v);
        return 0;
    }
    if (fm_script_write_count == SCRIPT_WRITES) {
        return luaL_error(L, "dt.set: more than %d writes in one frame", SCRIPT_WRITES);
    }
    fm_script_writes[fm_script_write_count++] = (Write){ fm_script_of(L), (int)(f - fm_script_game.fields),
                                                         (long long)v };
    return 0;
}

/* The repeats of w's write since it was logged, as one line. */
static void flush_write(Script *s, WriteLog *w)
{
    if (!w->repeats) {
        return;
    }
    char text[160];
    snprintf(text, sizeof text, "%s(%lld) repeated %ld more times", w->key, w->value, w->repeats);
    fm_script_log_line(s, "", text);
    w->repeats = 0;
}

/* Logs text for a write of value to key, unless it repeats the last value
 * written there: the first write and every change get a line, and repeats
 * are counted and logged with the next change or flush. */
void fm_script_log_write(Script *s, const char *key, long long value, const char *text)
{
    WriteLog *w = NULL;
    for (int i = 0; i < s->write_logs && !w; i++) {
        if (!strcmp(s->write_log[i].key, key)) {
            w = &s->write_log[i];
        }
    }
    if (w && w->value == value) {
        w->repeats++;
        return;
    }
    if (w) {
        flush_write(s, w);
    } else {
        if (s->write_logs == SCRIPT_WRITE_KEYS) {
            flush_write(s, &s->write_log[0]);
            memmove(&s->write_log[0], &s->write_log[1], (SCRIPT_WRITE_KEYS - 1) * sizeof s->write_log[0]);
            s->write_logs--;
        }
        w = &s->write_log[s->write_logs++];
        snprintf(w->key, sizeof w->key, "%s", key);
    }
    w->value = value;
    w->repeats = 0;
    fm_script_log_line(s, "", text);
}

void fm_script_flush_writes(Script *s)
{
    for (int i = 0; i < s->write_logs; i++) {
        flush_write(s, &s->write_log[i]);
    }
}

/* Writes value to the field f for s, and logs it with the value it replaced. */
void fm_script_apply(Script *s, const FmField *f, long long value)
{
    long long was = 0;
    bool known = f->get(&was);
    f->set(value);
    char key[48], text[160];
    snprintf(key, sizeof key, "dt.set.%s", f->name);
    if (known) {
        snprintf(text, sizeof text, "%s(%lld), was %lld", key, value, was);
    } else {
        snprintf(text, sizeof text, "%s(%lld)", key, value);
    }
    fm_script_log_write(s, key, value, text);
}
