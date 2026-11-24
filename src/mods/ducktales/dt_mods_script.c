/*
 * dt_mods_script.c - what the mods core (src/mods/core) is told about
 * DuckTales, and the game's side of the dt table its scripts get
 * (docs/MODS.md "Scripts"), told once, before the first folder, setting or
 * option is read (configure).
 *
 *   events     include/ducktales/events.h, with their values (Events_Args);
 *              a game event's site is on while a script handles it
 *   state      dt.state and dt.set = FIELDS below, dt.objects() and
 *              dt.camera() = QUERIES, read and written through the game API
 *              (ducktales/hud.h, game.h, player.h, objects.h) inside a RAM
 *              transaction
 *   store      dt.store in the data folder's mods-data
 */
#include "mods/app_mods.h"
#include "mods/ducktales/dt_mods_internal.h"

#include "ducktales/events.h"
#include "ducktales/game.h"
#include "ducktales/hud.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"
#include "game/ducktales_symbols.h"
#include "ui/app_rom.h"

#include <stdio.h>
#include <string.h>

static bool s_configured;
static char s_store_dir[1024];

/* ---- the script API's game side ---- */

static bool get_land(long long *v)
{
    *v = gLand;
    return true;
}

static bool get_lives(long long *v)
{
    *v = gLives;
    return true;
}

static bool get_hp(long long *v)
{
    *v = Hud_GetHp(NULL);
    return true;
}

static bool get_hp_max(long long *v)
{
    uint8_t max = 0;
    Hud_GetHp(&max);
    *v = max;
    return true;
}

static bool get_money(long long *v)
{
    uint32_t money = 0;
    if (!Hud_GetMoney(&money)) {
        return false;
    }
    *v = money;
    return true;
}

static bool get_time(long long *v)
{
    *v = Hud_GetTime();
    return true;
}

static bool get_difficulty(long long *v)
{
    *v = gDifficulty;
    return true;
}

static bool get_paused(long long *v)
{
    *v = gPaused != 0;
    return true;
}

static bool get_boss_hits(long long *v)
{
    *v = gBossHits;
    return true;
}

static bool get_treasures(long long *v)
{
    *v = gTreasures;
    return true;
}

static bool get_key_items(long long *v)
{
    *v = gKeyItems;
    return true;
}

static bool get_pad(long long *v)
{
    *v = gPadHeld;
    return true;
}

static bool get_pad_new(long long *v)
{
    *v = gPadNew;
    return true;
}

static bool get_music(long long *v)
{
    *v = gMusic;
    return true;
}

static bool get_mode(long long *v)
{
    *v = gPlayerMode;
    return true;
}

static bool get_invulnerable(long long *v)
{
    *v = gPlayerInvulnerable;
    return true;
}

static bool get_x(long long *v)
{
    *v = Obj_GetWorldX(PLAYER_SLOT);
    return true;
}

static bool get_y(long long *v)
{
    *v = gObjects->y[PLAYER_SLOT];
    return true;
}

static bool in_range(long long v, long long lo, long long hi, char *err, size_t cap)
{
    if (v >= lo && v <= hi) {
        return true;
    }
    snprintf(err, cap, "out of range %lld to %lld", lo, hi);
    return false;
}

static bool check_lives(long long v, char *err, size_t cap)
{
    return in_range(v, 0, 9, err, cap);
}

static void set_lives(long long v)
{
    gLives = (uint8_t)v;
}

static bool check_hp(long long v, char *err, size_t cap)
{
    uint8_t max = 0;
    Hud_GetHp(&max);
    if (!max) {
        snprintf(err, cap, "the HUD shows no HP now");
        return false;
    }
    return in_range(v, 0, max, err, cap);
}

/* The first v dots of the HUD's HP records full, the rest lost. */
static void set_hp(long long v)
{
    uint8_t max = 0;
    Hud_GetHp(&max);
    for (int i = 0; i < max; i++) {
        if (i < v) {
            HUD_RECORD(i)[2] &= (uint8_t)~DT_HP_DOT_LOST;
        } else {
            HUD_RECORD(i)[2] |= DT_HP_DOT_LOST;
        }
    }
}

static bool check_money(long long v, char *err, size_t cap)
{
    uint32_t now;
    if (!Hud_GetMoney(&now)) {
        snprintf(err, cap, "the HUD shows no money now");
        return false;
    }
    if (v % 10) {
        snprintf(err, cap, "money is a multiple of 10");
        return false;
    }
    return in_range(v, 0, 9999990, err, cap);
}

/* Six digit tiles of v / 10, leading zeros blank as the game draws them. */
static void set_money(long long v)
{
    long long digits = v / 10;
    for (int i = DT_MONEY_DIGIT_COUNT - 1; i >= 0; i--) {
        gRam[DT_ADDR_MONEY_DIGITS + i] = (uint8_t)(digits % 10);
        digits /= 10;
    }
    for (int i = 0; i < DT_MONEY_DIGIT_COUNT - 1 && gRam[DT_ADDR_MONEY_DIGITS + i] == 0; i++) {
        gRam[DT_ADDR_MONEY_DIGITS + i] = DT_MONEY_TILE_BLANK;
    }
}

