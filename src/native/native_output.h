/*
 * native_output.h - the native player's sound output (docs/audio.md,
 * "Output stage"): each frame's mix (the synth and the mods' files, in
 * 16-bit units, unclamped) in, the audio device's 16-bit samples out.
 *
 * The frame loop pushes one frame at a time; the device pulls from a ring
 * between them. The window calls both under SDL_LockAudioDevice;
 * ducktales.exe --audio-out drives them from a simulated device clock
 * (native_window.c, tests/audio_output_test.py).
 *
 * Owns no setting: the volume and the holds are the window's.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Why the sound is held (silent, nothing queued): the menu paused the game,
 * fast-forward, sound off in the settings, the window in a move or resize,
 * the program closing. */
enum {
    NATIVE_OUTPUT_HOLD_MENU = 1,
    NATIVE_OUTPUT_HOLD_FAST = 2,
    NATIVE_OUTPUT_HOLD_OFF = 4,
    NATIVE_OUTPUT_HOLD_WINDOW = 8,
    NATIVE_OUTPUT_HOLD_QUIT = 16,
};

typedef struct {
    uint64_t pushed, played;    /* samples queued, samples the device played from the queue */
    uint64_t underruns;         /* times the queue ran dry while sound was playing */
    uint64_t starved;           /* device samples with nothing queued and no hold */
    uint64_t dropped;           /* frames dropped because the queue was full */
    uint64_t stretched, shrunk; /* frames resampled to refill or drain the queue */
    uint64_t flushes;           /* queue emptied (a loaded state) */
    uint64_t over;              /* mix samples at or beyond full scale */
    uint64_t limited;           /* samples the limiter turned down */
    double peak_in, peak_out;   /* largest |sample| of the mix and of the output */
    size_t max_queued;          /* largest queue seen at a push, samples */
} NativeOutputStats;

/* Empty queue, statistics cleared; the device takes device_samples a pull. */
void native_output_reset(int rate, int device_samples);
/* Master volume, 0-100. */
void native_output_volume(int percent);
/* Sets or clears hold reasons (NATIVE_OUTPUT_HOLD_*). */
void native_output_hold(unsigned reasons, bool on);
/* Drops what is queued (a loaded state: the queued sound is the old one's). */
void native_output_flush(void);
/* One frame's mix, n samples. game, when not NULL, is the same frame without
 * the mods' files (the synth's own sound): what native_output_pull reports
 * the output against; NULL for the mix itself. */
void native_output_push(const double *mix, const double *game, size_t n);
/* The device's next n samples. game, when not NULL, gets half the game sample
 * (native_output_push) each one was made from, after the queue's resampling
 * (halved so a mix up to +6 dBFS fits), or INT16_MIN where the device played
 * no queued sample: what tests/audio_output_test.py compares the output's
 * sample-to-sample steps with. */
void native_output_pull(int16_t *out, int16_t *game, size_t n);
/* Samples queued. */
size_t native_output_queued(void);
/* Nothing audible is left to play: the device may be closed. */
bool native_output_quiet(void);
const NativeOutputStats *native_output_stats(void);

#ifdef __cplusplus
}
#endif
