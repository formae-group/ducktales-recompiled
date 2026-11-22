/*
 * enhancements.h - optional changes to the game, all off by default.
 *
 * Each enhancement is a handler of one game event (ducktales/events.h,
 * src/game/events.c), registered only while its value differs from stock. An
 * event without handlers keeps its site's hook disabled, so at the stock
 * value the generated code runs the original instruction with nothing
 * changed; the value is applied live and can be switched at any frame
 * boundary. The handlers are short examples of the typed game API.
 *
 *   JumpScale      take-off velocity percent (25-400, 100 = stock), game/ducktales_jump.c
 *   NoDamage       hits still knock Scrooge back but never cost an HP dot
 *   InfiniteTime   the HUD timer never counts down
 *   InfiniteLives  a death never costs a life
 *   StartLives     extra lives on a new game (0-9; -1 = stock, which is 2)
 *
 * The table gives each one a config.ini [Game] key, a command-line option, a
 * menu label and a one-line description, so the host, the menu and the tests
 * iterate it instead of naming each one.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DT_ENH_JUMP_SCALE,
    DT_ENH_NO_DAMAGE,
    DT_ENH_INFINITE_TIME,
    DT_ENH_INFINITE_LIVES,
    DT_ENH_START_LIVES,
    DT_ENH_COUNT
} DtEnh;

typedef enum { DT_ENH_KIND_PERCENT, DT_ENH_KIND_TOGGLE, DT_ENH_KIND_COUNT_OR_OFF } DtEnhKind;

typedef struct {
    const char *key;      /* config.ini [Game] key */
    const char *option;   /* command-line option (takes a value) */
    const char *label;    /* menu label */
    const char *help;     /* one line: the menu's description and the usage */
    DtEnhKind kind;
    int min, max, stock;  /* stock: the original game, hook disabled */
    int step;             /* menu step */
} DtEnhInfo;

const DtEnhInfo *dt_enh_info(DtEnh e);
/* Text to value: integers in range, plus on/off/true/false/yes/no for
 * toggles and "off" for StartLives. False leaves *out alone. */
bool dt_enh_parse(DtEnh e, const char *text, int *out);
/* The value as config.ini and the usage write it. */
const char *dt_enh_format(DtEnh e, int value, char *buf, unsigned cap);
/* False (nothing changes) outside min..max. */
bool dt_enh_set(DtEnh e, int value);
int  dt_enh_get(DtEnh e);
/* How many times the enhancement changed the game since start. */
uint64_t dt_enh_applied(DtEnh e);

/* The console's reset button, between frames: the CPU continues from the
 * reset vector as after its reset sequence (S - 3, I set), pending
 * interrupts dropped. The PPU and APU are not reset; the game's reset
 * handler re-initialises them itself (functions.md $FEE0). */
void dt_soft_reset(void);

#ifdef __cplusplus
}
#endif
