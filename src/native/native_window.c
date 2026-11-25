/*
 * native_window.c - the player build's window on the native runtime
 * (docs/NATIVE-RUNTIME.md "The player build"): the recompiled program runs on
 * the native bus one 60 Hz frame at a time (native_run_frame), the native
 * renderer draws the picture (native_present.c) and the native synth
 * (native_audio.h) the sound. No part of NESRecomp's cycle host is linked.
 *
 * Its entry, native_player.c, is the program entry's host
 * (host/ducktales_main.c calls ducktales_host_main, as it calls NESRecomp's
 * in the cycle build), and the window reaches the port through the same
 * calls the cycle host's extras make (host/ducktales_host.c), so the menu,
 * ROM import, settings, bindings and presentation in src/ui and the
 * enhancements and sprite compositor in src/game and src/host are the ones
 * the cycle build runs:
 *
 *   settings      config.ini through NESRecomp's cyc_settings (the same
 *                 file and keys); written on F11 and at exit, then
 *                 reconciled by app_settings.c as after the cycle host's saves
 *   input         cyc_input_eval of the player's bindings each frame, then
 *                 app_ui_input; Escape (the Menu binding) opens the menu
 *                 through app_ui_event; F11 and Alt+Enter full screen, Tab
 *                 (held) fast-forward, F12 a screenshot of the picture
 *   menu          the open menu calls menu_tick each menu frame: the game
 *                 frames due at 60.0988 Hz run with no buttons pressed and
 *                 their sound is queued, so the game keeps running behind the
 *                 menu ([Game] MenuPausesGame stops it and holds the sound,
 *                 app_ui.h)
 *   decompiled    [Game] DecompiledCode, with No slowdown on only and in the
 *   code          window only (off screen runs the recompiled code): the
 *                 decompiled routines in replace mode (decomp/decomp_native.c);
 *                 a window session that ran them writes
 *                 decomp-session-<time>.txt at exit beside the cycle host's
 *                 replays (NESRECOMP_REPLAY_DIR, NESRECOMP_DIAGNOSTICS_DIR or
 *                 <exe dir>/diagnostics): frames, each routine's calls and
 *                 the decomp error that stopped the game, if any
 *   picture       app_ui_present (ui/app_present.h) or the plain picture, in
 *                 a texture the size of the picture, logical size and integer
 *                 scaling as the cycle host keeps them
 *   pacing        each frame at its deadline (1 / 60.0988 s); VSync as
 *                 [Display] VSync says (FramePacing resolves it at launch),
 *                 off while fast-forwarding or --uncapped
 *   sound         native_audio_mix per frame, with the mods' files, through
 *                 the output stage (native_output.h: volume ramp, limiter,
 *                 queue pacing, fades) to the device's callback at 48 kHz.
 *                 The sound is held (faded out, nothing queued) while
 *                 fast-forwarding, with [Audio] off, while the menu pauses
 *                 the game, and from a click on the window's frame (a move,
 *                 a resize or its buttons, which Windows runs in a loop of
 *                 its own that stops the frame loop) until the next frame;
 *                 F9 fades out and drops what was queued; closing fades out
 *                 before the device closes
 *   scheduling    Windows: no power throttling (EcoQoS, timer coalescing),
 *                 the frame loop in the MMCSS "Games" class and a 1 ms timer,
 *                 so an unfocused window keeps its pace
 *   save states   F8 saves and F9 loads <exe dir>/saves/DuckTales.native-state:
 *                 CPU, bus, video state and the frame count (native_runtime.h)
 *
 * The player is three files that share native_player.h: native_player.c
 * (the entry, its options and the frame both kinds of run play), this window
 * and native_offscreen.c (the runs with no window, --frames N).
 */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include "native/native_present.h"
#include "native/native_runtime.h"
#include "native/native_state.h"
#include "native/native_audio.h"
#include "native/native_output.h"
#include "native/native_player.h"

#include "decomp/decomp.h"
#include "ducktales/events.h"
#include "game/enhancements.h"
#include "host/ducktales_host.h"
#include "host/ducktales_sprites.h"
#include "mods/app_mods.h"
#include "ui/app_ui.h"

