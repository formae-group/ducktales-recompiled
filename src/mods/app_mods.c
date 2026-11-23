/*
 * app_mods.c - the mod catalogue, its settings, loading, and the tile and
 * palette replacements (app_mods.h, docs/mods.md).
 *
 *   settings   config.ini [Mods] Enabled = on|off (default off) and
 *              Order = id:on, id:off, ... top first; ids no longer present
 *              are kept at the end of Order so a mod taken out and put back
 *              keeps its place
 *   mod.toml   id, name, version, author, description, game (must be
 *              "DuckTales (USA)") and prg_sha256 (must be the running PRG's)
 *   loading    top first; a mod is used whole or not at all: any unreadable
 *              or invalid asset refuses it with the reason. Per asset the
 *              first mod that has it wins: a tile key, a land's palette (the
 *              mod's file for that land, else its global.pal), a sound id
 *   tiles      tiles/<hash>.png (any palette) or <hash>_<c0c1c2c3>.png, in
 *              any subfolder of tiles/;
 *              hash = FNV-1a 64 of the tile's 16 CHR bytes, c0-c3 = the NES
 *              colours of the palette ($3F00 first), 2 hex digits each. The
 *              art is 8x8 for now; it is stored with its size so larger art
 *              can be sampled later
 *   palettes   palettes/global.pal or palettes/<land>.pal: 64 or 512 RGB
 *              triples (the emulators' .pal format); a land's palette is
 *              used while $F6 is that land and the music is neither Land
 *              Select's nor the title's
 *   painting   the picture is drawn again with nv_render_source to know each
 *              pixel's tile and palette; a pixel is changed only where the
 *              presented picture shows the colour that drawing gives it
 */
#include "mods/app_mods.h"
#include "mods/mods_internal.h"

#include "game/ducktales_symbols.h"
#include "ui/app_rom.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FOLDERS 4
#define MAX_MODS    256
#define LANDS       5
#define ANY_PALETTE 0xFFFFFFFFu

typedef struct {
    char id[64];
    bool on;
} OrderItem;

typedef struct {
    uint64_t  hash;
    uint32_t  palette;
    int       w, h;
    uint32_t *argb;
} TileArt;

static char s_folders[MAX_FOLDERS][1024];
static int s_folder_count;
static bool s_enabled = false;
static OrderItem s_order[MAX_MODS];
static int s_order_count;
static AppMod s_mods[MAX_MODS];
static int s_count;
static char s_prg_hex[65];
static bool s_started;
static bool s_loaded_enabled;
static char s_loaded_set[MAX_MODS * 66];

static TileArt *s_tiles;
static size_t s_tile_cap, s_tile_count;
static uint32_t *s_palettes[MAX_MODS * (LANDS + 1)];
static int s_palette_count;
static const uint32_t *s_palette_for[LANDS + 1];
static char s_dump_dir[1024];
static uint64_t *s_dumped;
static size_t s_dumped_cap, s_dumped_count;

static const char *const LAND_NAMES[LANDS] = { "amazon", "transylvania", "african-mines", "himalayas", "moon" };

/* ---- small text helpers ---- */

