/*
 * native_voices.c - the synth's extra voices (native_voices.h). Each function
 * names the driver code it follows (bank 3, docs/audio.md); the channel record
 * offsets are the driver's.
 */
#include "native/native_voices.h"

#include <string.h>

#define RECORD(c)  (0x370 + 31 * (c))
#define SOUND_TABLE 0x8A40

typedef struct {
    uint16_t ptr;          /* $E0/$E1 */
    uint8_t  wait, loop;   /* $E2, $E3 */
    uint8_t  priority;     /* $D0 high nibble, as the header's first byte */
    uint8_t  mask;         /* $DF */
    uint8_t  rec[4][32];   /* record offsets $10-$1E */
} Effect;

static struct {
    bool synced;
    uint32_t next_frame;
    uint8_t prev[0x800];
    uint8_t music_live;
    uint8_t music[4][32];  /* the music's offsets $14-$1E while an effect holds the record's */
    Effect effect;
    const uint8_t *prg;
    size_t prg_size;
    const uint8_t *ram, *ram_before; /* this update's RAM at the frame's end and start */
    NativeVoiceFrame *out;
    int voice;
} v;

void native_voices_reset(void) { memset(&v, 0, sizeof v); }

static uint8_t rom(uint16_t addr)
{
    size_t at = addr >= 0xC000 ? v.prg_size - 0x4000 + (addr - 0xC000u) : 3 * 0x4000u + (addr & 0x3FFFu);
    return v.prg_size >= 0x10000 && at < v.prg_size ? v.prg[at] : 0;
}

/* A byte the driver reads through its record pointer at an offset past the
 * bytes this voice keeps (the tremolo level of $8514 comes from wherever $E4
 * points): records the update already did hold their new value, the rest
 * their old one. */
static uint8_t driver_byte(int c, unsigned offset)
{
    unsigned at = (RECORD(c) + offset) & 0x7FF;
    return at >= RECORD(0) && at < RECORD(c) ? v.ram[at] : v.ram_before[at];
}

static void put(int c, int reg, uint8_t value)
{
    uint32_t *n = &v.out->count[v.voice];
    if (*n < NATIVE_VOICE_MAX_WRITES) v.out->writes[v.voice][(*n)++] = (NativeVoiceWrite){ (uint16_t)(0x4000 + 4 * c + reg), value };
}

/* $8228: silence the channel. */
static void mute(int c)
{
    if (c != 2) {
        put(c, 0, 0x30);
        return;
    }
    put(2, 0, 0);
    put(2, 2, 0);
    put(2, 3, 0);
}

/* $8514: restart vibrato and tremolo for a note; level is the byte the
 * driver loads the tremolo level from. $8546 keeps $1C. */
static void restart(uint8_t *r, int c, uint8_t level, bool keep_high)
{
    uint8_t high = r[0x1C];
    r[0x14] &= 0x7F;
    if (r[0x16] & 0x80) r[0x1E] = c == 2 ? level : (uint8_t)(level & 0x0F);
    memset(&r[0x18], 0, 6);
    r[0x1C] = keep_high ? high : 0xFF;
}

/* $83A2-$850A: tremolo cap, duty and volume, the effect's slide, vibrato,
 * period. */
static void output(uint8_t *r, int c, uint8_t volume, bool effect)
{
    if (r[0x16] & 0x7F) {
        if ((r[0x16] & 0x7F) == r[0x1D]) {
            r[0x1D] = 0;
            uint8_t level = (uint8_t)(r[0x17] + r[0x1E]);
            if (level == 0 || (level & 0x80)) {
                r[0x1E] = 1;
                r[0x17] = (uint8_t)-r[0x17];
            } else if (level >= 0x10) {
                r[0x1E] = 0x0F;
                r[0x17] = (uint8_t)-r[0x17];
            } else {
                r[0x1E] = level;
            }
        }
        if (r[0x1E] < volume) volume = r[0x1E];
    }
    uint8_t control = effect ? r[0x13] : r[0x0C];
    put(c, 0, c == 2 ? volume : (uint8_t)((control & 0xF0) | volume));
    int period = effect ? 0x11 : 0x0A;
    if (effect && r[0x10]) {
        uint16_t p = (uint16_t)((r[0x11] | r[0x12] << 8) + (int8_t)r[0x10]);
        r[0x11] = (uint8_t)p;
        r[0x12] = (uint8_t)(p >> 8);
    }
    if ((r[0x14] & 0x7F) && (r[0x14] & 0x7F) == r[0x18]) {
        r[0x18] = 0;
        uint8_t step = (r[0x15] >> 5) & 7;
        uint16_t add = (r[0x19] & 0x80) ? (uint16_t)(0xFF00 | (uint8_t)(0 - step)) : step;
        uint16_t offset = (uint16_t)((r[0x1A] | r[0x1B] << 8) + add);
        r[0x1A] = (uint8_t)offset;
        r[0x1B] = (uint8_t)(offset >> 8);
        r[0x19]++;
        if ((r[0x19] & 0x7F) == (r[0x15] & 0x1F)) {
            r[0x19] &= 0x80;
            if (r[0x14] & 0x80) {
                r[0x14] &= 0x7F;
            } else {
                r[0x14] |= 0x80;
                r[0x19] ^= 0x80;
            }
        }
    }
    unsigned low = (unsigned)r[0x1A] + r[period];
    uint8_t high = (uint8_t)(r[0x1B] + r[period + 1] + (low >> 8));
    if (c == 3) {
        low = (low & 0x0F) | (control & 0x80);
        high = 0;
    }
    put(c, 2, (uint8_t)low);
    if (high == r[0x1C]) return;
    r[0x1C] = high;
    put(c, 3, (uint8_t)(high | 8));
}

