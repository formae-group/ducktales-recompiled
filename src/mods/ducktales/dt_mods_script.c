/*
 * dt_mods_script.c - what the mods core (src/mods/core) is told about
 * DuckTales, and the game's side of the dt table its scripts get
 * (docs/MODS.md "Scripts"), told once, before the first folder, setting or
 * option is read (dt_mods_configure).
 *
 *   events     include/ducktales/events.h, with their values (Events_Args);
 *              a game event's site is on while a script handles it
 *   state      dt.state and dt.set = FIELDS below, dt.objects() and
 *              dt.camera() = QUERIES, read and written through the game API
 *              (ducktales/hud.h, game.h, player.h, objects.h) inside a RAM
 *              transaction; dt.objects() gives handles: o:set{x, y, action}
 *              and o:despawn() (the decompiled Obj_Despawn), and
 *              dt.spawn(kind, x, y [, action]) makes an enemy (the
 *              decompiled Obj_FindFreeSlot and Obj_SpawnTemplate)
 *   calls      dt.level.set(x, y, metatile) = CALLS below, a tile edit (the
 *              decompiled Level_EditCell)
 *   tunes      dt.tune = TUNES below, Scrooge's tunables (ducktales/tune.h)
 *   store      dt.store in the data folder's mods-data
 */
#include "mods/app_mods.h"
#include "mods/ducktales/dt_mods_internal.h"

#include "ducktales/events.h"
#include "ducktales/game.h"
#include "ducktales/hud.h"
#include "ducktales/level.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"
#include "ducktales/tune.h"
#include "decomp/decomp.h"
#include "decomp/level.h"
#include "decomp/objcommon.h"
#include "game/ducktales_symbols.h"
#include "ui/app_rom.h"

#include <stdio.h>
#include <string.h>

static bool s_configured;
static EventId s_event = EVENT_COUNT; /* the game event whose handlers run, EVENT_COUNT for none */
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

#define KNOCKBACK_END     0x3C /* the knock-back ends when $7D counts down to it ($EB00) */
#define INVULNERABLE_HELD 0x80 /* with this bit $7D does not count down ($E358) */

/* During the knock-back $7D must count down to KNOCKBACK_END to end it, and
 * at 0 the knock-back takes another hit; while Scrooge waits in mode 5 it
 * must count down to 0. */
static bool check_invulnerable(long long v, char *err, size_t cap)
{
    if (!in_range(v, 0, 255, err, cap)) {
        return false;
    }
    if (gPlayerMode == PLAYER_MODE_KNOCKBACK && (v <= KNOCKBACK_END || v >= INVULNERABLE_HELD)) {
        snprintf(err, cap, "during the knock-back it is %d to %d (it ends at %d)", KNOCKBACK_END + 1,
                 INVULNERABLE_HELD - 1, KNOCKBACK_END);
        return false;
    }
    if (gPlayerMode == PLAYER_MODE_HURT && v >= INVULNERABLE_HELD) {
        snprintf(err, cap, "while Scrooge is hurt it is below %d", INVULNERABLE_HELD);
        return false;
    }
    return true;
}

static void set_invulnerable(long long v)
{
    gPlayerInvulnerable = (uint8_t)v;
}

#define BOSS_HITS_START 5 /* what the spawner sets when a fight starts ($AF2B) */

static bool check_boss_hits(long long v, char *err, size_t cap)
{
    if (!(gBossFight & 0x80)) {
        snprintf(err, cap, "only during a boss fight");
        return false;
    }
    return in_range(v, 1, BOSS_HITS_START, err, cap);
}

static void set_boss_hits(long long v)
{
    gBossHits = (uint8_t)v;
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
    { "boss_hits", false, get_boss_hits, check_boss_hits, set_boss_hits },
    { "treasures", false, get_treasures, NULL, NULL },
    { "key_items", false, get_key_items, NULL, NULL },
    { "pad", false, get_pad, NULL, NULL },
    { "pad_new", false, get_pad_new, NULL, NULL },
    { "music", false, get_music, NULL, NULL },
    { "mode", false, get_mode, NULL, NULL },
    { "invulnerable", false, get_invulnerable, check_invulnerable, set_invulnerable },
    { "x", false, get_x, NULL, NULL },
    { "y", false, get_y, NULL, NULL },
};

static const char *const OBJECT_FIELDS[] = { "slot",     "kind",   "action", "state",  "x",     "y",
                                             "screen_x", "record", "player", "facing", "speed", "vy" };

