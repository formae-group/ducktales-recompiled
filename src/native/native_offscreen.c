/*
 * native_offscreen.c - the player build's runs with no window (--frames N,
 * native_player.c has the options): a route's buttons frame by frame, the
 * picture the window would show written after every Nth frame
 * (--present-out), the synth's sound as a WAV (--wav-out), what the audio
 * device would play through the window's output stage on simulated clocks
 * (--audio-out, --audio-sim), and the save-state round trip (--state-check).
 * It reads no config.ini and owns no setting.
 */
#include "native/native_audio.h"
#include "native/native_output.h"
#include "native/native_player.h"
#include "native/native_route.h"
#include "native/native_runtime.h"
#include "native/native_state.h"

#include "game/enhancements.h"

#include "cyc_png.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --audio-sim: the window's frame loop and the audio device on simulated
 * clocks, for --audio-out. Each frame's sound is pushed when the loop
 * starts the frame; the frame then waits for its deadline (1 / 60.0988 s)
 * and, with refresh set, for the next VSync after it, as player_run_window does.
 * The device pulls period samples every period / 48000 s, its clock off
 * by drift parts per million. The events stand for what the window sees:
 * menu=F:N the menu opens before frame F with MenuPausesGame on for N
 * frames; fast=F:N fast-forward for N frames; drag=F:MS the window is moved
 * for MS ms (the loop blocked in the move); hitch=F:MS the loop blocked for
 * MS ms with no warning; load=F a state loaded (the queue flushed); off=F:N
 * sound off for N frames; sweep=F the volume stepped by 5 every frame from
 * F, from 100 to 0 and back. */
bool player_parse_audio_sim(const char *spec, AudioSim *sim)
{
    memset(sim, 0, sizeof *sim);
    sim->period = 1024;
    sim->volume = 100;
    sim->load = sim->sweep = -1;
    sim->menu[0] = sim->fast[0] = sim->drag[0] = sim->hitch[0] = sim->off[0] = -1;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        char *eq = strchr(tok, '=');
        if (!eq) {
            return false;
        }
        *eq++ = 0;
        long a = 0, b = 0;
        int got = sscanf(eq, "%ld:%ld", &a, &b);
        long *pair = !strcmp(tok, "menu")    ? sim->menu
                     : !strcmp(tok, "fast")  ? sim->fast
                     : !strcmp(tok, "drag")  ? sim->drag
                     : !strcmp(tok, "hitch") ? sim->hitch
                     : !strcmp(tok, "off")   ? sim->off
                                             : NULL;
        if (pair && got == 2) {
            pair[0] = a, pair[1] = b;
        } else if (!strcmp(tok, "refresh") && got == 1) {
            sim->refresh = (int)a;
        } else if (!strcmp(tok, "period") && got == 1 && a > 0 && a <= 8192) {
            sim->period = (int)a;
        } else if (!strcmp(tok, "volume") && got == 1) {
            sim->volume = (int)a;
        } else if (!strcmp(tok, "drift")) {
            sim->drift = atof(eq);
        } else if (!strcmp(tok, "load") && got == 1) {
            sim->load = a;
        } else if (!strcmp(tok, "sweep") && got == 1) {
            sim->sweep = a;
        } else {
            return false;
        }
    }
    return true;
}

static struct {
    FILE *f;
    uint32_t samples;
    double t, device_t, next;
    const AudioSim *sim;
} s_sim;

static void sim_wav_header(void)
{
    uint32_t rate = AUDIO_RATE, bytes = s_sim.samples * 4, riff = 36 + bytes, fmt = 16, byte_rate = rate * 4;
    uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
    long at = ftell(s_sim.f);
    fseek(s_sim.f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, s_sim.f), fwrite(&riff, 4, 1, s_sim.f), fwrite("WAVEfmt ", 1, 8, s_sim.f);
    fwrite(&fmt, 4, 1, s_sim.f), fwrite(&pcm, 2, 1, s_sim.f), fwrite(&channels, 2, 1, s_sim.f);
    fwrite(&rate, 4, 1, s_sim.f), fwrite(&byte_rate, 4, 1, s_sim.f), fwrite(&align, 2, 1, s_sim.f);
    fwrite(&bits, 2, 1, s_sim.f), fwrite("data", 1, 4, s_sim.f), fwrite(&bytes, 4, 1, s_sim.f);
    if (at > 44) {
        fseek(s_sim.f, at, SEEK_SET);
    }
}

/* The device's pulls up to time t: the output and the synth's sound it came
 * from, as a stereo WAV (left the output, right half the synth's sample or
 * -32768). */
