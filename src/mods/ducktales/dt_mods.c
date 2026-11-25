/*
 * dt_mods.c - DuckTales' side of the mods (app_mods.h, docs/MODS.md): what
 * the core (src/mods/core) and the NES module (src/mods/nes) are told about
 * this game, what its assets mean, and the dt table its scripts get.
 *
 *   game       "DuckTales (USA)", mod.toml prg_sha256 = the running PRG's,
 *              archives *.dtmod, the data folder's mods folder first
 *   tiles      tiles/<key>.png (fm_nes.h keys), painted over the presented
 *              picture: it is drawn again with nv_render_source to know each
 *              pixel's tile and palette, and a pixel is changed only where
 *              the presented picture shows the colour that drawing gives it
 *   palettes   palettes/global.pal or palettes/<land>.pal; a land's palette is
 *              used while $F6 is that land and the music is neither Land
 *              Select's nor the title's
 *   sprites    sprites/<kind>/<frame>_<x>_<y>[_flip].png, a whole object
 *              frame drawn in place of its 8x8 sprites (dt_mods_sprites.c)
 *   sounds     music/<id> and sfx/<id> (dt_mods_audio.c)
 *   loading    top first; a mod is used whole or not at all: any unreadable
 *              or invalid asset refuses it with the reason. Per asset the
 *              first mod that has it wins: a tile key, a sprite key, a land's palette (the
 *              mod's file for that land, else its global.pal), a sound id
 *   scripts    events = include/ducktales/events.h, with their values
 *              (Events_Args); dt.state and dt.set = FIELDS, dt.objects()
 *              and dt.camera() = QUERIES, read and written through the game
 *              API (ducktales/hud.h, game.h, player.h, objects.h) inside a RAM
 *              transaction; dt.store in the data folder's mods-data; toasts
 *              and dt.draw drawn with cyc_overlay over the presented picture
 *              only
 *   files      dt_mods.c: the game, folders, settings, options, the
 *              catalogue and loading; dt_mods_script.c: the scripts' side;
 *              dt_mods_picture.c: painting, the tile dump and the overlay;
 *              dt_mods_sprites.c: the sprite replacements and their dump;
 *              dt_mods_audio.c: the sounds
 */
#include "mods/app_mods.h"
#include "mods/ducktales/dt_mods_internal.h"
#include "mods/nes/fm_nes.h"

#include "game/ducktales_symbols.h"
#include "ui/app_rom.h"

#include "cyc_render.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static bool s_started;
static uint32_t *s_palettes[FM_MAX_MODS * (LANDS + 1)];
int dt_mods_palette_count;
const uint32_t *dt_mods_palette_for[LANDS + 1];
static char s_log_path[1100];

static const char *const LAND_NAMES[LANDS] = { "amazon", "transylvania", "african-mines", "himalayas", "moon" };

/* ---- folders, settings and options ---- */

void app_mods_add_folder(const char *dir, bool create)
{
    dt_mods_configure();
    fm_add_folder(dir, create);
}

const char *app_mods_folder(void)
{
    return fm_folder();
}

const char *app_mods_log_path(void)
{
    return s_log_path;
}

bool app_mods_setting(const char *key, const char *value)
{
    dt_mods_configure();
    return fm_setting(key, value);
}

void app_mods_read_config(const char *path)
{
    dt_mods_configure();
    fm_read_config(path);
}

void app_mods_write_settings(FILE *f)
{
    fm_write_settings(f);
}

bool app_mods_enabled(void)
{
    return fm_enabled();
}

void app_mods_set_enabled(bool on)
{
    fm_set_enabled(on);
}

void app_mods_use_player_folders(const char *config_path)
{
    dt_mods_configure();
    char dir[1024];
    if (config_path) {
        fm_read_config(config_path);
    }
    if (app_data_dir(dir, sizeof dir - 8)) {
        strcat(dir, "\\mods");
        fm_put_folder_first(dir);
    }
}

static const char *const OPTION_NAME[APP_MODS_OPTION_COUNT] = { "--mods", "--mods-dir", "--mods-order", "--mods-dump" };
static const char *const OPTION_HELP[APP_MODS_OPTION_COUNT] = {
    "on|off: load the mods (default: config.ini [Mods] Enabled in the window, off headless)",
    "DIR: also read mods from DIR (docs/MODS.md)",
    "ID:on|off,...: the mods' order and switches for this run, top first",
    "DIR: write every tile drawn, once, as DIR/<key>.png (templates for tile mods)",
};

