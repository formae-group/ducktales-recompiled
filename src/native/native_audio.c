/*
 * native_audio.c - the native runtime's synth (native_audio.h,
 * docs/audio.md "Native synth"). A frame is played as a run of spans between
 * events (a register write at its cycle offset, a frame-sequencer clock);
 * within a span every channel's parameters are fixed, so its contribution to
 * a sample is the integral of a periodic step sequence, taken in closed form:
 *
 *   pulse      8-step duty sequence, one step per 2 x (period + 1) cycles;
 *              silent below period 8 or when the sweep target overflows
 *   triangle   32-step sequence, one step per period + 1 cycles while the
 *              length and linear counters are non-zero; below period 2 it
 *              keeps stepping (ultrasonic) while its output holds
 *
 * Each sequencer keeps its step and the time left until its timer next
 * expires, so a period change takes effect at the next expiry, as on the
 * console, and the waveforms stay in phase with the APU's.
 *   noise      15-bit LFSR, one shift per table period (the only loop: at
 *              most one iteration per LFSR shift, never per cycle)
 *   DMC        the $4011 level only; the ROM never starts a sample
 *              (docs/audio.md, "DMC")
 *
 * Envelopes, the linear counter, length counters and sweeps are clocked by
 * the 4- or 5-step frame sequencer at its NTSC cycle positions, carried
 * across frames. Register side effects follow the 2A03: $4003/$4007 restart
 * the duty sequence and the envelope, $400B sets the linear counter's reload
 * flag, $400F restarts the noise envelope, $4015 clears disabled length
 * counters, $4017 restarts the sequencer.
 */
#include "native/native_audio.h"

#include <string.h>

#define NA_CPU_HZ (21477272.0 / 12.0)

static const uint8_t length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};
static const uint16_t noise_periods[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};
static const uint8_t duty_bits[4] = { 0x40, 0x60, 0x78, 0x9F };
static const uint16_t seq_points[2][4] = { { 7457, 14913, 22371, 29829 }, { 7457, 14913, 22371, 37281 } };
static const uint16_t seq_period[2] = { 29830, 37282 };

typedef struct {
    uint8_t start, divider, decay;
} NaEnvelope;

static struct {
    double cps, fill, sum[4];
    double hp90_a, hp440_a, lp_a, hp90_in, hp90_out, hp440_in, hp440_out, lp_out;
    uint8_t reg[16], len[4], enabled;
    uint16_t pulse_period[2];
    uint8_t pulse_step[2];
    double pulse_wait[2];
    uint8_t sweep_divider[2], sweep_reload[2];
    NaEnvelope env[3];
    uint16_t tri_period;
    uint8_t tri_step, tri_out;
    double tri_wait;
    uint8_t tri_linear, tri_reload;
    uint16_t noise_lfsr;
    double noise_wait;
    uint8_t dmc_level;
    double seq_pos;
    uint8_t five_step, seq_next;
    uint8_t tri_levels[32];
    uint16_t tri_prefix[33];
} s;

void native_audio_reset(int sample_rate)
{
    memset(&s, 0, sizeof s);
    s.cps = NA_CPU_HZ / sample_rate;
    double dt = 1.0 / sample_rate, pi2 = 2 * 3.14159265358979;
    double rc90 = 1.0 / (pi2 * 90), rc440 = 1.0 / (pi2 * 440), rc14k = 1.0 / (pi2 * 14000);
    s.hp90_a = rc90 / (rc90 + dt);
    s.hp440_a = rc440 / (rc440 + dt);
    s.lp_a = dt / (rc14k + dt);
    s.noise_lfsr = 1;
    s.noise_wait = noise_periods[0];
    s.pulse_wait[0] = s.pulse_wait[1] = 2;
    s.tri_wait = 1;
    s.tri_out = 15;
    for (int k = 0; k < 32; k++) {
        s.tri_levels[k] = (uint8_t)(k < 16 ? 15 - k : k - 16);
        s.tri_prefix[k + 1] = (uint16_t)(s.tri_prefix[k] + s.tri_levels[k]);
    }
}

static int sweep_target(int ch)
{
    uint16_t period = s.pulse_period[ch];
    uint8_t r = s.reg[1 + 4 * ch];
    int change = period >> (r & 7);
    if (r & 0x08) {
        int target = period - change - (ch == 0 ? 1 : 0);
        return target < 0 ? 0 : target;
    }
    return period + change;
}

static uint8_t envelope_volume(const NaEnvelope *e, uint8_t reg) { return (reg & 0x10) ? (reg & 15) : e->decay; }

static void clock_envelope(NaEnvelope *e, uint8_t reg)
{
    if (e->start) {
        e->start = 0;
        e->decay = 15;
        e->divider = reg & 15;
    } else if (e->divider == 0) {
        e->divider = reg & 15;
        if (e->decay) e->decay--;
        else if (reg & 0x20) e->decay = 15;
    } else {
        e->divider--;
    }
}