enum {
    O_SLOT,
    O_KIND,
    O_ACTION,
    O_STATE,
    O_X,
    O_Y,
    O_SCREEN_X,
    O_RECORD,
    O_PLAYER,
    O_FACING,
    O_SPEED,
    O_VY,
    OBJECT_FIELD_COUNT
};

#define OBJ_Y_MAX 0xEF /* the last line of the picture */

/* The player of a slot, 0 for none: one player, in slot 0. */
static int player_of(int slot)
{
    return slot == PLAYER_SLOT ? 1 : 0;
}

/* The record of an object slot: x is the world X, y and screen_x where the
 * slot is on the screen, record the land record it was spawned from ($FF
 * none), player the player it is (0 for an object), facing 1 when it faces
 * right, speed its horizontal speed (8.8, the kind's code moves it left or
 * right by it) and vy its vertical velocity (8.8, signed). */
static void read_object(int slot, long long *r)
{
    r[O_SLOT] = slot;
    r[O_KIND] = gObjects->kind[slot];
    r[O_ACTION] = gObjects->action[slot];
    r[O_STATE] = gObjects->state[slot];
    r[O_X] = Obj_GetWorldX(slot);
    r[O_Y] = gObjects->y[slot];
    r[O_SCREEN_X] = gObjects->x[slot];
    r[O_RECORD] = gObjects->record[slot];
    r[O_PLAYER] = player_of(slot);
    r[O_FACING] = (gObjects->flags[slot] & OBJ_FLAG_FACING_RIGHT) != 0;
    r[O_SPEED] = gObjects->speed[slot] << 8 | gObjects->speedFrac[slot];
    r[O_VY] = (int16_t)(gObjects->vy[slot] << 8 | gObjects->vyFrac[slot]);
}

/* The live object slots (Scrooge is slot 0). */
static int read_objects(long long *v)
{
    int n = 0;
    for (int slot = 0; slot < OBJ_SLOTS; slot++) {
        if (gObjects->state[slot] & OBJ_STATUS_LIVE) {
            read_object(slot, &v[n++ * OBJECT_FIELD_COUNT]);
        }
    }
    return n;
}

/* The handle's slot still holds the live object of that kind and record. */
static bool same_object(const long long *v)
{
    if (v[O_SLOT] < 0 || v[O_SLOT] >= OBJ_SLOTS) {
        return false;
    }
    int slot = (int)v[O_SLOT];
    return (gObjects->state[slot] & OBJ_STATUS_LIVE) && gObjects->kind[slot] == v[O_KIND] &&
           gObjects->record[slot] == v[O_RECORD];
}

/* A byte of the land's record table i (the pointers at $60-$69): bank 6, or RAM. */
static uint8_t record_byte(int table, int y)
{
    uint16_t addr = (uint16_t)(RAM_U8(DT_ADDR_SPAWN_TABLES + 2 * table) | RAM_U8(DT_ADDR_SPAWN_TABLES + 2 * table + 1)
                                                                              << 8) +
                    (uint16_t)y;
    return addr < GAME_RAM_SIZE ? gRam[addr] : Game_ReadPrg(DT_BANK_OBJECTS, addr);
}

/* The 4-byte template of an object type ($BE16), and whether it is kind
 * with action (any action when action < 0). */
static bool type_spawns(uint8_t type, uint8_t kind, long long action, uint8_t template[4])
{
    uint16_t at = (uint16_t)(DT_CODE_SPAWN_TEMPLATES + ((type >> 6 & 1) << 8)) + (uint8_t)(type << 2);
    for (int i = 0; i < 4; i++) {
        template[i] = Game_ReadPrg(DT_BANK_OBJECTS, (uint16_t)(at + i));
    }
    return template[2] == kind && (action < 0 || template[1] == action);
}

static uint8_t timed_byte(uint16_t table, int y)
{
    return Game_ReadPrg(DT_BANK_OBJECTS, (uint16_t)(table + y));
}

/* The template of the first object of this land that spawns kind with
 * action (any action when action < 0); false when none does. Each map row
 * has its records ($AF2B: from the row's first, up to a page with bit 7)
 * and its timed objects ($B105: from the land's first timed row, up to a
 * page $FF). */
static bool land_template(uint8_t kind, long long action, uint8_t template[4])
{
    for (int row = 0; row < gMapRows; row++) {
        for (int y = record_byte(2, row); y < 256 && !(record_byte(0, y) & 0x80); y++) {
            if (type_spawns(record_byte(4, y), kind, action, template)) {
                return true;
            }
        }
        int timed = timed_byte(DT_B6_TIMED_LAND, gLand) + row;
        for (int y = timed_byte(DT_B6_TIMED_ROW, timed); y < 256 && timed_byte(DT_B6_TIMED_PAGE, y) != 0xFF; y++) {
            if (type_spawns(timed_byte(DT_B6_TIMED_TYPE, y), kind, action, template)) {
                return true;
            }
        }
    }
    return false;
}