const char *app_mods_option_name(int index)
{
    return OPTION_NAME[index];
}

const char *app_mods_option_help(int index)
{
    return OPTION_HELP[index];
}

bool app_mods_option(const char *name, const char *value, bool *ok)
{
    dt_mods_configure();
    if (!strcmp(name, "--mods")) {
        bool on = false;
        *ok = value && fm_word_on(value, &on);
        if (*ok) {
            fm_override_enabled(on);
        }
        return true;
    }
    bool dump = !strcmp(name, "--mods-dump");
    if (dump || !strcmp(name, "--mods-dir")) {
        char path[1024];
#ifdef _WIN32
        wchar_t wide[1024];
        bool named = value && *value && MultiByteToWideChar(CP_ACP, 0, value, -1, wide, 1024) &&
                     WideCharToMultiByte(CP_UTF8, 0, wide, -1, path, sizeof path, NULL, NULL);
#else
        bool named = value && *value && snprintf(path, sizeof path, "%s", value) < (int)sizeof path;
#endif
        bool exists = named && fm_is_dir(path);
        *ok = named && (dump ? app_mods_dump_dir(path) : exists && fm_add_folder(path, false));
        if (!*ok) {
            const char *why = !named    ? "missing or invalid path"
                              : dump    ? "the folder cannot be made"
                              : !exists ? "not a folder"
                                        : "at most 4 additional mods folders";
            fprintf(stderr, "[ducktales] %s %s: %s\n", name, value ? value : "", why);
        }
        return true;
    }
    if (!strcmp(name, "--mods-order")) {
        *ok = value != NULL;
        if (*ok) {
            fm_override_order(value);
        }
        return true;
    }
    return false;
}

/* ---- the catalogue ---- */

int app_mods_count(void)
{
    return fm_count();
}

const AppMod *app_mods_get(int index)
{
    return fm_get(index);
}

void app_mods_set_mod_enabled(int index, bool on)
{
    fm_set_mod_enabled(index, on);
}

void app_mods_move(int from, int to)
{
    fm_move(from, to);
}

static bool load_mod(AppMod *m, bool use, char *err, size_t cap);
static void log_catalogue(void);

/* The mods that are not loaded have their assets checked too, so the menu
 * shows now what a restart would refuse. */
void app_mods_rescan(void)
{
    fm_rescan();
    for (int i = 0; i < fm_count(); i++) {
        FmMod *m = fm_get(i);
        char err[300];
        if (!m->loaded && !m->error[0] && !load_mod(m, false, err, sizeof err)) {
            snprintf(m->error, sizeof m->error, "Not loaded: %s", err);
        }
    }
    fm_log("Mods: rescan, %d found", fm_count());
    log_catalogue();
}

bool app_mods_restart_needed(void)
{
    return fm_restart_needed();
}

void app_mods_scripts_frame(void)
{
    fm_scripts_frame();
}

/* ---- loading ---- */

static int land_of_file(const char *name)
{
    char base[FM_NAME_MAX];
    fm_base_name(name, base, sizeof base);
    size_t n = strlen(base);
    if (n < 4) {
        return -2;
    }
    base[n - 4] = 0;
    if (!_stricmp(base, "global")) {
        return LANDS;
    }
    for (int l = 0; l < LANDS; l++) {
        if (!_stricmp(base, LAND_NAMES[l])) {
            return l;
        }
    }
    return -2;
}

static bool sound_id(const char *name, const char *folder, int *id)
{
    char base[FM_NAME_MAX];
    if (strchr(name + strlen(folder), '/')) {
        return false;
    }
    fm_base_name(name, base, sizeof base);
    uint64_t v;
    if (strlen(base) < 6 || !fm_parse_hex(base, 2, &v) || (base[2] != '.' && base[2] != '-')) {
        return false;
    }
    *id = (int)v;
    return true;
}

typedef struct {
    bool music;
    int id;
    uint8_t *file;
    size_t size;
} PendingSound;

typedef struct {
    uint8_t kind, frame;
    bool flip;
    AppModsSprite art;
} PendingSprite;