static void quarter_frame(void)
{
    clock_envelope(&s.env[0], s.reg[0]);
    clock_envelope(&s.env[1], s.reg[4]);
    clock_envelope(&s.env[2], s.reg[12]);
    if (s.tri_reload) s.tri_linear = s.reg[8] & 0x7F;
    else if (s.tri_linear) s.tri_linear--;
    if (!(s.reg[8] & 0x80)) s.tri_reload = 0;
}

static void half_frame(void)
{
    static const uint8_t halt_bit[4] = { 0x20, 0x20, 0x80, 0x20 };
    static const uint8_t halt_reg[4] = { 0, 4, 8, 12 };
    for (int ch = 0; ch < 4; ch++)
        if (s.len[ch] && !(s.reg[halt_reg[ch]] & halt_bit[ch])) s.len[ch]--;
    for (int ch = 0; ch < 2; ch++) {
        uint8_t r = s.reg[1 + 4 * ch];
        int target = sweep_target(ch);
        if (s.sweep_divider[ch] == 0 && (r & 0x80) && (r & 7) && s.pulse_period[ch] >= 8 && target <= 0x7FF)
            s.pulse_period[ch] = (uint16_t)target;
        if (s.sweep_divider[ch] == 0 || s.sweep_reload[ch]) {
            s.sweep_divider[ch] = (r >> 4) & 7;
            s.sweep_reload[ch] = 0;
        } else {
            s.sweep_divider[ch]--;
        }
    }
}

static double cycle_sum(const uint8_t *levels, const uint16_t *prefix, unsigned n, unsigned from, unsigned long long steps)
{
    unsigned long long to = from + steps;
    return (double)(to / n) * prefix[n] + prefix[to % n] - prefix[from];
}

static double sequence(const uint8_t *levels, const uint16_t *prefix, unsigned n, uint8_t *step, double *wait,
                       double length, int gated, double span)
{
    if (span < *wait) {
        *wait -= span;
        return levels[*step] * span;
    }
    double sum = levels[*step] * *wait;
    span -= *wait;
    unsigned first = gated ? (*step + 1u) % n : *step;
    unsigned long long whole = (unsigned long long)(span / length);
    double rest = span - (double)whole * length;
    if (gated) {
        sum += cycle_sum(levels, prefix, n, first, whole) * length;
        *step = (uint8_t)((first + whole) % n);
    } else {
        sum += levels[first] * (double)whole * length;
        *step = (uint8_t)first;
    }
    sum += levels[*step] * rest;
    *wait = length - rest;
    return sum;
}

static void accumulate(double span)
{
    for (int ch = 0; ch < 2; ch++) {
        uint8_t r = s.reg[4 * ch], levels[8];
        uint8_t volume = (s.len[ch] && s.pulse_period[ch] >= 8 && sweep_target(ch) <= 0x7FF) ? envelope_volume(&s.env[ch], r) : 0;
        for (int k = 0; k < 8; k++) levels[k] = (uint8_t)(((duty_bits[r >> 6] >> (7 - k)) & 1) * volume);
        uint16_t prefix[9] = { 0 };
        for (int k = 0; k < 8; k++) prefix[k + 1] = (uint16_t)(prefix[k] + levels[k]);
        s.sum[ch] += sequence(levels, prefix, 8, &s.pulse_step[ch], &s.pulse_wait[ch], 2.0 * (s.pulse_period[ch] + 1), 1, span);
    }
    double tri = sequence(s.tri_levels, s.tri_prefix, 32, &s.tri_step, &s.tri_wait, s.tri_period + 1.0,
                          s.len[2] && s.tri_linear, span);
    if (s.tri_period >= 2) {
        s.sum[2] += tri;
        s.tri_out = s.tri_levels[s.tri_step];
    } else {
        s.sum[2] += s.tri_out * span;
    }
    uint8_t volume = s.len[3] ? envelope_volume(&s.env[2], s.reg[12]) : 0;
    unsigned tap = (s.reg[14] & 0x80) ? 6 : 1;
    double left = span;
    while (s.noise_wait <= left) {
        if (!(s.noise_lfsr & 1)) s.sum[3] += volume * s.noise_wait;
        left -= s.noise_wait;
        unsigned feedback = (s.noise_lfsr ^ (s.noise_lfsr >> tap)) & 1;
        s.noise_lfsr = (uint16_t)((s.noise_lfsr >> 1) | (feedback << 14));
        s.noise_wait = noise_periods[s.reg[14] & 15];
    }
    if (!(s.noise_lfsr & 1)) s.sum[3] += volume * left;
    s.noise_wait -= left;
}