static void sim_device_until(double t)
{
    static int16_t out[8192], game[8192], pair[16384];
    const int period = s_sim.sim->period;
    const double step = period / (AUDIO_RATE * (1.0 + s_sim.sim->drift * 1e-6));
    while (s_sim.device_t <= t) {
        native_output_pull(out, game, (size_t)period);
        for (int i = 0; i < period; i++) {
            pair[2 * i] = out[i];
            pair[2 * i + 1] = game[i];
        }
        fwrite(pair, sizeof pair[0], 2 * (size_t)period, s_sim.f);
        s_sim.samples += (uint32_t)period;
        s_sim.device_t += step;
    }
}

static bool sim_in(const long ev[2], long frame)
{
    return ev[0] >= 0 && frame >= ev[0] && frame < ev[0] + ev[1];
}

/* Before frame: the events, then the device up to the loop's start. */
static void sim_frame_begin(long frame, bool *push)
{
    const AudioSim *sim = s_sim.sim;
    const double T = 1.0 / FRAME_HZ;
    if (frame == sim->menu[0]) {
        native_output_hold(NATIVE_OUTPUT_HOLD_MENU, true);
        sim_device_until(s_sim.t += sim->menu[1] * T);
        native_output_hold(NATIVE_OUTPUT_HOLD_MENU, false);
    }
    if (frame == sim->drag[0]) {
        native_output_hold(NATIVE_OUTPUT_HOLD_WINDOW, true);
        sim_device_until(s_sim.t += sim->drag[1] / 1000.0);
        native_output_hold(NATIVE_OUTPUT_HOLD_WINDOW, false);
    }
    if (frame == sim->hitch[0]) {
        s_sim.t += sim->hitch[1] / 1000.0;
    }
    if (frame == sim->load) {
        native_output_flush();
    }
    if (sim->sweep >= 0 && frame >= sim->sweep && frame < sim->sweep + 40) {
        long k = frame - sim->sweep;
        native_output_volume(k < 20 ? 95 - 5 * (int)k : 5 * (int)(k - 19));
    }
    bool fast = sim_in(sim->fast, frame), off = sim_in(sim->off, frame);
    native_output_hold(NATIVE_OUTPUT_HOLD_FAST, fast);
    native_output_hold(NATIVE_OUTPUT_HOLD_OFF, off);
    *push = !fast && !off;
    sim_device_until(s_sim.t);
}

/* After the frame's push: its wait for the deadline and VSync. */
static void sim_frame_end(long frame)
{
    const AudioSim *sim = s_sim.sim;
    const double T = 1.0 / FRAME_HZ;
    bool unpaced = sim_in(sim->fast, frame);
    if (unpaced) {
        s_sim.t += 0.0005;
    } else {
        if (s_sim.t < s_sim.next) {
            s_sim.t = s_sim.next;
        }
        if (sim->refresh > 0) {
            s_sim.t = ceil(s_sim.t * sim->refresh - 1e-9) / sim->refresh;
        }
    }
    s_sim.next += T;
    if (unpaced || s_sim.t > s_sim.next + 4 * T) {
        s_sim.next = s_sim.t;
    }
}

static void sim_report(void)
{
    const NativeOutputStats *st = native_output_stats();
    printf("native player: audio: device_samples=%u played=%llu underruns=%llu starved=%llu dropped=%llu "
           "stretched=%llu shrunk=%llu flushes=%llu over=%llu limited=%llu peak_in=%.2f peak_out=%.2f "
           "max_queue_ms=%.1f\n",
           s_sim.samples, (unsigned long long)st->played, (unsigned long long)st->underruns,
           (unsigned long long)st->starved, (unsigned long long)st->dropped, (unsigned long long)st->stretched,
           (unsigned long long)st->shrunk, (unsigned long long)st->flushes, (unsigned long long)st->over,
           (unsigned long long)st->limited, st->peak_in > 0 ? 20 * log10(st->peak_in / 32768.0) : -999.0,
           st->peak_out > 0 ? 20 * log10(st->peak_out / 32768.0) : -999.0, st->max_queued * 1000.0 / AUDIO_RATE);
}

static uint64_t machine_hash(void)
{
    uint64_t h = 1469598103934665603ull;
    const NativeVideoMemory *m = native_video_memory();
    const uint8_t *parts[2] = { native_cpu_ram(), (const uint8_t *)m };
    const size_t sizes[2] = { 0x800, sizeof *m };
    for (int p = 0; p < 2; p++) {
        for (size_t i = 0; i < sizes[p]; i++) {
            h = (h ^ parts[p][i]) * 1099511628211ull;
        }
    }
    return h ^ native_cycle_count();
}

