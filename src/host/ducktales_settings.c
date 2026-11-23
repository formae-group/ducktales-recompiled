/*
 * ducktales_settings.c - the port's settings, shared by every host (the cycle
 * host's ducktales_host.c and the native player's native/native_window.c):
 * what config.ini [Game] keeps and what a command-line option may override
 * for one run (ducktales_host.h).
 *
 *   enhancements      game/enhancements.h: one [Game] key and one option
 *                     each, all off by default
 *   sprites           host/ducktales_sprites.h ([Game] NoSpriteFlicker,
 *                     --no-sprite-flicker); on by default, presentation only
 *   No slowdown       [Game] NoSlowdown (window builds, default on), applied
 *                     by the runtime that runs the game
 *                     (ducktales_host_apply_no_slowdown)
 *   presentation      window builds: ui/app_present.h and the menu's own keys
 *                     (ui/app_ui.h)
 *   extra voices      [Audio] MusicUnderEffects and OverlapEffects (off by
 *                     default), played by the native player only
 *
 * A command-line value applies to that run only; config.ini keeps what the
 * player saved (ducktales_host_saved_value).
 */
#include "host/ducktales_host.h"

#include "host/ducktales_sprites.h"
#if DUCKTALES_WINDOW
#include "ui/app_present.h"
#include "ui/app_ui.h"
#endif

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int s_saved[DT_ENH_COUNT];
static bool s_saved_ready;
static bool s_saved_sprites = DT_SPRITES_DEFAULT;
#if DUCKTALES_WINDOW
static bool s_no_slowdown = true;
#else
static bool s_no_slowdown = false;
#endif

static bool s_music_under_effects, s_overlap_effects;

bool ducktales_host_no_slowdown(void) { return s_no_slowdown; }

bool ducktales_host_music_under_effects(void) { return s_music_under_effects; }
void ducktales_host_set_music_under_effects(bool on) { s_music_under_effects = on; }
bool ducktales_host_overlap_effects(void) { return s_overlap_effects; }
void ducktales_host_set_overlap_effects(bool on) { s_overlap_effects = on; }

static bool same_text(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++;
    return !*a && !*b;
}

static void parse_on_off(const char *value, bool *on)
{
    static const char *ON[] = { "on", "1", "true", "yes" }, *OFF[] = { "off", "0", "false", "no" };
    for (int i = 0; i < 4; i++) {
        if (same_text(value, ON[i])) *on = true;
        if (same_text(value, OFF[i])) *on = false;
    }
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

void ducktales_host_load_audio_settings(const char *path)
{
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return;
    char line[1024];
    bool audio = false;
    while (fgets(line, sizeof line, f)) {
        char *t = trim(line), *eq = strchr(t, '=');
        if (*t == '[') {
            char *end = strchr(t, ']');
            if (end) *end = 0;
            audio = same_text(t + 1, "Audio");
            continue;
        }
        if (!audio || !eq) continue;
        *eq = 0;
        char *key = trim(t), *value = trim(eq + 1);
        if (same_text(key, "MusicUnderEffects")) parse_on_off(value, &s_music_under_effects);
        else if (same_text(key, "OverlapEffects")) parse_on_off(value, &s_overlap_effects);
    }
    fclose(f);
}

void ducktales_host_set_no_slowdown(bool on)
{
    s_no_slowdown = on;
    ducktales_host_apply_no_slowdown(on);
}

static void saved_init(void)
{
    if (s_saved_ready) return;
    for (int e = 0; e < DT_ENH_COUNT; e++) s_saved[e] = dt_enh_info((DtEnh)e)->stock;
    s_saved_ready = true;
}

int ducktales_host_saved_value(DtEnh e)
{
    saved_init();
    return s_saved[e];
}

void ducktales_host_set_saved_value(DtEnh e, int value)
{
    saved_init();
    s_saved[e] = value;
}

bool ducktales_host_saved_sprites(void) { return s_saved_sprites; }

void ducktales_host_set_saved_sprites(bool on) { s_saved_sprites = on; }

void ducktales_host_load_game_setting(const char *key, const char *value)
{
    saved_init();
    if (!strcmp(key, DT_SPRITES_KEY)) {
        bool on;
        if (!dt_sprites_parse(value, &on)) {
            fprintf(stderr, "[ducktales] config.ini [Game] %s = %s: expected on or off; using %s\n", key, value,
                    DT_SPRITES_DEFAULT ? "on" : "off");
            on = DT_SPRITES_DEFAULT;
        }
        s_saved_sprites = on;
        dt_sprites_set(on);
        return;
    }
    if (!strcmp(key, "NoSlowdown")) {
        parse_on_off(value, &s_no_slowdown);
        return;
    }
    for (int e = 0; e < DT_ENH_COUNT; e++) {
        const DtEnhInfo *in = dt_enh_info((DtEnh)e);
        if (strcmp(key, in->key)) continue;
        int v;
        if (!dt_enh_parse((DtEnh)e, value, &v)) {
            char stock[16];
            fprintf(stderr, "[ducktales] config.ini [Game] %s = %s: expected %d-%d; using %s\n", key, value, in->min,
                    in->max, dt_enh_format((DtEnh)e, in->stock, stock, sizeof stock));
            v = in->stock;
        }
        s_saved[e] = v;
        dt_enh_set((DtEnh)e, v);
        return;
    }
#if DUCKTALES_WINDOW
    if (!app_present_load(key, value)) app_ui_load_game_setting(key, value);
#endif
}

void ducktales_host_write_game_settings(FILE *f)
{
    saved_init();
    for (int e = 0; e < DT_ENH_COUNT; e++) {
        char v[16];
        fprintf(f, "%s = %s\n", dt_enh_info((DtEnh)e)->key, dt_enh_format((DtEnh)e, s_saved[e], v, sizeof v));
    }
    fprintf(f, "%s = %s\n", DT_SPRITES_KEY, s_saved_sprites ? "on" : "off");
    fprintf(f, "NoSlowdown = %s\n", s_no_slowdown ? "on" : "off");
#if DUCKTALES_WINDOW
    app_present_write(f);
    app_ui_write_game_settings(f);
#endif
    fprintf(f, "[Audio]\nMusicUnderEffects = %s\nOverlapEffects = %s\n", s_music_under_effects ? "on" : "off",
            s_overlap_effects ? "on" : "off");
}

bool ducktales_host_game_option(const char *name, const char *value, bool *ok)
{
    for (int e = 0; e < DT_ENH_COUNT; e++) {
        if (strcmp(name, dt_enh_info((DtEnh)e)->option)) continue;
        int v;
        *ok = dt_enh_parse((DtEnh)e, value, &v) && dt_enh_set((DtEnh)e, v);
        return true;
    }
    if (!strcmp(name, DT_SPRITES_OPTION)) {
        bool on;
        *ok = dt_sprites_parse(value, &on);
        if (*ok) dt_sprites_set(on);
        return true;
    }
    return false;
}
