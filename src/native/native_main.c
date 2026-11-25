/*
 * native_main.c - ducktales_native: the headless native-runtime prototype
 * (docs/NATIVE-RUNTIME.md). It runs the recompiled program on the native bus
 * (native_bus.c, native_video.c) with an input route and writes the same
 * per-frame state line as the cycle host's --ram-hash-out (native_hash.h).
 *
 *   ducktales_native ROM [--input FILE] [--frames N] [--ram-hash-out FILE]
 *                        [--lag none|faithful] [--lag-log FILE]
 *                        [--ram-dump FILE] [--nmi-audit FILE]
 *                        [--step-dump FILE] [--latch-log FILE | --latch-input FILE]
 *                        [--split-log FILE] [--trace-frame N --trace-out FILE]
 *                        [--wav-out FILE] [--music-under-effects on|off]
 *                        [--overlap-effects on|off] [--audio-channels MASK]
 *                        [--sound-request FRAME:IDS]
 *                        [--save-state F:FILE] [--load-state FILE]
 *                        [--decomp off|replace]
 *
 * --input takes the cycle host's route format (tests/routes): one
 * "<frame> BUTTONS" line per change, buttons joined by +, "-" releases, "2:"
 * selects controller 2; each line holds from its frame on. --split-log writes
 * the sprite-0 split and the write counts native_state.h hands a renderer.
 * --lag picks the lag mode (native_runtime.h): none, the default, removes the
 * console's slowdown; faithful keeps it and is what tests/native_routes.py
 * compares with the cycle build. --lag-log writes one line per frame:
 *
 *   <frame> nmi=0|1 pic=0|1 lag=0|1 held=<cycles> end=-|idle|poll|cap land=<$F6>
 *           row=<$F1> screen=<$2D> [overrun=<frame>:<cycles>]
 *
 * overrun names the lag frame whose task work finished in this frame and how
 * many cycles of it ran after that frame's VBlank (tools/lag_report.py).
 * --ram-dump appends the 2 KiB of CPU RAM after every frame. --nmi-audit
 * lists each RAM byte the NMI handler wrote, with the write count and the
 * number of reads the tasks made of it. --step-dump appends the 2 KiB of CPU
 * RAM each time a frame's logic completes instead. --latch-log records the
 * buttons latched at every controller strobe; --latch-input replays such a
 * log strobe by strobe in place of --input, so two lag modes can be given
 * the same input per logic step (tools/lag_report.py --steps). --wav-out
 * plays every frame's APU writes through the native synth (native_audio.h)
 * and writes the result as 48 kHz mono 16-bit PCM (tests/audio_compare.py).
 * --music-under-effects and --overlap-effects turn on the native player's
 * extra voices ([Audio] MusicUnderEffects, OverlapEffects; docs/AUDIO.md),
 * --audio-channels keeps only the channels of a hex mask in the mix (2 =
 * pulse 2, extra voices included). --sound-request fills the sound request
 * ring ($C6-$CF) with hex ids joined by + (13+15) after frame FRAME runs, as
 * if the game had queued them in it (tools/audio_record.py does the same to a
 * save state), so the next NMI hands them to the driver; it changes the
 * machine and is for audio measurements only. --save-state F:FILE
 * (repeatable) writes the machine (native_state_save) after frame F;
 * --load-state FILE starts from one: the run continues at the frame after it,
 * with the route's input from there and the frame numbers of every log
 * continuing the saved run's (tests/quick_verify.py --from-state). The lag
 * mode is not part of a state: give the same --lag. --decomp replace runs the
 * decompiled routines alone in place of the recompiled ones
 * (src/decomp/decomp_native.c), with --lag none only (the C's cycles are not
 * the console's), and prints each routine's call counts at exit
 * (tests/native_decomp_test.py).
 */
#include "decomp/decomp.h"
#include "native/native_audio.h"
#include "native/native_hash.h"
#include "native/native_route.h"
#include "native/native_runtime.h"
#include "native/native_state.h"

#include "cpu6502.h"
#include "cyc_recomp.h"
#include "cyc_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STATE_MAGIC     "DTNFRAME"
#define STATE_VERSION   1u
#define MAX_STATE_SAVES 256

static NativeRoute route;

static struct {
    long frame;
    const char *path;
} state_saves[MAX_STATE_SAVES];

static size_t state_save_count;

static FILE *step_f, *latch_log_f;
static uint8_t (*latches)[2];
static size_t latch_count, latch_next;

static void dump_step(void)
{
    fwrite(native_cpu_ram(), 1, 0x800, step_f);
}

static void record_latch(uint8_t buttons[2])
{
    fprintf(latch_log_f, "%02X %02X\n", buttons[0], buttons[1]);
}

