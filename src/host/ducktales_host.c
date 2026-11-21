/*
 * ducktales_host.c - the port's systems layer on the NESRecomp cycle host
 * (cyc_host_extras.h). Both the window and the headless host use it.
 *
 *   per-frame state   frame_end decodes the typed state (game/ducktales_state.h)
 *                     only when a consumer asks for it (developer diagnostics)
 *   jump experiment   config.ini [Game] JumpScale = <percent> (window),
 *                     --jump-scale <percent> (both); 100 = stock, the default
 *   diagnostics       DUCKTALES_DEV_DIAGNOSTICS builds only
 *                     (host/ducktales_diagnostics.h)
 */
#include "cyc_host_extras.h"

#include "game/ducktales_jump.h"
#include "host/ducktales_diagnostics.h"

#include <stdlib.h>
#include <string.h>

static int s_config_jump_scale = DT_JUMP_SCALE_STOCK;

static bool parse_percent(const char *value, int *out)
{
    char *end;
    long v = strtol(value, &end, 10);
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    if (end == value || *end || v < DT_JUMP_SCALE_MIN || v > DT_JUMP_SCALE_MAX) return false;
    *out = (int)v;
    return true;
}

static void load_setting(void *ctx, const char *key, const char *value)
{
    (void)ctx;
    if (strcmp(key, "JumpScale")) return;
    int percent;
    if (!parse_percent(value, &percent)) {
        fprintf(stderr, "[ducktales] config.ini [Game] JumpScale = %s: expected %d-%d; using %d\n", value,
                DT_JUMP_SCALE_MIN, DT_JUMP_SCALE_MAX, DT_JUMP_SCALE_STOCK);
        percent = DT_JUMP_SCALE_STOCK;
    }
    s_config_jump_scale = percent;
    dt_jump_set_scale(percent);
}

static void save_settings(void *ctx, FILE *f)
{
    (void)ctx;
    fprintf(f, "JumpScale = %d\n", s_config_jump_scale);
}

static const CycHostOption OPTIONS[] = {
    { "--jump-scale", true, "jump take-off velocity in percent (25-400; 100 = stock)" },
    DT_DIAG_OPTIONS
};

static bool option(void *ctx, const char *name, const char *value)
{
    (void)ctx;
    if (!strcmp(name, "--jump-scale")) {
        int percent;
        return parse_percent(value, &percent) && dt_jump_set_scale(percent);
    }
    return dt_diag_option(name, value);
}

static void power_on(void *ctx)
{
    (void)ctx;
    dt_diag_power_on();
}

static void frame_begin(void *ctx)
{
    (void)ctx;
    dt_diag_frame_begin();
}

static void frame_end(void *ctx)
{
    (void)ctx;
    dt_diag_frame_end();
}

static void tcp_setup(void *ctx)
{
    (void)ctx;
    dt_diag_tcp_setup();
}

static const CycHostExtras EXTRAS = {
    .load_setting = load_setting,
    .save_settings = save_settings,
    .power_on = power_on,
    .frame_begin = frame_begin,
    .frame_end = frame_end,
    .options = OPTIONS,
    .option_count = sizeof OPTIONS / sizeof OPTIONS[0],
    .option = option,
    .tcp_setup = tcp_setup,
};

const CycHostExtras *cyc_host_extras(void) { return &EXTRAS; }
