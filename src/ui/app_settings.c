/*
 * app_settings.c - config.ini, live settings, the player's bindings and the
 * menu shortcut for the application (app_ui.h).
 *
 * NESRecomp's window keeps its CycSettings private (cyc_sdl.c) and offers no
 * call to change them while it runs, so this file keeps its own copy of the
 * same config.ini (cyc_settings_load / cyc_settings_save, the host's own
 * format) and applies each setting through what the window does expose:
 *
 *   display mode, window size   SDL_SetWindowFullscreen / SDL_SetWindowSize
 *   integer scaling             SDL_RenderSetIntegerScale, kept in force each
 *                               frame (the host resets it on a resize or F11)
 *   volume                      CycHostExtras.audio_mix scales the samples by
 *                               the new / starting volume (the host scales by
 *                               the starting one after it)
 *   bindings                    CycHostExtras.input replaces the controller
 *                               bytes with cyc_input_eval() of the new bindings
 *   VSync                       saved; NESRecomp reads it at start and also
 *                               switches it for fast-forward, so it applies at
 *                               the next launch
 *
 * The host writes config.ini itself (on F11 and when it exits) from its stale
 * copy. Its save calls our [Game] writer (app_ui_host_saved), and the next
 * frame, or app_ui_finish after it exits, rewrites the file from this copy,
 * taking only the full-screen choice from the host's file (F11 is the one
 * setting it changes at run time without recomp-ui). Nothing of this runs
 * until the player changes something in the menu.
 */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include "app_settings.h"
#include "app_ui.h"

#include "cyc_input.h"
#include "cyc_settings.h"
#include "host/ducktales_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CycSettings s_cfg;         /* config.ini as the player wants it */
static CycSettings s_launch;      /* as the window started with it */
static char s_path[1024];
static AppLaunch s_flags;
static bool s_loaded;
static bool s_unsaved;            /* changed since the last write */
static bool s_changed;            /* changed since launch: reconcile after the host's saves */
static bool s_host_saved;
static bool s_remap;              /* bindings differ from the window's */
static bool s_hold;               /* after the menu: no buttons until all are up */

static SDL_GameController *s_pads[CYC_INPUT_MAX_PADS];
static char s_pad_guid[CYC_INPUT_MAX_PADS][40];
static int s_pad_count = -1;

static void game_save(void *ctx, FILE *f)
{
    (void)ctx;
    ducktales_host_write_game_settings(f);
}
static const CycSettingsGame GAME_WRITER = { NULL, NULL, game_save };

static void ensure_loaded(void)
{
    if (s_loaded) return;
    s_loaded = true;
    if (!s_path[0]) snprintf(s_path, sizeof s_path, "%s", cyc_settings_default_path());
    cyc_settings_default(&s_cfg);
    cyc_settings_load(&s_cfg, s_path, NULL, NULL);
    s_launch = s_cfg;
    /* the host turns the FDS Disk shortcut off for a cartridge (cyc_sdl.c) */
    s_launch.bind.shortcut_off |= 1u << CYC_SC_DISK;
}

void app_ui_configure(const AppLaunch *launch)
{
    if (launch) {
        s_flags = *launch;
        if (launch->config_path) snprintf(s_path, sizeof s_path, "%s", launch->config_path);
    }
    s_loaded = false;
    ensure_loaded();
}

static void save_now(void)
{
    if (!cyc_settings_save(&s_cfg, s_path, &GAME_WRITER))
        fprintf(stderr, "[ducktales] cannot write the settings file %s\n", s_path);
    s_unsaved = false;
}

/* After the host wrote its stale copy: keep its F11 choice, restore the rest. */
static void reconcile(void)
{
    s_host_saved = false;
    if (!s_changed) return;
    CycSettings disk;
    cyc_settings_default(&disk);
    if (cyc_settings_load(&disk, s_path, NULL, NULL)) s_cfg.fullscreen = disk.fullscreen;
    save_now();
}

void app_ui_finish(void)
{
    if (s_unsaved || s_host_saved) {
        s_host_saved = true;
        reconcile();
    }
}

void app_ui_host_saved(void) { s_host_saved = true; }

void app_settings_changed(void)
{
    s_unsaved = true;
    s_changed = true;
}

void app_settings_commit(void)
{
    if (s_unsaved) save_now();
}

/* ---- the window ---- */

static SDL_Window *find_window(void)
{
    for (Uint32 id = 1; id < 64; id++) {
        SDL_Window *w = SDL_GetWindowFromID(id);
        if (w && SDL_GetRenderer(w)) return w;
    }
    return NULL;
}

