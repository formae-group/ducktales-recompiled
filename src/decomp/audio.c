/*
 * audio.c - the sound driver of bank 3, decompiled (docs/audio.md,
 * docs/decompilation.md).
 *
 * The NMI maps bank 3, hands every queued request to Sound_Play ($8003) and
 * then calls Sound_Update ($8000) once. Both keep the driver RAM of
 * docs/audio.md exactly as the original does, every frame: the priorities
 * ($D0), speed and fade ($D7-$D9), the effect state ($DF-$E3) and the four
 * 31-byte channel records at $0370, which the native player's audio options
 * read. Scratch $D5/$D6 and $E4/$E5 are left as the original leaves them.
 *
 * A channel record holds the music's sequencer state (offsets $00-$0F), the
 * effect's ($10-$13) and the modulation of whoever owns the channel
 * ($14-$1E). The update walks the records with $DC/$DD and the APU register
 * offset in $DB; a request walks them with $DC alone as the offset from
 * $0370. The effect mask $DF is rotated one bit per channel, so bit 0 always
 * tells whether the effect owns the channel at hand.
 *
 * The streams, headers and tables are read from the ROM (bank 3, mapped
 * while the driver runs). Music and effect commands outside the ten and
 * seven the jump tables hold never occur in the game's data (docs/audio.md)
 * and are not modelled.
 */
#include "decomp/decomp.h"
#include "decomp/devices.h"

#include "ducktales/game.h"

#define gPriority     RAM_U8(DT_ADDR_SND_PRIORITY)
#define gOldMask      RAM_U8(DT_ADDR_SND_OLD_MASK)
#define gBusy         RAM_U8(DT_ADDR_SND_BUSY)
#define gCount        RAM_U8(DT_ADDR_SND_COUNT)
#define gReg          RAM_U8(DT_ADDR_SND_REG)
#define gSpeed        RAM_U8(DT_ADDR_SND_SPEED)
#define gFade         RAM_U8(DT_ADDR_SND_FADE)
#define gFadeStep     RAM_U8(DT_ADDR_SND_FADE_STEP)
#define gSndFrame     RAM_U8(DT_ADDR_SND_FRAME)
#define gApuReg       RAM_U8(DT_ADDR_SND_APU_REG)
#define gRecordOffset RAM_U8(DT_ADDR_SND_RECORD)
#define gChannel      RAM_U8(DT_ADDR_SND_CHANNEL)
#define gEffectMask   RAM_U8(DT_ADDR_SND_EFFECT_MASK)
#define gEffectWait   RAM_U8(DT_ADDR_SND_EFFECT_WAIT)
#define gEffectLoop   RAM_U8(DT_ADDR_SND_EFFECT_LOOP)
#define gTemp         RAM_U8(DT_ADDR_SND_TEMP)
#define gTempHi       RAM_U8(DT_ADDR_SND_TEMP + 1)

/* Channel record offsets (docs/audio.md "Channel records"). */
#define REC_STREAM       0x00 /* $00/$01 */
#define REC_TIME         0x02 /* $02/$03, quarter frames */
#define REC_TEMPO        0x04
#define REC_LOOP         0x05 /* bit 7 triplet, bits 0-6 loop counter */
#define REC_NOTE         0x06 /* bits 5-7 repeat-pitch counter, bits 0-4 instrument */
#define REC_PERIODS      0x07 /* $07/$08: period table + 2 x transpose */
#define REC_SLIDE        0x09
#define REC_PERIOD       0x0A /* $0A/$0B */
#define REC_VOICE        0x0C
#define REC_ENVELOPE     0x0D
#define REC_ENV_TIMER    0x0E
#define REC_ENV_LEVEL    0x0F
#define REC_FX_SLIDE     0x10
#define REC_FX_PERIOD    0x11 /* $11/$12 */
#define REC_FX_VOICE     0x13
#define REC_VIBRATO      0x14
#define REC_VIB_SHAPE    0x15
#define REC_TREMOLO      0x16
#define REC_TREM_STEP    0x17
#define REC_VIB_TIMER    0x18
#define REC_VIB_STEP     0x19
#define REC_VIB_OFFSET   0x1A /* $1A/$1B */
#define REC_LAST_HIGH    0x1C
#define REC_TREM_TIMER   0x1D
#define REC_TREM_LEVEL   0x1E
#define REC_EFFECT_PART  0x10
#define REC_EFFECT_BYTES 15

#define CHANNEL_TRIANGLE 2
#define CHANNEL_NOISE    1
#define CHANNELS         4

