#include "host/ducktales_diagnostics.h"

#include "cyc_core.h"
#include "cyc_render.h"
#include "cyc_hooks.h"
#include "cyc_ring.h"
#if defined(CYC_WITH_SDL)
#include "cyc_tcp.h"
#endif

#include "game/ducktales_jump.h"
#include "game/enhancements.h"
#include "game/ducktales_state.h"
#include "host/ducktales_sprites.h"

#include "ducktales/events.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JUMP_EVENTS 32

static FILE *s_log;
static unsigned long s_every = 1;
static struct { unsigned long frame; int percent; } s_jump_at[MAX_JUMP_EVENTS];
static int s_jump_at_n;
static struct { unsigned long frame; DtEnh e; int value; } s_enh_at[MAX_JUMP_EVENTS];
static int s_enh_at_n;
static unsigned long s_reset_at[MAX_JUMP_EVENTS];
static int s_reset_at_n;
static FILE *s_sprite_log;
static FILE *s_events_log;

static EventResult log_event(EventId id, void *user)
{
    (void)user;
    fprintf(s_events_log, "frame=%lu event=%s\n", (unsigned long)cyc_ring_frame, Events_Name(id));
    return EVENT_CONTINUE;
}

static void report(void)
{
    CycHookStats st;
    cyc_hooks_stats(-1, &st);
    FILE *f = s_log ? s_log : stdout;
    fprintf(f, "dt-diag: jump_scale=%d jumps_rescaled=%llu hook_fired=%llu\n", dt_jump_scale(),
            (unsigned long long)dt_jump_rescaled(), (unsigned long long)st.fired);
    for (int e = 0; e < DT_ENH_COUNT; e++) {
        char v[16];
        fprintf(f, "dt-diag: %s=%s applied=%llu\n", dt_enh_info((DtEnh)e)->key,
                dt_enh_format((DtEnh)e, dt_enh_get((DtEnh)e), v, sizeof v), (unsigned long long)dt_enh_applied((DtEnh)e));
    }
    DtSpritesStats sp;
    dt_sprites_stats(&sp);
    fprintf(f, "dt-diag: %s=%s composed=%llu reused=%llu fallback=%llu declined=%llu hud_restored=%llu leftover=%llu lines_over_8=%llu beyond_63=%llu\n",
            DT_SPRITES_KEY, dt_sprites_get() ? "on" : "off", (unsigned long long)sp.composed,
            (unsigned long long)sp.reused, (unsigned long long)sp.fallback, (unsigned long long)sp.declined, (unsigned long long)sp.hud_restored,
            (unsigned long long)sp.leftover, (unsigned long long)sp.lines_over_8, (unsigned long long)sp.beyond_63);
    for (int e = 0; e < EVENT_COUNT; e++)
        fprintf(f, "dt-diag: event %s raised=%llu\n", Events_Name((EventId)e), (unsigned long long)Events_Raised((EventId)e));
    fflush(f);
    if (s_events_log && s_events_log != stdout) fclose(s_events_log);
    s_events_log = NULL;
    if (s_log && s_log != stdout) fclose(s_log);
    dt_sprites_set_log(NULL);
    if (s_sprite_log && s_sprite_log != stdout) fclose(s_sprite_log);
    s_sprite_log = NULL;
    s_log = NULL;
}

bool dt_diag_option(const char *name, const char *value)
{
    if (!strcmp(name, "--dt-state-log")) {
        s_log = !strcmp(value, "-") ? stdout : fopen(value, "w");
        return s_log != NULL;
    }
    if (!strcmp(name, "--dt-sprite-log")) {
        s_sprite_log = !strcmp(value, "-") ? stdout : fopen(value, "w");
        dt_sprites_set_log(s_sprite_log);
        return s_sprite_log != NULL;
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
    if (!strcmp(name, "--dt-enh-at")) {
        unsigned long frame;
        char key[32], text[16];
        if (s_enh_at_n >= MAX_JUMP_EVENTS || sscanf(value, "%lu:%31[^=]=%15s", &frame, key, text) != 3) return false;
        for (int e = 0; e < DT_ENH_COUNT; e++) {
            int v;
            if (strcmp(key, dt_enh_info((DtEnh)e)->key) || !dt_enh_parse((DtEnh)e, text, &v)) continue;
            s_enh_at[s_enh_at_n].frame = frame;
            s_enh_at[s_enh_at_n].e = (DtEnh)e;
            s_enh_at[s_enh_at_n].value = v;
            s_enh_at_n++;
            return true;
        }
        return false;
    }
    if (!strcmp(name, "--dt-events-log")) {
        s_events_log = !strcmp(value, "-") ? stdout : fopen(value, "w");
        if (!s_events_log) return false;
        for (int e = 0; e < EVENT_COUNT; e++) Events_On((EventId)e, log_event, NULL);
        return true;
    }
    if (!strcmp(name, "--dt-reset-at")) {
        if (s_reset_at_n >= MAX_JUMP_EVENTS) return false;
        s_reset_at[s_reset_at_n++] = strtoul(value, NULL, 10);
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
    for (int i = 0; i < s_enh_at_n; i++)
        if (s_enh_at[i].frame == cyc_ring_frame) dt_enh_set(s_enh_at[i].e, s_enh_at[i].value);
    for (int i = 0; i < s_reset_at_n; i++)
        if (s_reset_at[i] == cyc_ring_frame) dt_soft_reset();
}

void dt_diag_frame_end(void)
{
    if (s_sprite_log) {
        int w, h;
        cyc_render_present(&w, &h);
    }
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
