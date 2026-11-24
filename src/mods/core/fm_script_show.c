/*
 * fm_script_show.c - what script mods show (fm_mods.h), on the game's
 * presentation, never the machine's.
 *
 *   toast      dt.toast(text): a notice for SCRIPT_TOAST_FRAMES frames, at
 *              most one per mod every SCRIPT_TOAST_GAP frames,
 *              SCRIPT_TOAST_CHARS characters (UTF-8 code points, control
 *              characters as spaces); fm_toast gives it to the game
 *   draw       dt.draw.text and dt.draw.rect collect a frame's drawings, at
 *              most FM_DRAW_PER_MOD a mod; at the frame's end they become
 *              what fm_draws gives the game for the next picture
 */
#include "mods/core/fm_script_internal.h"

#include "lauxlib.h"

#include <stdio.h>
#include <string.h>

#define SCRIPT_TOAST_FRAMES 180
#define SCRIPT_TOAST_GAP    60
#define SCRIPT_TOAST_CHARS  64

static char s_toast_title[128], s_toast_body[SCRIPT_TOAST_CHARS * 4 + 1];
int s_toast_frames;
static FmDraw s_drawing[FM_DRAW_MAX], s_drawn[FM_DRAW_MAX];
static Script *s_drawing_by[FM_DRAW_MAX];
static int s_drawing_count, s_drawn_count;

/* text cut to chars UTF-8 code points, control characters as spaces. */
static void copy_text(char *out, size_t cap, const char *text, int chars)
{
    size_t n = 0;
    for (int seen = 0; text[n] && n + 1 < cap; n++) {
        unsigned char c = (unsigned char)text[n];
        if ((c & 0xC0) != 0x80 && seen++ == chars) {
            break;
        }
        out[n] = c < 0x20 || c == 0x7F ? ' ' : (char)c;
    }
    out[n] = 0;
}

int sb_toast(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    Script *s = script_of(L);
    if (s->last_toast && s_frame - s->last_toast < SCRIPT_TOAST_GAP) {
        lua_pushboolean(L, 0);
        return 1;
    }
    s->last_toast = s_frame ? s_frame : 1;
    copy_text(s_toast_body, sizeof s_toast_body, text, SCRIPT_TOAST_CHARS);
    snprintf(s_toast_title, sizeof s_toast_title, "%s", s->name);
    s_toast_frames = SCRIPT_TOAST_FRAMES;
    log_line(s, "toast: ", s_toast_body);
    lua_pushboolean(L, 1);
    return 1;
}

/* ---- dt.draw ---- */

static uint32_t check_color(lua_State *L, int arg)
{
    lua_Integer c = luaL_checkinteger(L, arg);
    luaL_argcheck(L, c >= 0 && c <= 0xFFFFFF, arg, "a colour is 0xRRGGBB");
    return (uint32_t)c;
}

/* A slot for this frame's drawing, or NULL (logged once) past the caps. */
static FmDraw *add_draw(lua_State *L)
{
    Script *s = script_of(L);
    if (s->draws == FM_DRAW_PER_MOD || s_drawing_count == FM_DRAW_MAX) {
        if (!s->draw_warned) {
            s->draw_warned = true;
            char text[120];
            snprintf(text, sizeof text, "dt.draw: more than %d drawings in one frame; the rest are not shown",
                     s->draws == FM_DRAW_PER_MOD ? FM_DRAW_PER_MOD : FM_DRAW_MAX);
            log_line(s, "", text);
        }
        return NULL;
    }
    s->draws++;
    s_drawing_by[s_drawing_count] = s;
    FmDraw *d = &s_drawing[s_drawing_count++];
    memset(d, 0, sizeof *d);
    return d;
}

/* Coordinates far off the picture are as good as any other off it. */
static int clamp(lua_Integer v)
{
    return v < -0x10000 ? -0x10000 : v > 0x10000 ? 0x10000 : (int)v;
}

int sb_draw_text(lua_State *L)
{
    lua_Integer x = luaL_checkinteger(L, 1), y = luaL_checkinteger(L, 2);
    const char *text = luaL_checkstring(L, 3);
    uint32_t color = lua_isnoneornil(L, 4) ? 0xFFFFFF : check_color(L, 4);
    FmDraw *d = add_draw(L);
    if (d) {
        *d = (FmDraw){ true, (int)clamp(x), (int)clamp(y), 0, 0, color, "" };
        copy_text(d->str, sizeof d->str, text, FM_DRAW_CHARS);
    }
    lua_pushboolean(L, d != NULL);
    return 1;
}

int sb_draw_rect(lua_State *L)
{
    lua_Integer x = luaL_checkinteger(L, 1), y = luaL_checkinteger(L, 2);
    lua_Integer w = luaL_checkinteger(L, 3), h = luaL_checkinteger(L, 4);
    uint32_t color = check_color(L, 5);
    FmDraw *d = add_draw(L);
    if (d) {
        *d = (FmDraw){ false, (int)clamp(x), (int)clamp(y), (int)clamp(w), (int)clamp(h), color, "" };
    }
    lua_pushboolean(L, d != NULL);
    return 1;
}

/* The frame's drawings become the shown ones; a stopped script's are dropped. */
void finish_drawing(void)
{
    s_drawn_count = 0;
    for (int i = 0; i < s_drawing_count; i++) {
        if (!s_drawing_by[i]->stopped) {
            s_drawn[s_drawn_count++] = s_drawing[i];
        }
    }
    s_drawing_count = 0;
    for (int i = 0; i < s_script_count; i++) {
        s_scripts[i]->draws = 0;
    }
}

int fm_draws(const FmDraw **draws)
{
    *draws = s_drawn;
    return s_drawn_count;
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
