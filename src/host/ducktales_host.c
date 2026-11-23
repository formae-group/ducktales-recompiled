/*
 * ducktales_host.c - the port's systems layer on the NESRecomp cycle host
 * (cyc_host_extras.h). Both the window and the headless host use it; NESRecomp
 * links exactly one cyc_host_extras(), so everything the port adds to the
 * host goes through this file.
 *
 *   enhancements      game/enhancements.h: config.ini [Game] keys (window)
 *                     and one command-line option each (both); every one is
 *                     off by default
 *   sprites           host/ducktales_sprites.h: the presented picture without
 *                     sprite flicker or dropout ([Game] NoSpriteFlicker,
 *                     --no-sprite-flicker); on by default, presentation only
 *   No slowdown       config.ini [Game] NoSlowdown (window, default on):
 *                     the cycle host's extra scanlines (ducktales_host.h)
 *   Application UI    window builds (DUCKTALES_WINDOW): the in-game menu,
 *                     live settings and bindings (ui/app_ui.h) through the
 *                     event, input, audio_mix and frame_begin callbacks
 *   presentation      window builds: the picture the window shows (present,
 *                     ui/app_present.h) and its [Game] keys; present_rate with
 *                     present_from_snapshot and no present_phase turns on the
 *                     window's steady hold while VSync is off, with no
 *                     in-between pictures (docs/presentation.md)
 *   mods              mods/app_mods.h: config.ini [Mods] (window), the mods
 *                     folders (--mods-dir; the window adds the data folder's),
 *                     loaded at the first frame; their sounds follow the
 *                     driver at each frame's end (the APU's audio-only mute)
 *                     and are mixed in audio_mix; their tiles and palettes
 *                     are painted by the sprite compositor
 *   diagnostics       DUCKTALES_DEV_DIAGNOSTICS builds only
 *                     (host/ducktales_diagnostics.h)
 *   native check      headless builds only: the native renderer against
 *                     the PPU every frame (native/native_check.h)
 *
 * A command-line value applies to that run only; config.ini keeps what the
 * player saved (ducktales_host_saved_value).
 */
#include "cyc_core.h"
#include "cyc_host_extras.h"
#include "cyc_mod.h"

#include "game/enhancements.h"
#include "host/ducktales_diagnostics.h"
#include "host/ducktales_host.h"
#include "host/ducktales_sprites.h"
#include "host/ducktales_ramhash.h"
#include "mods/app_mods.h"
#include "native/native_check.h"
#if DUCKTALES_WINDOW
#include "ui/app_present.h"
#include "ui/app_ui.h"
#endif

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

bool ducktales_host_no_slowdown(void) { return s_no_slowdown; }

void ducktales_host_set_no_slowdown(bool on)
{
    s_no_slowdown = on;
    if (!cyc_set_extra_scanlines(on ? DT_NO_SLOWDOWN_LINES : 0))
        fprintf(stderr, "[ducktales] No slowdown: the cycle host refused %u extra scanlines\n", DT_NO_SLOWDOWN_LINES);
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
        static const char *ON[] = { "on", "1", "true", "yes" }, *OFF[] = { "off", "0", "false", "no" };
        for (int i = 0; i < 4; i++) {
            if (!strcmp(value, ON[i])) s_no_slowdown = true;
            if (!strcmp(value, OFF[i])) s_no_slowdown = false;
        }
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
    app_mods_write_settings(f);
}

static void load_setting(void *ctx, const char *key, const char *value)
{
    (void)ctx;
    ducktales_host_load_game_setting(key, value);
}

static void save_settings(void *ctx, FILE *f)
{
    (void)ctx;
    ducktales_host_write_game_settings(f);
#if DUCKTALES_WINDOW
    app_ui_host_saved();
#endif
}

/* One option per enhancement, the sprites one, then the developer ones. */
static const CycHostOption DIAG_OPTIONS[] = { DT_RAMHASH_OPTIONS DT_DIAG_OPTIONS NV_CHECK_OPTIONS { NULL, false, NULL } };
static const CycHostOption MOD_OPTIONS[] = {
    { "--mods", true, "on|off: load the mods (default: config.ini [Mods] Enabled in the window, on headless)" },
    { "--mods-dir", true, "DIR: also read mods from DIR (docs/mods.md)" },
    { "--mods-order", true, "ID:on|off,...: the mods' order and switches for this run, top first" },
    { "--mods-dump", true, "DIR: write every tile drawn, once, as DIR/<key>.png (templates for tile mods)" },
};
static CycHostOption s_options[DT_ENH_COUNT + 1 + sizeof MOD_OPTIONS / sizeof MOD_OPTIONS[0] +
                                sizeof DIAG_OPTIONS / sizeof DIAG_OPTIONS[0]];
static char s_option_help[DT_ENH_COUNT][160];

