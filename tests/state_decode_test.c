/* Unit test of src/game/ducktales_state.c on hand-made RAM images. */
#include "game/ducktales_state.h"
#include "game/ducktales_symbols.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c)                                               \
    do {                                                       \
        if (!(c)) {                                            \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            failures++;                                        \
        }                                                      \
    } while (0)

static void hud(uint8_t *ram, int dots, int lost)
{
    for (int i = 0; i < dots; i++) {
        uint8_t *rec = &ram[DT_ADDR_HUD_SPRITES + 4 * i];
        rec[0] = 0x20;
        rec[2] = i >= dots - lost ? DT_HP_DOT_LOST : 0;
    }
    ram[DT_ADDR_HUD_SPRITES + 4 * dots] = DT_HUD_RECORD_END_Y;
}

int main(void)
{
    static uint8_t ram[DT_RAM_SIZE];
    DtState s;

    memset(ram, 0, sizeof ram);
    ram[DT_ADDR_DIFFICULTY] = 1;
    memset(&ram[DT_ADDR_MONEY_DIGITS], DT_MONEY_TILE_BLANK, DT_MONEY_DIGIT_COUNT);
    ram[DT_ADDR_MONEY_DIGITS + 3] = 2;
    hud(ram, 3, 0);
    ram[DT_ADDR_LIVES] = 2;
    ram[DT_ROW_X] = 0x44;
    ram[DT_ROW_Y] = 0xB4;
    ram[DT_ROW_VY] = 0x04;
    ram[DT_ROW_VY_FRAC] = 0xBB;
    ram[DT_ADDR_PAD_HELD] = DT_PAD_A | DT_PAD_B | DT_PAD_DOWN;
    ram[DT_ADDR_PAD_NEW] = DT_PAD_A;
    dt_state_decode(ram, &s);
    CHECK(s.difficulty == DT_DIFFICULTY_NORMAL);
    CHECK(s.money_valid && s.money == 2000);
    CHECK(s.hp == 3 && s.hp_max == 3);
    CHECK(s.lives == 2);
    CHECK(s.scrooge_x == 0x44 && s.scrooge_y == 0xB4);
    CHECK(s.scrooge_vy == 0x04BB);
    CHECK(s.pad_held == 0xC4 && s.pad_new == 0x80);

    ram[DT_ROW_VY] = 0xFF;
    ram[DT_ROW_VY_FRAC] = 0xC0;
    CHECK(dt_slot_vy(ram, 0) == -0x40);

    hud(ram, 4, 3);
    dt_state_decode(ram, &s);
    CHECK(s.hp == 1 && s.hp_max == 4);

    for (int i = 0; i < DT_MONEY_DIGIT_COUNT; i++) {
        ram[DT_ADDR_MONEY_DIGITS + i] = (uint8_t)(i + 1);
    }
    CHECK(dt_money(ram, &s.money) && s.money == 1234560);
    ram[DT_ADDR_MONEY_DIGITS] = 0x30;
    CHECK(!dt_money(ram, &s.money));

    ram[DT_ADDR_DIFFICULTY] = 7;
    CHECK(dt_difficulty(ram) == DT_DIFFICULTY_UNKNOWN);

    memset(&ram[DT_ADDR_HUD_SPRITES], 0, DT_HUD_SPRITE_RECORDS * 4);
    dt_hp(ram, &s.hp, &s.hp_max);
    CHECK(s.hp == 0 && s.hp_max == 0);

    char line[256];
    ram[DT_ADDR_DIFFICULTY] = 0;
    dt_state_decode(ram, &s);
    dt_state_format(&s, line, sizeof line);
    CHECK(strstr(line, "diff=Easy") != NULL);

    printf(failures ? "state_decode_test: %d failure(s)\n" : "state_decode_test: ok\n", failures);
    return failures != 0;
}