#define ID_SPEED      0xFC
#define ID_FADE       0xFD
#define ID_STOP_FX    0xFE
#define ID_STOP_MUSIC 0xFF

#define MUTE_VOLUME   0x30 /* constant volume 0 */
#define SWEEP_OFF     0x08
#define CHANNELS_ON   0x0F
#define LENGTH_LOAD   0x08
#define NOTE_TIME_STEP 4
#define FX_NOTE       0x80
#define FX_RESTART    0x0F
#define FX_END        0x06
#define VOICE_MUSIC   REC_VOICE
#define VOICE_EFFECT  (0x80 | REC_FX_VOICE)
#define SLIDE_MUSIC   REC_SLIDE
#define SLIDE_EFFECT  (0x80 | REC_FX_SLIDE)

static void apu_write(uint8_t reg, uint8_t value)
{
    Decomp_Write((uint16_t)(APU_BASE + reg), value);
}

static uint16_t rom_word(uint16_t addr)
{
    return (uint16_t)(Game_ReadRom(addr) | Game_ReadRom((uint16_t)(addr + 1)) << 8);
}

static bool effect_owns_channel(void)
{
    return gEffectMask & 1;
}

/* $820D's rotation: the next channel's bit comes to bit 0. */
static void rotate_effect_mask(void)
{
    gEffectMask = (uint8_t)(gEffectMask >> 1 | gEffectMask << 7);
}

/* $8228: silence a channel (the triangle by zeroing its control and period
 * registers). A request passes the offset $D6, which is 2 above the channel's
 * first register; the original does the same. */
static void mute_channel(uint8_t reg, uint8_t channel)
{
    if (channel != CHANNEL_TRIANGLE) {
        apu_write(reg, MUTE_VOLUME);
        return;
    }
    apu_write(reg, 0);
    apu_write((uint8_t)(reg + 2), 0);
    apu_write((uint8_t)(reg + 3), 0);
}

/* ---- the request handler ($8003) ---- */

/* A byte of the channel record at $DC in a request (an X-indexed access from
 * $0370). */
static uint8_t *request_field(uint8_t offset)
{
    return &gRam[DT_ADDR_SND_CHANNELS + (uint8_t)(gRecordOffset + offset)];
}

/* $8217 */
static void next_request_record(void)
{
    gRecordOffset = (uint8_t)(gRecordOffset + DT_SND_RECORD_SIZE);
}

/* $81B8 */
static void clear_effect_part(void)
{
    for (int i = 0; i < REC_EFFECT_BYTES; i++) {
        *request_field((uint8_t)(REC_EFFECT_PART + i)) = 0;
    }
}

/* $81CA: the channel's music instrument into its modulation bytes. */
static void reload_instrument(void)
{
    uint16_t table = Ram_GetU16(DT_ADDR_SND_INSTRUMENTS, DT_ADDR_SND_INSTRUMENTS + 1);
    uint8_t first = (uint8_t)((*request_field(REC_NOTE) & 0x1F) * 4);
    for (int i = 0; i < 4; i++) {
        *request_field((uint8_t)(REC_VIBRATO + i)) = Game_ReadRom((uint16_t)(table + (uint8_t)(first + i)));
    }
}

/* $8165: an effect gave the channel back; the music resumes on it, or it is
 * muted when the music is resting there. */
static void give_channel_back(void)
{
    if (*request_field(REC_PERIOD) | *request_field(REC_PERIOD + 1)) {
        *request_field(REC_LAST_HIGH) = 0xFF;
        reload_instrument();
        return;
    }
    mute_channel(gReg, gCount);
    if (*request_field(REC_STREAM) | *request_field(REC_STREAM + 1)) {
        reload_instrument();
    }
}

/* $8044: a song starts when its priority is at least the playing music's. */
static void start_music(uint16_t header, uint8_t priority)
{
    gCount = gPriority & 0x0F;
    if (priority < gCount) {
        return;
    }
    gCount = priority;
    gPriority = (gPriority & 0xF0) | priority;
    gBusy = 1;
    gRecordOffset = 0;
    gSpeed = 0;
    gFade = 0;
    gCount = CHANNELS;
    uint8_t step = 1;
    for (;;) {
        header = (uint16_t)(header + step);
        Ram_SetU16(DT_ADDR_SND_HEADER, DT_ADDR_SND_HEADER + 1, header);
        *request_field(REC_STREAM) = Game_ReadRom(header);
        *request_field(REC_STREAM + 1) = Game_ReadRom((uint16_t)(header + 1));
        for (int i = REC_TIME; i < REC_EFFECT_PART; i++) {
            *request_field((uint8_t)i) = 0;
        }
        if (!effect_owns_channel()) {
            clear_effect_part();
        }
        rotate_effect_mask();
        next_request_record();
        if (!--gCount) {
            break;
        }
        step = 2;
    }
    RAM_U8(DT_ADDR_SND_INSTRUMENTS) = Game_ReadRom((uint16_t)(header + 2));
    RAM_U8(DT_ADDR_SND_INSTRUMENTS + 1) = Game_ReadRom((uint16_t)(header + 3));
    gEffectMask >>= 4;
    gBusy = 0;
}

