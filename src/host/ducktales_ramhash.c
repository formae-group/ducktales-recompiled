/*
 * ducktales_ramhash.c - native-runtime verification output for the cycle host
 * (docs/native-runtime.md). Reads only.
 *
 *   --ram-hash-out FILE  one native_hash.h line per frame (CPU RAM, PPU
 *                        memories, cycles, registers), the reference the
 *                        native runtime's own --ram-hash-out is compared with
 *                        (tools/native_compare.py)
 *   --lag-log FILE       one line per frame, as VBlank ends it:
 *                        <frame> nmi= pic= lag= land= row= screen= pc=
 *                        lag=1 when the game task is still running: the
 *                        instruction VBlank landed on is outside the
 *                        scheduler's pass ($FF38-$FF7C). nmi and pic come from
 *                        the program's $2000/$2001 shadows ($FF, $FE)
 */
#include "host/ducktales_ramhash.h"

#include "cyc_core.h"
#include "native/native_hash.h"

static FILE *s_file, *s_lag;
static long s_frame;

static FILE *open_out(FILE *old, const char *option, const char *path)
{
    if (old) fclose(old);
    FILE *f = fopen(path, "w");
    if (!f) fprintf(stderr, "%s: cannot write %s\n", option, path);
    return f;
}

bool dt_ramhash_open(const char *path) { return (s_file = open_out(s_file, "--ram-hash-out", path)) != NULL; }
bool dt_laglog_open(const char *path) { return (s_lag = open_out(s_lag, "--lag-log", path)) != NULL; }

void dt_ramhash_power_on(void)
{
    s_frame = 0;
}

void dt_ramhash_frame_end(void)
{
    if (!s_file && !s_lag) {
        s_frame++;
        return;
    }
    CycCpuState cpu_state;
    cyc_cpu_state(&cpu_state);
    const uint8_t *ram = cyc_cpu_ram();
    if (s_lag) {
        bool nmi = (ram[0xFF] & 0x80) != 0, pic = (ram[0xFE] & 0x18) != 0;
        bool running = cpu_state.pc < 0xFF38 || cpu_state.pc > 0xFF7C;
        fprintf(s_lag, "%ld nmi=%d pic=%d lag=%d land=%02X row=%02X screen=%02X pc=%04X\n", s_frame, nmi, pic,
                nmi && running, ram[0xF6], ram[0xF1], ram[0x2D], cpu_state.pc);
        fflush(s_lag);
    }
    if (s_file) {
        size_t ciram_len, chr_len;
        NativeHashInput in = {
            .ram = ram,
            .ciram = cyc_ppu_ciram(&ciram_len),
            .palette = cyc_ppu_palette(),
            .oam = cyc_ppu_oam(),
            .chr = cyc_chr_ram(&chr_len),
            .cycles = cyc_cycle_count(),
            .pc = cpu_state.pc,
            .a = cpu_state.a, .x = cpu_state.x, .y = cpu_state.y, .s = cpu_state.s, .p = cpu_state.p,
        };
        in.chr_len = chr_len;
        native_hash_line(s_file, s_frame, &in);
        fflush(s_file);
    }
    s_frame++;
}
