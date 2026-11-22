/*
 * native_audio.h - the native runtime's synth: the frame's APU register
 * writes (NativeFrame.apu_writes, native_state.h) in, the frame's samples
 * out (docs/audio.md, "Native synth").
 *
 * It keeps the four tone channels, the DMC's output level and the frame
 * sequencer as the program set them and renders each sample as the average
 * of the channels' outputs over that sample's span of CPU time, computed in
 * closed form between register writes and sequencer clocks. Nothing is
 * stepped per CPU cycle. The console's nonlinear mixer and the NES output
 * stage (90 Hz and 440 Hz high-pass, 14 kHz low-pass) follow, as in the
 * cycle host's APU. Nothing here is read back by the game.
 *
 * Owns no setting; the sample rate is the caller's.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "native/native_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Enough for one frame at up to 96 kHz. */
#define NATIVE_AUDIO_MAX_FRAME_SAMPLES 2048

/* Power-on: every register 0, silence, samples at sample_rate Hz. */
void native_audio_reset(int sample_rate);

/* Plays one machine frame (native_last_frame()): its apu_writes[] at their
 * cycle offsets over its cycles. A frame the no-slowdown mode stretched
 * (held_cycles > 0) is played in cycles - held_cycles, its offsets scaled to
 * match, since it is still presented for one 60 Hz period. Writes the frame's
 * signed 16-bit mono samples to out (at most max) and returns how many:
 * sample_rate / 60.0988 per frame on average. */
size_t native_audio_frame(const NativeFrame *frame, int16_t *out, size_t max);

#ifdef __cplusplus
}
#endif