static bool land_spawns(uint8_t kind, uint8_t action)
{
    uint8_t template[4];
    return land_template(kind, action, template);
}

/* Kinds the game's progress waits for (a boss fight, a scene, the final
 * climb): despawned, they would stall it. */
static bool keeps_progress(uint8_t kind)
{
    switch (kind) {
        case OBJ_KIND_TRANSYLVANIA_BOSS:
        case OBJ_KIND_MINES_BOSS:
        case OBJ_KIND_MOON_BOSS:
        case OBJ_KIND_HIMALAYAS_BOSS:
        case OBJ_KIND_AMAZON_BOSS:
        case OBJ_KIND_LAUNCHPAD_RIDE:
        case OBJ_KIND_SKY_FLYER:
        case OBJ_KIND_SKY_ROPE:
            return true;
        default:
            return kind >= OBJ_KIND_FINAL_STAND_IN && kind <= OBJ_KIND_FINAL_TREASURE;
    }
}

/* The action of a pickup, a hidden item or a chest is the item it gives,
 * and a block with one of these actions holds an item. */
static bool action_is_item(uint8_t kind)
{
    return kind == OBJ_KIND_PICKUP || kind == OBJ_KIND_HIDDEN_ITEM || kind == OBJ_KIND_CHEST;
}

static bool action_holds_item(uint8_t action)
{
    return action == OBJ_ACTION_CHEST || action == OBJ_ACTION_LAND_CHEST;
}

static bool check_object(const long long *v, const char *field, long long value, char *err, size_t cap)
{
    int slot = (int)v[O_SLOT];
    uint8_t kind = gObjects->kind[slot];
    if (player_of(slot)) {
        snprintf(err, cap, "slot %d is player %d's: read only", slot, player_of(slot));
        return false;
    }
    if (!field) {
        if (keeps_progress(kind)) {
            snprintf(err, cap, "kind %d is one the game's progress waits for", kind);
            return false;
        }
        return true;
    }
    if (!strcmp(field, "x")) {
        if (!(gObjects->state[slot] & OBJ_STATUS_WORLD)) {
            snprintf(err, cap, "this object has no world position");
            return false;
        }
        long long left = gCameraPage << 8 | gCameraX;
        if (!in_range(value, left, left + 0xFF, err, cap)) {
            strncat(err, " (the screen)", cap - strlen(err) - 1);
            return false;
        }
        return true;
    }
    if (!strcmp(field, "y")) {
        return in_range(value, 0, OBJ_Y_MAX, err, cap);
    }
    if (!strcmp(field, "action")) {
        if (action_is_item(kind)) {
            snprintf(err, cap, "the action of kind %d is the item it gives", kind);
            return false;
        }
        if (action_holds_item((uint8_t)value)) {
            snprintf(err, cap, "an object with that action holds an item");
            return false;
        }
        if (value < 0 || value > 0xFF || !land_spawns(kind, (uint8_t)value)) {
            snprintf(err, cap, "this land spawns no kind %d with that action", kind);
            return false;
        }
        return true;
    }
    snprintf(err, cap, "%s cannot be set (x, y, action)", field);
    return false;
}

/* A new action starts its animation from the first frame, as a spawn does. */
static void write_object(const long long *v, const char *field, long long value)
{
    int slot = (int)v[O_SLOT];
    if (!strcmp(field, "x")) {
        Obj_SetWorldX(slot, (uint16_t)value);
    } else if (!strcmp(field, "y")) {
        gObjects->y[slot] = (uint8_t)value;
    } else {
        gObjects->action[slot] = (uint8_t)value;
        gObjects->animFrame[slot] = 0;
        gObjects->animTimer[slot] = 0;
    }
}

static void despawn(void *slot)
{
    Obj_Despawn(*(uint8_t *)slot);
}

static void despawn_object(const long long *v)
{
    uint8_t slot = (uint8_t)v[O_SLOT];
    Decomp_Outside(despawn, &slot);
}

static void describe_object(const long long *v, char *out, size_t cap)
{
    snprintf(out, cap, "object %lld (kind %lld)", v[O_SLOT], v[O_KIND]);
}

/* The object slots a mod's spawn leaves free, for the game's own spawns and
 * shots (and, later, a second player's slot). */
#define SPAWN_KEEP_FREE 2

/* The kinds dt.spawn makes: the enemies, without the plant that catches
 * Scrooge and the enemy that lives in a block. Bosses, scenes, doors,
 * blocks, chests and pickups are left out: a pickup or chest would give
 * its treasure twice. */
