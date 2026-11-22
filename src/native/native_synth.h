/*
 * native_synth.h - the seam between the native runtime and a synth: one
 * machine frame's APU register writes in, that frame's sound out.
 *
 * The runtime records every $4000-$4013, $4015 and $4017 write with its CPU
 * cycle in the frame (NativeFrame.apu_writes, native_state.h). A synth plays
 * them at those offsets, 1789773 Hz; a frame the no-slowdown mode stretched
 * (cycles above 29781) still lasts one 60.0988 Hz period, so its offsets are
 * scaled by 29781 / cycles. Nothing here is read back by the game.
 *
 * native_synth.c is a placeholder that plays silence until the native synth
 * (docs/audio.md) replaces it behind the same three calls.
 */
#pragma once
#include "native/native_state.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Power-on state at this output rate (Hz). */
void native_synth_reset(int sample_rate);
/* The frame's sound as mono signed 16-bit samples at the rate given to reset:
 * rate / 60.0988 of them, the fraction carried to the next frame. Returns how
 * many were written (at most cap). */
size_t native_synth_frame(const NativeRegWrite *apu_writes, uint32_t count, uint32_t cycles, int16_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
