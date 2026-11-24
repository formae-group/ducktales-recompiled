/*
 * native_output.c - the native player's sound output (native_output.h,
 * docs/AUDIO.md "Output stage").
 *
 * Producer (one push per frame):
 *   volume    the master volume, ramped to a new value over 10 ms
 *   limiter   1.3 ms look-ahead peak limiter at -1 dBFS: the gain of each
 *             sample is the mean over the look-ahead of the running minimum
 *             of the gain its neighbours need, recovering over 50 ms, so it
 *             never lets a sample through above -1 dBFS and never steps;
 *             below -1 dBFS it is exactly 1 and the samples pass unchanged
 *   pacing    the queue is kept near two device buffers and a frame: while
 *             its average (over about 32 frames) is more than half a frame
 *             off that, each frame is resampled (linear interpolation) by at
 *             most 0.5%, 2% when under half a device buffer is left, so the
 *             queue follows the frame clock and the device's clock without a
 *             gap, a jump or an audible change of pitch
 *   holds     while held nothing is queued; after a hold has faded out (or
 *             a flush) the queue is dropped, and so are the limiter's
 *             look-ahead samples from before it, so the sound picks up from
 *             the frames after the hold with no seam between old and new
 *
 * Consumer (the device):
 *   fades     playing starts with a 5 ms fade-in once two device buffers and
 *             a frame are queued, from the newest of them (the frames that
 *             catch up after a stall would only add delay); a hold or a
 *             flush fades out over 5 ms of what is queued, then plays
 *             silence
 *   underrun  if the queue runs dry while playing, the last sample decays
 *             to silence (3 ms time constant) instead of stepping to 0, and
 *             the sound comes back with the fade-in
 */
#include "native/native_output.h"

#include <math.h>
#include <string.h>

#define RING    32768
#define LOOK    64
#define FULL    32767.0
#define CEILING (FULL * 0.89125)

typedef struct {
    double out, game;
} Slot;

static struct {
    int rate, device;
    double volume, gain, ramp, fade, decay, release, average;
    unsigned hold;
    bool flushing, playing;
    double c, tail, last;
    int skip;
    Slot ring[RING];
    size_t head, count;

    struct {
        double need[LOOK + 1], held[LOOK + 1], sum, last;
        Slot delay[LOOK + 1];
        size_t at;
    } lim;

    NativeOutputStats st;
} s;

void native_output_reset(int rate, int device_samples)
{
    memset(&s, 0, sizeof s);
    s.rate = rate;
    s.device = device_samples;
    s.volume = s.gain = 1.0;
    s.ramp = 1.0 / (0.010 * rate);
    s.fade = 1.0 / (0.005 * rate);
    s.decay = exp(-1.0 / (0.003 * rate));
    s.release = 1.0 - exp(-1.0 / (0.050 * rate));
    for (int i = 0; i <= LOOK; i++) {
        s.lim.need[i] = s.lim.held[i] = 1.0;
    }
    s.lim.sum = LOOK + 1;
    s.lim.last = 1.0;
}