int player_run_off_screen(long frames, const char *input, int lag, const char *present_out, long present_every,
                   long state_check, const char *wav_out, const char *audio_out, const AudioSim *sim)
{
    enum {
        CHECK_FRAMES = 300
    };

    static uint64_t first[CHECK_FRAMES];
    uint8_t *state = NULL;
    size_t state_size = 0;
    long check_mismatch = -1;
    NativeRoute replay;

    NativeRoute route;
    memset(&route, 0, sizeof route);
    if (input && !native_route_load(&route, input)) {
        return 2;
    }
    FILE *wav = wav_out ? fopen(wav_out, "wb") : NULL;
    if (wav_out && !wav) {
        fprintf(stderr, "cannot write %s\n", wav_out);
        return 2;
    }
    uint32_t wav_samples = 0;
    if (wav) {
        native_audio_reset(AUDIO_RATE);
        native_audio_wav_header(wav, AUDIO_RATE, 0);
    }
    if (audio_out) {
        s_sim.f = fopen(audio_out, "wb");
        if (!s_sim.f) {
            fprintf(stderr, "cannot write %s\n", audio_out);
            return 2;
        }
        s_sim.sim = sim;
        native_audio_reset(AUDIO_RATE);
        native_output_reset(AUDIO_RATE, sim->period);
        native_output_volume(sim->volume);
        sim_wav_header();
    }
    player_power_on();
    if (lag >= 0) {
        native_set_lag_mode((NativeLagMode)lag);
    }
    for (long frame = 0; frame < frames; frame++) {
        uint8_t held[2];
        native_route_frame(&route, frame, held);
        bool push = true;
        if (s_sim.f) {
            sim_frame_begin(frame, &push);
        }
        player_run_frame(held[0], held[1]);
        if (wav) {
            static int16_t pcm[NATIVE_AUDIO_MAX_FRAME_SAMPLES];
            size_t n = player_frame_sound(pcm, NATIVE_AUDIO_MAX_FRAME_SAMPLES, AUDIO_RATE);
            fwrite(pcm, sizeof pcm[0], n, wav);
            wav_samples += (uint32_t)n;
        }
        if (s_sim.f) {
            static double mix[NATIVE_AUDIO_MAX_FRAME_SAMPLES], game[NATIVE_AUDIO_MAX_FRAME_SAMPLES];
            size_t n = player_frame_mix(mix, game, NATIVE_AUDIO_MAX_FRAME_SAMPLES, AUDIO_RATE);
            if (push) {
                native_output_push(mix, game, n);
            }
            sim_frame_end(frame);
        }
        if (state_check >= 0 && frame == state_check) {
            state_size = native_state_save(NULL, 0);
            state = (uint8_t *)malloc(state_size);
            if (state) {
                native_state_save(state, state_size);
            }
            replay = route;
        }
        if (state && frame > state_check && frame <= state_check + CHECK_FRAMES) {
            first[frame - state_check - 1] = machine_hash();
            if (frame == state_check + CHECK_FRAMES) {
                NativeRoute saved = route;
                bool loaded = native_state_load(state, state_size);
                route = replay;
                check_mismatch = loaded ? 0 : state_check;
                for (long f = state_check + 1; loaded && f <= state_check + CHECK_FRAMES; f++) {
                    uint8_t h2[2];
                    native_route_frame(&route, f, h2);
                    player_run_frame(h2[0], h2[1]);
                    if (machine_hash() != first[f - state_check - 1] && !check_mismatch) {
                        check_mismatch = f;
                    }
                }
                route = saved;
                printf("native player: state check after frame %ld: %s over %d frames\n", state_check,
                       !loaded          ? "the state did not load"
                       : check_mismatch ? "differs"
                                        : "identical",
                       CHECK_FRAMES);
                if (check_mismatch) {
                    printf("  first difference at frame %ld\n", check_mismatch);
                }
                free(state);
                state = NULL;
            }
        }
        if (present_out && present_every > 0 && frame % present_every == 0) {
            int w, h;
            const uint32_t *pic = player_picture(&w, &h);
            char path[1100];
            player_numbered_path(path, sizeof path, present_out, frame);
            if (!cyc_write_png(path, pic, w, h)) {
                fprintf(stderr, "cannot write %s\n", path);
            }
        }
    }
    if (wav) {
        native_audio_wav_header(wav, AUDIO_RATE, wav_samples);
        fclose(wav);
    }
    if (s_sim.f) {
        native_output_hold(NATIVE_OUTPUT_HOLD_QUIT, true);
        sim_device_until(s_sim.t + 0.05);
        sim_wav_header();
        fclose(s_sim.f);
        s_sim.f = NULL;
        sim_report();
    }
    printf("native player: frames=%ld cycles=%llu fallback_instructions=%llu unmodeled_reads=%llu\n", frames,
           (unsigned long long)native_cycle_count(), (unsigned long long)native_fallback_instructions(),
           (unsigned long long)native_unmodeled_reads());
    printf("native player: hooks acted:");
    for (int e = 0; e < DT_ENH_COUNT; e++) {
        printf(" %s=%llu", dt_enh_info((DtEnh)e)->key, (unsigned long long)dt_enh_applied((DtEnh)e));
    }
    printf("\n");
    return check_mismatch > 0 ? 1 : 0;
}
