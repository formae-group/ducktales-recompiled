/*
 * native_player.c - the player build on the native runtime (ducktales.exe,
 * docs/NATIVE-RUNTIME.md "The player build"): the program entry's host
 * (host/ducktales_main.c calls ducktales_host_main, as it calls NESRecomp's
 * in the cycle build), which reads the options, config.ini and the ROM and
 * starts the window (native_window.c) or a run with no window
 * (native_offscreen.c), and the frame both play: power-on, one 60 Hz frame
 * on the native bus (native_run_frame), its sound and its picture.
 *
 *   No slowdown   [Game] NoSlowdown: the runtime's lag mode
 *                 (NATIVE_LAG_NONE when on, NATIVE_LAG_FAITHFUL when off)
 *   decompiled    [Game] DecompiledCode, with No slowdown on only and in the
 *   code          window only (off screen runs the recompiled code): the
 *                 decompiled routines in replace mode (decomp/decomp_native.c)
 *   extra voices  [Audio] MusicUnderEffects and OverlapEffects, or
 *                 --music-under-effects and --overlap-effects for one run
 *
 * Options: ROM [--hidden] [--exit-after N] [--config FILE] [--uncapped]
 * [--present-out FILE [--present-every N]] and each enhancement and sprite
 * option (--jump-scale 150 ...). Off screen (no window opens):
 * --frames N [--input ROUTE] [--lag none|faithful] [--present-out FILE
 * [--present-every N]] writes the picture the window would show (at 1x, the
 * presentation settings at their defaults) after every Nth frame, named as
 * the cycle host's --present-out names them, for tests/native_player_test.py;
 * --wav-out FILE writes the synth's sound as 48 kHz mono 16-bit PCM, before
 * the window's volume and queue stretching. --audio-out FILE writes what the
 * audio device would play instead, through the window's output stage
 * (native_output.h) on simulated clocks (--audio-sim SPEC,
 * native_offscreen.c), as a stereo WAV for tests/audio_output_test.py, and
 * prints the output stage's statistics.
 * --state-check F saves the machine after frame F, runs 300 frames, loads the
 * state and runs them again, and reports whether CPU RAM, the cycle count and
 * the video memories matched on every one. The summary line lists how often
 * each enhancement's hook acted.
 */
#include "native/native_audio.h"
#include "native/native_player.h"
#include "native/native_present.h"
#include "native/native_runtime.h"

#include "decomp/decomp.h"
#include "ducktales/events.h"
#include "ducktales/tune.h"
#include "game/enhancements.h"
#include "host/ducktales_host.h"
#include "host/ducktales_sprites.h"
#include "mods/app_mods.h"
#include "ui/app_ui.h"

#include "cyc_render.h"
#include "cyc_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ducktales_host_main(int argc, char **argv);

CycSettings player_settings;
char player_settings_path[1100];

void player_game_save(void *ctx, FILE *f)
{
    (void)ctx;
    ducktales_host_write_game_settings(f);
}

static void game_load(void *ctx, const char *key, const char *value)
{
    (void)ctx;
    ducktales_host_load_game_setting(key, value);
}

void ducktales_host_apply_no_slowdown(bool on)
{
    native_set_lag_mode(on ? NATIVE_LAG_NONE : NATIVE_LAG_FAITHFUL);
    Decomp_SetMode(on && ducktales_host_decompiled() ? DECOMP_REPLACE : DECOMP_OFF);
}

uint8_t *player_read_file(const char *path, size_t *size)
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

uint8_t player_frame_mute;

void player_power_on(void)
{
    native_power_on();
    dt_sprites_forget();
    app_mods_reset();
    player_frame_mute = 0;
    ducktales_host_apply_no_slowdown(ducktales_host_no_slowdown());
    if (ducktales_host_no_slowdown()) {
        printf("[ducktales] No slowdown: the native runtime holds VBlank until each frame's logic is done\n");
    }
    if (ducktales_host_no_slowdown() && ducktales_host_decompiled()) {
        printf("[ducktales] Decompiled code: the routines decompiled to C run in place of the recompiled ones\n");
    }
    fflush(stdout);
}

unsigned long long player_frames, player_decomp_frames;

