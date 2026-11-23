/*
 * app_mods_audio.c - music and effect replacements (app_mods.h, docs/mods.md
 * "Music and effects"), following the game's sound driver from its RAM
 * (docs/audio.md):
 *
 *   requests   at a frame's end the ring $C8-$CF (read index $C7, $88 =
 *              empty) holds what the game queued in that frame; the NMI that
 *              opens the next frame hands them to the driver in ring order
 *   music      ids $00-$0F: a request switches the song, $FF stops it; the
 *              replacement loops (from the file's loop start), except the two
 *              songs the game ends itself ($09 game over, $0A death), and is
 *              held while the game pauses (bit 0 of $E6); while the song is
 *              replaced, the channels no effect owns are muted
 *   effects    ids $10-$37: one at a time; a request wins when its priority
 *              (the high nibble of its header, read from the player's PRG) is
 *              at least the playing effect's ($D0 high nibble), $FE stops it;
 *              while a replaced effect owns channels ($DF, or the header's
 *              mask for one starting) they are muted; the file plays to its
 *              end unless another effect starts
 *   timing     a frame's sound is mixed after its end, so what the requests
 *              start takes effect from the next mix, the frame the NMI
 *              starts them in; the mute is for the next frame's emulation
 *
 * Only RAM is read: the APU keeps running every channel (the mute is the
 * host's audio-only mask), so the machine is the console's.
 */
#include "mods/app_mods.h"
#include "mods/mods_internal.h"

#include "game/ducktales_symbols.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SOUND_IDS DT_SOUND_IDS
#define MUSIC_IDS DT_SOUND_MUSIC_IDS

typedef struct {
    ModSound *sound;
    uint8_t  *file;
} Replacement;

typedef struct {
    ModSound *sound;
    bool      on, loop, held;
    float     buf[1024];
    size_t    len, pos;
    double    frac;
    float     a, b;
    bool      primed;
} Voice;

static Replacement s_rep[SOUND_IDS];
static int s_count;
static uint8_t s_priority[SOUND_IDS], s_mask[SOUND_IDS];

static int s_music = -1, s_effect = -1;
static bool s_music_playing, s_effect_playing;
static uint8_t s_effect_channels, s_mute;
static Voice s_voice[2];
static struct {
    bool    pending;
    int     music_start, effect_start;
    bool    music_stop, effect_stop, paused;
} s_next = { false, -1, -1, false, false, false };

void mods_audio_read_driver(const uint8_t *prg, size_t size)
{
    size_t bank = (size_t)DT_SOUND_BANK * 0x4000u;
    if (size < bank + 0x4000u) return;
    const uint8_t *b = prg + bank;
    for (int id = 0; id < SOUND_IDS; id++) {
        unsigned at = (unsigned)(DT_SOUND_TABLE - 0x8000 + id * 2);
        unsigned header = (unsigned)(b[at] | b[at + 1] << 8);
        if (header < 0x8000 || header > 0xBFFE) continue;
        const uint8_t *h = b + (header - 0x8000);
        s_priority[id] = (h[0] & 0x0F) ? 0 : h[0] >> 4;
        s_mask[id] = (h[0] & 0x0F) ? 0x0F : (h[1] & 0x0F);
    }
}

bool mods_audio_has(bool music, int id)
{
    (void)music;
    return id >= 0 && id < SOUND_IDS && s_rep[id].sound;
}

bool mods_audio_add(bool music, int id, uint8_t *file, size_t size, char *err, size_t cap)
{
    if (music ? (id < 0 || id >= MUSIC_IDS) : (id < MUSIC_IDS || id >= SOUND_IDS)) {
        snprintf(err, cap, music ? "music ids are 00-0F (docs/audio.md)" : "effect ids are 10-37 (docs/audio.md)");
        free(file);
        return false;
    }
    if (s_rep[id].sound) {
        free(file);
        return true;
    }
    ModSound *s = mod_sound_open(file, size, err, cap);
    if (!s) {
        free(file);
        return false;
    }
    s_rep[id].sound = s;
    s_rep[id].file = file;
    s_count++;
    return true;
}

static void voice_stop(Voice *v)
{
    v->on = false;
    v->sound = NULL;
}

static void voice_start(Voice *v, ModSound *s, bool loop)
{
    memset(v, 0, sizeof *v);
    mod_sound_rewind(s);
    v->sound = s;
    v->loop = loop;
    v->on = true;
}

void app_mods_reset(void)
{
    voice_stop(&s_voice[0]);
    voice_stop(&s_voice[1]);
    s_music = s_effect = -1;
    s_music_playing = s_effect_playing = false;
    s_effect_channels = s_mute = 0;
    memset(&s_next, 0, sizeof s_next);
    s_next.music_start = s_next.effect_start = -1;
}

