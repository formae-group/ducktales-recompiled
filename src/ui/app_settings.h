/*
 * app_settings.h - between app_settings.c (settings, input, events; C, because
 * NESRecomp's cyc_settings.h is a C header) and app_ui.cpp (drawing).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Window SDL_Window;

/* ---- display and sound (config.ini [Display] / [Audio]) ---- */
typedef struct {
    int fullscreen;     /* 0 windowed, 1 borderless, 2 exclusive */
    int window_scale;   /* 1..8 */
    int integer_scale;  /* bool */
    int vsync;          /* bool; applies at the next launch */
    int volume;         /* 0..100 */
} AppDisplay;

void app_display_get(AppDisplay *out);
/* Applies what can be applied live and remembers the rest for the next launch. */
void app_display_set(SDL_Window *win, const AppDisplay *d);
bool app_vsync_pending(void);    /* VSync differs from what the window started with */
bool app_volume_pending(void);   /* volume needs a restart (the window started at 0) */

/* ---- player 1's buttons and the menu shortcut ([Keyboard.*] / [Gamepad.*]) ---- */
#define APP_BIND_MENU 8          /* rows 0-7: A B Select Start Up Down Left Right */
#define APP_BIND_COUNT 9
const char *app_bind_label(int row);
void app_bind_text(int row, bool pad, char *out, size_t cap);   /* "Unbound" when empty */
/* Sets a key (SDL scancode) or a controller binding (cyc_input.h encoding;
 * 0 clears). Returns NULL, or a sentence explaining why it was refused. A
 * binding another row had moves to that row's old one (a swap). */
const char *app_bind_set(int row, bool pad, int value);
void app_bind_defaults(void);

/* ---- the game's own settings were changed in the menu ---- */
void app_settings_changed(void);
/* Write config.ini now if anything changed (the menu calls it on close). */
void app_settings_commit(void);

/* ---- the menu (app_ui.cpp) ---- */
bool app_is_menu_key(int scancode);
bool app_is_menu_button(int sdl_button);
/* Runs the paused menu until the player resumes; false if it could not open. */
bool app_menu_run(SDL_Window *win);
/* The input hold after the menu closes, until every button is up. */
void app_menu_closed(void);

#ifdef __cplusplus
}
#endif
