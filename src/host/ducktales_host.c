/*
 * ducktales_host.c - the port's systems layer on the NESRecomp cycle host
 * (cyc_host_extras.h). Both the window and the headless host use it; NESRecomp
 * links exactly one cyc_host_extras(), so everything the port adds to the
 * host goes through this file.
 *
 *   enhancements      game/enhancements.h: config.ini [Game] keys (window)
 *                     and one command-line option each (both); every one is
 *                     off by default
 *   No slowdown       config.ini [Game] NoSlowdown (window, default on):
 *                     the cycle host's extra scanlines (ducktales_host.h)
 *   Application UI    window builds (DUCKTALES_WINDOW): the in-game menu,
 *                     live settings and bindings (ui/app_ui.h) through the
 *                     event, input, audio_mix and frame_begin callbacks
 *   diagnostics       DUCKTALES_DEV_DIAGNOSTICS builds only
 *                     (host/ducktales_diagnostics.h)
 *
 * A command-line value applies to that run only; config.ini keeps what the
 * player saved (ducktales_host_saved_value).
 */
#include "cyc_core.h"
#include "cyc_host_extras.h"

#include "game/enhancements.h"
#include "host/ducktales_diagnostics.h"
#include "host/ducktales_host.h"
#include "host/ducktales_ramhash.h"
#if DUCKTALES_WINDOW
#include "ui/app_ui.h"
#endif

#include <stdlib.h>
#include <string.h>

static int s_saved[DT_ENH_COUNT];
static bool s_saved_ready;
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

void ducktales_host_load_game_setting(const char *key, const char *value)
{
    saved_init();
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
}

void ducktales_host_write_game_settings(FILE *f)
{
    saved_init();
    for (int e = 0; e < DT_ENH_COUNT; e++) {
        char v[16];
        fprintf(f, "%s = %s\n", dt_enh_info((DtEnh)e)->key, dt_enh_format((DtEnh)e, s_saved[e], v, sizeof v));
    }
    fprintf(f, "NoSlowdown = %s\n", s_no_slowdown ? "on" : "off");
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

/* One option per enhancement, then the developer ones. */
static const CycHostOption DIAG_OPTIONS[] = { DT_RAMHASH_OPTIONS DT_DIAG_OPTIONS { NULL, false, NULL } };
static CycHostOption s_options[DT_ENH_COUNT + sizeof DIAG_OPTIONS / sizeof DIAG_OPTIONS[0]];
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
    if (!strcmp(name, "--ram-hash-out")) return dt_ramhash_open(value);
    if (!strcmp(name, "--lag-log")) return dt_laglog_open(value);
    return dt_diag_option(name, value);
}

static void power_on(void *ctx)
{
    (void)ctx;
    if (s_no_slowdown) {
        ducktales_host_set_no_slowdown(true);
        printf("[ducktales] No slowdown: %u extra scanlines per frame\n", cyc_extra_scanlines());
        fflush(stdout);
    }
    dt_ramhash_power_on();
    dt_diag_power_on();
}

static void frame_begin(void *ctx)
{
    (void)ctx;
#if DUCKTALES_WINDOW
    app_ui_frame_begin();
#endif
    dt_diag_frame_begin();
}

static void frame_end(void *ctx)
{
    (void)ctx;
    dt_ramhash_frame_end();
    dt_diag_frame_end();
}

static void tcp_setup(void *ctx)
{
    (void)ctx;
    dt_diag_tcp_setup();
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

static void audio_mix(void *ctx, int16_t *samples, size_t count)
{
    (void)ctx;
    app_ui_audio(samples, count);
}
#endif

static CycHostExtras s_extras = {
    .load_setting = load_setting,
    .save_settings = save_settings,
    .power_on = power_on,
    .frame_begin = frame_begin,
    .frame_end = frame_end,
    .option = option,
    .tcp_setup = tcp_setup,
#if DUCKTALES_WINDOW
    .input = input,
    .event = event,
    .audio_mix = audio_mix,
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