void player_run_frame(uint8_t pad0, uint8_t pad1)
{
    player_frames++;
    if (Decomp_Mode() == DECOMP_REPLACE) {
        player_decomp_frames++;
    }
    native_set_controller(0, pad0);
    native_set_controller(1, pad1);
    uint8_t mute = app_mods_audio_mute();
    native_run_frame();
    native_present_frame(native_last_frame());
    dt_sprites_frame_end();
    player_frame_mute = mute;
    if (app_mods_audio_active()) {
        app_mods_audio_frame_end(native_cpu_ram());
    }
    Events_FrameEnd();
    app_mods_scripts_frame();
    Tune_FrameEnd();
    app_mods_dump_frame();
}

/* What the window shows (ui/app_present.h, or the plain picture) for a
 * drawable of out_w x out_h; 0 x 0 for the picture at 1x. */
const uint32_t *player_picture(int *w, int *h)
{
    const uint32_t *p = app_ui_present(w, h);
    return p && *w > 0 && *h > 0 ? p : cyc_render_present(w, h);
}

void player_numbered_path(char *out, size_t cap, const char *base, long frame)
{
    const char *dot = strrchr(base, '.');
    const char *slash = strrchr(base, '/'), *bslash = strrchr(base, '\\');
    if (dot && ((slash && dot < slash) || (bslash && dot < bslash))) {
        dot = NULL;
    }
    int stem = (int)(dot ? (size_t)(dot - base) : strlen(base));
    snprintf(out, cap, "%.*s_%05ld%s", stem, base, frame, dot ? dot : ".png");
}

/* --music-under-effects / --overlap-effects for this run, else -1: the saved
 * [Audio] value (ducktales_host.h). */
static int s_voice_override[2] = { -1, -1 };

static void apply_voices(void)
{
    native_audio_voices(s_voice_override[0] >= 0 ? s_voice_override[0] != 0 : ducktales_host_music_under_effects(),
                        s_voice_override[1] >= 0 ? s_voice_override[1] != 0 : ducktales_host_overlap_effects());
}

/* The frame's sound: the synth and its extra voices with the channels the
 * mods took muted, then their files mixed in (mods/app_mods.h). */
size_t player_frame_sound(int16_t *pcm, size_t cap, int rate)
{
    apply_voices();
    native_audio_set_mute(player_frame_mute);
    size_t n = native_audio_frame(native_last_frame(), pcm, cap);
    app_mods_audio_mix(pcm, n, rate);
    return n;
}

/* The same before the 16-bit clamp, for the output stage (native_output.h);
 * game, when not NULL, gets the synth's part alone. */
size_t player_frame_mix(double *mix, double *game, size_t cap, int rate)
{
    apply_voices();
    native_audio_set_mute(player_frame_mute);
    size_t n = native_audio_mix(native_last_frame(), mix, cap);
    if (game) {
        memcpy(game, mix, n * sizeof *mix);
    }
    app_mods_audio_add(mix, n, rate);
    return n;
}

static bool game_option_name(const char *name)
{
    for (int e = 0; e < DT_ENH_COUNT; e++) {
        if (!strcmp(name, dt_enh_info((DtEnh)e)->option)) {
            return true;
        }
    }
    return !strcmp(name, DT_SPRITES_OPTION);
}

static int usage(const char *self)
{
    fprintf(stderr,
            "usage: %s ROM [--hidden] [--exit-after N] [--config FILE] [--uncapped]\n"
            "       [--present-out FILE [--present-every N]]\n"
            "       [--frames N [--input ROUTE] [--lag none|faithful] [--state-check FRAME] [--wav-out FILE]\n"
            "        [--audio-out FILE [--audio-sim SPEC]]]\n"
            "       [--jump-scale N] [--no-damage on|off] [--infinite-time on|off] [--infinite-lives on|off]\n"
            "       [--start-lives 0-9|off] [--no-sprite-flicker on|off]\n"
            "       [--music-under-effects on|off] [--overlap-effects on|off]\n"
            "       [--mods on|off] [--mods-dir DIR] [--mods-order ID:on|off,...] [--mods-dump DIR]\n",
            self);
    return 2;
}