#include "cpu6502.h"
#include "cyc_input.h"
#include "cyc_overlay.h"
#include "cyc_png.h"
#include "cyc_recomp.h"
#include "cyc_render.h"
#include "cyc_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <avrt.h>
#include <timeapi.h>
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif
#endif

#define STATE_MAGIC   "DTNSTATE"
#define STATE_VERSION 1u

/* ---- the window ---- */

static SDL_Window *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture *s_tex;
static int s_tex_w, s_tex_h;
static bool s_vsync_active;
static SDL_GameController *s_pads[CYC_INPUT_MAX_PADS];
static char s_pad_guid[CYC_INPUT_MAX_PADS][40];
static int s_pad_count;
static bool s_fullscreen_chord;
static char s_note_title[64], s_note_body[128];
static uint64_t s_note_until;

static void save_settings(void)
{
    static const CycSettingsGame GAME = { NULL, NULL, player_game_save };
    if (!cyc_settings_save(&player_settings, player_settings_path, &GAME)) {
        fprintf(stderr, "cannot write %s\n", player_settings_path);
    }
    app_ui_host_saved();
}

static void note(const char *title, const char *body)
{
    snprintf(s_note_title, sizeof s_note_title, "%s", title);
    snprintf(s_note_body, sizeof s_note_body, "%s", body ? body : "");
    s_note_until = SDL_GetTicks64() + 2500;
    printf("[ducktales] %s%s%s\n", title, body && *body ? ": " : "", body ? body : "");
    fflush(stdout);
}

static char s_session_dir[1100], s_session_path[1200], s_session_start[32];

/* At exit (an exit from a decomp error included), when the decompiled code
 * ran in this window session. */
static void session_end(void)
{
    if (!player_decomp_frames) {
        return;
    }
#ifdef _WIN32
    _mkdir(s_session_dir);
#endif
    FILE *f = fopen(s_session_path, "w");
    if (!f) {
        return;
    }
    const char *error = Decomp_Error();
    fprintf(f, "DuckTales Recompiled native player: decompiled code session\nstarted %s\n", s_session_start);
    fprintf(f, "frames %llu, %llu of them with the decompiled code on\n", player_frames, player_decomp_frames);
    fprintf(f, "error %s\n", *error ? error : "none");
    Decomp_WriteStats(f, DECOMP_REPLACE);
    fclose(f);
}

static void session_begin(void)
{
    const char *dir = getenv("NESRECOMP_REPLAY_DIR");
    if (!dir || !*dir) {
        dir = getenv("NESRECOMP_DIAGNOSTICS_DIR");
    }
    if (dir && *dir) {
        snprintf(s_session_dir, sizeof s_session_dir, "%s", dir);
    } else {
        char *base = SDL_GetBasePath();
        snprintf(s_session_dir, sizeof s_session_dir, "%sdiagnostics", base ? base : "");
        SDL_free(base);
    }
    time_t now = time(NULL);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    strftime(s_session_start, sizeof s_session_start, "%Y-%m-%d %H:%M:%S", localtime(&now));
    snprintf(s_session_path, sizeof s_session_path, "%s/decomp-session-%s.txt", s_session_dir, stamp);
    atexit(session_end);
}

static void state_path(char *out, size_t cap)
{
    char dir[1100];
    char *base = SDL_GetBasePath();
    snprintf(dir, sizeof dir, "%ssaves", base ? base : "");
    if (base) {
        SDL_free(base);
    }
#ifdef _WIN32
    _mkdir(dir);
#endif
    snprintf(out, cap, "%s/DuckTales.native-state", dir);
}

static void save_state(void)
{
    char path[1200];
    state_path(path, sizeof path);
    size_t size = native_state_save(NULL, 0);
    uint8_t *blob = (uint8_t *)malloc(size);
    FILE *f = blob ? fopen(path, "wb") : NULL;
    bool ok = false;
    if (f) {
        uint32_t head[3] = { STATE_VERSION, native_prg_hash(), (uint32_t)size };
        native_state_save(blob, size);
        ok = fwrite(STATE_MAGIC, 1, 8, f) == 8 && fwrite(head, sizeof head, 1, f) == 1 &&
             fwrite(blob, 1, size, f) == size;
        ok = fclose(f) == 0 && ok;
    }
    free(blob);
    note(ok ? "STATE SAVED" : "STATE NOT SAVED", ok ? NULL : path);
}