static bool word_on(const char *v, bool *out)
{
    static const char *ON[] = { "on", "1", "true", "yes" }, *OFF[] = { "off", "0", "false", "no" };
    for (int i = 0; i < 4; i++) {
        if (!_stricmp(v, ON[i])) { *out = true; return true; }
        if (!_stricmp(v, OFF[i])) { *out = false; return true; }
    }
    return false;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static bool valid_id(const char *id)
{
    size_t n = strlen(id);
    if (!n || n > 63 || !isalnum((unsigned char)id[0])) return false;
    for (size_t i = 0; i < n; i++)
        if (!(islower((unsigned char)id[i]) || isdigit((unsigned char)id[i]) || id[i] == '-' || id[i] == '_' || id[i] == '.'))
            return false;
    return true;
}

static bool ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !_stricmp(s + a - b, tail);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static bool parse_hex(const char *s, int digits, uint64_t *out)
{
    uint64_t v = 0;
    for (int i = 0; i < digits; i++) {
        int d = hex_digit(s[i]);
        if (d < 0) return false;
        v = v << 4 | (uint64_t)d;
    }
    *out = v;
    return true;
}

static void lower(char *s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* ---- settings ---- */

static void order_parse(const char *text)
{
    s_order_count = 0;
    char buf[MAX_MODS * 72];
    snprintf(buf, sizeof buf, "%s", text);
    for (char *item = strtok(buf, ","); item && s_order_count < MAX_MODS; item = strtok(NULL, ",")) {
        char *t = trim(item), *colon = strrchr(t, ':');
        bool on = true;
        if (colon) {
            *colon = 0;
            if (!word_on(trim(colon + 1), &on)) on = true;
        }
        t = trim(t);
        if (!valid_id(t)) continue;
        bool dup = false;
        for (int i = 0; i < s_order_count; i++) dup |= !strcmp(s_order[i].id, t);
        if (dup) continue;
        snprintf(s_order[s_order_count].id, sizeof s_order[0].id, "%s", t);
        s_order[s_order_count++].on = on;
    }
}

static void order_from_catalogue(void)
{
    OrderItem kept[MAX_MODS];
    int n = 0;
    for (int i = 0; i < s_count && n < MAX_MODS; i++) {
        if (!valid_id(s_mods[i].id)) continue;
        bool dup = false;
        for (int k = 0; k < n; k++) dup |= !strcmp(kept[k].id, s_mods[i].id);
        if (dup) continue;
        snprintf(kept[n].id, sizeof kept[n].id, "%s", s_mods[i].id);
        kept[n++].on = s_mods[i].enabled;
    }
    for (int j = 0; j < s_order_count && n < MAX_MODS; j++) {
        bool present = false;
        for (int k = 0; k < n; k++) present |= !strcmp(kept[k].id, s_order[j].id);
        if (!present) kept[n++] = s_order[j];
    }
    memcpy(s_order, kept, (size_t)n * sizeof kept[0]);
    s_order_count = n;
}

bool app_mods_setting(const char *key, const char *value)
{
    if (!_stricmp(key, "Enabled")) {
        bool on;
        if (word_on(value, &on)) s_enabled = on;
        else fprintf(stderr, "[ducktales] config.ini [Mods] Enabled = %s: expected on or off; using %s\n", value, s_enabled ? "on" : "off");
        return true;
    }
    if (!_stricmp(key, "Order")) {
        order_parse(value);
        return true;
    }
    return false;
}

void app_mods_read_config(const char *path)
{
    size_t size;
    char *text = (char *)mod_read_file(path, &size);
    if (!text) return;
    bool in = false;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char *t = trim(line);
        if (*t == '[') {
            in = !_strnicmp(t, "[Mods]", 6);
            continue;
        }
        char *eq = strchr(t, '=');
        if (!in || !eq || *t == '#' || *t == ';') continue;
        *eq = 0;
        app_mods_setting(trim(t), trim(eq + 1));
    }
    free(text);
}

void app_mods_write_settings(FILE *f)
{
    fprintf(f, "[Mods]\nEnabled = %s\nOrder = ", s_enabled ? "on" : "off");
    for (int i = 0; i < s_order_count; i++) fprintf(f, "%s%s:%s", i ? ", " : "", s_order[i].id, s_order[i].on ? "on" : "off");
    fprintf(f, "\n");
}

bool app_mods_enabled(void) { return s_enabled; }
void app_mods_set_enabled(bool on) { s_enabled = on; }

void app_mods_add_folder(const char *dir, bool create)
{
    if (!dir || !*dir || s_folder_count >= MAX_FOLDERS) return;
    for (int i = 0; i < s_folder_count; i++)
        if (!_stricmp(s_folders[i], dir)) return;
    if (create) mod_make_dir(dir);
    snprintf(s_folders[s_folder_count++], sizeof s_folders[0], "%s", dir);
}

const char *app_mods_folder(void) { return s_folder_count ? s_folders[0] : ""; }

void app_mods_use_player_folders(const char *config_path)
{
    char dir[1024];
    if (config_path) {
        app_mods_read_config(config_path);
    }
    if (app_data_dir(dir, sizeof dir - 8)) {
        strcat(dir, "\\mods");
        char moved[MAX_FOLDERS][1024];
        int n = s_folder_count;
        memcpy(moved, s_folders, sizeof moved);
        s_folder_count = 0;
        app_mods_add_folder(dir, true);
        for (int i = 0; i < n; i++) {
            app_mods_add_folder(moved[i], false);
        }
    }
}

static const char *const OPTION_NAME[APP_MODS_OPTION_COUNT] = { "--mods", "--mods-dir", "--mods-order", "--mods-dump" };
static const char *const OPTION_HELP[APP_MODS_OPTION_COUNT] = {
    "on|off: load the mods (default: config.ini [Mods] Enabled in the window, off headless)",
    "DIR: also read mods from DIR (docs/mods.md)",
    "ID:on|off,...: the mods' order and switches for this run, top first",
    "DIR: write every tile drawn, once, as DIR/<key>.png (templates for tile mods)",
};

const char *app_mods_option_name(int index) { return OPTION_NAME[index]; }
const char *app_mods_option_help(int index) { return OPTION_HELP[index]; }

bool app_mods_option(const char *name, const char *value, bool *ok)
{
    if (!strcmp(name, "--mods")) {
        bool on = false;
        *ok = value && word_on(value, &on);
        if (*ok) {
            s_enabled = on;
        }
        return true;
    }
    if (!strcmp(name, "--mods-dir")) {
        *ok = value && *value;
        app_mods_add_folder(value, false);
        return true;
    }
    if (!strcmp(name, "--mods-order")) {
        *ok = value != NULL;
        if (*ok) {
            order_parse(value);
        }
        return true;
    }
    if (!strcmp(name, "--mods-dump")) {
        *ok = value && app_mods_dump_dir(value);
        return true;
    }
    return false;
}

/* ---- mod.toml: top-level keys with string values (basic, literal and multi-line) ---- */

static bool toml_string(char **cursor, char *out, size_t cap, char *err, size_t errcap)
{
    char *p = *cursor;
    size_t n = 0;
    bool multi = !strncmp(p, "\"\"\"", 3) || !strncmp(p, "'''", 3), literal = *p == '\'';
    char quote = *p;
    p += multi ? 3 : 1;
    if (multi && *p == '\r') p++;
    if (multi && *p == '\n') p++;
    for (;;) {
        if (!*p || (!multi && (*p == '\n' || *p == '\r'))) {
            snprintf(err, errcap, "unterminated string");
            return false;
        }
        if (multi ? (p[0] == quote && p[1] == quote && p[2] == quote) : *p == quote) break;
        char c = *p++;
        if (c == '\\' && !literal) {
            char e = *p++;
            c = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == '"' ? '"' : e == '\\' ? '\\' : 0;
            if (!c) {
                snprintf(err, errcap, "unsupported escape \\%c", e);
                return false;
            }
        }
        if (c == '\r') continue;
        if (n + 1 < cap) out[n++] = c;
    }
    out[n] = 0;
    *cursor = p + (multi ? 3 : 1);
    return true;
}

static bool parse_toml(char *text, AppMod *m, char *game, size_t game_cap, char *prg, size_t prg_cap, char *err, size_t cap)
{
    char *p = text;
    bool top = true;
    int line = 1;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '\n') { p++; line++; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (*p == '[') { top = false; while (*p && *p != '\n') p++; continue; }
        char *key = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-')) p++;
        size_t klen = (size_t)(p - key);
        while (*p == ' ' || *p == '\t') p++;
        if (!klen || *p != '=') {
            snprintf(err, cap, "mod.toml line %d: expected key = value", line);
            return false;
        }
        p++;
        while (*p == ' ' || *p == '\t') p++;
        char value[1024], why[64];
        if (*p == '"' || *p == '\'') {
            char *start = p;
            if (!toml_string(&p, value, sizeof value, why, sizeof why)) {
                snprintf(err, cap, "mod.toml line %d: %s", line, why);
                return false;
            }
            for (char *q = start; q < p; q++) line += *q == '\n';
        } else {
            char *v = p;
            while (*p && *p != '\n' && *p != '#') p++;
            snprintf(value, sizeof value, "%.*s", (int)(p - v), v);
            trim(value);
        }
        while (*p && *p != '\n') {
            if (*p == '#') { while (*p && *p != '\n') p++; break; }
            if (*p != ' ' && *p != '\t' && *p != '\r') {
                snprintf(err, cap, "mod.toml line %d: unexpected text after the value", line);
                return false;
            }
            p++;
        }
        if (!top) continue;
#define KEY(name) (klen == sizeof name - 1 && !strncmp(key, name, klen))
        if (KEY("id")) snprintf(m->id, sizeof m->id, "%s", value);
        else if (KEY("name")) snprintf(m->name, sizeof m->name, "%s", value);
        else if (KEY("version")) snprintf(m->version, sizeof m->version, "%s", value);
        else if (KEY("author")) snprintf(m->author, sizeof m->author, "%s", value);
        else if (KEY("description")) snprintf(m->description, sizeof m->description, "%s", value);
        else if (KEY("game")) snprintf(game, game_cap, "%s", value);
        else if (KEY("prg_sha256")) snprintf(prg, prg_cap, "%s", value);
#undef KEY
    }
    return true;
}