/* $82FF-$838D: the music's volume after the global fade ($D8/$D9) and the
 * per-note envelope, whose level the driver keeps in $0F for an owned
 * channel too. */
static uint8_t music_volume(const uint8_t *r, int c, const uint8_t *ram)
{
    uint8_t volume = c == 2 ? r[0x0C] : (uint8_t)(r[0x0C] & 0x0F);
    if (ram[0xD8] & 0x7F) {
        unsigned y = c == 2 ? (uint8_t)(ram[0xD9] * 13) : ram[0xD9];
        if (y == 0) y = 256;
        if (!(ram[0xD8] & 0x80)) {
            if (volume > y - 1) volume = (uint8_t)(y - 1);
        } else {
            do volume--;
            while (volume && --y);
        }
    }
    if (c != 2 && (r[0x0D] & 0x7F) && (r[0x0F] & 0x0F) < volume) volume = r[0x0F] & 0x0F;
    return volume;
}

static unsigned note_time(const uint8_t *r) { return r[0x02] | r[0x03] << 8; }

static uint16_t rom_word(uint16_t addr) { return (uint16_t)(rom(addr) | rom((uint16_t)(addr + 1)) << 8); }

/* The header of the song the NMI started this frame ($8044: a song is taken
 * when its priority is at least the music's), or 0. */
static uint16_t song_start(const uint8_t *ram, const uint8_t *prev)
{
    uint16_t song = 0;
    uint8_t priority = prev[0xD0] & 0x0F;
    for (uint8_t at = prev[0xC7] & 7, n = 0; at != (ram[0xC7] & 7) && n < 8; at = (at + 1) & 7, n++) {
        uint8_t id = prev[0xC8 + at];
        if (id == 0xFF) {
            priority = 0;
            song = 0;
        }
        if (id >= 0x38) continue;
        uint16_t header = rom_word((uint16_t)(SOUND_TABLE + 2 * id));
        uint8_t p = rom(header) & 0x0F;
        if (!p || p < priority) continue;
        priority = p;
        song = header;
    }
    return song;
}

/* The instrument the events read before the next note select (command $08),
 * or -1: the music stream format's commands from p, with the channel's loop
 * counter ($86FE-$8885). */
static int instrument_command(uint16_t p, uint8_t loop)
{
    static const int8_t operands[10] = { 1, 1, 1, 1, 3, 1, 0, 2, 1, 0 };
    int instrument = -1;
    for (int guard = 0; guard < 64; guard++) {
        uint8_t b = rom(p++);
        if ((b & 0xF0) == 0x20 || (b & 0xF0) == 0x30) continue;
        if (b & 0xF0 || b >= 10 || b == 9) return instrument;
        if (b == 8) instrument = rom(p) & 0x1F;
        if (b != 4) {
            p = (uint16_t)(p + operands[b]);
            continue;
        }
        uint8_t count = rom(p++);
        if (count && count == loop) {
            loop = 0;
            p += 2;
        } else {
            if (count) loop++;
            p = rom_word(p);
        }
    }
    return instrument;
}

/* One driver update of the music on channel c, with r holding the record's
 * offsets $00-$13 as the update left them and the music's own $14-$1E: the
 * sequencer itself ran on the record ($86AC), what the driver skipped for an
 * owned channel ($8278-$829E, $878A, $842F) is done here. */
