/*
 * native_voices.h - the synth's two extra voices (docs/audio.md, "Extra
 * voices"), for the native player's [Audio] MusicUnderEffects and
 * OverlapEffects:
 *
 *   music   on each channel an effect owns, the music's own part, computed
 *           from the driver's channel record with the driver's output steps
 *           ($82FF volume, fade and envelope, $83A2 tremolo, duty, $8435
 *           vibrato, period); vibrato and tremolo, which the effect holds in
 *           the record, run on a copy of the music's own
 *   effect  a request the driver dropped, or the effect a newer request cut
 *           off, played by a port of the driver's effect player ($856B)
 *           from the sound data in the ROM
 *
 * Both read the driver's RAM after each frame and compare it with the frame
 * before; the requests are the ring entries the NMI took ($C7 moved). They
 * write nothing: what they make is APU register writes for channels of
 * their own, which native_audio.c plays beside the console's four.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NATIVE_VOICE_MAX_WRITES 64

typedef struct {
    uint16_t addr; /* $4000-$400F */
    uint8_t  value;
} NativeVoiceWrite;

typedef struct {
    uint8_t live[2];   /* channels each voice plays (bit 0 pulse 1 .. bit 3 noise) */
    uint32_t count[2];
    NativeVoiceWrite writes[2][NATIVE_VOICE_MAX_WRITES]; /* [0] music, [1] effect */
} NativeVoiceFrame;

/* Forgets everything; the next frame only takes its snapshot. */
void native_voices_reset(void);

/* After machine frame number frame: ram is the CPU RAM at its end, prg the
 * PRG image. A voice that is off plays nothing. A frame that does not follow
 * the last one (power-on, a loaded state) is a snapshot only. */
void native_voices_frame(uint32_t frame, const uint8_t *ram, const uint8_t *prg, size_t prg_size, bool music,
                         bool effects, NativeVoiceFrame *out);