/* ---- the catalogue ---- */

static void base_name(const char *path, char *out, size_t cap)
{
    const char *b = path;
    for (const char *c = path; *c; c++)
        if (*c == '\\' || *c == '/') b = c + 1;
    snprintf(out, cap, "%s", b);
}

static void read_mod(AppMod *m)
{
    char err[256] = "";
    ModArchive a;
    base_name(m->path, m->name, sizeof m->name);
    snprintf(m->id, sizeof m->id, "%s", m->name);
    lower(m->id);
    if (!mod_archive_open(&a, m->path, m->folder, err, sizeof err)) {
        snprintf(m->error, sizeof m->error, "Cannot be read: %s", err);
        return;
    }
    int at = mod_archive_find(&a, "mod.toml");
    size_t size;
    char *text = at < 0 ? NULL : (char *)mod_archive_read(&a, at, &size, err, sizeof err);
    char game[128] = "", prg[80] = "";
    if (at < 0) snprintf(m->error, sizeof m->error, "Cannot be read: no mod.toml in the archive.");
    else if (!text) snprintf(m->error, sizeof m->error, "Cannot be read: %s", err);
    else {
        char name_from_file[128];
        snprintf(name_from_file, sizeof name_from_file, "%s", m->name);
        m->id[0] = m->name[0] = 0;
        if (!parse_toml(text, m, game, sizeof game, prg, sizeof prg, err, sizeof err))
            snprintf(m->error, sizeof m->error, "Cannot be read: %s", err);
        else if (!valid_id(m->id))
            snprintf(m->error, sizeof m->error, "mod.toml: id \"%s\" must be 1-63 characters of a-z, 0-9, '-', '_' or '.'.", m->id);
        else if (!m->name[0] || !m->version[0] || !game[0] || !prg[0])
            snprintf(m->error, sizeof m->error, "mod.toml: name, version, game and prg_sha256 are required.");
        else if (strcmp(game, APP_MODS_GAME))
            snprintf(m->error, sizeof m->error, "Made for \"%s\", not %s.", game, APP_MODS_GAME);
        else {
            lower(prg);
            const char *running = s_prg_hex[0] ? s_prg_hex : APP_MODS_PRG_SHA256;
            if (strcmp(prg, running))
                snprintf(m->error, sizeof m->error, "Made for a different version of the game: it targets PRG SHA-256 %.12s..., this game's is %.12s....",
                         prg, running);
        }
        if (!m->id[0]) {
            snprintf(m->id, sizeof m->id, "%s", name_from_file);
            lower(m->id);
        }
        if (!m->name[0]) snprintf(m->name, sizeof m->name, "%s", name_from_file);
    }
    free(text);
    for (int i = 0; i < a.count; i++) {
        const char *n = mod_archive_name(&a, i);
        if (!n) continue;
        m->tiles += !_strnicmp(n, "tiles/", 6) && ends_with(n, ".png");
        m->palettes += !_strnicmp(n, "palettes/", 9) && ends_with(n, ".pal");
        m->music += !_strnicmp(n, "music/", 6);
        m->sfx += !_strnicmp(n, "sfx/", 4);
    }
    mod_archive_close(&a);
}