static bool check_time(long long v, char *err, size_t cap)
{
    return in_range(v, 0, 999, err, cap);
}

static void set_time(long long v)
{
    gRam[DT_ADDR_TIME_DIGITS] = (uint8_t)(v / 100);
    gRam[DT_ADDR_TIME_DIGITS + 4] = (uint8_t)(v / 10 % 10);
    gRam[DT_ADDR_TIME_DIGITS + 8] = (uint8_t)(v % 10);
}

static const FmField FIELDS[] = {
    { "land", false, get_land, NULL, NULL },
    { "lives", false, get_lives, check_lives, set_lives },
    { "hp", false, get_hp, check_hp, set_hp },
    { "hp_max", false, get_hp_max, NULL, NULL },
    { "money", false, get_money, check_money, set_money },
    { "time", false, get_time, check_time, set_time },
    { "difficulty", false, get_difficulty, NULL, NULL },
    { "paused", true, get_paused, NULL, NULL },
    { "boss_hits", false, get_boss_hits, NULL, NULL },
    { "treasures", false, get_treasures, NULL, NULL },
    { "key_items", false, get_key_items, NULL, NULL },
    { "pad", false, get_pad, NULL, NULL },
    { "pad_new", false, get_pad_new, NULL, NULL },
    { "music", false, get_music, NULL, NULL },
    { "mode", false, get_mode, NULL, NULL },
    { "invulnerable", false, get_invulnerable, NULL, NULL },
    { "x", false, get_x, NULL, NULL },
    { "y", false, get_y, NULL, NULL },
};

static const char *const OBJECT_FIELDS[] = { "slot", "kind", "action", "state", "x", "y", "screen_x" };
#define OBJECT_FIELD_COUNT ((int)(sizeof OBJECT_FIELDS / sizeof OBJECT_FIELDS[0]))

/* The live object slots (Scrooge is slot 0): x is the world X, y and
 * screen_x where the slot is on the screen. */
static int read_objects(long long *v)
{
    int n = 0;
    for (int slot = 0; slot < OBJ_SLOTS; slot++) {
        if (!(gObjects->state[slot] & OBJ_STATUS_LIVE)) {
            continue;
        }
        long long *r = &v[n++ * OBJECT_FIELD_COUNT];
        r[0] = slot;
        r[1] = gObjects->kind[slot];
        r[2] = gObjects->action[slot];
        r[3] = gObjects->state[slot];
        r[4] = Obj_GetWorldX(slot);
        r[5] = gObjects->y[slot];
        r[6] = gObjects->x[slot];
    }
    return n;
}

static const char *const CAMERA_FIELDS[] = { "x", "row", "screen" };

/* The camera's world X (its left edge), map row and screen index. */
static int read_camera(long long *v)
{
    v[0] = gCameraPage << 8 | gCameraX;
    v[1] = gMapRow;
    v[2] = gCameraScreen;
    return 1;
}

static const FmQuery QUERIES[] = {
    { "objects", true, OBJECT_FIELD_COUNT, OBJECT_FIELDS, read_objects },
    { "camera", false, (int)(sizeof CAMERA_FIELDS / sizeof CAMERA_FIELDS[0]), CAMERA_FIELDS, read_camera },
};

static EventResult on_event(EventId id, void *user)
{
    (void)user;
    const EventArg *args;
    FmArg values[EVENT_MAX_ARGS];
    int n = Events_Args(&args);
    for (int i = 0; i < n; i++) {
        values[i] = (FmArg){ args[i].name, args[i].value };
    }
    return fm_scripts_event((int)id, values, n) ? EVENT_CANCEL : EVENT_CONTINUE;
}

static void listen(int event, bool on)
{
    if (on) {
        Events_On((EventId)event, on_event, NULL);
    } else {
        Events_Off((EventId)event, on_event, NULL);
    }
}

void configure(void)
{
    if (s_configured) {
        return;
    }
    s_configured = true;
    static const FmGame GAME = { APP_MODS_GAME, "prg_sha256", "PRG", APP_MODS_EXTENSION, "[ducktales]" };
    fm_configure(&GAME);
    fm_set_target(APP_MODS_PRG_SHA256);
    static FmEvent events[EVENT_COUNT];
    for (int e = 0; e < EVENT_COUNT; e++) {
        events[e] = (FmEvent){ Events_Name((EventId)e), Events_Cancelable((EventId)e) };
    }
    if (app_data_dir(s_store_dir, sizeof s_store_dir - 16)) {
        strcat(s_store_dir, "\\mods-data");
    }
    FmScriptGame script = {
        .events = events,
        .event_count = EVENT_COUNT,
        .fields = FIELDS,
        .field_count = (int)(sizeof FIELDS / sizeof FIELDS[0]),
        .queries = QUERIES,
        .query_count = (int)(sizeof QUERIES / sizeof QUERIES[0]),
        .state_open = GameRam_Open,
        .listen = listen,
        .begin_frame = GameRam_Begin,
        .end_frame = GameRam_End,
        .store_dir = s_store_dir,
    };
    fm_scripts_configure(&script);
}