typedef struct {
    uint64_t hash;
    uint32_t palette;
    int entry;
} TileKey;

static int by_key(const void *a, const void *b)
{
    const TileKey *x = a, *y = b;
    if (x->hash != y->hash) {
        return x->hash < y->hash ? -1 : 1;
    }
    return x->palette < y->palette ? -1 : x->palette > y->palette;
}

/* The archive's name of the later of two tile files with the same key, or NULL. */
static const char *same_tile(const FmArchive *a, const FmNesTile *tiles, const int *entries, int n)
{
    TileKey *keys = malloc((size_t)n * sizeof *keys + 1);
    const char *twice = NULL;
    if (!keys) {
        return NULL;
    }
    for (int i = 0; i < n; i++) {
        keys[i] = (TileKey){ tiles[i].hash, tiles[i].palette, entries[i] };
    }
    qsort(keys, (size_t)n, sizeof *keys, by_key);
    for (int i = 1; i < n && !twice; i++) {
        if (!by_key(&keys[i - 1], &keys[i])) {
            twice = fm_archive_name(a, keys[i - 1].entry > keys[i].entry ? keys[i - 1].entry : keys[i].entry);
        }
    }
    free(keys);
    return twice;
}

/* Reads and checks every asset of a mod, then uses it whole when use is set:
 * false with the reason. */