typedef struct {
    const char *dir;
} ScanCtx;

static void scan_entry(void *user, const char *name, bool folder)
{
    const ScanCtx *ctx = user;
    if (s_count >= MAX_MODS) return;
    char path[1024];
    snprintf(path, sizeof path, "%s\\%s", ctx->dir, name);
    if (folder) {
        char toml[1100];
        snprintf(toml, sizeof toml, "%s\\mod.toml", path);
        size_t n;
        uint8_t *probe = mod_read_file(toml, &n);
        if (!probe) return;
        free(probe);
    } else if (!ends_with(name, APP_MODS_EXTENSION)) {
        return;
    }
    AppMod *m = &s_mods[s_count++];
    memset(m, 0, sizeof *m);
    snprintf(m->path, sizeof m->path, "%s", path);
    m->folder = folder;
    read_mod(m);
}

static int by_name(const void *a, const void *b)
{
    return _stricmp(((const AppMod *)a)->name, ((const AppMod *)b)->name);
}

static void scan(void)
{
    s_count = 0;
    for (int f = 0; f < s_folder_count; f++) {
        ScanCtx ctx = { s_folders[f] };
        mod_list_dir(s_folders[f], scan_entry, &ctx);
    }
    qsort(s_mods, (size_t)s_count, sizeof s_mods[0], by_name);
    for (int i = 0; i < s_count; i++)
        for (int k = 0; k < i; k++)
            if (!s_mods[i].error[0] && !s_mods[k].error[0] && !strcmp(s_mods[i].id, s_mods[k].id))
                snprintf(s_mods[i].error, sizeof s_mods[i].error, "Another mod has the same id \"%s\": %s.", s_mods[i].id, s_mods[k].path);
    static AppMod sorted[MAX_MODS];
    bool used[MAX_MODS] = { false };
    int n = 0;
    for (int j = 0; j < s_order_count; j++)
        for (int i = 0; i < s_count; i++)
            if (!used[i] && !strcmp(s_mods[i].id, s_order[j].id)) {
                used[i] = true;
                sorted[n] = s_mods[i];
                sorted[n++].enabled = s_order[j].on;
                break;
            }
    for (int i = 0; i < s_count; i++)
        if (!used[i]) {
            sorted[n] = s_mods[i];
            sorted[n++].enabled = true;
        }
    memcpy(s_mods, sorted, (size_t)n * sizeof sorted[0]);
    order_from_catalogue();
}

