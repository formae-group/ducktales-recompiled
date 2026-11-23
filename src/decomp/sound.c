/*
 * sound.c - the sound request of the fixed bank, decompiled
 * (docs/audio.md, docs/decompilation.md).
 *
 * Game code asks for a sound or a music by putting its id in an 8-entry ring
 * ($C8-$CF) that the NMI drains; a request finding its entry still full is
 * dropped. The routine keeps the caller's X in scratch $00 and the id in $01,
 * so those two bytes are part of what it leaves in RAM.
 */
#include "decomp/decomp.h"
#include "decomp/sound.h"

#include "ducktales/game.h"

#define gSoundWrite RAM_U8(DT_ADDR_SOUND_WRITE)
#define gSoundRing  (&gRam[DT_ADDR_SOUND_RING])

static uint8_t s_a;

/* $F754 */
uint8_t Sound_Request(uint8_t id, uint8_t x)
{
    RAM_U8(DT_ADDR_SCRATCH0) = x;
    RAM_U8(DT_ADDR_SCRATCH1) = id;
    uint8_t slot = gSoundWrite;
    uint8_t entry = gSoundRing[slot];
    uint8_t p = Flags_Cmp(0, entry, DT_SOUND_RING_EMPTY);
    s_a = entry;
    if (entry == DT_SOUND_RING_EMPTY) {
        gSoundRing[slot] = id;
        gSoundWrite = s_a = (uint8_t)((slot + 1) & 7);
    }
    return (uint8_t)(Flags_NZ(p, x) & (P_N | P_Z | P_C));
}

/* $F752: a music id is also remembered in $C5. */
uint8_t Music_Request(uint8_t id, uint8_t x)
{
    gMusic = id;
    return Sound_Request(id, x);
}

static void finish(Regs *r, uint8_t flags)
{
    r->a = s_a;
    r->p = (uint8_t)((r->p & ~(P_N | P_Z | P_C)) | flags);
}

static void Run_SoundRequest(Regs *r) { finish(r, Sound_Request(r->a, r->x)); }
static void Run_MusicRequest(Regs *r) { finish(r, Music_Request(r->a, r->x)); }

const DecompRoutine gSoundRoutines[] = {
    { DT_CODE_MUSIC_REQUEST, "ducktales.decomp.F752", "Music_Request", Run_MusicRequest, OUT_A | OUT_X | OUT_Y | OUT_P },
    { DT_CODE_SOUND_REQUEST, "ducktales.decomp.F754", "Sound_Request", Run_SoundRequest, OUT_A | OUT_X | OUT_Y | OUT_P },
};
const int gSoundRoutineCount = sizeof gSoundRoutines / sizeof gSoundRoutines[0];