/* $80BB: one effect at a time; a new one replaces the playing one when its
 * priority is at least as high. Channels the old effect held and the new one
 * does not go back to the music. */
static void start_effect(uint16_t header, uint8_t priority)
{
    gCount = gPriority & 0xF0;
    if (priority < gCount) {
        return;
    }
    gCount = priority;
    gPriority = (gPriority & 0x0F) | priority;
    gBusy = 1;
    gRecordOffset = 0;
    Ram_SetU16(DT_ADDR_SND_EFFECT_STREAM, DT_ADDR_SND_EFFECT_STREAM + 1, (uint16_t)(header + 2));
    gEffectWait = 0;
    gEffectLoop = 0;
    gOldMask = gEffectMask;
    gEffectMask = Game_ReadRom((uint16_t)(header + 1)) & 0x0F;
    gCount = CHANNELS;
    gReg = 2;
    do {
        bool held = gOldMask & 1;
        if (effect_owns_channel()) {
            clear_effect_part();
            gOldMask >>= 1;
        } else {
            gOldMask >>= 1;
            if (held) {
                give_channel_back();
            }
        }
        rotate_effect_mask();
        next_request_record();
        gReg = (uint8_t)(gReg + 4);
    } while (--gCount);
    gEffectMask >>= 4;
    gBusy = 0;
}

/* $8139. The mask is left rotated by four, as the original leaves it. */
static void stop_effect(void)
{
    gPriority &= 0x0F;
    gCount = CHANNELS;
    gReg = 0;
    do {
        if (effect_owns_channel()) {
            clear_effect_part();
            give_channel_back();
        }
        rotate_effect_mask();
        next_request_record();
        gReg = (uint8_t)(gReg + 4);
    } while (--gCount);
    gBusy = 0;
}

/* $8192 */
static void stop_music(void)
{
    gPriority &= 0xF0;
    gSpeed = 0;
    gFade = 0;
    gCount = CHANNELS;
    do {
        *request_field(REC_STREAM) = 0;
        *request_field(REC_STREAM + 1) = 0;
        next_request_record();
    } while (--gCount);
    gBusy = 0;
}

/* bank 3 $8003: sound id, and Y for the speed and fade ids. */
static void Sound_Play(uint8_t id, uint8_t y)
{
    Decomp_Enter(DT_CODE_SOUND_PLAY, id, 0, y);
    switch (id) {
    case ID_SPEED:
        gSpeed = (uint8_t)(y + 1);
        return;
    case ID_FADE:
        gFade = y;
        gFadeStep = 1;
        gSndFrame &= 1;
        return;
    case ID_STOP_FX:
        gBusy = 1;
        gRecordOffset = 0;
        stop_effect();
        return;
    case ID_STOP_MUSIC:
        gBusy = 1;
        gRecordOffset = 0;
        stop_music();
        return;
    default:
        break;
    }
    uint16_t header = rom_word((uint16_t)(DT_TABLE_SOUNDS + (uint8_t)(id << 1)));
    Ram_SetU16(DT_ADDR_SND_HEADER, DT_ADDR_SND_HEADER + 1, header);
    uint8_t first = Game_ReadRom(header);
    if (first & 0x0F) {
        start_music(header, first);
    } else {
        start_effect(header, first);
    }
}

/* ---- the update ($823D) ---- */

static uint16_t record(void)
{
    return Ram_GetU16(DT_ADDR_SND_RECORD, DT_ADDR_SND_RECORD + 1);
}

/* A byte of the record being updated (a ($DC),Y access). */
static uint8_t *field(uint8_t offset)
{
    return &gRam[(uint16_t)(record() + offset)];
}

static uint16_t field_word(uint8_t offset)
{
    return (uint16_t)(*field(offset) | *field((uint8_t)(offset + 1)) << 8);
}