static void music_step(uint8_t *r, int c, const uint8_t *ram, const uint8_t *prev, uint16_t song)
{
    const uint8_t *now = ram + RECORD(c), *was = prev + RECORD(c);
    if (song) memset(&r[0x14], 0, 11);
    r[0x18]++;
    r[0x1D]++;
    if ((ram[0xE6] & 1) || !(now[0x00] | now[0x01])) {
        mute(c);
        return;
    }
    bool read = song || now[0x00] != was[0x00] || now[0x01] != was[0x01] || note_time(now) >= note_time(was);
    bool playing = (now[0x0A] | now[0x0B]) != 0;
    if (!read) {
        if (playing) output(r, c, music_volume(r, c, ram), false);
        return;
    }
    bool twice = (was[0x05] & 0x80) && (ram[0xDA] & 1);
    if (!song && twice && note_time(was) > 4 && (was[0x0A] | was[0x0B])) {
        uint8_t t[32];
        memcpy(t, was, 0x14);
        memcpy(&t[0x14], &r[0x14], 11);
        uint16_t period = (uint16_t)((was[0x0A] | was[0x0B] << 8) + (int8_t)was[0x09]);
        t[0x0A] = (uint8_t)period;
        t[0x0B] = (uint8_t)(period >> 8);
        output(t, c, music_volume(t, c, ram), false);
        memcpy(&r[0x14], &t[0x14], 11);
    }
    int instrument = song ? instrument_command(rom_word((uint16_t)(song + 1 + 2 * c)), 0)
                          : instrument_command((uint16_t)(was[0x00] | was[0x01] << 8), was[0x05] & 0x7F);
    if (instrument >= 0) {
        uint16_t at = (uint16_t)((ram[0x3EC] | ram[0x3ED] << 8) + (uint8_t)(4 * instrument));
        for (int k = 0; k < 4; k++) r[0x14 + k] = rom((uint16_t)(at + k));
    }
    if (!song && was[0x06] >> 5) {
        uint8_t at = now[0x06] & 0xE0;
        restart(r, c, at < 0x1F ? now[at] : driver_byte(c, at), true);
    } else {
        restart(r, c, r[0x0C], false);
        mute(c);
        if (twice && note_time(was) <= 4 && playing) output(r, c, music_volume(r, c, ram), false);
    }
}

/* The music on each channel an effect owns, on the music's own modulation
 * bytes (taken from the record the update before the effect took it). */
static void music_frame(const uint8_t *ram, const uint8_t *prev)
{
    uint16_t song = song_start(ram, prev);
    for (int c = 0; c < 4; c++) {
        uint8_t bit = (uint8_t)(1 << c);
        if (!(ram[0xDF] & bit)) {
            if (v.music_live & bit) mute(c);
            v.music_live &= (uint8_t)~bit;
            continue;
        }
        uint8_t r[32];
        memcpy(r, ram + RECORD(c), 0x14);
        if (!(v.music_live & bit)) {
            memcpy(&v.music[c][0x14], &prev[RECORD(c) + 0x14], 11);
            v.music[c][0x1C] = 0xFF;
            mute(c);
            v.music_live |= bit;
        }
        memcpy(&r[0x14], &v.music[c][0x14], 11);
        music_step(r, c, ram, prev, song);
        memcpy(&v.music[c][0x14], &r[0x14], 11);
    }
}

/* $80BB-$80F4: an effect as its request starts it. */
static bool effect_load(Effect *e, uint8_t id)
{
    memset(e, 0, sizeof *e);
    if (id >= 0x38) return false;
    uint16_t header = (uint16_t)(rom((uint16_t)(SOUND_TABLE + 2 * id)) | rom((uint16_t)(SOUND_TABLE + 2 * id + 1)) << 8);
    e->priority = rom(header);
    if (e->priority & 0x0F) return false;
    e->mask = rom((uint16_t)(header + 1)) & 0x0F;
    e->ptr = (uint16_t)(header + 2);
    return true;
}

/* The effect playing at the end of a frame, as the driver keeps it. */
static void effect_from_ram(Effect *e, const uint8_t *ram)
{
    memset(e, 0, sizeof *e);
    e->ptr = (uint16_t)(ram[0xE0] | ram[0xE1] << 8);
    e->wait = ram[0xE2];
    e->loop = ram[0xE3];
    e->priority = ram[0xD0] & 0xF0;
    e->mask = ram[0xDF] & 0x0F;
    for (int c = 0; c < 4; c++) memcpy(&e->rec[c][0x10], &ram[RECORD(c) + 0x10], 15);
}

/* The extra voice takes an effect the way the driver would: when it is free
 * or the new one's priority is at least its own. */
static void offer(const Effect *e)
{
    if (!e->mask || (v.effect.mask && e->priority < v.effect.priority)) return;
    for (int c = 0; c < 4; c++)
        if (v.effect.mask >> c & 1 && !(e->mask >> c & 1)) mute(c);
    v.effect = *e;
    for (int c = 0; c < 4; c++) {
        if (!(e->mask >> c & 1)) continue;
        v.effect.rec[c][0x1C] = 0xFF;
        mute(c);
    }
}