static bool spawns_enemy(long long kind)
{
    switch (kind) {
        case OBJ_KIND_HOPPER:
        case OBJ_KIND_WALKER:
        case OBJ_KIND_THROWER:
        case OBJ_KIND_WATCHER:
        case OBJ_KIND_DANGLER:
        case OBJ_KIND_CRAWLER:
        case OBJ_KIND_FLYER:
        case OBJ_KIND_CHARGER:
        case OBJ_KIND_SWOOPER:
        case OBJ_KIND_PATROLLER:
        case OBJ_KIND_POUNCER:
        case OBJ_KIND_PIT_JUMPER:
        case OBJ_KIND_BOUNCER:
        case OBJ_KIND_LEAPER:
        case OBJ_KIND_BLOCK_KICKER:
        case OBJ_KIND_LEDGE_WALKER:
        case OBJ_KIND_PACER:
        case OBJ_KIND_BOBBER:
        case OBJ_KIND_HOP_WALKER:
        case OBJ_KIND_CEILING_HOPPER:
        case OBJ_KIND_THIEF:
        case OBJ_KIND_SHORT_THIEF:
        case OBJ_KIND_PICKPOCKET:
            return true;
        default:
            return false;
    }
}

typedef struct {
    uint8_t template[4];
    uint16_t x;
    uint8_t y;
    uint8_t slot;
} Spawn;

/* $AF2B's spawn without a land record: no enemy template is a hidden one
 * with pose bit 7, so its hidden-object tail never applies, and spawns are
 * refused in a boss fight. */
static void spawn(void *ctx)
{
    Spawn *s = ctx;
    Obj_FindFreeSlot(&s->slot);
    gObjects->record[s->slot] = 0xFF;
    Obj_SetWorldX(s->slot, s->x);
    gObjects->unk_0740[s->slot] = gObjects->y[s->slot] = s->y;
    Obj_SpawnTemplate(s->slot, s->template);
}

/* dt.spawn(kind, x, y [, action]): an enemy this land's records spawn, with
 * the template of the first record that spawns it (with that action), at
 * world X x on the screen and Y y, in the highest free slot. */