static void replay_latch(uint8_t buttons[2])
{
    if (latch_next < latch_count) {
        buttons[0] = latches[latch_next][0];
        buttons[1] = latches[latch_next][1];
        latch_next++;
    }
}

static bool load_latches(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    size_t cap = 0;
    unsigned a, b;
    while (fscanf(f, "%x %x", &a, &b) == 2) {
        if (latch_count == cap) {
            cap = cap ? cap * 2 : 4096;
            latches = realloc(latches, cap * sizeof *latches);
        }
        latches[latch_count][0] = (uint8_t)a;
        latches[latch_count][1] = (uint8_t)b;
        latch_count++;
    }
    fclose(f);
    return true;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = n > 0 ? (uint8_t *)malloc((size_t)n) : NULL;
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return data;
}

static bool write_state(const char *path, long next_frame)
{
    size_t size = native_state_save(NULL, 0);
    uint8_t *blob = (uint8_t *)malloc(size);
    FILE *f = blob ? fopen(path, "wb") : NULL;
    bool ok = false;
    if (f) {
        uint32_t head[4] = { STATE_VERSION, native_prg_hash(), (uint32_t)size, (uint32_t)next_frame };
        native_state_save(blob, size);
        ok = fwrite(STATE_MAGIC, 1, 8, f) == 8 && fwrite(head, sizeof head, 1, f) == 1 &&
             fwrite(blob, 1, size, f) == size;
        ok = fclose(f) == 0 && ok;
    }
    free(blob);
    return ok;
}

static long read_state(const char *path)
{
    size_t size = 0;
    uint8_t *data = read_file(path, &size);
    uint32_t head[4];
    long frame = -1;
    if (data && size >= 8 + sizeof head && !memcmp(data, STATE_MAGIC, 8)) {
        memcpy(head, data + 8, sizeof head);
        if (head[0] == STATE_VERSION && head[1] == native_prg_hash() && head[2] == size - 8 - sizeof head &&
            native_state_load(data + 8 + sizeof head, head[2])) {
            frame = (long)head[3];
        }
    }
    free(data);
    return frame;
}