static void load_state(void)
{
    char path[1200], why[160] = "";
    state_path(path, sizeof path);
    size_t size = 0;
    uint8_t *data = player_read_file(path, &size);
    uint32_t head[3];
    if (!data) {
        snprintf(why, sizeof why, "no saved state");
    } else if (size < 8 + sizeof head || memcmp(data, STATE_MAGIC, 8)) {
        snprintf(why, sizeof why, "not a state file");
    } else {
        memcpy(head, data + 8, sizeof head);
        if (head[0] != STATE_VERSION || head[1] != native_prg_hash() || head[2] != size - 8 - sizeof head) {
            snprintf(why, sizeof why, "saved by another version");
        } else if (!native_state_load(data + 8 + sizeof head, size - 8 - sizeof head)) {
            snprintf(why, sizeof why, "the state does not fit this build");
        }
    }
    free(data);
    if (!why[0]) {
        dt_sprites_forget();
        app_mods_reset();
        player_frame_mute = 0;
    }
    note(why[0] ? "STATE NOT LOADED" : "STATE LOADED", why[0] ? why : NULL);
}

static void open_pads(void)
{
    for (int i = 0; i < s_pad_count; i++) {
        SDL_GameControllerClose(s_pads[i]);
    }
    s_pad_count = 0;
    for (int i = 0; i < SDL_NumJoysticks() && s_pad_count < CYC_INPUT_MAX_PADS; i++) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
        SDL_GameController *c = SDL_GameControllerOpen(i);
        if (!c) {
            continue;
        }
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(c)), s_pad_guid[s_pad_count],
                                  sizeof s_pad_guid[0]);
        s_pads[s_pad_count++] = c;
    }
}

/* One frame of device state, as the cycle host reads it (cyc_sdl.c): the
 * Enter of an Alt+Enter stays the full-screen chord's until it is released. */
static void read_frame(CycInputFrame *f)
{
    static uint8_t keys[SDL_NUM_SCANCODES];
    memset(f, 0, sizeof *f);
    int n = 0;
    const Uint8 *real = SDL_GetKeyboardState(&n);
    for (int i = 0; i < SDL_NUM_SCANCODES; i++) {
        keys[i] = (uint8_t)(i < n && real[i]);
    }
    bool alt = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
    bool enter = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER];
    if (!enter) {
        s_fullscreen_chord = false;
    } else if (alt) {
        s_fullscreen_chord = true;
    }
    if (s_fullscreen_chord && !alt) {
        keys[SDL_SCANCODE_RETURN] = keys[SDL_SCANCODE_KP_ENTER] = 0;
    }
    f->keys = keys;
    f->key_count = SDL_NUM_SCANCODES;
    f->pad_count = s_pad_count;
    for (int i = 0; i < s_pad_count; i++) {
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            if (SDL_GameControllerGetButton(s_pads[i], (SDL_GameControllerButton)b)) {
                f->pad_buttons[i] |= 1u << b;
            }
        }
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX && a < CYC_INPUT_PAD_AXES; a++) {
            f->pad_axes[i][a] = SDL_GameControllerGetAxis(s_pads[i], (SDL_GameControllerAxis)a);
        }
        f->pad_guid[i] = s_pad_guid[i];
    }
}

static void set_vsync(bool on)
{
    if (on == s_vsync_active) {
        return;
    }
    if (SDL_RenderSetVSync(s_ren, on ? 1 : 0) == 0) {
        s_vsync_active = on;
    }
}

static void apply_integer_scale(void)
{
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(s_ren, &ow, &oh);
    SDL_bool want = player_settings.integer_scale && ow >= s_tex_w && oh >= s_tex_h ? SDL_TRUE : SDL_FALSE;
    if (SDL_RenderGetIntegerScale(s_ren) != want) {
        SDL_RenderSetIntegerScale(s_ren, want);
    }
}

