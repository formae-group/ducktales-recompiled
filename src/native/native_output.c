/*
 * native_output.c - the native player's sound output (native_output.h).
 *
 * As the window has queued its sound so far (SDL_QueueAudio): each frame is
 * clamped to 16 bits, dropped when 100 ms are already queued, stretched by
 * linear interpolation (to at most twice its length) when the queue is below
 * the device's buffer plus one frame, and scaled by the volume; nothing is
 * queued while held, and the device plays silence when the queue is empty.
 */
#include "native/native_output.h"

#include <math.h>
#include <string.h>

#define RING 32768

typedef struct {
    double out, game;
} Slot;

static struct {
    int rate, device;
    int volume;
    unsigned hold;
    Slot ring[RING];
    size_t head, count;
    bool playing;
    NativeOutputStats st;
} s;

void native_output_reset(int rate, int device_samples)
{
    memset(&s, 0, sizeof s);
    s.rate = rate;
    s.device = device_samples;
    s.volume = 100;
}

void native_output_volume(int percent)
{
    s.volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
}

void native_output_hold(unsigned reasons, bool on)
{
    s.hold = on ? (s.hold | reasons) : (s.hold & ~reasons);
}

void native_output_flush(void)
{
    s.count = 0;
    s.st.flushes++;
}

size_t native_output_queued(void)
{
    return s.count;
}

bool native_output_quiet(void)
{
    return true;
}

const NativeOutputStats *native_output_stats(void)
{
    return &s.st;
}

static void stretch(double *pcm, size_t n, size_t m)
{
    if (n < 2 || m <= n) {
        return;
    }
    for (size_t i = m; i-- > 0;) {
        double x = (double)i * (double)(n - 1) / (double)(m - 1);
        size_t j = (size_t)x;
        double frac = x - (double)j, a = pcm[j], b = pcm[j + 1 < n ? j + 1 : j];
        pcm[i] = a + (b - a) * frac;
    }
}

void native_output_push(const double *mix, size_t n)
{
    static double out[4096], game[4096];
    if (n > 2048) {
        n = 2048;
    }
    for (size_t i = 0; i < n; i++) {
        double a = fabs(mix[i]);
        s.st.peak_in = a > s.st.peak_in ? a : s.st.peak_in;
        s.st.over += a >= 32767;
    }
    if (s.count > s.st.max_queued) {
        s.st.max_queued = s.count;
    }
    if (!n || s.hold) {
        return;
    }
    if (s.count >= (size_t)s.rate / 10) {
        s.st.dropped++;
        return;
    }
    for (size_t i = 0; i < n; i++) {
        double v = mix[i] > 32767 ? 32767 : mix[i] < -32768 ? -32768 : mix[i];
        out[i] = (int16_t)v;
        game[i] = mix[i];
    }
    size_t m = n, low = (size_t)s.device + n;
    if (s.count < low) {
        m = n + (low - s.count);
        m = m > 2 * n ? 2 * n : m;
        stretch(out, n, m);
        stretch(game, n, m);
        for (size_t i = 0; i < m; i++) {
            out[i] = (int16_t)out[i];
        }
        s.st.stretched++;
    }
    for (size_t i = 0; i < m && s.count < RING; i++) {
        if (s.volume < 100) {
            out[i] = (int16_t)((int)out[i] * s.volume / 100);
        }
        Slot *slot = &s.ring[(s.head + s.count++) % RING];
        slot->out = out[i];
        slot->game = game[i];
    }
    s.st.pushed += m;
}

void native_output_pull(int16_t *out, int16_t *game, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (!s.count) {
            if (s.playing) {
                s.st.underruns++;
            }
            s.playing = false;
            s.st.starved++;
            out[i] = 0;
            if (game) {
                game[i] = INT16_MIN;
            }
            continue;
        }
        Slot *slot = &s.ring[s.head];
        s.head = (s.head + 1) % RING;
        s.count--;
        s.playing = true;
        s.st.played++;
        out[i] = (int16_t)slot->out;
        if (game) {
            double g = slot->game > 32767 ? 32767 : slot->game < -32767 ? -32767 : slot->game;
            game[i] = (int16_t)g;
        }
        double a = fabs(slot->out);
        s.st.peak_out = a > s.st.peak_out ? a : s.st.peak_out;
    }
}
