/*
 * native_synth.c - the placeholder behind native_synth.h: silence of the
 * right length, so the window's audio path, its volume and its pacing run
 * as they will with the native synth plugged in.
 */
#include "native/native_synth.h"

#include <string.h>

#define FRAME_HZ 60.0988

static int s_rate;
static double s_carry;

void native_synth_reset(int sample_rate)
{
    s_rate = sample_rate;
    s_carry = 0;
}

size_t native_synth_frame(const NativeRegWrite *apu_writes, uint32_t count, uint32_t cycles, int16_t *out, size_t cap)
{
    (void)apu_writes;
    (void)count;
    (void)cycles;
    s_carry += s_rate / FRAME_HZ;
    size_t n = (size_t)s_carry;
    s_carry -= (double)n;
    if (n > cap) n = cap;
    memset(out, 0, n * sizeof *out);
    return n;
}