static size_t options_init(void)
{
    size_t n = 0;
    for (int e = 0; e < DT_ENH_COUNT; e++) {
        const DtEnhInfo *in = dt_enh_info((DtEnh)e);
        const char *range = in->kind == DT_ENH_KIND_TOGGLE ? "on|off" : in->kind == DT_ENH_KIND_COUNT_OR_OFF ? "0-9|off" : "25-400";
        snprintf(s_option_help[e], sizeof s_option_help[e], "%s: %s (default %s)", range, in->help,
                 in->kind == DT_ENH_KIND_PERCENT ? "100" : "off");
        s_options[n++] = (CycHostOption){ in->option, true, s_option_help[e] };
    }
    s_options[n++] = (CycHostOption){ DT_SPRITES_OPTION, true,
                                      "on|off: present every sprite, without flicker or dropout (default on)" };
    for (size_t i = 0; i < sizeof MOD_OPTIONS / sizeof MOD_OPTIONS[0]; i++) s_options[n++] = MOD_OPTIONS[i];
    for (size_t i = 0; DIAG_OPTIONS[i].name; i++) s_options[n++] = DIAG_OPTIONS[i];
    return n;
}

static bool option(void *ctx, const char *name, const char *value)
{
    (void)ctx;
    for (int e = 0; e < DT_ENH_COUNT; e++) {
        if (strcmp(name, dt_enh_info((DtEnh)e)->option)) continue;
        int v;
        return dt_enh_parse((DtEnh)e, value, &v) && dt_enh_set((DtEnh)e, v);
    }
    if (!strcmp(name, DT_SPRITES_OPTION)) {
        bool on;
        if (!dt_sprites_parse(value, &on)) return false;
        dt_sprites_set(on);
        return true;
    }
    if (!strcmp(name, "--mods")) {
        bool on;
        if (!dt_sprites_parse(value, &on)) return false;
        app_mods_set_enabled(on);
        return true;
    }
    if (!strcmp(name, "--mods-dir")) {
        app_mods_add_folder(value, false);
        return true;
    }
    if (!strcmp(name, "--mods-order")) return app_mods_setting("Order", value);
    if (!strcmp(name, "--mods-dump")) return app_mods_dump_dir(value);
    if (!strcmp(name, "--ram-hash-out")) return dt_ramhash_open(value);
    if (!strcmp(name, "--lag-log")) return dt_laglog_open(value);
    return dt_diag_option(name, value) || nv_check_option(name, value);
}

static void power_on(void *ctx)
{
    (void)ctx;
    dt_sprites_forget();
    app_mods_reset();
    cyc_mod_set_apu_audio_mute(0);
    if (s_no_slowdown) {
        ducktales_host_set_no_slowdown(true);
        printf("[ducktales] No slowdown: %u extra scanlines per frame\n", cyc_extra_scanlines());
        fflush(stdout);
    }
    dt_ramhash_power_on();
    dt_diag_power_on();
}

/* The mods load once, before the first frame (after the options and the
 * window's settings): the running PRG decides which are refused. */
static void start_mods(void)
{
    static bool started;
    if (started) return;
    started = true;
    size_t size = cyc_mod_copy_cart_payload(NULL, 0);
    uint8_t *prg = size ? malloc(size) : NULL;
    if (prg && cyc_mod_copy_cart_payload(prg, size) != size) size = 0;
    app_mods_start(prg, prg ? size : 0);
    free(prg);
    dt_sprites_mods_changed();
}

static void frame_begin(void *ctx)
{
    (void)ctx;
    start_mods();
#if DUCKTALES_WINDOW
    app_ui_frame_begin();
#endif
    dt_diag_frame_begin();
}

static void frame_end(void *ctx)
{
    (void)ctx;
    dt_sprites_frame_end();
    if (app_mods_audio_active()) {
        app_mods_audio_frame_end(cyc_cpu_ram());
        cyc_mod_set_apu_audio_mute(app_mods_audio_mute());
    }
    dt_ramhash_frame_end();
    dt_diag_frame_end();
    nv_check_frame_end();
}

static void state_loaded(void *ctx)
{
    (void)ctx;
    dt_sprites_forget();
    app_mods_reset();
    cyc_mod_set_apu_audio_mute(0);
}

static void tcp_setup(void *ctx)
{
    (void)ctx;
    dt_diag_tcp_setup();
}

/* The host mixes at 48 kHz (its default rate, CycHostExtras.audio_rate 0). */
static void audio_mix(void *ctx, int16_t *samples, size_t count)
{
    (void)ctx;
    app_mods_audio_mix(samples, count, 48000);
#if DUCKTALES_WINDOW
    app_ui_audio(samples, count);
#endif
}

#if DUCKTALES_WINDOW
static void input(void *ctx, uint8_t buttons[2])
{
    (void)ctx;
    app_ui_input(buttons);
}

static void event(void *ctx, const void *ev, int player)
{
    (void)ctx;
    (void)player;
    app_ui_event(ev);
}


static const uint32_t *present(void *ctx, int *width, int *height)
{
    (void)ctx;
    return app_ui_present(width, height);
}
#endif

static CycHostExtras s_extras = {
    .load_setting = load_setting,
    .save_settings = save_settings,
    .power_on = power_on,
    .frame_begin = frame_begin,
    .frame_end = frame_end,
    .state_loaded = state_loaded,
    .option = option,
    .tcp_setup = tcp_setup,
    .audio_mix = audio_mix,
#if DUCKTALES_WINDOW
    .input = input,
    .event = event,
    .present = present,
    .present_rate = 60,
    .present_from_snapshot = true,
#endif
};

const CycHostExtras *cyc_host_extras(void)
{
    if (!s_extras.options) {
        s_extras.option_count = options_init();
        s_extras.options = s_options;
    }
    return &s_extras;
}