static void set_field_word(uint8_t offset, uint16_t value)
{
    *field(offset) = (uint8_t)value;
    *field((uint8_t)(offset + 1)) = (uint8_t)(value >> 8);
}

/* $892B: the next byte of the channel's music stream; $E4/$E5 keep its
 * address. */
static uint8_t next_music_byte(void)
{
    uint16_t at = field_word(REC_STREAM);
    Ram_SetU16(DT_ADDR_SND_TEMP, DT_ADDR_SND_TEMP + 1, at);
    set_field_word(REC_STREAM, (uint16_t)(at + 1));
    return Game_ReadRom(at);
}

/* $8698 */
static uint8_t next_effect_byte(void)
{
    uint16_t at = Ram_GetU16(DT_ADDR_SND_EFFECT_STREAM, DT_ADDR_SND_EFFECT_STREAM + 1);
    Ram_SetU16(DT_ADDR_SND_EFFECT_STREAM, DT_ADDR_SND_EFFECT_STREAM + 1, (uint16_t)(at + 1));
    return Game_ReadRom(at);
}

/* $8514: restart the vibrato and tremolo on a note. The tremolo level
 * restarts from the volume in the voice byte at offset $E4 when $16 bit 7
 * asks for it. */
static void restart_modulation(void)
{
    *field(REC_VIBRATO) &= 0x7F;
    if (*field(REC_TREMOLO) & 0x80) {
        uint8_t level = *field(gTemp);
        if (gChannel != CHANNEL_TRIANGLE) {
            level &= 0x0F;
        }
        *field(REC_TREM_LEVEL) = level;
    }
    for (int i = REC_VIB_TIMER; i <= REC_TREM_TIMER; i++) {
        *field((uint8_t)i) = 0;
    }
    *field(REC_LAST_HIGH) = 0xFF;
}

/* $8546: the same without forcing the next period high write. */
static void restart_modulation_keep_period(void)
{
    uint8_t last_high = *field(REC_LAST_HIGH);
    restart_modulation();
    *field(REC_LAST_HIGH) = last_high;
}

/* $88A1: restart the per-note envelope, at 15 for a decay ($E4 bit 7) or 0
 * for an attack. */
static void restart_envelope(void)
{
    *field(REC_ENV_TIMER) = 0;
    gTemp = gTemp & 0x80 ? 0x0F : 0;
    *field(REC_ENV_LEVEL) = (*field(REC_ENV_LEVEL) & 0xF0) | gTemp;
}

/* $88D9: instrument n of the song into the modulation bytes. */
static void load_instrument(uint8_t n)
{
    uint16_t at = (uint16_t)(Ram_GetU16(DT_ADDR_SND_INSTRUMENTS, DT_ADDR_SND_INSTRUMENTS + 1) + (uint8_t)(n * 4));
    for (int i = 0;; i++) {
        Ram_SetU16(DT_ADDR_SND_TEMP, DT_ADDR_SND_TEMP + 1, at);
        *field((uint8_t)(REC_VIBRATO + i)) = Game_ReadRom(at);
        if (i == 3) {
            return;
        }
        at++;
    }
}

/* $894A: a note's time in quarter frames, units x tempo. */
static void set_note_time(uint8_t units)
{
    gTemp = units;
    gTempHi = 0;
    uint8_t time = 0;
    uint8_t tempo = *field(REC_TEMPO);
    do {
        if (time + gTemp > 0xFF) {
            gTempHi++;
        }
        time = (uint8_t)(time + gTemp);
    } while (--tempo);
    *field(REC_TIME) = time;
    *field(REC_TIME + 1) = gTempHi;
}

/* $872C: a note of the music. Under a repeat count only its time counts and
 * the modulation restarts; otherwise its pitch gives the period (the noise
 * channel takes the pitch itself). On a channel the music owns the note
 * starts muted: the output comes on the next tick. */
