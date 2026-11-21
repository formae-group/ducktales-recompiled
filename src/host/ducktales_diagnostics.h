/*
 * ducktales_diagnostics.h - developer diagnostics, compiled only with the
 * CMake option DUCKTALES_DEV_DIAGNOSTICS (default OFF). Distribution builds
 * get the empty inline versions below, so the host carries no trace of them.
 *
 *   --dt-state-log FILE   one line of typed state per frame ("-" = stdout)
 *   --dt-state-every N    only every Nth frame (default 1)
 *   --dt-jump-at F:PCT    set the jump scale before frame F (repeatable), to
 *                         switch the experiment on and off inside one run
 *   --dt-enh-at F:KEY=V   set an enhancement (game/enhancements.h config key)
 *                         before frame F (repeatable)
 *   --dt-reset-at F       press the console's reset before frame F (repeatable)
 *   TCP (window --tcp PORT): dt_state, dt_jump_scale [value]
 */
#pragma once
#include <stdbool.h>

#if DUCKTALES_DEV_DIAGNOSTICS
#define DT_DIAG_OPTIONS \
    { "--dt-state-log", true, "dev: typed game state per frame to FILE (- = stdout)" }, \
    { "--dt-state-every", true, "dev: log every Nth frame" }, \
    { "--dt-jump-at", true, "dev: FRAME:PERCENT jump scale change (repeatable)" }, \
    { "--dt-enh-at", true, "dev: FRAME:KEY=VALUE enhancement change (repeatable)" }, \
    { "--dt-reset-at", true, "dev: FRAME console reset (repeatable)" },
bool dt_diag_option(const char *name, const char *value);
void dt_diag_power_on(void);
void dt_diag_frame_begin(void);
void dt_diag_frame_end(void);
void dt_diag_tcp_setup(void);
#else
#define DT_DIAG_OPTIONS
static inline bool dt_diag_option(const char *name, const char *value) { (void)name; (void)value; return false; }
static inline void dt_diag_power_on(void) {}
static inline void dt_diag_frame_begin(void) {}
static inline void dt_diag_frame_end(void) {}
static inline void dt_diag_tcp_setup(void) {}
#endif
