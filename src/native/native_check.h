/*
 * native_check.h - the native renderer (native/native_render.h) checked
 * against the PPU on the cycle host, every frame. Headless builds only
 * (DUCKTALES_NATIVE_CHECK); other builds get the empty versions below.
 *
 *   --native-render-check FILE   fill NvVideo from the PPU at each frame's
 *                                end, draw it, compare it with the machine's
 *                                picture; one line per frame that differs and
 *                                a summary at exit to FILE ("-" = stdout)
 *   --native-render-dump DIR     the first differing frames as PPM: ours,
 *                                the machine's, and the differing pixels
 */
#pragma once
#include <stdbool.h>

#if DUCKTALES_NATIVE_CHECK
#define NV_CHECK_OPTIONS \
    { "--native-render-check", true, "check: native renderer vs the PPU every frame, report to FILE (- = stdout)" }, \
    { "--native-render-dump", true, "check: first differing frames as PPM into DIR" },
bool nv_check_option(const char *name, const char *value);
void nv_check_frame_end(void);
#else
#define NV_CHECK_OPTIONS
static inline bool nv_check_option(const char *name, const char *value) { (void)name; (void)value; return false; }
static inline void nv_check_frame_end(void) {}
#endif