static bool ensure_texture(int w, int h)
{
    if (s_tex && w == s_tex_w && h == s_tex_h) {
        return true;
    }
    if (s_tex) {
        SDL_DestroyTexture(s_tex);
    }
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    s_tex_w = w;
    s_tex_h = h;
    if (!s_tex) {
        return false;
    }
    SDL_SetTextureScaleMode(s_tex, player_settings.linear_filter ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    SDL_RenderSetLogicalSize(s_ren, w, h);
    apply_integer_scale();
    return true;
}

/* The windowed size: the picture at 1x times the window scale, the largest
 * multiple that fits the display's usable area. */
static void window_size(int display, int *w, int *h)
{
    int pw, ph, scale = player_settings.window_scale < 1 ? 1 : player_settings.window_scale;
    player_picture(&pw, &ph);
    SDL_Rect area;
    if (display >= 0 && SDL_GetDisplayUsableBounds(display, &area) == 0) {
        while (scale > 1 && (pw * scale > area.w || ph * scale > area.h - 48)) {
            --scale;
        }
    }
    *w = pw * scale;
    *h = ph * scale;
}

static void apply_fullscreen(void)
{
    Uint32 fs = player_settings.fullscreen == 1   ? SDL_WINDOW_FULLSCREEN_DESKTOP
                : player_settings.fullscreen == 2 ? SDL_WINDOW_FULLSCREEN
                                                  : 0;
    if ((SDL_GetWindowFlags(s_win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) == fs) {
        return;
    }
    SDL_SetWindowFullscreen(s_win, fs);
    if (!fs) {
        int w, h;
        window_size(SDL_GetWindowDisplayIndex(s_win), &w, &h);
        SDL_SetWindowSize(s_win, w, h);
    }
}

static void show(void)
{
    int ow = 0, oh = 0, w, h;
    SDL_GetRendererOutputSize(s_ren, &ow, &oh);
    const uint32_t *pic = app_ui_present(&w, &h);
    if (!pic || w <= 0 || h <= 0) {
        pic = cyc_render_present(&w, &h);
    }
    if (!ensure_texture(w, h)) {
        return;
    }
    if (s_note_until > SDL_GetTicks64()) {
        static uint32_t *copy;
        static size_t copy_cap;
        size_t need = (size_t)w * (size_t)h;
        if (need > copy_cap) {
            free(copy);
            copy = (uint32_t *)malloc(need * sizeof *copy);
            copy_cap = copy ? need : 0;
        }
        if (copy) {
            memcpy(copy, pic, need * sizeof *copy);
            cyc_overlay_toast(copy, w, h, s_note_title, s_note_body);
            pic = copy;
        }
    }
    SDL_UpdateTexture(s_tex, NULL, pic, w * 4);
    SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 255);
    SDL_RenderClear(s_ren);
    apply_integer_scale();
    SDL_Rect dst = { 0, 0, w, h };
    SDL_RenderCopy(s_ren, s_tex, NULL, &dst);
}

static void present_shot(const char *base, long frame)
{
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(s_ren, &w, &h);
    uint32_t *px = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
    if (!px) {
        return;
    }
    if (SDL_RenderReadPixels(s_ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4) == 0) {
        char path[1100];
        player_numbered_path(path, sizeof path, base, frame);
        if (!cyc_write_png(path, px, w, h)) {
            fprintf(stderr, "cannot write %s\n", path);
        }
    }
    free(px);
}

#ifdef _WIN32
static HANDLE s_mmcss;

/* Windows lowers an unfocused process: power throttling (EcoQoS, E-cores on
 * hybrid CPUs), coarser timers and no foreground boost. The frame loop opts
 * out so frames stay on time and the audio queue fed. */
static void scheduling_begin(void)
{
    PROCESS_POWER_THROTTLING_STATE state = { 0 };
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    state.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state);
    timeBeginPeriod(1);
    DWORD task = 0;
    s_mmcss = AvSetMmThreadCharacteristicsW(L"Games", &task);
}

static void scheduling_end(void)
{
    if (s_mmcss) {
        AvRevertMmThreadCharacteristics(s_mmcss);
    }
    s_mmcss = NULL;
    timeEndPeriod(1);
}
#else
static void scheduling_begin(void)
{
}

static void scheduling_end(void)
{
}
#endif

static SDL_AudioDeviceID s_dev;
static SDL_AudioSpec s_have;

static void SDLCALL audio_callback(void *data, Uint8 *stream, int len)
{
    (void)data;
    native_output_pull((int16_t *)stream, NULL, (size_t)len / sizeof(int16_t));
}