static void play_note(uint8_t note)
{
    uint8_t repeat = *field(REC_NOTE) & 0xE0;
    if (repeat) {
        gTemp = (uint8_t)(repeat - 0x20);
        *field(REC_NOTE) = (*field(REC_NOTE) & 0x1F) | gTemp;
        if (!effect_owns_channel()) {
            restart_modulation_keep_period();
        }
        return;
    }
    uint8_t pitch = note & 0x1F;
    uint16_t period = 0;
    if (pitch && gChannel == CHANNEL_NOISE) {
        period = pitch;
    } else if (pitch) {
        uint16_t entry = (uint16_t)(field_word(REC_PERIODS) + pitch * 2);
        Ram_SetU16(DT_ADDR_SND_TEMP, DT_ADDR_SND_TEMP + 1, entry);
        period = rom_word(entry);
    }
    set_field_word(REC_PERIOD, period);
    gTemp = *field(REC_ENVELOPE);
    if (gTemp & 0x7F) {
        restart_envelope();
    }
    if (effect_owns_channel()) {
        return;
    }
    gTemp = VOICE_MUSIC;
    restart_modulation();
    mute_channel(gApuReg, gChannel);
}

/* $8554: the command dispatch jumps through $E4/$E5, which keep the
 * handler's address from the jump table. */
static void dispatch(uint16_t table, uint8_t command)
{
    Ram_SetU16(DT_ADDR_SND_TEMP, DT_ADDR_SND_TEMP + 1, rom_word((uint16_t)(table + (uint8_t)(command * 2))));
}

/* $87B8: a music command; false when the channel stops reading. */
static bool music_command(uint8_t command)
{
    dispatch(DT_TABLE_MUSIC_COMMANDS, command);
    switch (command) {
    case 0x00:
        *field(REC_TEMPO) = next_music_byte();
        return true;
    case 0x01:
        *field(REC_SLIDE) = next_music_byte();
        return true;
    case 0x02:
        gTemp = next_music_byte();
        *field(REC_VOICE) = (*field(REC_VOICE) & 0x3F) | gTemp;
        return true;
    case 0x03: {
        uint8_t voice = next_music_byte();
        if (gChannel != CHANNEL_TRIANGLE) {
            gTemp = voice;
            voice = (*field(REC_VOICE) & 0xC0) | gTemp;
        }
        *field(REC_VOICE) = voice;
        return true;
    }
    case 0x04: {
        uint8_t count = next_music_byte();
        if (count) {
            gTemp = *field(REC_LOOP) & 0x7F;
            if (count == gTemp) {
                *field(REC_LOOP) &= 0x80;
                set_field_word(REC_STREAM, (uint16_t)(field_word(REC_STREAM) + 2));
                return true;
            }
            gTemp++;
            *field(REC_LOOP) = (*field(REC_LOOP) & 0x80) | gTemp;
        }
        uint8_t lo = next_music_byte();
        uint8_t hi = next_music_byte();
        set_field_word(REC_STREAM, (uint16_t)(hi << 8 | lo));
        return true;
    }
    case 0x05: {
        uint8_t transpose = next_music_byte();
        Ram_SetU16(DT_ADDR_SND_TEMP, DT_ADDR_SND_TEMP + 1, DT_TABLE_PERIODS);
        set_field_word(REC_PERIODS, (uint16_t)(DT_TABLE_PERIODS + (uint8_t)(transpose << 1)));
        return true;
    }
    case 0x06: {
        uint8_t note = next_music_byte();
        set_note_time(Game_ReadRom((uint16_t)(DT_TABLE_DOTTED_LENGTHS + (note >> 5))));
        play_note(note);
        return false;
    }
    case 0x07: {
        uint8_t envelope = next_music_byte();
        *field(REC_ENVELOPE) = envelope;
        *field(REC_ENV_LEVEL) = next_music_byte();
        gTemp = envelope;
        if (envelope & 0x7F) {
            restart_envelope();
        }
        return true;
    }
    case 0x08: {
        uint8_t instrument = next_music_byte();
        gTemp = instrument;
        *field(REC_NOTE) = (*field(REC_NOTE) & 0xE0) | instrument;
        if (!effect_owns_channel()) {
            load_instrument(instrument);
        }
        return true;
    }
    default:
        set_field_word(REC_STREAM, 0);
        gPriority &= 0xF0;
        if (!effect_owns_channel()) {
            mute_channel(gApuReg, gChannel);
        }
        return false;
    }
}

/* $86FE: read the channel's events up to its next note. */
static void read_music_events(void)
{
    for (;;) {
        uint8_t event = next_music_byte();
        switch (event & 0xF0) {
        case 0x00:
            if (!music_command(event)) {
                return;
            }
            continue;
        case 0x20:
            read_music_events();
            gTemp = (uint8_t)((event & 0x07) << 5);
            *field(REC_NOTE) = (*field(REC_NOTE) & 0x1F) | gTemp;
            return;
        case 0x30:
            *field(REC_LOOP) |= 0x80;
            continue;
        default:
            set_note_time(Game_ReadRom((uint16_t)(DT_TABLE_NOTE_LENGTHS + (event >> 5))));
            play_note(event);
            return;
        }
    }
}