int main(int argc, char **argv)
{
    const char *rom = NULL, *input = NULL, *hash_out = NULL, *split_out = NULL, *trace_out = NULL, *lag_out = NULL,
               *dump_out = NULL, *audit_out = NULL, *step_out = NULL, *latch_out = NULL, *latch_in = NULL,
               *wav_out = NULL, *load_state = NULL;
    NativeLagMode lag_mode = NATIVE_LAG_NONE;
    DecompMode decomp = DECOMP_OFF;
    long frames = 600, trace_frame = -1, request_frame = -1;
    uint8_t requests[8];
    int request_count = 0;
    bool music_under = false, overlap = false;
    unsigned channels = 0x0F;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--input") && i + 1 < argc) {
            input = argv[++i];
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--ram-hash-out") && i + 1 < argc) {
            hash_out = argv[++i];
        } else if (!strcmp(argv[i], "--split-log") && i + 1 < argc) {
            split_out = argv[++i];
        } else if (!strcmp(argv[i], "--trace-frame") && i + 1 < argc) {
            trace_frame = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--trace-out") && i + 1 < argc) {
            trace_out = argv[++i];
        } else if (!strcmp(argv[i], "--lag-log") && i + 1 < argc) {
            lag_out = argv[++i];
        } else if (!strcmp(argv[i], "--ram-dump") && i + 1 < argc) {
            dump_out = argv[++i];
        } else if (!strcmp(argv[i], "--nmi-audit") && i + 1 < argc) {
            audit_out = argv[++i];
        } else if (!strcmp(argv[i], "--step-dump") && i + 1 < argc) {
            step_out = argv[++i];
        } else if (!strcmp(argv[i], "--latch-log") && i + 1 < argc) {
            latch_out = argv[++i];
        } else if (!strcmp(argv[i], "--latch-input") && i + 1 < argc) {
            latch_in = argv[++i];
        } else if (!strcmp(argv[i], "--wav-out") && i + 1 < argc) {
            wav_out = argv[++i];
        } else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) {
            load_state = argv[++i];
        } else if (!strcmp(argv[i], "--save-state") && i + 1 < argc && state_save_count < MAX_STATE_SAVES &&
                   strchr(argv[i + 1], ':') && strchr(argv[i + 1], ':')[1]) {
            state_saves[state_save_count].frame = atol(argv[++i]);
            state_saves[state_save_count++].path = strchr(argv[i], ':') + 1;
        } else if (!strcmp(argv[i], "--music-under-effects") && i + 1 < argc) {
            music_under = !strcmp(argv[++i], "on");
        } else if (!strcmp(argv[i], "--overlap-effects") && i + 1 < argc) {
            overlap = !strcmp(argv[++i], "on");
        } else if (!strcmp(argv[i], "--audio-channels") && i + 1 < argc) {
            channels = (unsigned)strtoul(argv[++i], NULL, 16);
        } else if (!strcmp(argv[i], "--sound-request") && i + 1 < argc) {
            char *s = argv[++i];
            request_frame = strtol(s, &s, 10);
            while (*s == ':' || (*s == '+' && request_count > 0)) {
                if (request_count == 8) {
                    break;
                }
                requests[request_count++] = (uint8_t)strtoul(s + 1, &s, 16);
            }
        } else if (!strcmp(argv[i], "--lag") && i + 1 < argc && !strcmp(argv[i + 1], "none")) {
            lag_mode = NATIVE_LAG_NONE, i++;
        } else if (!strcmp(argv[i], "--lag") && i + 1 < argc && !strcmp(argv[i + 1], "faithful")) {
            lag_mode = NATIVE_LAG_FAITHFUL, i++;
        } else if (!strcmp(argv[i], "--decomp") && i + 1 < argc && Decomp_ParseMode(argv[i + 1], &decomp) &&
                   decomp != DECOMP_CHECK) {
            i++;
        } else if (argv[i][0] != '-' && !rom) {
            rom = argv[i];
        } else {
            fprintf(stderr,
                    "usage: %s ROM [--input FILE] [--frames N] [--ram-hash-out FILE] [--lag none|faithful] [--lag-log "
                    "FILE] [--split-log FILE] [--wav-out FILE] [--save-state F:FILE] [--load-state FILE]\n",
                    argv[0]);
            return 2;
        }
    }
    if (!rom) {
        fprintf(stderr,
                "usage: %s ROM [--input FILE] [--frames N] [--ram-hash-out FILE] [--lag none|faithful] [--lag-log "
                "FILE] [--split-log FILE] [--wav-out FILE] [--save-state F:FILE] [--load-state FILE]\n",
                argv[0]);
        return 2;
    }
    size_t size;
    uint8_t *image = read_file(rom, &size);
    char err[160];
    if (!image) {
        fprintf(stderr, "cannot read %s\n", rom);
        return 2;
    }
    if (!native_load_ines(image, size, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", rom, err);
        return 2;
    }
    free(image);
    if (input && !native_route_load(&route, input)) {
        return 2;
    }
    if (latch_in && !load_latches(latch_in)) {
        return 2;
    }
    FILE *hash_f = hash_out ? fopen(hash_out, "w") : NULL;
    FILE *split_f = split_out ? fopen(split_out, "w") : NULL;
    FILE *trace_f = trace_out ? fopen(trace_out, "w") : NULL;
    FILE *lag_f = lag_out ? fopen(lag_out, "w") : NULL;
    FILE *dump_f = dump_out ? fopen(dump_out, "wb") : NULL;
    FILE *audit_f = audit_out ? fopen(audit_out, "w") : NULL;
    step_f = step_out ? fopen(step_out, "wb") : NULL;
    latch_log_f = latch_out ? fopen(latch_out, "w") : NULL;
    FILE *wav_f = wav_out ? fopen(wav_out, "wb") : NULL;
    if ((hash_out && !hash_f) || (split_out && !split_f) || (trace_out && !trace_f) || (lag_out && !lag_f) ||
        (dump_out && !dump_f) || (audit_out && !audit_f) || (step_out && !step_f) || (latch_out && !latch_log_f) ||
        (wav_out && !wav_f)) {
        fprintf(stderr, "cannot write the output files\n");
        return 2;
    }

    if (decomp != DECOMP_OFF && lag_mode != NATIVE_LAG_NONE) {
        fprintf(stderr, "--decomp replace needs --lag none\n");
        return 2;
    }
    native_power_on();
    native_set_lag_mode(lag_mode);
    Decomp_SetMode(decomp);
    if (step_f) {
        native_set_step_hook(dump_step);
    }
    if (latch_log_f) {
        native_set_latch_hook(record_latch);
    }
    if (latches) {
        native_set_latch_hook(replay_latch);
    }
    uint32_t wav_samples = 0;
    if (wav_f) {
        native_audio_reset(48000);
        native_audio_voices(music_under, overlap);
        native_audio_channels(channels);
        native_audio_wav_header(wav_f, 48000, 0);
    }
    long first = 0;
    if (load_state && (first = read_state(load_state)) < 0) {
        fprintf(stderr, "--load-state %s: not a state of this build\n", load_state);
        return 2;
    }
    uint8_t held[2] = { 0, 0 };
    clock_t start = clock();
    for (long frame = first; frame < frames; frame++) {
        native_route_frame(&route, frame, held);
        native_set_controller(0, held[0]);
        native_set_controller(1, held[1]);
        cyc_trace_enabled = trace_f && frame == trace_frame;
        cyc_trace_file = cyc_trace_enabled ? trace_f : NULL;
        native_run_frame();
        if (frame == request_frame) {
            for (int k = 0; k < 8; k++) {
                native_poke((uint16_t)(0xC8 + k), k < request_count ? requests[k] : 0x88);
            }
            native_poke(0xC6, (uint8_t)(request_count & 7));
            native_poke(0xC7, 0);
        }
        if (hash_f) {
            const NativeVideoMemory *m = native_video_memory();
            NativeHashInput in = {
                .ram = native_cpu_ram(),
                .ciram = m->ciram,
                .palette = m->palette,
                .oam = m->oam,
                .chr = m->chr,
                .chr_len = sizeof m->chr,
                .cycles = native_cycle_count(),
                .pc = cpu.pc,
                .a = cpu.a,
                .x = cpu.x,
                .y = cpu.y,
                .s = cpu.s,
                .p = (uint8_t)(cpu_get_p(0) & 0xCF),
            };
            native_hash_line(hash_f, frame, &in);
        }
        if (dump_f) {
            fwrite(native_cpu_ram(), 1, 0x800, dump_f);
        }
        if (wav_f) {
            static int16_t pcm[NATIVE_AUDIO_MAX_FRAME_SAMPLES];
            const NativeFrame *f = native_last_frame();
            size_t n = native_audio_frame(f, pcm, NATIVE_AUDIO_MAX_FRAME_SAMPLES);
            fwrite(pcm, sizeof pcm[0], n, wav_f);
            wav_samples += (uint32_t)n;
        }
        if (lag_f) {
            static const char *const END[] = { "-", "idle", "poll", "cap" };
            const NativeLagFrame *l = native_last_lag();
            const uint8_t *ram = native_cpu_ram();
            fprintf(lag_f, "%ld nmi=%d pic=%d lag=%d held=%u end=%s land=%02X row=%02X screen=%02X", frame, l->nmi,
                    l->picture, l->lag, l->deferred_cycles, END[l->defer_end], ram[0xF6], ram[0xF1], ram[0x2D]);
            if (l->overrun_frame >= 0) {
                fprintf(lag_f, " overrun=%d:%u", (int)l->overrun_frame, l->overrun_cycles);
            }
            fputc('\n', lag_f);
        }
        if (split_f) {
            const NativeFrame *f = native_last_frame();
            fprintf(split_f, "%u cycles=%u rendered=%d split=%d,%d ppu_writes=%u apu_writes=%u%s\n", f->frame,
                    f->cycles, f->rendered, f->split_line, f->split_x, f->ppu_write_count, f->apu_write_count,
                    f->overflowed ? " overflowed" : "");
        }
        for (size_t k = 0; k < state_save_count; k++) {
            if (state_saves[k].frame == frame && !write_state(state_saves[k].path, frame + 1)) {
                fprintf(stderr, "--save-state: cannot write %s\n", state_saves[k].path);
                return 2;
            }
        }
    }
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (lag_f) {
        fclose(lag_f);
    }
    if (wav_f) {
        native_audio_wav_header(wav_f, 48000, wav_samples);
        fclose(wav_f);
    }
    if (dump_f) {
        fclose(dump_f);
    }
    if (step_f) {
        fclose(step_f);
    }
    if (latch_log_f) {
        fclose(latch_log_f);
    }
    if (audit_f) {
        const uint32_t *w = native_nmi_writes(), *r = native_task_reads();
        for (unsigned a = 0; a < 0x800; a++) {
            if (w[a]) {
                fprintf(audit_f, "%04X nmi_writes=%u task_reads=%u\n", a, w[a], r[a]);
            }
        }
        fclose(audit_f);
    }
    if (hash_f) {
        fclose(hash_f);
    }
    if (split_f) {
        fclose(split_f);
    }
    if (trace_f) {
        fclose(trace_f);
    }
    printf("native: frames=%ld cycles=%llu fallback_instructions=%llu unmodeled_reads=%llu seconds=%.2f (%.0f fps)\n",
           frames, (unsigned long long)native_cycle_count(), (unsigned long long)native_fallback_instructions(),
           (unsigned long long)native_unmodeled_reads(), seconds, seconds > 0 ? (frames - first) / seconds : 0.0);
    return 0;
}