void native_output_volume(int percent)
{
    s.volume = (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100.0;
}

void native_output_hold(unsigned reasons, bool on)
{
    s.hold = on ? (s.hold | reasons) : (s.hold & ~reasons);
}

void native_output_flush(void)
{
    if (s.playing) {
        s.flushing = true;
        return;
    }
    s.count = 0;
    s.skip = LOOK;
    s.st.flushes++;
}

size_t native_output_queued(void)
{
    return s.count;
}

bool native_output_quiet(void)
{
    return !s.playing && fabs(s.tail) < 1.0;
}

const NativeOutputStats *native_output_stats(void)
{
    return &s.st;
}

static Slot limit(double x, double game)
{
    size_t at = s.lim.at;
    double a = fabs(x);
    s.lim.need[at] = a > CEILING ? CEILING / a : 1.0;
    s.lim.delay[at].out = x;
    s.lim.delay[at].game = game;
    double low = 1.0;
    for (int i = 0; i <= LOOK; i++) {
        low = s.lim.need[i] < low ? s.lim.need[i] : low;
    }
    double held = s.lim.last + (1.0 - s.lim.last) * s.release;
    held = low < held ? low : held;
    s.lim.last = held;
    s.lim.sum += held - s.lim.held[at];
    s.lim.held[at] = held;
    s.lim.at = (at + 1) % (LOOK + 1);
    Slot y = s.lim.delay[s.lim.at];
    double g = s.lim.sum / (LOOK + 1);
    if (g < 1.0) {
        y.out *= g;
        s.st.limited++;
    }
    return y;
}

static void resample(Slot *v, size_t n, size_t m)
{
    static Slot src[4096];
    if (n < 2 || m == n) {
        return;
    }
    memcpy(src, v, n * sizeof *v);
    for (size_t i = 0; i < m; i++) {
        double x = (double)i * (double)(n - 1) / (double)(m - 1);
        size_t j = (size_t)x;
        double frac = x - (double)j;
        const Slot *a = &src[j], *b = &src[j + 1 < n ? j + 1 : j];
        v[i].out = a->out + (b->out - a->out) * frac;
        v[i].game = a->game + (b->game - a->game) * frac;
    }
}

void native_output_push(const double *mix, const double *game, size_t n)
{
    static Slot frame[4096];
    if (n > 2048) {
        n = 2048;
    }
    for (size_t i = 0; i < n; i++) {
        double a = fabs(mix[i]);
        s.st.peak_in = a > s.st.peak_in ? a : s.st.peak_in;
        s.st.over += a >= FULL;
    }
    if (s.count > s.st.max_queued) {
        s.st.max_queued = s.count;
    }
    if (!n || s.hold) {
        return;
    }
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        double d = s.volume - s.gain;
        s.gain += d > s.ramp ? s.ramp : d < -s.ramp ? -s.ramp : d;
        Slot y = limit(mix[i] * s.gain, game ? game[i] : mix[i]);
        if (s.skip > 0) {
            s.skip--;
            continue;
        }
        frame[k++] = y;
    }
    n = k;
    if (n < 2) {
        return;
    }
    s.average = s.playing ? s.average + (s.count - s.average) / 32.0 : (double)s.count;
    double off = s.average - (double)(2 * (size_t)s.device + (size_t)(s.rate / 60)), dead = n / 2.0;
    size_t m = n, most = n / 200 + 1;
    if (s.playing && s.count < (size_t)s.device / 2) {
        m = n + n / 50 + 1;
    } else if (s.playing && off < -dead) {
        size_t add = (size_t)((-off - dead) / 32) + 1;
        m = n + (add < most ? add : most);
    } else if (off > dead) {
        size_t cut = (size_t)((off - dead) / 32) + 1;
        m = n - (cut < most ? cut : most);
    }
    s.st.stretched += m > n;
    s.st.shrunk += m < n;
    resample(frame, n, m);
    if (s.count + m > RING) {
        s.st.dropped++;
        return;
    }
    for (size_t i = 0; i < m; i++) {
        s.ring[(s.head + s.count++) % RING] = frame[i];
    }
    s.st.pushed += m;
}

static int16_t to16(double v)
{
    v = v > 32766 ? 32766 : v < -32766 ? -32766 : v;
    return (int16_t)v;
}

void native_output_pull(int16_t *out, int16_t *game, size_t n)
{
    size_t prime = 2 * (size_t)s.device + (size_t)(s.rate / 60);
    for (size_t i = 0; i < n; i++) {
        bool want = !s.hold && !s.flushing;
        if (!s.playing && want && s.count >= prime) {
            s.playing = true;
            s.c = 0;
            s.head = (s.head + s.count - prime) % RING;
            s.count = prime;
        }
        if (s.playing && (!s.count || (!want && s.c <= 0))) {
            s.playing = false;
            s.tail = s.last;
            if (want) {
                s.st.underruns++;
            } else {
                s.count = 0;
                s.skip = LOOK;
                s.st.flushes += s.flushing;
                s.flushing = false;
            }
        }
        s.tail *= s.decay;
        if (fabs(s.tail) < 0.01) {
            s.tail = 0;
        }
        double v = s.tail, g = INT16_MIN;
        if (s.playing) {
            Slot *slot = &s.ring[s.head];
            s.head = (s.head + 1) % RING;
            s.count--;
            s.st.played++;
            s.c += want ? s.fade : -s.fade;
            s.c = s.c > 1 ? 1 : s.c < 0 ? 0 : s.c;
            v += s.c * slot->out;
            g = slot->game / 2 > 32767 ? 32767 : slot->game / 2 < -32767 ? -32767 : slot->game / 2;
        } else {
            s.st.starved += want && s.st.played;
        }
        s.last = v;
        out[i] = to16(v);
        if (game) {
            game[i] = (int16_t)g;
        }
        double a = fabs(v);
        s.st.peak_out = a > s.st.peak_out ? a : s.st.peak_out;
    }
}
