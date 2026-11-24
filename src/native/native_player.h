/*
 * native_player.h - what the files of the player build on the native runtime
 * (ducktales.exe, docs/NATIVE-RUNTIME.md "The player build") share; no other
 * file includes it.
 *
 *   native_player.c     the program entry's host (ducktales_host_main): the
 *                       options, config.ini, the ROM, and the frame both kinds
 *                       of run play with its sound and picture
 *   native_window.c     the window
 *   native_offscreen.c  the runs with no window (--frames N): pictures, sound
 *                       files, --audio-sim and --state-check
 *
 * The settings (config.ini, player_settings) and options are
 * native_player.c's; it owns nothing itself.
 */
#pragma once
#include "cyc_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FRAME_HZ   60.0988
#define AUDIO_RATE 48000

/* --audio-sim SPEC (native_offscreen.c has the account). */
typedef struct {
    int refresh, period, volume;
    double drift;
    long menu[2], fast[2], drag[2], hitch[2], off[2], load, sweep;
} AudioSim;

/* config.ini as the window uses it, and the file. */
extern CycSettings player_settings;
extern char player_settings_path[1100];
void player_game_save(void *ctx, FILE *f);

/* The channels the mods muted at the last frame, and the frames played, all
 * and those that ran the decompiled code. */
extern uint8_t player_frame_mute;
extern unsigned long long player_frames, player_decomp_frames;

uint8_t *player_read_file(const char *path, size_t *size);
void player_power_on(void);
void player_run_frame(uint8_t pad0, uint8_t pad1);
const uint32_t *player_picture(int *w, int *h);
void player_numbered_path(char *out, size_t cap, const char *base, long frame);
size_t player_frame_sound(int16_t *pcm, size_t cap, int rate);
size_t player_frame_mix(double *mix, double *game, size_t cap, int rate);

bool player_parse_audio_sim(const char *spec, AudioSim *sim);
int player_run_off_screen(long frames, const char *input, int lag, const char *present_out, long present_every,
                          long state_check, const char *wav_out, const char *audio_out, const AudioSim *sim);
int player_run_window(bool hidden, bool uncapped, long exit_after, const char *present_out, long present_every);