static bool load_mod(AppMod *m, bool use, char *err, size_t cap)
{
    FmArchive a;
    if (!fm_archive_open(&a, m->path, m->folder, err, cap)) {
        return false;
    }
    FmNesTile *tiles = NULL;
    int *tile_entries = NULL;
    int tile_n = 0;
    uint32_t *pals[LANDS + 1] = { NULL };
    PendingSound *sounds = NULL;
    int sound_n = 0;
    PendingSprite *sprites = NULL;
    int sprite_n = 0;
    bool ok = true;
    for (int i = 0; i < a.count && ok; i++) {
        const char *n = fm_archive_name(&a, i);
        if (!n) {
            continue;
        }
        bool tile = !_strnicmp(n, "tiles/", 6), pal = !_strnicmp(n, "palettes/", 9);
        bool music = !_strnicmp(n, "music/", 6), sfx = !_strnicmp(n, "sfx/", 4);
        bool sprite = !_strnicmp(n, "sprites/", 8);
        if (!tile && !pal && !music && !sfx && !sprite) {
            continue;
        }
        if ((tile || sprite) && !fm_ends_with(n, ".png")) {
            continue;
        }
        if (pal && !fm_ends_with(n, ".pal")) {
            continue;
        }
        if ((music || sfx) && !fm_ends_with(n, ".ogg") && !fm_ends_with(n, ".wav")) {
            continue;
        }
        uint64_t hash = 0;
        uint32_t palette = 0;
        int land = 0, id = 0;
        PendingSprite key = { 0 };
        if (sprite && !dt_mods_sprite_key(n, &key.kind, &key.frame, &key.flip, &key.art.ax, &key.art.ay)) {
            snprintf(err, cap,
                     "%s: a sprite is sprites/<kind in 2 hex digits>/<frame in 2 hex digits>_<x>_<y>.png, "
                     "or _<x>_<y>_flip.png.",
                     n);
            ok = false;
            break;
        }
        for (int k = 0; k < sprite_n && sprite; k++) {
            if (sprites[k].kind == key.kind && sprites[k].frame == key.frame && sprites[k].flip == key.flip) {
                snprintf(err, cap, "%s: the mod has another file for kind %02x frame %02x%s.", n, key.kind, key.frame,
                         key.flip ? " flipped" : "");
                ok = false;
            }
        }
        if (!ok) {
            break;
        }
        if (tile && !fm_nes_tile_key(n, &hash, &palette)) {
            snprintf(err, cap, "%s: a tile is named <16 hex digits>.png or <16 hex digits>_<8 hex digits>.png.", n);
            ok = false;
            break;
        }
        if (pal && (land = land_of_file(n)) < 0) {
            snprintf(err, cap,
                     "%s: palettes are global.pal or amazon, transylvania, african-mines, himalayas, moon .pal.", n);
            ok = false;
            break;
        }
        if ((music || sfx) && !sound_id(n, music ? "music/" : "sfx/", &id)) {
            snprintf(err, cap, "%s: a sound is named by its request id in 2 hex digits (07.ogg, 13-swing.wav).", n);
            ok = false;
            break;
        }
        if (pal && pals[land]) {
            snprintf(err, cap, "%s: the mod has another %s.pal.", n, land == LANDS ? "global" : LAND_NAMES[land]);
            ok = false;
            break;
        }
        for (int k = 0; k < sound_n && (music || sfx); k++) {
            if (sounds[k].music == music && sounds[k].id == id) {
                snprintf(err, cap, "%s: the mod has another file for %s %02X.", n, music ? "music" : "effect", id);
                ok = false;
            }
        }
        if (!ok) {
            break;
        }
        size_t size;
        uint8_t *data = fm_archive_read(&a, i, &size, err, cap);
        if (!data) {
            ok = false;
            break;
        }
        if (sprite) {
            int w, h;
            uint32_t *argb = fm_png_decode(data, size, &w, &h);
            free(data);
            PendingSprite *grown = argb ? realloc(sprites, (size_t)(sprite_n + 1) * sizeof *grown) : NULL;
            if (!argb) {
                snprintf(err, cap, "%s: not a readable PNG.", n);
                ok = false;
            } else if (w > NV_WIDTH || h > NV_HEIGHT) {
                free(argb);
                snprintf(err, cap, "%s: %dx%d; a sprite is at most %dx%d.", n, w, h, NV_WIDTH, NV_HEIGHT);
                ok = false;
            } else if (!grown) {
                free(argb);
                snprintf(err, cap, "Out of memory.");
                ok = false;
            } else {
                sprites = grown;
                key.art.w = w;
                key.art.h = h;
                key.art.argb = argb;
                sprites[sprite_n++] = key;
            }
        } else if (tile) {
            int w, h;
            uint32_t *argb = fm_png_decode(data, size, &w, &h);
            free(data);
            if (!argb) {
                snprintf(err, cap, "%s: not a readable PNG.", n);
                ok = false;
            } else if (w != 8 || h != 8) {
                free(argb);
                snprintf(err, cap, "%s: %dx%d; replacement tiles must be 8x8 for now.", n, w, h);
                ok = false;
            } else {
                FmNesTile *grown = realloc(tiles, (size_t)(tile_n + 1) * sizeof *grown);
                int *grown_entries = grown ? realloc(tile_entries, (size_t)(tile_n + 1) * sizeof *grown_entries) : NULL;
                tiles = grown ? grown : tiles;
                tile_entries = grown_entries ? grown_entries : tile_entries;
                if (!grown || !grown_entries) {
                    free(argb);
                    ok = false;
                    snprintf(err, cap, "Out of memory.");
                } else {
                    tile_entries[tile_n] = i;
                    tiles[tile_n++] = (FmNesTile){ hash, palette, w, h, argb };
                }
            }
        } else if (pal) {
            uint32_t *p = fm_nes_pal_read(data, size, nv_palette_argb());
            free(data);
            if (!p) {
                snprintf(err, cap, "%s: %zu bytes; a .pal holds 64 or 512 RGB triples (192 or 1536 bytes).", n, size);
                ok = false;
            } else {
                free(pals[land]);
                pals[land] = p;
            }
        } else {
            FmSound *probe = fm_sound_open(data, size, err, cap);
            if (!probe) {
                char why[200];
                snprintf(why, sizeof why, "%s", err);
                snprintf(err, cap, "%s: %s", n, why);
                free(data);
                ok = false;
            } else {
                fm_sound_close(probe);
                PendingSound *grown = realloc(sounds, (size_t)(sound_n + 1) * sizeof *grown);
                if (!grown) {
                    free(data);
                    ok = false;
                    snprintf(err, cap, "Out of memory.");
                } else {
                    sounds = grown;
                    sounds[sound_n++] = (PendingSound){ music, id, data, size };
                    if (music ? id >= DT_SOUND_MUSIC_IDS : (id < DT_SOUND_MUSIC_IDS || id >= DT_SOUND_IDS)) {
                        snprintf(err, cap, "%s: %s", n,
                                 music ? "music ids are 00-0F (docs/AUDIO.md)."
                                       : "effect ids are 10-37 (docs/AUDIO.md).");
                        ok = false;
                    }
                }
            }
        }
    }
    const char *twice = ok ? same_tile(&a, tiles, tile_entries, tile_n) : NULL;
    if (twice) {
        snprintf(err, cap, "%s: the mod has another file for this tile and palette.", twice);
        ok = false;
    }
    fm_archive_close(&a);
    if (ok && use) {
        for (int i = 0; i < tile_n; i++) {
            fm_nes_tile_insert(&tiles[i]);
        }
        for (int i = 0; i < sprite_n; i++) {
            dt_mods_sprite_insert(sprites[i].kind, sprites[i].frame, sprites[i].flip, &sprites[i].art);
        }
        for (int l = 0; l <= LANDS; l++) {
            if (pals[l]) {
                s_palettes[dt_mods_palette_count++] = pals[l];
            }
        }
        for (int l = 0; l < LANDS; l++) {
            if (!dt_mods_palette_for[l] && (pals[l] || pals[LANDS])) {
                dt_mods_palette_for[l] = pals[l] ? pals[l] : pals[LANDS];
            }
        }
        if (!dt_mods_palette_for[LANDS] && pals[LANDS]) {
            dt_mods_palette_for[LANDS] = pals[LANDS];
        }
        for (int i = 0; i < sound_n; i++) {
            char why[160];
            if (!mods_audio_has(sounds[i].music, sounds[i].id)) {
                mods_audio_add(sounds[i].music, sounds[i].id, sounds[i].file, sounds[i].size, why, sizeof why);
            } else {
                free(sounds[i].file);
            }
        }
    } else {
        for (int i = 0; i < tile_n; i++) {
            free(tiles[i].argb);
        }
        for (int i = 0; i < sprite_n; i++) {
            free((void *)sprites[i].art.argb);
        }
        for (int l = 0; l <= LANDS; l++) {
            free(pals[l]);
        }
        for (int i = 0; i < sound_n; i++) {
            free(sounds[i].file);
        }
    }
    free(tiles);
    free(tile_entries);
    free(sounds);
    free(sprites);
    return ok;
}

