#include "host/ducktales_diagnostics.h"

#include "cyc_core.h"
#include "cyc_hooks.h"
#include "cyc_ring.h"
#if defined(CYC_WITH_SDL)
#include "cyc_tcp.h"
#endif

#include "game/ducktales_jump.h"
#include "game/ducktales_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JUMP_EVENTS 32

static FILE *s_log;
static unsigned long s_every = 1;
static struct { unsigned long frame; int percent; } s_jump_at[MAX_JUMP_EVENTS];
static int s_jump_at_n;

static void report(void)
{
    CycHookStats st;
    cyc_hooks_stats(-1, &st);
    FILE *f = s_log ? s_log : stdout;
    fprintf(f, "dt-diag: jump_scale=%d jumps_rescaled=%llu hook_fired=%llu\n", dt_jump_scale(),
            (unsigned long long)dt_jump_rescaled(), (unsigned long long)st.fired);
    fflush(f);
    if (s_log && s_log != stdout) fclose(s_log);
    s_log = NULL;
}

bool dt_diag_option(const char *name, const char *value)
{
    if (!strcmp(name, "--dt-state-log")) {
        s_log = !strcmp(value, "-") ? stdout : fopen(value, "w");
        return s_log != NULL;
    }
    if (!strcmp(name, "--dt-state-every")) {
        s_every = strtoul(value, NULL, 10);
        return s_every > 0;
    }
    if (!strcmp(name, "--dt-jump-at")) {
        unsigned long frame;
        int percent;
        if (s_jump_at_n >= MAX_JUMP_EVENTS || sscanf(value, "%lu:%d", &frame, &percent) != 2 ||
            percent < DT_JUMP_SCALE_MIN || percent > DT_JUMP_SCALE_MAX) return false;
        s_jump_at[s_jump_at_n].frame = frame;
        s_jump_at[s_jump_at_n].percent = percent;
        s_jump_at_n++;
        return true;
    }
    return false;
}

void dt_diag_power_on(void)
{
    static bool once;
    if (!once) { once = true; atexit(report); }
}

void dt_diag_frame_begin(void)
{
    for (int i = 0; i < s_jump_at_n; i++)
        if (s_jump_at[i].frame == cyc_ring_frame) dt_jump_set_scale(s_jump_at[i].percent);
}

void dt_diag_frame_end(void)
{
    if (!s_log) return;
    unsigned long frame = cyc_ring_frame - 1;
    if (frame % s_every) return;
    DtState s;
    char line[256];
    dt_state_decode(cyc_cpu_ram(), &s);
    dt_state_format(&s, line, sizeof line);
    fprintf(s_log, "frame=%lu %s jump=%d\n", frame, line, dt_jump_scale());
}

#if defined(CYC_WITH_SDL)
static void tcp_state(int id, const char *line)
{
    (void)line;
    DtState s;
    char fields[512];
    dt_state_decode(cyc_cpu_ram(), &s);
    snprintf(fields, sizeof fields,
             "\"frame\":%lu,\"difficulty\":\"%s\",\"money\":%lu,\"money_valid\":%s,\"hp\":%u,\"hp_max\":%u,"
             "\"lives\":%u,\"x\":%u,\"y\":%u,\"vy\":%d,\"held\":%u,\"new\":%u,\"jump_scale\":%d",
             (unsigned long)cyc_ring_frame, dt_difficulty_name(s.difficulty), (unsigned long)s.money,
             s.money_valid ? "true" : "false", s.hp, s.hp_max, s.lives, s.scrooge_x, s.scrooge_y, s.scrooge_vy,
             s.pad_held, s.pad_new, dt_jump_scale());
    cyc_tcp_ok(id, fields);
}

static void tcp_jump_scale(int id, const char *line)
{
    long value;
    if (cyc_tcp_long(line, "value", &value) && !dt_jump_set_scale((int)value)) {
        cyc_tcp_err(id, "value must be 25-400");
        return;
    }
    char fields[64];
    snprintf(fields, sizeof fields, "\"jump_scale\":%d", dt_jump_scale());
    cyc_tcp_ok(id, fields);
}
#endif

void dt_diag_tcp_setup(void)
{
#if defined(CYC_WITH_SDL)
    cyc_tcp_register("dt_state", "DuckTales typed state (difficulty, money, HP, lives, Scrooge, pad)", tcp_state);
    cyc_tcp_register("dt_jump_scale", "get or set the jump scale percent ({\"value\":N})", tcp_jump_scale);
#endif
}