void mods_audio_clear(void)
{
    app_mods_reset();
    for (int i = 0; i < SOUND_IDS; i++) {
        mod_sound_close(s_rep[i].sound);
        free(s_rep[i].file);
    }
    memset(s_rep, 0, sizeof s_rep);
    s_count = 0;
}

bool app_mods_audio_active(void) { return s_count > 0; }

uint8_t app_mods_audio_mute(void) { return s_mute; }

static bool replaced(int id) { return id >= 0 && id < SOUND_IDS && s_rep[id].sound; }

void app_mods_audio_frame_end(const uint8_t *ram)
{
    if (!s_count || !ram) return;
    uint8_t effect_priority = ram[DT_ADDR_SOUND_PRIORITY] >> 4;
    bool music_now = (ram[DT_ADDR_SOUND_PRIORITY] & 0x0F) != 0, effect_now = effect_priority != 0;
    if (s_effect_playing && !effect_now) s_effect_playing = false;
    if (s_music_playing && !music_now) {
        s_music_playing = false;
        if (s_voice[0].loop) s_next.music_stop = true;
    }
    s_effect_channels = effect_now ? (ram[DT_ADDR_SOUND_EFFECT_CH] & 0x0F) : 0;
    for (int k = 0; k < 8; k++) {
        uint8_t id = ram[DT_ADDR_SOUND_RING + ((ram[DT_ADDR_SOUND_RING_READ] + k) & 7)];
        if (id == DT_SOUND_RING_EMPTY) continue;
        if (id < MUSIC_IDS) {
            s_music = id;
            s_music_playing = true;
            s_next.music_start = id;
            s_next.music_stop = true;
        } else if (id == DT_SOUND_STOP_MUSIC) {
            s_music = -1;
            s_music_playing = false;
            s_next.music_start = -1;
            s_next.music_stop = true;
        } else if (id == DT_SOUND_STOP_EFFECT) {
            s_effect = -1;
            s_effect_playing = false;
            s_effect_channels = 0;
            effect_priority = 0;
            s_next.effect_start = -1;
            s_next.effect_stop = true;
        } else if (id < SOUND_IDS && s_priority[id] >= effect_priority) {
            s_effect = id;
            s_effect_playing = true;
            s_effect_channels = s_mask[id];
            effect_priority = s_priority[id];
            s_next.effect_start = id;
            s_next.effect_stop = true;
        }
    }
    s_next.paused = (ram[DT_ADDR_PAUSE] & 1) != 0;
    s_next.pending = true;
    uint8_t mute = 0;
    if (s_music_playing && replaced(s_music)) mute |= (uint8_t)(0x0F & ~(s_effect_playing ? s_effect_channels : 0));
    if (s_effect_playing && replaced(s_effect)) mute |= s_effect_channels;
    s_mute = mute;
}

static void apply_next(void)
{
    if (!s_next.pending) return;
    s_next.pending = false;
    if (s_next.music_stop) voice_stop(&s_voice[0]);
    if (s_next.effect_stop) voice_stop(&s_voice[1]);
    if (replaced(s_next.music_start))
        voice_start(&s_voice[0], s_rep[s_next.music_start].sound, s_next.music_start != DT_MUSIC_GAME_OVER && s_next.music_start != DT_MUSIC_DEATH);
    if (replaced(s_next.effect_start)) voice_start(&s_voice[1], s_rep[s_next.effect_start].sound, false);
    s_voice[0].held = s_next.paused;
    s_next.music_stop = s_next.effect_stop = false;
    s_next.music_start = s_next.effect_start = -1;
}

static bool next_sample(Voice *v, float *out)
{
    if (v->pos >= v->len) {
        v->len = mod_sound_read(v->sound, v->buf, sizeof v->buf / sizeof v->buf[0], v->loop);
        v->pos = 0;
        if (!v->len) return false;
    }
    *out = v->buf[v->pos++];
    return true;
}

static float voice_sample(Voice *v, double step)
{
    if (!v->primed) {
        if (!next_sample(v, &v->a) || !next_sample(v, &v->b)) v->b = v->a;
        v->primed = true;
    }
    float out = v->a + (v->b - v->a) * (float)v->frac;
    v->frac += step;
    while (v->frac >= 1.0) {
        v->frac -= 1.0;
        v->a = v->b;
        if (!next_sample(v, &v->b)) {
            v->on = false;
            break;
        }
    }
    return out;
}

void app_mods_audio_mix(int16_t *samples, size_t count, int rate)
{
    if (!s_count || rate <= 0) return;
    for (int k = 0; k < 2; k++) {
        Voice *v = &s_voice[k];
        if (!v->on || v->held || !v->sound) continue;
        double step = (double)mod_sound_rate(v->sound) / rate;
        for (size_t i = 0; i < count && v->on; i++) {
            long mixed = samples[i] + lrintf(voice_sample(v, step) * 32767.0f);
            samples[i] = (int16_t)(mixed > 32767 ? 32767 : mixed < -32768 ? -32768 : mixed);
        }
    }
    apply_next();
}
