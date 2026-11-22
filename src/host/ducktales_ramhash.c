/*
 * ducktales_ramhash.c - --ram-hash-out FILE for the cycle host: one
 * native_hash.h line per frame (CPU RAM, PPU memories, cycles, registers),
 * the reference the native runtime's own --ram-hash-out is compared with
 * (tools/native_compare.py, docs/native-runtime.md). Reads only.
 */
#include "host/ducktales_ramhash.h"

#include "cyc_core.h"
#include "native/native_hash.h"

static FILE *s_file;
static long s_frame;

bool dt_ramhash_open(const char *path)
{
    if (s_file) fclose(s_file);
    s_file = fopen(path, "w");
    if (!s_file) fprintf(stderr, "--ram-hash-out: cannot write %s\n", path);
    return s_file != NULL;
}

void dt_ramhash_power_on(void)
{
    s_frame = 0;
}

void dt_ramhash_frame_end(void)
{
    if (!s_file) return;
    size_t ciram_len, chr_len;
    CycCpuState cpu_state;
    cyc_cpu_state(&cpu_state);
    NativeHashInput in = {
        .ram = cyc_cpu_ram(),
        .ciram = cyc_ppu_ciram(&ciram_len),
        .palette = cyc_ppu_palette(),
        .oam = cyc_ppu_oam(),
        .chr = cyc_chr_ram(&chr_len),
        .cycles = cyc_cycle_count(),
        .pc = cpu_state.pc,
        .a = cpu_state.a, .x = cpu_state.x, .y = cpu_state.y, .s = cpu_state.s, .p = cpu_state.p,
    };
    in.chr_len = chr_len;
    native_hash_line(s_file, s_frame++, &in);
    fflush(s_file);
}