/* $8411: the pitch slide at offset slide & $7F moves the period after it;
 * then, unless this is the music on a channel the effect owns, the vibrato
 * and the period writes. */
static void slide_and_write_period(uint8_t slide)
{
    gTempHi = slide;
    uint8_t offset = slide & 0x7F;
    int8_t step = (int8_t)*field(offset);
    if (step) {
        set_field_word((uint8_t)(offset + 1), (uint16_t)(field_word((uint8_t)(offset + 1)) + step));
    }
    if (!(slide & 0x80) && effect_owns_channel()) {
        return;
    }
    uint8_t rate = *field(REC_VIBRATO) & 0x7F;
    if (rate && rate == *field(REC_VIB_TIMER)) {
        *field(REC_VIB_TIMER) = 0;
        gTemp = *field(REC_VIB_SHAPE) >> 5;
        uint16_t high = 0;
        if (*field(REC_VIB_STEP) & 0x80) {
            gTemp = (uint8_t)-gTemp;
            high = 0xFF00;
        }
        set_field_word(REC_VIB_OFFSET, (uint16_t)(field_word(REC_VIB_OFFSET) + (high | gTemp)));
        gTemp = *field(REC_VIB_SHAPE) & 0x1F;
        *field(REC_VIB_STEP) += 1;
        if ((*field(REC_VIB_STEP) & 0x7F) == gTemp) {
            *field(REC_VIB_STEP) &= 0x80;
            if (*field(REC_VIBRATO) & 0x80) {
                *field(REC_VIBRATO) &= 0x7F;
            } else {
                *field(REC_VIBRATO) |= 0x80;
                *field(REC_VIB_STEP) ^= 0x80;
            }
        }
    }
    gTempHi = (uint8_t)((slide & 0x7F) + 1);
    uint16_t period = (uint16_t)(field_word(REC_VIB_OFFSET) + field_word(gTempHi));
    gTempHi++;
    uint8_t lo = (uint8_t)period;
    uint8_t hi = (uint8_t)(period >> 8);
    if (gChannel == CHANNEL_NOISE) {
        gTempHi++;
        gTemp = *field(gTempHi) & 0x80;
        lo = (lo & 0x0F) | gTemp;
        hi = 0;
    }
    apu_write((uint8_t)(gApuReg + 2), lo);
    if (hi == *field(REC_LAST_HIGH)) {
        return;
    }
    *field(REC_LAST_HIGH) = hi;
    apu_write((uint8_t)(gApuReg + 3), hi | LENGTH_LOAD);
}

/* $83A2: the tremolo caps the volume in $E4; then the volume register, with
 * the duty of the voice byte, and the period (voice is VOICE_MUSIC or
 * VOICE_EFFECT). */
static void write_channel(uint8_t voice)
{
    gTempHi = voice;
    uint8_t rate = *field(REC_TREMOLO) & 0x7F;
    if (rate) {
        if (rate == *field(REC_TREM_TIMER)) {
            *field(REC_TREM_TIMER) = 0;
            uint8_t level = (uint8_t)(*field(REC_TREM_STEP) + *field(REC_TREM_LEVEL));
            bool reflect = true;
            if (!level || (level & 0x80)) {
                *field(REC_TREM_LEVEL) = 1;
            } else if (level >= 0x10) {
                *field(REC_TREM_LEVEL) = 0x0F;
            } else {
                *field(REC_TREM_LEVEL) = level;
                reflect = false;
            }
            if (reflect) {
                *field(REC_TREM_STEP) = (uint8_t)-*field(REC_TREM_STEP);
            }
        }
        if (*field(REC_TREM_LEVEL) < gTemp) {
            gTemp = *field(REC_TREM_LEVEL);
        }
    }
    if (gChannel != CHANNEL_TRIANGLE) {
        gTemp = (*field(voice & 0x7F) & 0xF0) | gTemp;
    }
    apu_write(gApuReg, gTemp);
    slide_and_write_period(voice & 0x80 ? SLIDE_EFFECT : SLIDE_MUSIC);
}

/* $82FF: the music's volume through the global fade and the per-note
 * envelope, then the output (or only the slide when the effect owns the
 * channel). */