static int spawn_object(const long long *args, int nargs, long long *values, char *err, size_t cap)
{
    if (nargs < 3 || nargs > 4) {
        snprintf(err, cap, "kind, x, y and an optional action are needed");
        return FM_INVALID;
    }
    long long kind = args[0];
    Spawn s = { .x = (uint16_t)args[1], .y = (uint8_t)args[2] };
    if (!spawns_enemy(kind)) {
        snprintf(err, cap, "kind %lld is not one of the enemies (docs/MODS.md)", kind);
        return FM_INVALID;
    }
    if (gBossFight & 0x80) {
        snprintf(err, cap, "not during a boss fight");
        return FM_INVALID;
    }
    if (!land_template((uint8_t)kind, nargs == 4 ? args[3] : -1, s.template)) {
        snprintf(err, cap, "this land spawns no kind %lld%s", kind, nargs == 4 ? " with that action" : "");
        return FM_INVALID;
    }
    long long left = gCameraPage << 8 | gCameraX;
    if (!in_range(args[1], left, left + 0xFF, err, cap)) {
        strncat(err, " (the screen)", cap - strlen(err) - 1);
        return FM_INVALID;
    }
    if (!in_range(args[2], 0, OBJ_Y_MAX, err, cap)) {
        return FM_INVALID;
    }
    int empty = 0;
    for (int slot = 1; slot < OBJ_SLOTS; slot++) {
        empty += !(gObjects->state[slot] & OBJ_STATUS_LIVE);
    }
    if (empty <= SPAWN_KEEP_FREE) {
        snprintf(err, cap, "%d free slots, %d kept for the game", empty, SPAWN_KEEP_FREE);
        return FM_REFUSED;
    }
    Decomp_Outside(spawn, &s);
    read_object(s.slot, values);
    return FM_DONE;
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

#define LEVEL_Y_MIN     0x30 /* the first line below the HUD (tile rows 0-5) */
#define TILE_EDIT_ENTRY 14
#define CLASS_SPECIAL   3

/* dt.level.set(x, y, metatile): the metatile at world X x and Y y on the
 * screen becomes metatile of the land's tileset, as an object's tile edit
 * does it ($F574): in the $0440 queue, the collision map and the attribute
 * shadow. Only in an "update" handler, with the queue empty and not yet
 * given to the NMI ($5C and $1A 0, so one edit a step) and no row scroll.
 * The tileset is read from its bank, with no bank switch. A special
 * metatile (class 3) is refused: the game finds its effect from the
 * metatile of the map, which is not this one. */
static int level_set(const long long *args, int nargs, char *err, size_t cap)
{
    if (nargs != 3) {
        snprintf(err, cap, "x, y and a metatile are needed");
        return FM_INVALID;
    }
    if (s_event != EVENT_UPDATE) {
        snprintf(err, cap, "only in an \"update\" handler");
        return FM_INVALID;
    }
    long long left = gCameraPage << 8 | gCameraX;
    if (!in_range(args[0], left, left + 0xFF, err, cap)) {
        strncat(err, " (the screen)", cap - strlen(err) - 1);
        return FM_INVALID;
    }
    if (!in_range(args[1], LEVEL_Y_MIN, OBJ_Y_MAX, err, cap)) {
        strncat(err, " (below the HUD)", cap - strlen(err) - 1);
        return FM_INVALID;
    }
    if (!in_range(args[2], 0, 0xFF, err, cap)) {
        return FM_INVALID;
    }
    uint8_t metatile[5];
    for (int i = 0; i < 4; i++) {
        metatile[i] = Game_ReadPrg(gTilesetBank, (uint16_t)(DT_METATILE_TILES + 0x100 * i + args[2]));
    }
    metatile[4] = Game_ReadPrg(gTilesetBank, (uint16_t)(DT_METATILE_ATTR + args[2]));
    if ((metatile[4] >> 4 & 3) == CLASS_SPECIAL) {
        snprintf(err, cap, "metatile %lld is a special one (collision class 3)", args[2]);
        return FM_INVALID;
    }
    if (RAM_U8(DT_ADDR_TILE_EDIT_END) || gTileEditReady || gRowScroll) {
        snprintf(err, cap, "the tile edit queue is in use");
        return FM_REFUSED;
    }
    Level_EditCell((uint8_t)args[0], (uint8_t)(args[0] >> 8), (uint8_t)args[1], metatile);
    RAM_U8(DT_ADDR_TILE_EDIT_END) = TILE_EDIT_ENTRY;
    return FM_DONE;
}

static const FmCall CALLS[] = {
    { "level", "set", level_set },
};

static const FmQuery QUERIES[] = {
    { "objects", true, OBJECT_FIELD_COUNT, OBJECT_FIELDS, read_objects, same_object, check_object, write_object,
      "despawn", despawn_object, describe_object, "spawn", spawn_object },
    { "camera", false, (int)(sizeof CAMERA_FIELDS / sizeof CAMERA_FIELDS[0]), CAMERA_FIELDS, read_camera },
};

/* 8.8 fixed point: 256 is one pixel per frame. No walk faster than the
 * slippery floor's top speed $0280, and no take-off or pogo velocity above
 * 16 pixels a frame (JumpScale's 400% take-off is $12EC). */
static const FmTune TUNES[TUNE_COUNT] = {
    [TUNE_GRAVITY] = { "gravity", 0x10, 0xFF },
    [TUNE_WALK_SPEED] = { "walk_speed", 0x40, 0x280 },
    [TUNE_POGO_VELOCITY] = { "pogo_velocity", 0x100, 0x1000 },
    [TUNE_JUMP_VELOCITY] = { "jump_velocity", 0x100, 0x1000 },
};

static void tune(int t, bool on, long long value)
{
    Tune_Set((TuneId)t, on ? (int32_t)value : TUNE_UNSET);
}

static EventResult on_event(EventId id, void *user)
{
    (void)user;
    const EventArg *args;
    FmArg values[EVENT_MAX_ARGS];
    int n = Events_Args(&args);
    for (int i = 0; i < n; i++) {
        values[i] = (FmArg){ args[i].name, args[i].value };
    }
    EventId was = s_event;
    s_event = id;
    bool cancel = fm_scripts_event((int)id, values, n);
    s_event = was;
    return cancel ? EVENT_CANCEL : EVENT_CONTINUE;
}

static void listen(int event, bool on)
{
    if (on) {
        Events_On((EventId)event, on_event, NULL);
    } else {
        Events_Off((EventId)event, on_event, NULL);
    }
}

void dt_mods_configure(void)
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
        .tunes = TUNES,
        .tune_count = TUNE_COUNT,
        .calls = CALLS,
        .call_count = (int)(sizeof CALLS / sizeof CALLS[0]),
        .tune = tune,
        .state_open = GameRam_Open,
        .listen = listen,
        .begin_frame = GameRam_Begin,
        .end_frame = GameRam_End,
        .store_dir = s_store_dir,
    };
    fm_scripts_configure(&script);
}