static void apply_integer(SDL_Window *win)
{
    SDL_Renderer *ren = win ? SDL_GetRenderer(win) : NULL;
    if (!ren) return;
    int ow = 0, oh = 0, lw = 0, lh = 0;
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    SDL_RenderGetLogicalSize(ren, &lw, &lh);
    /* the host's rule: integer scaling only when the picture fits */
    SDL_bool want = s_cfg.integer_scale && lw > 0 && ow >= lw && oh >= lh ? SDL_TRUE : SDL_FALSE;
    if (SDL_RenderGetIntegerScale(ren) != want) SDL_RenderSetIntegerScale(ren, want);
}

void app_display_get(AppDisplay *out)
{
    ensure_loaded();
    out->fullscreen = s_cfg.fullscreen;
    out->window_scale = s_cfg.window_scale;
    out->integer_scale = s_cfg.integer_scale;
    out->vsync = s_cfg.vsync;
    out->volume = s_cfg.volume;
}

void app_display_set(SDL_Window *win, const AppDisplay *d)
{
    ensure_loaded();
    bool mode = d->fullscreen != s_cfg.fullscreen, size = d->window_scale != s_cfg.window_scale;
    s_cfg.fullscreen = d->fullscreen < 0 ? 0 : d->fullscreen > 2 ? 2 : d->fullscreen;
    s_cfg.window_scale = d->window_scale < 1 ? 1 : d->window_scale > 8 ? 8 : d->window_scale;
    s_cfg.integer_scale = d->integer_scale != 0;
    s_cfg.vsync = d->vsync != 0;
    s_cfg.volume = d->volume < 0 ? 0 : d->volume > 100 ? 100 : d->volume;
    app_settings_changed();
    if (!win) return;
    if (mode) {
        Uint32 fs = s_cfg.fullscreen == 1 ? SDL_WINDOW_FULLSCREEN_DESKTOP : s_cfg.fullscreen == 2 ? SDL_WINDOW_FULLSCREEN : 0;
        SDL_SetWindowFullscreen(win, fs);
    }
    if (!s_cfg.fullscreen && (mode || size)) {
        SDL_Renderer *ren = SDL_GetRenderer(win);
        int lw = 256, lh = 240, scale = s_cfg.window_scale;
        if (ren) SDL_RenderGetLogicalSize(ren, &lw, &lh);
        if (lw <= 0 || lh <= 0) lw = 256, lh = 240;
        SDL_Rect area;
        int display = SDL_GetWindowDisplayIndex(win);
        if (display >= 0 && SDL_GetDisplayUsableBounds(display, &area) == 0)
            while (scale > 1 && (lw * scale > area.w || lh * scale > area.h - 48)) --scale;
        SDL_SetWindowSize(win, lw * scale, lh * scale);
        if (display >= 0) SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    }
    apply_integer(win);
}

bool app_vsync_pending(void) { return s_cfg.vsync != s_launch.vsync; }
bool app_volume_pending(void) { return s_cfg.volume != s_launch.volume && s_launch.volume == 0; }

void app_ui_frame_begin(void)
{
    /* DUCKTALES_UI_SELFTEST (app_ui.cpp): open the menu once, by itself. */
    static long frames;
    if (++frames == 120 && getenv("DUCKTALES_UI_SELFTEST")) {
        SDL_Window *win = find_window();
        if (win && app_menu_run(win)) app_menu_closed();
    }
    if (s_host_saved) reconcile();
    if (s_changed && s_cfg.integer_scale != s_launch.integer_scale) apply_integer(find_window());
}

void app_ui_audio(int16_t *samples, size_t count)
{
    if (s_cfg.volume == s_launch.volume || s_launch.volume <= 0) return;
    const int num = s_cfg.volume, den = s_launch.volume;
    for (size_t i = 0; i < count; i++) {
        int v = samples[i] * num / den;
        samples[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

/* ---- input ---- */

static void open_pads(void)
{
    for (int i = 0; i < s_pad_count; i++) SDL_GameControllerClose(s_pads[i]);
    s_pad_count = 0;
    for (int i = 0; i < SDL_NumJoysticks() && s_pad_count < CYC_INPUT_MAX_PADS; i++) {
        if (!SDL_IsGameController(i)) continue;
        SDL_GameController *c = SDL_GameControllerOpen(i);
        if (!c) continue;
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(c)), s_pad_guid[s_pad_count],
                                  sizeof s_pad_guid[0]);
        s_pads[s_pad_count++] = c;
    }
}

/* The same device frame the host builds (cyc_sdl.c read_frame), so the
 * same bindings give the same buttons. */
static void eval(const CycBindings *b, CycInputState *st)
{
    if (s_pad_count < 0) open_pads();
    CycInputFrame f;
    memset(&f, 0, sizeof f);
    int n = 0;
    f.keys = SDL_GetKeyboardState(&n);
    f.key_count = n;
    f.pad_count = s_pad_count;
    for (int i = 0; i < s_pad_count; i++) {
        for (int k = 0; k < SDL_CONTROLLER_BUTTON_MAX; k++)
            if (SDL_GameControllerGetButton(s_pads[i], (SDL_GameControllerButton)k)) f.pad_buttons[i] |= 1u << k;
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX && a < CYC_INPUT_PAD_AXES; a++)
            f.pad_axes[i][a] = SDL_GameControllerGetAxis(s_pads[i], (SDL_GameControllerAxis)a);
        f.pad_guid[i] = s_pad_guid[i];
    }
    cyc_input_eval(b, &f, st);
}