static void music_output(void)
{
    gTemp = *field(REC_VOICE);
    if (gChannel != CHANNEL_TRIANGLE) {
        gTemp &= 0x0F;
    }
    if (gFade & 0x7F) {
        uint8_t steps = gFadeStep;
        if (gChannel == CHANNEL_TRIANGLE) {
            for (int i = 0; i < 12; i++) {
                steps = (uint8_t)(steps + gFadeStep);
            }
        }
        if (gFade & 0x80) {
            do {
                if (!--gTemp) {
                    break;
                }
            } while (--steps);
        } else {
            uint8_t level = 0xFF;
            for (;;) {
                level++;
                if (level == gTemp) {
                    break;
                }
                if (!--steps) {
                    gTemp = level;
                    break;
                }
            }
        }
    }
    uint8_t envelope = *field(REC_ENVELOPE);
    if (gChannel != CHANNEL_TRIANGLE && (envelope & 0x7F)) {
        uint8_t level = *field(REC_ENV_LEVEL) & 0x0F;
        bool cap = true;
        if ((envelope & 0x7F) == *field(REC_ENV_TIMER)) {
            *field(REC_ENV_TIMER) = 0;
            gTempHi = *field(REC_ENV_LEVEL) >> 4;
            if (envelope & 0x80) {
                gTempHi = (uint8_t)-gTempHi;
            }
            level = (uint8_t)(level + gTempHi);
            if (level & 0x80) {
                level = 0;
                cap = false;
            }
        }
        if (cap && level >= gTemp) {
            level = gTemp;
        }
        gTemp = level;
        *field(REC_ENV_LEVEL) = (*field(REC_ENV_LEVEL) & 0xF0) | gTemp;
    }
    if (effect_owns_channel()) {
        slide_and_write_period(SLIDE_MUSIC);
        return;
    }
    write_channel(VOICE_MUSIC);
}

/* $86CB: one tick of the music: the note's time runs down by a quarter
 * frame, and when it is out the stream is read. */
static void music_step(void)
{
    uint16_t time = field_word(REC_TIME);
    if (time) {
        time = (uint16_t)(time - NOTE_TIME_STEP);
        set_field_word(REC_TIME, time);
        if (time) {
            if (field_word(REC_PERIOD)) {
                music_output();
            }
            return;
        }
    }
    *field(REC_LOOP) &= 0x7F;
    read_music_events();
}

/* $86BB: a triplet note ticks twice on frames with odd $DA. */
static void music_tick(void)
{
    if ((*field(REC_LOOP) & 0x80) && (gSndFrame & 1)) {
        music_step();
    }
    music_step();
}

/* $86AC: $D7 ticks, one when it is 0. */
static void music_ticks(void)
{
    uint8_t ticks = gSpeed;
    if (!ticks) {
        music_tick();
        return;
    }
    do {
        music_tick();
    } while (--ticks);
}

/* $8654: the effect ends on this channel; the music gets it back. */
static void end_effect_on_channel(void)
{
    Ram_SetU16(DT_ADDR_SND_EFFECT_STREAM, DT_ADDR_SND_EFFECT_STREAM + 1,
               (uint16_t)(Ram_GetU16(DT_ADDR_SND_EFFECT_STREAM, DT_ADDR_SND_EFFECT_STREAM + 1) - 1));
    gPriority &= 0x0F;
    gEffectMask &= 0xFE;
    if (!field_word(REC_PERIOD)) {
        mute_channel(gApuReg, gChannel);
        if (!field_word(REC_STREAM)) {
            return;
        }
    }
    load_instrument(*field(REC_NOTE) & 0x1F);
    gTemp = VOICE_MUSIC;
    restart_modulation();
}

/* $856B: the effect on a channel it owns. While it waits, only the output;
 * otherwise its stream is read up to the next note. */