/* The ring entries the NMI handed to the driver this frame ($C110), in order,
 * with the driver's priority rule ($80BB): a dropped effect, and one that a
 * newer request replaced, go to the extra voice. */
static void requests(const uint8_t *ram, const uint8_t *prev)
{
    Effect current, next;
    effect_from_ram(&current, prev);
    uint8_t priority = prev[0xD0] & 0xF0;
    for (uint8_t at = prev[0xC7] & 7, n = 0; at != (ram[0xC7] & 7) && n < 8; at = (at + 1) & 7, n++) {
        uint8_t id = prev[0xC8 + at];
        if (id == 0xFE) {
            priority = 0;
            current.mask = 0;
            continue;
        }
        if (!effect_load(&next, id)) continue;
        if (next.priority < priority) {
            offer(&next);
            continue;
        }
        offer(&current);
        current = next;
        priority = next.priority;
    }
}

/* $856B-$8698: one channel of the effect for one update. e4 follows the
 * driver's $E4, which the command dispatch ($8554) leaves at the handler's
 * low address byte; a $xF note restarts the tremolo from the byte it
 * points at. */
static void effect_channel(Effect *e, int c)
{
    static const uint8_t handler[7] = { 0xD1, 0xD9, 0xE3, 0xF3, 0x0D, 0x3E, 0x54 };
    uint8_t *r = e->rec[c];
    if (e->wait) {
        if (r[0x11] | r[0x12]) output(r, c, c == 2 ? r[0x13] : (uint8_t)(r[0x13] & 0x0F), true);
        return;
    }
    uint8_t e4 = 0x13;
    for (int guard = 0; guard < 64; guard++) {
        uint8_t b = rom(e->ptr++);
        if (b & 0x80) {
            if ((b & 0x0F) == 0x0F) {
                e->ptr++;
                restart(r, c, e4 >= 0x10 && e4 < 0x1F ? r[e4] : driver_byte(c, e4), true);
                return;
            }
            r[0x11] = rom(e->ptr++);
            r[0x12] = b & 7;
            restart(r, c, r[0x13], false);
            mute(c);
            return;
        }
        if (b < 7) e4 = handler[b];
        switch (b) {
        case 0: e->wait = rom(e->ptr++); break;
        case 1: r[0x10] = rom(e->ptr++); break;
        case 2:
            e4 = rom(e->ptr++);
            r[0x13] = (uint8_t)((r[0x13] & 0x3F) | e4);
            break;
        case 3: {
            uint8_t x = rom(e->ptr++);
            if (c != 2) e4 = x;
            r[0x13] = c == 2 ? x : (uint8_t)((r[0x13] & 0xC0) | x);
            break;
        }
        case 4: {
            uint8_t count = rom(e->ptr++);
            if (count && count == e->loop) {
                e->loop = 0;
                e->ptr += 2;
            } else {
                if (count) e->loop++;
                e4 = rom(e->ptr);
                e->ptr = rom_word(e->ptr);
            }
            break;
        }
        case 5:
            for (int k = 0; k < 4; k++) r[0x14 + k] = rom(e->ptr++);
            e4 = 0x18;
            break;
        default:
            e->ptr--;
            e->priority = 0;
            e->mask &= (uint8_t)~(1 << c);
            mute(c);
            return;
        }
    }
}

/* $823D: the counters of every record, the owned channels in order, then
 * the wait counter ($82F0). */
static void effect_frame(Effect *e)
{
    for (int c = 0; c < 4; c++) {
        e->rec[c][0x18]++;
        e->rec[c][0x1D]++;
        if (e->mask >> c & 1) effect_channel(e, c);
    }
    if (e->wait) e->wait--;
}

void native_voices_frame(uint32_t frame, const uint8_t *ram, const uint8_t *prg, size_t prg_size, bool music,
                         bool effects, NativeVoiceFrame *out)
{
    out->count[0] = out->count[1] = 0;
    v.prg = prg;
    v.prg_size = prg_size;
    v.out = out;
    v.ram = ram;
    v.ram_before = v.prev;
    bool follows = v.synced && frame == v.next_frame;
    bool updated = follows && ram[0xDA] != v.prev[0xDA];
    if (!follows || !music) v.music_live = 0;
    if (!follows || !effects) v.effect.mask = 0;
    v.voice = 0;
    if (music && updated) music_frame(ram, v.prev);
    v.voice = 1;
    if (effects && updated) {
        requests(ram, v.prev);
        if (v.effect.mask) effect_frame(&v.effect);
    }
    out->live[0] = v.music_live;
    out->live[1] = v.effect.mask;
    memcpy(v.prev, ram, sizeof v.prev);
    v.synced = true;
    v.next_frame = frame + 1;
}