/* One line per mod: what it is, and what was loaded or why not. */
static void log_catalogue(void)
{
    for (int i = 0; i < fm_count(); i++) {
        const FmMod *m = fm_get(i);
        if (m->error[0]) {
            fm_log("Mod %s: %s", m->id, m->error);
        } else {
            fm_log("Mod %s: %s %s, %s%s%s", m->id, m->name, m->version, m->loaded ? "loaded" : "not loaded",
                   m->enabled ? "" : ", switched off", m->folder ? ", folder" : "");
        }
    }
}

void app_mods_start(const uint8_t *prg, size_t prg_size)
{
    dt_mods_configure();
    if (s_started) {
        return;
    }
    s_started = true;
    char log[sizeof s_log_path];
    if (fm_enabled() && app_data_dir(log, sizeof log - 16)) {
        fm_make_dir(log);
        strcat(log, "\\mods.log");
        if (fm_log_open(log)) {
            snprintf(s_log_path, sizeof s_log_path, "%s", log);
        }
    }
    if (prg && prg_size) {
        uint8_t digest[32];
        char hex[65];
        app_sha256(prg, prg_size, digest);
        app_sha256_hex(digest, hex);
        fm_set_target(hex);
        mods_audio_read_driver(prg, prg_size);
    }
    fm_scan();
    int used = 0, scripts = 0;
    for (int i = 0; i < fm_count() && fm_enabled(); i++) {
        FmMod *m = fm_get(i);
        if (!m->enabled || m->error[0]) {
            continue;
        }
        char err[300];
        if (load_mod(m, true, err, sizeof err)) {
            m->loaded = true;
            used++;
            scripts += m->script;
        } else {
            snprintf(m->error, sizeof m->error, "Not loaded: %s", err);
        }
    }
    fm_mark_loaded_set();
    fm_log("Mods: %s, %d found, %d loaded (%zu tiles, %d sprites, %d palettes, sounds %s, %d scripts)",
           fm_enabled() ? "on" : "off", fm_count(), used, fm_nes_tile_count(), dt_mods_sprite_count(),
           dt_mods_palette_count, app_mods_audio_active() ? "replaced" : "none", scripts);
    log_catalogue();
    for (int i = 0; i < fm_count(); i++) {
        if (fm_get(i)->loaded && fm_get(i)->script) {
            fm_script_add(fm_get(i));
        }
    }
}