static void effect_tick(void)
{
    if (gEffectWait) {
        if (!field_word(REC_FX_PERIOD)) {
            return;
        }
        gTemp = *field(REC_FX_VOICE);
        if (gChannel != CHANNEL_TRIANGLE) {
            gTemp &= 0x0F;
        }
        write_channel(VOICE_EFFECT);
        return;
    }
    for (;;) {
        uint8_t event = next_effect_byte();
        if (event & FX_NOTE) {
            if ((event & 0x0F) == FX_RESTART) {
                (void)next_effect_byte();
                restart_modulation_keep_period();
                return;
            }
            gTemp = event & 0x07;
            *field(REC_FX_PERIOD) = next_effect_byte();
            *field(REC_FX_PERIOD + 1) = gTemp;
            gTemp = REC_FX_VOICE;
            restart_modulation();
            mute_channel(gApuReg, gChannel);
            return;
        }
        dispatch(DT_TABLE_EFFECT_COMMANDS, event);
        switch (event) {
        case 0x00:
            gEffectWait = next_effect_byte();
            break;
        case 0x01:
            *field(REC_FX_SLIDE) = next_effect_byte();
            break;
        case 0x02:
            gTemp = next_effect_byte();
            *field(REC_FX_VOICE) = (*field(REC_FX_VOICE) & 0x3F) | gTemp;
            break;
        case 0x03: {
            uint8_t voice = next_effect_byte();
            if (gChannel != CHANNEL_TRIANGLE) {
                gTemp = voice;
                voice = (*field(REC_FX_VOICE) & 0xC0) | gTemp;
            }
            *field(REC_FX_VOICE) = voice;
            break;
        }
        case 0x04: {
            uint8_t count = next_effect_byte();
            if (count && count == gEffectLoop) {
                gEffectLoop = 0;
                Ram_SetU16(DT_ADDR_SND_EFFECT_STREAM, DT_ADDR_SND_EFFECT_STREAM + 1,
                           (uint16_t)(Ram_GetU16(DT_ADDR_SND_EFFECT_STREAM, DT_ADDR_SND_EFFECT_STREAM + 1) + 2));
                break;
            }
            if (count) {
                gEffectLoop++;
            }
            gTemp = next_effect_byte();
            RAM_U8(DT_ADDR_SND_EFFECT_STREAM + 1) = next_effect_byte();
            RAM_U8(DT_ADDR_SND_EFFECT_STREAM) = gTemp;
            break;
        }
        case 0x05:
            for (gTemp = REC_VIBRATO; gTemp != REC_VIB_TIMER; gTemp++) {
                *field(gTemp) = next_effect_byte();
            }
            break;
        default:
            end_effect_on_channel();
            return;
        }
    }
}

/* bank 3 $8000 (JMP $823D): once per NMI. */
static void Sound_Update(void)
{
    Decomp_Enter(DT_CODE_SOUND_UPDATE, 0, 0, 0);
    apu_write(0x01, SWEEP_OFF);
    apu_write(0x05, SWEEP_OFF);
    Decomp_Write(APU_STATUS, CHANNELS_ON);
    gSndFrame++;
    if (gBusy) {
        return;
    }
    Ram_SetU16(DT_ADDR_SND_RECORD, DT_ADDR_SND_RECORD + 1, DT_ADDR_SND_CHANNELS);
    gApuReg = 0;
    gChannel = CHANNELS;
    for (;;) {
        *field(REC_VIB_TIMER) += 1;
        *field(REC_TREM_TIMER) += 1;
        if (effect_owns_channel()) {
            effect_tick();
        }
        if (!(RAM_U8(DT_ADDR_PAUSE) & 1) && field_word(REC_STREAM)) {
            *field(REC_ENV_TIMER) += 1;
            music_ticks();
        } else if (!effect_owns_channel()) {
            mute_channel(gApuReg, gChannel);
        }
        rotate_effect_mask();
        if (!--gChannel) {
            break;
        }
        gApuReg = (uint8_t)(gApuReg + 4);
        Ram_SetU16(DT_ADDR_SND_RECORD, DT_ADDR_SND_RECORD + 1, (uint16_t)(record() + DT_SND_RECORD_SIZE));
    }
    uint8_t rate = gFade & 0x7F;
    if (rate && rate == gSndFrame) {
        gSndFrame &= 1;
        gFadeStep++;
        if (gFadeStep == 0x10) {
            if (!(gFade & 0x80)) {
                gFade = 0;
            }
            gFadeStep = 0x0F;
        }
    }
    if (gEffectWait) {
        gEffectWait--;
    }
    gEffectMask >>= 4;
}

static void Run_SoundUpdate(Regs *r)
{
    (void)r;
    Sound_Update();
}

static void Run_SoundPlay(Regs *r)
{
    Sound_Play(r->a, r->y);
}

const DecompRoutine gAudioRoutines[] = {
    { DT_CODE_SOUND_UPDATE, "ducktales.decomp.3.8000", "Sound_Update", Run_SoundUpdate, OUT_DEVICE, DT_SND_BANK },
    { DT_CODE_SOUND_PLAY, "ducktales.decomp.3.8003", "Sound_Play", Run_SoundPlay, OUT_DEVICE, DT_SND_BANK },
};
const int gAudioRoutineCount = sizeof gAudioRoutines / sizeof gAudioRoutines[0];
