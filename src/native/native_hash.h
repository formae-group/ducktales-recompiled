/*
 * native_hash.h - the per-frame state line both runtimes write with
 * --ram-hash-out, so tools/native_compare.py can compare them line by line.
 *
 *   <frame> ram=H nt=H pal=H oam=H chr=H cycles=N PC=.. A=.. X=.. Y=.. S=.. P=..
 *
 * ram is CPU RAM $0000-$07FF; nt, pal, oam and chr are the PPU memories
 * (palette entries on their 6 bits). Each H is 64-bit FNV-1a. The cycle host
 * writes it from its frame_end callback (src/host/ducktales_ramhash.c), the
 * native runtime from its frame loop (src/native/native_main.c).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    const uint8_t *ram, *ciram, *palette, *oam, *chr;
    size_t chr_len;
    uint64_t cycles;
    uint16_t pc;
    uint8_t a, x, y, s, p;
} NativeHashInput;

static inline uint64_t native_fnv1a(const uint8_t *p, size_t n, uint8_t mask)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ (uint8_t)(p[i] & mask)) * 0x100000001B3ull;
    }
    return h;
}

static inline void native_hash_line(FILE *f, long frame, const NativeHashInput *in)
{
    fprintf(f,
            "%ld ram=%016llX nt=%016llX pal=%016llX oam=%016llX chr=%016llX cycles=%llu "
            "PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X\n",
            frame, (unsigned long long)native_fnv1a(in->ram, 0x800, 0xFF),
            (unsigned long long)native_fnv1a(in->ciram, 0x800, 0xFF),
            (unsigned long long)native_fnv1a(in->palette, 32, 0x3F),
            (unsigned long long)native_fnv1a(in->oam, 256, 0xFF),
            (unsigned long long)(in->chr ? native_fnv1a(in->chr, in->chr_len, 0xFF) : 0),
            (unsigned long long)in->cycles, in->pc, in->a, in->x, in->y, in->s, in->p);
}