static void audio_hold(unsigned reasons, bool on)
{
    if (s_dev) {
        SDL_LockAudioDevice(s_dev);
        native_output_hold(reasons, on);
        SDL_UnlockAudioDevice(s_dev);
    }
}

/* The frame's sound to the output stage; a frame that runs releases the
 * holds of the menu and of the window's own loops. */
static void queue_audio(bool fast)
{
    static double mix[NATIVE_AUDIO_MAX_FRAME_SAMPLES];
    size_t n = player_frame_mix(mix, NULL, NATIVE_AUDIO_MAX_FRAME_SAMPLES, s_dev ? s_have.freq : AUDIO_RATE);
    if (!s_dev) {
        return;
    }
    int volume = app_ui_volume();
    SDL_LockAudioDevice(s_dev);
    native_output_volume(volume >= 0 ? volume : player_settings.volume);
    native_output_hold(NATIVE_OUTPUT_HOLD_MENU | NATIVE_OUTPUT_HOLD_WINDOW, false);
    native_output_hold(NATIVE_OUTPUT_HOLD_FAST, fast);
    native_output_hold(NATIVE_OUTPUT_HOLD_OFF, !player_settings.audio_enabled);
    native_output_push(mix, NULL, n);
    SDL_UnlockAudioDevice(s_dev);
}

#ifdef _WIN32
/* A click on the window's frame starts one of Windows' own loops (a move, a
 * resize, a caption button) inside the event pump, and no frame runs until
 * it ends: the sound is held before it starts instead of running dry. */
static void SDLCALL window_message(void *data, void *hwnd, unsigned int message, Uint64 wparam, Sint64 lparam)
{
    (void)data, (void)hwnd, (void)wparam, (void)lparam;
    if (message == WM_NCLBUTTONDOWN || message == WM_NCRBUTTONDOWN) {
        audio_hold(NATIVE_OUTPUT_HOLD_WINDOW, true);
    }
}
#endif

static int menu_tick(bool run)
{
    static Uint64 next;
    audio_hold(NATIVE_OUTPUT_HOLD_MENU, !run);
    if (!run) {
        return 0;
    }
    const Uint64 freq = SDL_GetPerformanceFrequency(), frame_ticks = (Uint64)((double)freq / FRAME_HZ);
    const Uint64 now = SDL_GetPerformanceCounter();
    if (next == 0 || now > next + 4 * frame_ticks) {
        next = now;
    }
    int ran = 0;
    while (next <= now) {
        player_run_frame(0, 0);
        queue_audio(false);
        next += frame_ticks;
        ran++;
    }
    return ran;
}

int player_run_window(bool hidden, bool uncapped, long exit_after, const char *present_out, long present_every)
{
    if (hidden) {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    session_begin();
    const char *title = cyc_native_display_name ? cyc_native_display_name : "DuckTales";
    int w, h;
    window_size(0, &w, &h);
    s_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (hidden ? SDL_WINDOW_HIDDEN : 0));
    s_ren = s_win ? SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (s_win && !s_ren) {
        s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!s_ren) {
        char message[600];
        snprintf(message, sizeof message, "Could not create the game window or renderer: %s", SDL_GetError());
        fprintf(stderr, "%s\n", message);
        if (!hidden) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cannot start game", message, NULL);
        }
        SDL_Quit();
        return 1;
    }
    set_vsync(player_settings.vsync && !uncapped);
    apply_fullscreen();

    SDL_AudioSpec want = { 0 };
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audio_callback;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &s_have, 0);
    s_dev = dev;
    native_audio_reset(dev ? s_have.freq : AUDIO_RATE);
    native_output_reset(dev ? s_have.freq : AUDIO_RATE, dev ? s_have.samples : 1024);
#ifdef _WIN32
    SDL_SetWindowsMessageHook(window_message, NULL);