static void loaded_set(char *out, size_t cap, bool *enabled)
{
    size_t n = 0;
    out[0] = 0;
    *enabled = s_enabled;
    if (!s_enabled) return;
    for (int i = 0; i < s_count; i++)
        if (s_mods[i].enabled && !s_mods[i].error[0]) n += (size_t)snprintf(out + n, n < cap ? cap - n : 0, "%s,", s_mods[i].id);
}

int app_mods_count(void) { return s_count; }
const AppMod *app_mods_get(int index) { return index >= 0 && index < s_count ? &s_mods[index] : NULL; }

void app_mods_set_mod_enabled(int index, bool on)
{
    if (index < 0 || index >= s_count) return;
    s_mods[index].enabled = on;
    order_from_catalogue();
}

void app_mods_move(int from, int to)
{
    if (from < 0 || from >= s_count || to < 0 || to >= s_count || from == to) return;
    AppMod m = s_mods[from];
    if (from < to) memmove(&s_mods[from], &s_mods[from + 1], (size_t)(to - from) * sizeof m);
    else memmove(&s_mods[to + 1], &s_mods[to], (size_t)(from - to) * sizeof m);
    s_mods[to] = m;
    order_from_catalogue();
}

void app_mods_rescan(void)
{
    bool loaded[MAX_MODS];
    char ids[MAX_MODS][64];
    int n = s_count;
    for (int i = 0; i < n; i++) {
        loaded[i] = s_mods[i].loaded;
        snprintf(ids[i], sizeof ids[i], "%s", s_mods[i].id);
    }
    order_from_catalogue();
    scan();
    for (int i = 0; i < s_count; i++)
        for (int k = 0; k < n; k++)
            if (!strcmp(ids[k], s_mods[i].id)) s_mods[i].loaded = loaded[k];
}

bool app_mods_restart_needed(void)
{
    static char now[sizeof s_loaded_set];
    bool enabled;
    loaded_set(now, sizeof now, &enabled);
    return s_started && (enabled != s_loaded_enabled || strcmp(now, s_loaded_set));
}

/* ---- loading ---- */

static uint64_t tile_hash(const uint8_t *chr)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < 16; i++) h = (h ^ chr[i]) * 0x100000001b3ull;
    return h;
}

static size_t tile_slot(uint64_t hash, uint32_t palette)
{
    uint64_t k = hash ^ ((uint64_t)palette * 0x9E3779B97F4A7C15ull);
    return (size_t)(k ^ (k >> 29)) & (s_tile_cap - 1);
}

static TileArt *tile_find(uint64_t hash, uint32_t palette)
{
    if (!s_tile_cap) return NULL;
    for (size_t i = tile_slot(hash, palette);; i = (i + 1) & (s_tile_cap - 1)) {
        TileArt *t = &s_tiles[i];
        if (!t->argb) return NULL;
        if (t->hash == hash && t->palette == palette) return t;
    }
}

static void tile_put(const TileArt *art)
{
    for (size_t i = tile_slot(art->hash, art->palette);; i = (i + 1) & (s_tile_cap - 1))
        if (!s_tiles[i].argb) {
            s_tiles[i] = *art;
            return;
        }
}

/* Takes art->argb; false (and frees it) when the key is taken or memory is short. */
static bool tile_insert(TileArt *art)
{
    if (tile_find(art->hash, art->palette)) {
        free(art->argb);
        return false;
    }
    if ((s_tile_count + 1) * 2 > s_tile_cap) {
        size_t old_cap = s_tile_cap, cap = old_cap ? old_cap * 2 : 1024;
        TileArt *old = s_tiles, *grown = calloc(cap, sizeof *grown);
        if (!grown) {
            free(art->argb);
            return false;
        }
        s_tiles = grown;
        s_tile_cap = cap;
        for (size_t i = 0; i < old_cap; i++)
            if (old[i].argb) tile_put(&old[i]);
        free(old);
    }
    tile_put(art);
    s_tile_count++;
    return true;
}