void app_ui_input(uint8_t buttons[2])
{
    if (s_remap && !s_flags.scripted_input) {
        CycInputState st;
        eval(&s_cfg.bind, &st);
        buttons[0] = st.buttons[0];
        buttons[1] = st.buttons[1];
    }
    if (s_hold) {
        if (buttons[0] || buttons[1]) buttons[0] = buttons[1] = 0;
        else s_hold = false;
    }
}

void app_menu_closed(void) { s_hold = true; }

static void update_remap(void)
{
    s_remap = memcmp(s_cfg.bind.button, s_launch.bind.button, sizeof s_cfg.bind.button) != 0;
}

bool app_is_menu_key(int scancode)
{
    return scancode > 0 && scancode == s_cfg.bind.shortcut[CYC_SC_MENU].key;
}

bool app_is_menu_button(int sdl_button)
{
    return s_cfg.bind.shortcut[CYC_SC_MENU].pad == CYC_PAD_BUTTON(sdl_button);
}

void app_ui_event(const void *event)
{
    const SDL_Event *ev = (const SDL_Event *)event;
    if (s_flags.hidden) return;
    ensure_loaded();
    if (ev->type == SDL_CONTROLLERDEVICEADDED || ev->type == SDL_CONTROLLERDEVICEREMOVED) {
        if (s_pad_count >= 0) open_pads();
        return;
    }
    bool open = (ev->type == SDL_KEYDOWN && !ev->key.repeat && app_is_menu_key(ev->key.keysym.scancode)) ||
                (ev->type == SDL_CONTROLLERBUTTONDOWN && app_is_menu_button(ev->cbutton.button));
    if (!open) return;
    SDL_Window *win = ev->type == SDL_KEYDOWN ? SDL_GetWindowFromID(ev->key.windowID) : NULL;
    if (!win || !SDL_GetRenderer(win)) win = find_window();
    if (win && app_menu_run(win)) {
        app_settings_commit();
        app_menu_closed();
    }
}

/* ---- bindings ---- */

static CycBinding *row_binding(int row)
{
    return row == APP_BIND_MENU ? &s_cfg.bind.shortcut[CYC_SC_MENU] : &s_cfg.bind.button[0][row];
}

const char *app_bind_label(int row)
{
    return row == APP_BIND_MENU ? "Menu" : cyc_button_label(row);
}

void app_bind_text(int row, bool pad, char *out, size_t cap)
{
    ensure_loaded();
    const CycBinding *b = row_binding(row);
    if (!pad) {
        if (b->key) cyc_key_text(b->key, out, cap);
        else snprintf(out, cap, "Unbound");
        return;
    }
    if (!b->pad) { snprintf(out, cap, "Unbound"); return; }
    CycBinding only = { 0, b->pad };
    cyc_binding_hint(&only, out, cap);
}

const char *app_bind_set(int row, bool pad, int value)
{
    static char why[160];
    ensure_loaded();
    if (row < 0 || row >= APP_BIND_COUNT) return "No such control.";
    CycBinding *b = row_binding(row);
    int *slot = pad ? &b->pad : &b->key;
    if (value) {
        /* The host reads its own shortcuts with the bindings it started with. */
        for (int s = 0; s < CYC_SC_COUNT; s++) {
            if (s == CYC_SC_MENU || (s_launch.bind.shortcut_off & (1u << s))) continue;
            const CycBinding *sc = &s_launch.bind.shortcut[s];
            if (pad ? sc->pad == value : sc->key == value) {
                char name[48];
                if (pad) { CycBinding only = { 0, value }; cyc_binding_hint(&only, name, sizeof name); }
                else cyc_key_text(value, name, sizeof name);
                snprintf(why, sizeof why, "%s is used by %s.", name, cyc_shortcut_label(s));
                return why;
            }
        }
        for (int other = 0; other < APP_BIND_COUNT; other++) {
            if (other == row) continue;
            CycBinding *ob = row_binding(other);
            int *oslot = pad ? &ob->pad : &ob->key;
            if (*oslot == value) *oslot = *slot;
        }
    }
    *slot = value;
    update_remap();
    app_settings_changed();
    return NULL;
}

void app_bind_defaults(void)
{
    ensure_loaded();
    CycBindings def;
    cyc_bindings_default(&def);
    memcpy(s_cfg.bind.button[0], def.button[0], sizeof def.button[0]);
    s_cfg.bind.shortcut[CYC_SC_MENU] = def.shortcut[CYC_SC_MENU];
    update_remap();
    app_settings_changed();
}
