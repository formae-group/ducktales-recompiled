/*
 * dt_mods_sprites.c - DuckTales' sprite replacements (app_mods.h,
 * docs/MODS.md "Sprites"): one picture for a whole object frame, in place of
 * the 8x8 sprites the game builds it from. Presentation only: the compositor
 * (host/ducktales_sprites.c) draws them, the machine never sees them.
 *
 *   key       sprites/<kind>/<frame>_<x>_<y>.png: the object's kind ($0710)
 *             and the metasprite id the animation shows, each in 2 hex
 *             digits, and the anchor (the pixel at the object's position) in
 *             decimal; _flip before .png is the art for the flipped frame,
 *             else the plain art is mirrored about the game's own axis
 *   table     first added wins (mods are added top first)
 *   dump      --mods-dump: every frame drawn, once, as DIR/sprites/<kind>/
 *             <frame>_<x>_<y>.png, unflipped, in the colours it was drawn in
 */
#include "mods/app_mods.h"
#include "mods/ducktales/dt_mods_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t kind, frame;
    bool flip;
    AppModsSprite art;
} Entry;

static Entry *s_entries;
static int s_count;
static char s_dump_dir[1024];
static uint8_t s_dumped[256 * 256 / 8];

static int hex2(const char *s)
{
    int v = 0;
    for (int i = 0; i < 2; i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9'   ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                       : -1;
        if (d < 0) {
            return -1;
        }
        v = v * 16 + d;
    }
    return v;
}

static const char *number(const char *s, int *out)
{
    bool minus = *s == '-';
    const char *c = s + minus;
    int v = 0, digits = 0;
    while (*c >= '0' && *c <= '9' && digits < 4) {
        v = v * 10 + (*c++ - '0');
        digits++;
    }
    if (!digits) {
        return NULL;
    }
    *out = minus ? -v : v;
    return c;
}

bool dt_mods_sprite_key(const char *name, uint8_t *kind, uint8_t *frame, bool *flip, int *ax, int *ay)
{
    if (_strnicmp(name, "sprites/", 8)) {
        return false;
    }
    const char *s = name + 8;
    int k = hex2(s), f = s[2] == '/' ? hex2(s + 3) : -1;
    if (k < 0 || f < 0 || s[5] != '_') {
        return false;
    }
    const char *c = number(s + 6, ax);
    if (!c || *c != '_' || !(c = number(c + 1, ay))) {
        return false;
    }
    *flip = !_strnicmp(c, "_flip", 5);
    c += *flip ? 5 : 0;
    if (_stricmp(c, ".png")) {
        return false;
    }
    *kind = (uint8_t)k;
    *frame = (uint8_t)f;
    return true;
}

static Entry *find(uint8_t kind, uint8_t frame, bool flip)
{
    for (int i = 0; i < s_count; i++) {
        if (s_entries[i].kind == kind && s_entries[i].frame == frame && s_entries[i].flip == flip) {
            return &s_entries[i];
        }
    }
    return NULL;
}

bool dt_mods_sprite_insert(uint8_t kind, uint8_t frame, bool flip, const AppModsSprite *art)
{
    Entry *grown = find(kind, frame, flip) ? NULL : realloc(s_entries, (size_t)(s_count + 1) * sizeof *grown);
    if (!grown) {
        free((void *)art->argb);
        return false;
    }
    s_entries = grown;
    s_entries[s_count++] = (Entry){ kind, frame, flip, *art };
    return true;
}

int dt_mods_sprite_count(void)
{
    return s_count;
}

const AppModsSprite *app_mods_sprite(uint8_t kind, uint8_t frame, bool flip, bool *mirror)
{
    const Entry *e = flip ? find(kind, frame, true) : NULL;
    *mirror = false;
    if (!e && (e = find(kind, frame, false)) != NULL) {
        *mirror = flip;
    }
    return e ? &e->art : NULL;
}

bool dt_mods_sprites_dump_to(const char *dir)
{
    snprintf(s_dump_dir, sizeof s_dump_dir, "%s", dir);
    return true;
}

bool app_mods_sprites_active(void)
{
    return s_count > 0 || s_dump_dir[0];
}

bool app_mods_sprite_wanted(uint8_t kind, uint8_t frame)
{
    int bit = kind << 8 | frame;
    return s_dump_dir[0] && !(s_dumped[bit >> 3] & (1 << (bit & 7)));
}

void app_mods_sprite_dump(uint8_t kind, uint8_t frame, const uint32_t *argb, int w, int h, int ax, int ay)
{
    if (!app_mods_sprite_wanted(kind, frame)) {
        return;
    }
    int bit = kind << 8 | frame;
    s_dumped[bit >> 3] |= (uint8_t)(1 << (bit & 7));
    char dir[1100], path[1200];
    snprintf(dir, sizeof dir, "%s\\sprites\\%02x", s_dump_dir, kind);
    snprintf(path, sizeof path, "%s\\%02x_%d_%d.png", dir, frame, ax, ay);
    if (!fm_make_dir(dir) || !fm_png_write(path, argb, w, h)) {
        fm_log("--mods-dump: cannot write %s", path);
    }
}
