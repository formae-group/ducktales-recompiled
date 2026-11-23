/*
 * ducktales_ramhash.h - native-runtime verification output for the cycle host
 * (src/host/ducktales_ramhash.c): --ram-hash-out and --lag-log. Headless
 * builds only; the window build gets the empty inline versions.
 */
#pragma once
#include <stdbool.h>

#if DUCKTALES_WINDOW
#define DT_RAMHASH_OPTIONS

static inline bool dt_ramhash_open(const char *path)
{
    (void)path;
    return false;
}

static inline bool dt_laglog_open(const char *path)
{
    (void)path;
    return false;
}

static inline void dt_ramhash_power_on(void)
{
}

static inline void dt_ramhash_frame_end(void)
{
}
#else
#define DT_RAMHASH_OPTIONS                                                                           \
    { "--ram-hash-out", true, "per-frame CPU RAM/PPU memory hashes, cycles and registers to FILE" }, \
        { "--lag-log", true, "per-frame lag (game task still running at VBlank) to FILE" },
bool dt_ramhash_open(const char *path);
bool dt_laglog_open(const char *path);
void dt_ramhash_power_on(void);
void dt_ramhash_frame_end(void);
#endif