static int16_t emit(void)
{
    double p = (s.sum[0] + s.sum[1]) / s.fill, t = (3 * s.sum[2] + 2 * s.sum[3]) / s.fill + s.dmc_level;
    double level = (p > 0 ? 95.52 / (8128.0 / p + 100) : 0) + (t > 0 ? 163.67 / (24329.0 / t + 100) : 0);
    double hp = s.hp90_a * (s.hp90_out + level - s.hp90_in);
    s.hp90_in = level;
    s.hp90_out = hp;
    double hp2 = s.hp440_a * (s.hp440_out + hp - s.hp440_in);
    s.hp440_in = hp;
    s.hp440_out = hp2;
    s.lp_out += s.lp_a * (hp2 - s.lp_out);
    double v = s.lp_out * 30000.0;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    s.fill = 0;
    memset(s.sum, 0, sizeof s.sum);
    return (int16_t)v;
}

static void render(double span, int16_t *out, size_t max, size_t *n)
{
    while (span > 0) {
        double take = s.cps - s.fill;
        if (take > span) take = span;
        accumulate(take);
        s.fill += take;
        span -= take;
        if (s.fill >= s.cps - 1e-9) {
            int16_t v = emit();
            if (*n < max) out[*n] = v;
            ++*n;
        }
    }
}

static void advance(double span, int16_t *out, size_t max, size_t *n)
{
    while (span > 0) {
        double to_clock = seq_points[s.five_step][s.seq_next] - s.seq_pos;
        if (to_clock > span) {
            render(span, out, max, n);
            s.seq_pos += span;
            return;
        }
        render(to_clock, out, max, n);
        span -= to_clock;
        s.seq_pos = seq_points[s.five_step][s.seq_next];
        quarter_frame();
        if (s.seq_next == 1 || s.seq_next == 3) half_frame();
        if (++s.seq_next == 4) {
            s.seq_next = 0;
            s.seq_pos -= seq_period[s.five_step];
        }
    }
}

static void length_load(int ch, uint8_t value)
{
    if (s.enabled >> ch & 1) s.len[ch] = length_table[value >> 3];
}

static void reg_write(uint16_t addr, uint8_t value)
{
    if (addr <= 0x400F) s.reg[addr & 15] = value;
    switch (addr) {
    case 0x4001:
    case 0x4005: s.sweep_reload[(addr - 0x4001) / 4] = 1; break;
    case 0x4002:
    case 0x4006: {
        uint16_t *p = &s.pulse_period[(addr - 0x4002) / 4];
        *p = (uint16_t)((*p & 0x700) | value);
        break;
    }
    case 0x4003:
    case 0x4007: {
        int ch = (addr - 0x4003) / 4;
        length_load(ch, value);
        s.pulse_period[ch] = (uint16_t)((s.pulse_period[ch] & 0xFF) | ((value & 7) << 8));
        s.pulse_step[ch] = 0;
        s.env[ch].start = 1;
        break;
    }
    case 0x400A: s.tri_period = (uint16_t)((s.tri_period & 0x700) | value); break;
    case 0x400B:
        length_load(2, value);
        s.tri_period = (uint16_t)((s.tri_period & 0xFF) | ((value & 7) << 8));
        s.tri_reload = 1;
        break;
    case 0x400F:
        length_load(3, value);
        s.env[2].start = 1;
        break;
    case 0x4011: s.dmc_level = value & 0x7F; break;
    case 0x4015:
        s.enabled = value & 0x0F;
        for (int ch = 0; ch < 4; ch++)
            if (!(s.enabled >> ch & 1)) s.len[ch] = 0;
        break;
    case 0x4017:
        s.five_step = (value & 0x80) != 0;
        s.seq_pos = 0;
        s.seq_next = 0;
        if (s.five_step) {
            quarter_frame();
            half_frame();
        }
        break;
    default: break;
    }
}

size_t native_audio_frame(const NativeFrame *frame, int16_t *out, size_t max)
{
    double length = frame->cycles, now = 0;
    if (frame->held_cycles && frame->held_cycles < frame->cycles) length -= frame->held_cycles;
    double scale = frame->cycles ? length / frame->cycles : 1.0;
    size_t n = 0;
    for (uint32_t i = 0; i < frame->apu_write_count; i++) {
        const NativeRegWrite *w = &frame->apu_writes[i];
        double at = w->cycle * scale;
        if (at > length) at = length;
        if (at > now) {
            advance(at - now, out, max, &n);
            now = at;
        }
        reg_write(w->addr, w->value);
    }
    if (length > now) advance(length - now, out, max, &n);
    return n < max ? n : max;
}

void native_audio_wav_header(FILE *f, int sample_rate, uint32_t samples)
{
    uint32_t rate = (uint32_t)sample_rate, bytes = samples * 2, riff = 36 + bytes, fmt = 16, byte_rate = rate * 2;
    uint16_t pcm = 1, channels = 1, align = 2, bits = 16;
    long at = ftell(f);
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f), fwrite(&channels, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&byte_rate, 4, 1, f);
    fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&bytes, 4, 1, f);
    if (at > 44) fseek(f, at, SEEK_SET);
}
