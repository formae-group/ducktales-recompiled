/*
 * ducktales_state.h - typed view of DuckTales' state, decoded from CPU RAM.
 *
 * Pure functions over a copy of the 2 KiB CPU RAM ($0000-$07FF): no machine
 * access, no side effects, so they can be called from a frame_end callback,
 * a hook site, a TCP command or a unit test with a fake RAM image.
 * Addresses come from ducktales_symbols.h.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DT_RAM_SIZE 0x800

typedef enum {
    DT_DIFFICULTY_EASY = 0,
    DT_DIFFICULTY_NORMAL = 1,
    DT_DIFFICULTY_DIFFICULT = 2,
    DT_DIFFICULTY_UNKNOWN = -1,
} DtDifficulty;

typedef struct {
    DtDifficulty difficulty;
    uint32_t money;     /* as the HUD shows it, trailing 0 included */
    bool money_valid;   /* every digit byte is 0-9 or the blank tile */
    uint8_t hp;         /* full HP dots */
    uint8_t hp_max;     /* dots before the end record; 0 when the HUD is not set up */
    uint8_t lives;      /* extra lives (0-9) */
    uint8_t scrooge_x;  /* screen X of slot 0 */
    uint8_t scrooge_y;  /* screen Y of slot 0 */
    int16_t scrooge_vy; /* slot 0 vertical velocity, 8.8 fixed point, up positive */
    uint8_t pad_held;   /* DT_PAD_* bits */
    uint8_t pad_new;
} DtState;

const char *dt_difficulty_name(DtDifficulty d);
DtDifficulty dt_difficulty(const uint8_t *ram);
bool dt_money(const uint8_t *ram, uint32_t *money);
void dt_hp(const uint8_t *ram, uint8_t *hp, uint8_t *hp_max);
uint8_t dt_lives(const uint8_t *ram);
int16_t dt_slot_vy(const uint8_t *ram, unsigned slot);
void dt_state_decode(const uint8_t *ram, DtState *out);

/* One line of text ("diff=Normal money=2000 hp=3/3 ..."), for logs. */
int dt_state_format(const DtState *s, char *out, int cap);

#ifdef __cplusplus
}
#endif