/* The base name of a tile file as its key: false when it is not one. */
static bool tile_key(const char *name, uint64_t *hash, uint32_t *palette)
{
    char base[MOD_NAME_MAX];
    base_name(name, base, sizeof base);
    size_t n = strlen(base);
    if (n < 4 || _stricmp(base + n - 4, ".png")) return false;
    n -= 4;
    uint64_t pal;
    if (n == 16 && parse_hex(base, 16, hash)) {
        *palette = ANY_PALETTE;
        return true;
    }
    if (n == 25 && base[16] == '_' && parse_hex(base, 16, hash) && parse_hex(base + 17, 8, &pal)) {
        if ((pal & 0xC0C0C0C0u) != 0) return false;
        *palette = (uint32_t)pal;
        return true;
    }
    return false;
}

static int land_of_file(const char *name)
{
    char base[MOD_NAME_MAX];
    base_name(name, base, sizeof base);
    size_t n = strlen(base);
    if (n < 4) return -2;
    base[n - 4] = 0;
    if (!_stricmp(base, "global")) return LANDS;
    for (int l = 0; l < LANDS; l++)
        if (!_stricmp(base, LAND_NAMES[l])) return l;
    return -2;
}

static uint32_t *read_pal(const uint8_t *d, size_t size)
{
    if (size != 64 * 3 && size != 512 * 3) return NULL;
    const uint32_t *base = nv_palette_argb();
    uint32_t *pal = malloc(512 * sizeof *pal);
    if (!pal) return NULL;
    size_t entries = size / 3;
    for (size_t i = 0; i < 512; i++) {
        if (entries == 64 && i >= 64) {
            pal[i] = base[i];
            continue;
        }
        const uint8_t *p = d + i * 3;
        pal[i] = 0xFF000000u | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    }
    return pal;
}

static bool sound_id(const char *name, const char *folder, int *id)
{
    char base[MOD_NAME_MAX];
    if (strchr(name + strlen(folder), '/')) return false;
    base_name(name, base, sizeof base);
    uint64_t v;
    if (strlen(base) < 6 || !parse_hex(base, 2, &v) || (base[2] != '.' && base[2] != '-')) return false;
    *id = (int)v;
    return true;
}

typedef struct {
    bool     music;
    int      id;
    uint8_t *file;
    size_t   size;
} PendingSound;

