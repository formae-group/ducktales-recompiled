#include "ducktales_state.h"
#include "ducktales_symbols.h"

#include <stdio.h>

const char *dt_difficulty_name(DtDifficulty d)
{
    switch (d) {
    case DT_DIFFICULTY_EASY: return "Easy";
    case DT_DIFFICULTY_NORMAL: return "Normal";
    case DT_DIFFICULTY_DIFFICULT: return "Difficult";
    default: return "?";
    }
}

DtDifficulty dt_difficulty(const uint8_t *ram)
{
    uint8_t v = ram[DT_ADDR_DIFFICULTY];
    return v <= DT_DIFFICULTY_DIFFICULT ? (DtDifficulty)v : DT_DIFFICULTY_UNKNOWN;
}

bool dt_money(const uint8_t *ram, uint32_t *money)
{
    uint32_t value = 0;
    for (int i = 0; i < DT_MONEY_DIGIT_COUNT; i++) {
        uint8_t tile = ram[DT_ADDR_MONEY_DIGITS + i];
        if (tile == DT_MONEY_TILE_BLANK) tile = 0;
        else if (tile > 9) { *money = 0; return false; }
        value = value * 10 + tile;
    }
    *money = value * 10;
    return true;
}

void dt_hp(const uint8_t *ram, uint8_t *hp, uint8_t *hp_max)
{
    uint8_t full = 0;
    for (uint8_t i = 0; i < DT_HUD_SPRITE_RECORDS; i++) {
        const uint8_t *rec = &ram[DT_ADDR_HUD_SPRITES + 4 * i];
        if (rec[0] == DT_HUD_RECORD_END_Y) {
            *hp = full;
            *hp_max = i;
            return;
        }
        if (!(rec[2] & DT_HP_DOT_LOST)) full++;
    }
    *hp = 0;
    *hp_max = 0;
}

uint8_t dt_lives(const uint8_t *ram)
{
    return ram[DT_ADDR_LIVES];
}

int16_t dt_slot_vy(const uint8_t *ram, unsigned slot)
{
    return (int16_t)(uint16_t)(ram[DT_ROW_VY + slot] << 8 | ram[DT_ROW_VY_FRAC + slot]);
}

void dt_state_decode(const uint8_t *ram, DtState *out)
{
    out->difficulty = dt_difficulty(ram);
    out->money_valid = dt_money(ram, &out->money);
    dt_hp(ram, &out->hp, &out->hp_max);
    out->lives = dt_lives(ram);
    out->scrooge_x = ram[DT_ROW_X + DT_SLOT_SCROOGE];
    out->scrooge_y = ram[DT_ROW_Y + DT_SLOT_SCROOGE];
    out->scrooge_vy = dt_slot_vy(ram, DT_SLOT_SCROOGE);
    out->pad_held = ram[DT_ADDR_PAD_HELD];
    out->pad_new = ram[DT_ADDR_PAD_NEW];
}

int dt_state_format(const DtState *s, char *out, int cap)
{
    char money[16];
    if (s->money_valid) snprintf(money, sizeof money, "%lu", (unsigned long)s->money);
    else snprintf(money, sizeof money, "?");
    return snprintf(out, (size_t)cap,
                    "diff=%s money=%s hp=%u/%u lives=%u x=%u y=%u vy=%+.3f held=%02X new=%02X",
                    dt_difficulty_name(s->difficulty), money, s->hp, s->hp_max, s->lives, s->scrooge_x,
                    s->scrooge_y, s->scrooge_vy / 256.0, s->pad_held, s->pad_new);
}