#endif
    app_ui_set_menu_tick(menu_tick);
    if (dev) {
        SDL_PauseAudioDevice(dev, 0);
    } else {
        fprintf(stderr, "SDL audio: %s (continuing without sound)\n", SDL_GetError());
    }
    open_pads();
    scheduling_begin();

    const Uint64 freq = SDL_GetPerformanceFrequency(), frame_ticks = (Uint64)((double)freq / FRAME_HZ);
    Uint64 next = SDL_GetPerformanceCounter(), shown = 0;
    long frames_done = 0, loops = 0;
    int shot = 0;
    bool running = true, prev[CYC_SC_COUNT] = { false };
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) &&
                ((ev.key.keysym.mod & KMOD_ALT) || s_fullscreen_chord) &&
                (ev.key.keysym.scancode == SDL_SCANCODE_RETURN || ev.key.keysym.scancode == SDL_SCANCODE_KP_ENTER)) {
                continue;
            }
            app_ui_event(&ev);
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                open_pads();
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat && ev.key.keysym.scancode == SDL_SCANCODE_F8) {
                save_state();
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat && ev.key.keysym.scancode == SDL_SCANCODE_F9) {
                load_state();
                if (dev) {
                    SDL_LockAudioDevice(dev);
                    native_output_flush();
                    SDL_UnlockAudioDevice(dev);
                }
            }
        }
        if (!running) {
            break;
        }

        CycInputFrame in;
        CycInputState st;
        read_frame(&in);
        cyc_input_eval(&player_settings.bind, &in, &st);
        bool edge[CYC_SC_COUNT];
        for (int i = 0; i < CYC_SC_COUNT; i++) {
            edge[i] = st.shortcut[i] && !prev[i];
            prev[i] = st.shortcut[i];
        }
        if (edge[CYC_SC_FULLSCREEN]) {
            player_settings.fullscreen = player_settings.fullscreen ? 0 : 1;
            audio_hold(NATIVE_OUTPUT_HOLD_WINDOW, true);
            apply_fullscreen();
            save_settings();
        }
        if (edge[CYC_SC_SCREENSHOT]) {
            int pw, ph;
            const uint32_t *pic = player_picture(&pw, &ph);
            char name[64];
            snprintf(name, sizeof name, "cyc_shot_%04d.png", shot++);
            if (cyc_write_png(name, pic, pw, ph)) {
                printf("saved %s\n", name);
            }
        }
        uint8_t buttons[2] = { st.buttons[0], st.buttons[1] };
        app_ui_input(buttons);
        app_ui_frame_begin();
        player_run_frame(buttons[0], buttons[1]);
        frames_done++;
        bool fast = st.shortcut[CYC_SC_FAST_FORWARD];
        queue_audio(fast);

        bool unpaced = fast || uncapped;
        set_vsync(player_settings.vsync && !unpaced);
        Uint64 now = SDL_GetPerformanceCounter();
        if (!unpaced || now - shown >= frame_ticks) {
            if (!unpaced && now < next) {
                Uint32 wait_ms = (Uint32)((next - now) * 1000 / freq);
                if (wait_ms > 1) {
                    SDL_Delay(wait_ms - 1);
                }
                while (SDL_GetPerformanceCounter() < next) {
                }
            }
            show();
            if (present_out && present_every > 0 && loops % present_every == 0) {
                present_shot(present_out, frames_done);
            }
            SDL_RenderPresent(s_ren);
            shown = SDL_GetPerformanceCounter();
        }
        loops++;
        next += frame_ticks;
        now = SDL_GetPerformanceCounter();
        if (unpaced || now > next + 4 * frame_ticks) {
            next = now;
        }
        if (exit_after > 0 && frames_done >= exit_after) {
            running = false;
        }
    }
    scheduling_end();
    app_ui_set_menu_tick(NULL);
#ifdef _WIN32
    SDL_SetWindowsMessageHook(NULL, NULL);
#endif
    save_settings();
    if (dev) {
        audio_hold(NATIVE_OUTPUT_HOLD_QUIT, true);
        for (int i = 0; i < 50; i++) {
            SDL_LockAudioDevice(dev);
            bool quiet = native_output_quiet();
            SDL_UnlockAudioDevice(dev);
            if (quiet) {
                break;
            }
            SDL_Delay(2);
        }
        SDL_CloseAudioDevice(dev);
        s_dev = 0;
    }
    if (s_tex) {
        SDL_DestroyTexture(s_tex);
    }
    SDL_DestroyRenderer(s_ren);
    SDL_DestroyWindow(s_win);
    SDL_Quit();
    return 0;
}