/* Reads and checks every asset of a mod, then uses it whole: false with the reason. */
static bool load_mod(AppMod *m, char *err, size_t cap)
{
    ModArchive a;
    if (!mod_archive_open(&a, m->path, m->folder, err, cap)) return false;
    TileArt *tiles = NULL;
    int tile_n = 0;
    uint32_t *pals[LANDS + 1] = { NULL };
    PendingSound *sounds = NULL;
    int sound_n = 0;
    bool ok = true;
    for (int i = 0; i < a.count && ok; i++) {
        const char *n = mod_archive_name(&a, i);
        if (!n) continue;
        bool tile = !_strnicmp(n, "tiles/", 6), pal = !_strnicmp(n, "palettes/", 9);
        bool music = !_strnicmp(n, "music/", 6), sfx = !_strnicmp(n, "sfx/", 4);
        if (!tile && !pal && !music && !sfx) continue;
        if (tile && !ends_with(n, ".png")) continue;
        if (pal && !ends_with(n, ".pal")) continue;
        if ((music || sfx) && !ends_with(n, ".ogg") && !ends_with(n, ".wav")) continue;
        uint64_t hash = 0;
        uint32_t palette = 0;
        int land = 0, id = 0;
        if (tile && !tile_key(n, &hash, &palette)) {
            snprintf(err, cap, "%s: a tile is named <16 hex digits>.png or <16 hex digits>_<8 hex digits>.png.", n);
            ok = false;
            break;
        }
        if (pal && (land = land_of_file(n)) < 0) {
            snprintf(err, cap, "%s: palettes are global.pal or amazon, transylvania, african-mines, himalayas, moon .pal.", n);
            ok = false;
            break;
        }
        if ((music || sfx) && !sound_id(n, music ? "music/" : "sfx/", &id)) {
            snprintf(err, cap, "%s: a sound is named by its request id in 2 hex digits (07.ogg, 13-swing.wav).", n);
            ok = false;
            break;
        }
        size_t size;
        uint8_t *data = mod_archive_read(&a, i, &size, err, cap);
        if (!data) {
            ok = false;
            break;
        }
        if (tile) {
            int w, h;
            uint32_t *argb = mod_png_decode(data, size, &w, &h);
            free(data);
            if (!argb) {
                snprintf(err, cap, "%s: not a readable PNG.", n);
                ok = false;
            } else if (w != 8 || h != 8) {
                free(argb);
                snprintf(err, cap, "%s: %dx%d; replacement tiles must be 8x8 for now.", n, w, h);
                ok = false;
            } else {
                TileArt *grown = realloc(tiles, (size_t)(tile_n + 1) * sizeof *grown);
                if (!grown) {
                    free(argb);
                    ok = false;
                    snprintf(err, cap, "Out of memory.");
                } else {
                    tiles = grown;
                    tiles[tile_n++] = (TileArt){ hash, palette, w, h, argb };
                }
            }
        } else if (pal) {
            uint32_t *p = read_pal(data, size);
            free(data);
            if (!p) {
                snprintf(err, cap, "%s: %zu bytes; a .pal holds 64 or 512 RGB triples (192 or 1536 bytes).", n, size);
                ok = false;
            } else {
                free(pals[land]);
                pals[land] = p;
            }
        } else {
            ModSound *probe = mod_sound_open(data, size, err, cap);
            if (!probe) {
                char why[200];
                snprintf(why, sizeof why, "%s", err);
                snprintf(err, cap, "%s: %s", n, why);
                free(data);
                ok = false;
            } else {
                mod_sound_close(probe);
                PendingSound *grown = realloc(sounds, (size_t)(sound_n + 1) * sizeof *grown);
                if (!grown) {
                    free(data);
                    ok = false;
                    snprintf(err, cap, "Out of memory.");
                } else {
                    sounds = grown;
                    sounds[sound_n++] = (PendingSound){ music, id, data, size };
                    if (music ? id >= DT_SOUND_MUSIC_IDS : (id < DT_SOUND_MUSIC_IDS || id >= DT_SOUND_IDS)) {
                        snprintf(err, cap, "%s: %s", n, music ? "music ids are 00-0F (docs/audio.md)." : "effect ids are 10-37 (docs/audio.md).");
                        ok = false;
                    }
                }
            }
        }
    }
    mod_archive_close(&a);
    if (ok) {
        for (int i = 0; i < tile_n; i++) m->tiles += tile_insert(&tiles[i]) ? 1 : 0;
        for (int l = 0; l <= LANDS; l++)
            if (pals[l]) s_palettes[s_palette_count++] = pals[l];
        for (int l = 0; l < LANDS; l++)
            if (!s_palette_for[l] && (pals[l] || pals[LANDS])) s_palette_for[l] = pals[l] ? pals[l] : pals[LANDS];
        if (!s_palette_for[LANDS] && pals[LANDS]) s_palette_for[LANDS] = pals[LANDS];
        for (int i = 0; i < sound_n; i++) {
            char why[160];
            if (!mods_audio_has(sounds[i].music, sounds[i].id))
                mods_audio_add(sounds[i].music, sounds[i].id, sounds[i].file, sounds[i].size, why, sizeof why);
            else
                free(sounds[i].file);
        }
    } else {
        for (int i = 0; i < tile_n; i++) free(tiles[i].argb);
        for (int l = 0; l <= LANDS; l++) free(pals[l]);
        for (int i = 0; i < sound_n; i++) free(sounds[i].file);
    }
    free(tiles);
    free(sounds);
    return ok;
}

void app_mods_start(const uint8_t *prg, size_t prg_size)
{
    if (s_started) return;
    s_started = true;
    if (prg && prg_size) {
        uint8_t digest[32];
        app_sha256(prg, prg_size, digest);
        app_sha256_hex(digest, s_prg_hex);
        mods_audio_read_driver(prg, prg_size);
    }
    scan();
    int used = 0;
    for (int i = 0; i < s_count && s_enabled; i++) {
        AppMod *m = &s_mods[i];
        if (!m->enabled || m->error[0]) continue;
        char err[300];
        if (load_mod(m, err, sizeof err)) {
            m->loaded = true;
            used++;
        } else {
            snprintf(m->error, sizeof m->error, "Not loaded: %s", err);
        }
    }
    loaded_set(s_loaded_set, sizeof s_loaded_set, &s_loaded_enabled);
    printf("[ducktales] Mods: %s, %d found, %d loaded (%zu tiles, %d palettes, sounds %s)\n", s_enabled ? "on" : "off", s_count,
           used, s_tile_count, s_palette_count, app_mods_audio_active() ? "replaced" : "none");
    for (int i = 0; i < s_count; i++)
        if (s_mods[i].error[0]) printf("[ducktales] Mod %s: %s\n", s_mods[i].id, s_mods[i].error);
    fflush(stdout);
}

/* ---- the picture ---- */

bool app_mods_video_active(void) { return s_tile_count || s_palette_count || s_dump_dir[0]; }