int ducktales_host_main(int argc, char **argv)
{
    const char *rom = NULL, *present_out = NULL, *input = NULL, *config = NULL, *wav_out = NULL, *audio_out = NULL;
    AudioSim sim;
    player_parse_audio_sim("refresh=0", &sim);
    long exit_after = -1, present_every = 1, frames = -1, state_check = -1;
    int lag = -1;
    bool hidden = false, uncapped = false, mod_ok = false;

    struct {
        const char *name, *value;
    } game[16];

    int game_n = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--hidden")) {
            hidden = true;
        } else if (!strcmp(a, "--uncapped")) {
            uncapped = true;
        } else if (!strcmp(a, "--window-input") || !strcmp(a, "--virtual-pad")) {
            continue;
        } else if (!strcmp(a, "--exit-after") && v) {
            exit_after = atol(argv[++i]);
        } else if (!strcmp(a, "--config") && v) {
            config = argv[++i];
        } else if (!strcmp(a, "--present-out") && v) {
            present_out = argv[++i];
        } else if (!strcmp(a, "--present-every") && v) {
            present_every = atol(argv[++i]);
        } else if (!strcmp(a, "--frames") && v) {
            frames = atol(argv[++i]);
        } else if (!strcmp(a, "--input") && v) {
            input = argv[++i];
        } else if (!strcmp(a, "--state-check") && v) {
            state_check = atol(argv[++i]);
        } else if (!strcmp(a, "--wav-out") && v) {
            wav_out = argv[++i];
        } else if (!strcmp(a, "--audio-out") && v) {
            audio_out = argv[++i];
        } else if (!strcmp(a, "--audio-sim") && v) {
            if (!player_parse_audio_sim(argv[++i], &sim)) {
                fprintf(stderr, "--audio-sim %s: invalid\n", v);
                return 2;
            }
        } else if (!strncmp(a, "--mods", 6) && v && app_mods_option(a, v, &mod_ok)) {
            i++;
            if (!mod_ok) {
                fprintf(stderr, "%s %s: invalid value\n", a, v);
                return 2;
            }
        } else if ((!strcmp(a, "--music-under-effects") || !strcmp(a, "--overlap-effects")) && v &&
                   (!strcmp(v, "on") || !strcmp(v, "off"))) {
            s_voice_override[a[2] == 'o'] = !strcmp(argv[++i], "on");
        } else if (!strcmp(a, "--lag") && v && !strcmp(v, "none")) {
            lag = NATIVE_LAG_NONE, i++;
        } else if (!strcmp(a, "--lag") && v && !strcmp(v, "faithful")) {
            lag = NATIVE_LAG_FAITHFUL, i++;
        } else if (a[0] == '-' && v && game_n < 16 && game_option_name(a)) {
            game[game_n].name = a, game[game_n++].value = argv[++i];
        } else if (a[0] != '-' && !rom) {
            rom = a;
        } else {
            return usage(argv[0]);
        }
    }
    if (!rom) {
        return usage(argv[0]);
    }
    size_t size;
    uint8_t *image = player_read_file(rom, &size);
    char err[160];
    if (!image) {
        fprintf(stderr, "cannot read %s\n", rom);
        return 2;
    }
    if (!native_load_ines(image, size, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", rom, err);
        free(image);
        return 2;
    }
    bool off_screen = frames >= 0;
    size_t prg_at = 16 + ((image[6] & 4) ? 512 : 0), prg_size = (size_t)image[4] * 0x4000;
    app_mods_start(prg_at + prg_size <= size ? image + prg_at : NULL, prg_at + prg_size <= size ? prg_size : 0);
    free(image);
    if (!native_hooks_init()) {
        return 2;
    }
    if (!off_screen) {
        snprintf(player_settings_path, sizeof player_settings_path, "%s",
                 config ? config : cyc_settings_default_path());
        static const CycSettingsGame GAME = { NULL, game_load, player_game_save };
        cyc_settings_default(&player_settings);
        cyc_settings_load(&player_settings, player_settings_path, stderr, &GAME);
        ducktales_host_load_audio_settings(player_settings_path);
        player_settings.bind.shortcut_off |= 1u << CYC_SC_DISK;
    } else {
        ducktales_host_set_decompiled(false);
    }
    for (int g = 0; g < game_n; g++) {
        bool ok = false;
        ducktales_host_game_option(game[g].name, game[g].value, &ok);
        if (!ok) {
            fprintf(stderr, "%s %s: invalid value\n", game[g].name, game[g].value);
            return 2;
        }
    }
    dt_sprites_mods_changed();
    if (off_screen) {
        if (wav_out && audio_out) {
            fprintf(stderr, "--wav-out and --audio-out cannot be used together\n");
            return 2;
        }
        return player_run_off_screen(frames, input, lag, present_out, present_every, state_check, wav_out, audio_out,
                                     &sim);
    }
    player_power_on();
    return player_run_window(hidden, uncapped, exit_after, present_out, present_every);
}