bool app_mods_dump_dir(const char *dir)
{
    if (!mod_make_dir(dir)) return false;
    snprintf(s_dump_dir, sizeof s_dump_dir, "%s", dir);
    return true;
}

static bool dumped(uint64_t key)
{
    if ((s_dumped_count + 1) * 2 > s_dumped_cap) {
        size_t old_cap = s_dumped_cap, cap = old_cap ? old_cap * 2 : 4096;
        uint64_t *old = s_dumped, *grown = calloc(cap, sizeof *grown);
        if (!grown) return true;
        s_dumped = grown;
        s_dumped_cap = cap;
        for (size_t i = 0; i < old_cap; i++)
            if (old[i])
                for (size_t k = old[i] & (cap - 1);; k = (k + 1) & (cap - 1))
                    if (!grown[k]) { grown[k] = old[i]; break; }
        free(old);
    }
    key |= 1;
    for (size_t k = key & (s_dumped_cap - 1);; k = (k + 1) & (s_dumped_cap - 1)) {
        if (s_dumped[k] == key) return true;
        if (!s_dumped[k]) {
            s_dumped[k] = key;
            s_dumped_count++;
            return false;
        }
    }
}

static void dump_tile(const NvVideo *v, unsigned tile, unsigned group, uint64_t hash, uint32_t palette)
{
    if (dumped(hash * 31 + palette * 0x9E3779B1u)) return;
    const uint8_t *chr = &v->chr[tile * 16];
    const uint32_t *base = nv_palette_argb();
    uint32_t px[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int c = ((chr[y] >> (7 - x)) & 1) | ((chr[y + 8] >> (7 - x)) & 1) << 1;
            px[y * 8 + x] = c == 0 && group >= 4 ? 0 : base[(palette >> (8 * (3 - c))) & 0x3F];
        }
    char path[1200];
    snprintf(path, sizeof path, "%s\\%016llx_%08x.png", s_dump_dir, (unsigned long long)hash, palette);
    mod_png_write(path, px, 8, 8);
}

void app_mods_video_apply(const NvVideo *video, const NvRenderOptions *options, const uint8_t *ram, uint32_t *argb)
{
    if (!app_mods_video_active() || !video || !argb) return;
    static uint16_t index[NV_WIDTH * NV_HEIGHT];
    static NvSource source[NV_WIDTH * NV_HEIGHT];
    static const TileArt *cache[512 * 8];
    static uint8_t cached[512 * 8];
    uint64_t hashes[512];
    nv_render_source(video, options, index, source);
    for (int t = 0; t < 512; t++) hashes[t] = tile_hash(&video->chr[t * 16]);
    memset(cached, 0, sizeof cached);
    int land = LANDS;
    if (ram && ram[DT_ADDR_LAND] < LANDS && ram[DT_ADDR_LAST_MUSIC] != DT_MUSIC_LAND_SELECT &&
        ram[DT_ADDR_LAST_MUSIC] != DT_MUSIC_TITLE)
        land = ram[DT_ADDR_LAND];
    const uint32_t *pal = s_palette_for[land] ? s_palette_for[land] : s_palette_for[LANDS];
    const uint32_t *base = nv_palette_argb();
    for (int p = 0; p < NV_WIDTH * NV_HEIGHT; p++) {
        uint16_t i = index[p] & 0x1FF;
        if (argb[p] != base[i]) continue;
        uint32_t out = pal ? pal[i] : base[i];
        NvSource s = source[p];
        if (s != NV_SOURCE_NONE && (s_tile_count || s_dump_dir[0])) {
            unsigned tile = NV_SOURCE_TILE(s), group = NV_SOURCE_PALETTE(s), slot = tile * 8 + group;
            if (!cached[slot]) {
                const uint8_t *e = &video->palette[group * 4];
                uint32_t palette = (uint32_t)(video->palette[0] & 0x3F) << 24 | (uint32_t)(e[1] & 0x3F) << 16 |
                                   (uint32_t)(e[2] & 0x3F) << 8 | (e[3] & 0x3F);
                const TileArt *art = tile_find(hashes[tile], palette);
                cache[slot] = art ? art : tile_find(hashes[tile], ANY_PALETTE);
                cached[slot] = 1;
                if (s_dump_dir[0]) dump_tile(video, tile, group, hashes[tile], palette);
            }
            const TileArt *art = cache[slot];
            if (art) {
                uint32_t c = art->argb[(int)NV_SOURCE_ROW(s) * art->h / 8 * art->w + (int)NV_SOURCE_COLUMN(s) * art->w / 8];
                if ((c >> 24) >= 0x80) out = c | 0xFF000000u;
            }
        }
        argb[p] = out;
    }
}
