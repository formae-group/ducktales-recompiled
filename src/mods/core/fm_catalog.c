/*
 * fm_catalog.c - the catalogue (fm_mods.h): the mods folders, mod.toml, the
 * player's order and switches, and what a restart would change.
 *
 *   settings   [Mods] Enabled = on|off (default off) and Order = id:on,
 *              id:off, ... top first; ids no longer present are kept at the
 *              end of Order so a mod taken out and put back keeps its place
 *   mod.toml   id, name, version, author, description, game (must be the
 *              game's name) and the target's SHA-256 (the game's key, which
 *              must be the running target's)
 *   order      new mods join the bottom of the list, switched on
 */
#include "mods/core/fm_mods.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char id[64];
    bool on;
} OrderItem;

static FmGame s_game = { "", "target_sha256", "target", ".mod", "[mods]" };
static char s_target[65];
static char s_folders[FM_MAX_FOLDERS + 1][1024];
static int s_folder_count;
static bool s_player_folder;
static bool s_enabled = false;
static int s_saved_enabled = -1;
static OrderItem s_order[FM_MAX_MODS];
static int s_order_count;
static OrderItem s_saved_order[FM_MAX_MODS];
static int s_saved_order_count = -1;
static FmMod s_mods[FM_MAX_MODS];
static int s_count;
static bool s_marked, s_loaded_enabled;
static FmMod s_loaded[FM_MAX_MODS];
static int s_loaded_count;

void fm_configure(const FmGame *game)
{
    s_game = *game;
}

void fm_set_target(const char *sha256_hex)
{
    snprintf(s_target, sizeof s_target, "%s", sha256_hex);
}

const char *fm_log_prefix(void)
{
    return s_game.log_prefix;
}

static FILE *s_log;
static size_t s_log_bytes;

bool fm_log_open(const char *path)
{
    if (s_log) {
        fclose(s_log);
        s_log = NULL;
    }
    s_log = fm_open_file(path, "wb");
    s_log_bytes = 0;
    return s_log != NULL;
}

void fm_log(const char *fmt, ...)
{
    char line[1200];
    va_list ap;
    va_start(ap, fmt);
    int n = snprintf(line, sizeof line, "%s ", s_game.log_prefix);
    vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    printf("%s\n", line);
    fflush(stdout);
    if (!s_log) {
        return;
    }
    size_t len = strlen(line) + 2;
    if (s_log_bytes + len > FM_LOG_BYTES) {
        fprintf(s_log, "%s Mods: the log is full (%u MiB); later lines go to the console only\r\n", s_game.log_prefix,
                FM_LOG_BYTES >> 20);
        fclose(s_log);
        s_log = NULL;
        return;
    }
    fprintf(s_log, "%s\r\n", line);
    fflush(s_log);
    s_log_bytes += len;
}

/* ---- small text helpers ---- */

bool fm_word_on(const char *v, bool *out)
{
    static const char *ON[] = { "on", "1", "true", "yes" }, *OFF[] = { "off", "0", "false", "no" };
    for (int i = 0; i < 4; i++) {
        if (!_stricmp(v, ON[i])) {
            *out = true;
            return true;
        }
        if (!_stricmp(v, OFF[i])) {
            *out = false;
            return true;
        }
    }
    return false;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = 0;
    }
    return s;
}

bool fm_valid_id(const char *id)
{
    size_t n = strlen(id);
    if (!n || n > 63 || !isalnum((unsigned char)id[0])) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!(islower((unsigned char)id[i]) || isdigit((unsigned char)id[i]) || id[i] == '-' || id[i] == '_' ||
              id[i] == '.')) {
            return false;
        }
    }
    return true;
}

bool fm_ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !_stricmp(s + a - b, tail);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = (char)tolower((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

bool fm_parse_hex(const char *s, int digits, uint64_t *out)
{
    uint64_t v = 0;
    for (int i = 0; i < digits; i++) {
        int d = hex_digit(s[i]);
        if (d < 0) {
            return false;
        }
        v = v << 4 | (uint64_t)d;
    }
    *out = v;
    return true;
}

const char *fm_skip_bom(const char *text)
{
    return strncmp(text, "\xEF\xBB\xBF", 3) ? text : text + 3;
}

static void lower(char *s)
{
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

/* ---- settings ---- */

void fm_set_order(const char *text)
{
    s_order_count = 0;
    char buf[FM_MAX_MODS * 72];
    snprintf(buf, sizeof buf, "%s", text);
    for (char *item = strtok(buf, ","); item && s_order_count < FM_MAX_MODS; item = strtok(NULL, ",")) {
        char *t = trim(item), *colon = strrchr(t, ':');
        bool on = true;
        if (colon) {
            *colon = 0;
            if (!fm_word_on(trim(colon + 1), &on)) {
                on = true;
            }
        }
        t = trim(t);
        if (!fm_valid_id(t)) {
            continue;
        }
        bool dup = false;
        for (int i = 0; i < s_order_count; i++) {
            dup |= !strcmp(s_order[i].id, t);
        }
        if (dup) {
            continue;
        }
        snprintf(s_order[s_order_count].id, sizeof s_order[0].id, "%s", t);
        s_order[s_order_count++].on = on;
    }
}

static void order_from_catalogue(void)
{
    OrderItem kept[FM_MAX_MODS];
    int n = 0;
    for (int i = 0; i < s_count && n < FM_MAX_MODS; i++) {
        if (s_mods[i].error[0]) {
            continue;
        }
        bool dup = false;
        for (int k = 0; k < n; k++) {
            dup |= !strcmp(kept[k].id, s_mods[i].id);
        }
        if (dup) {
            continue;
        }
        snprintf(kept[n].id, sizeof kept[n].id, "%s", s_mods[i].id);
        kept[n++].on = s_mods[i].enabled;
    }
    for (int j = 0; j < s_order_count && n < FM_MAX_MODS; j++) {
        bool present = false;
        for (int k = 0; k < n; k++) {
            present |= !strcmp(kept[k].id, s_order[j].id);
        }
        if (!present) {
            kept[n++] = s_order[j];
        }
    }
    memcpy(s_order, kept, (size_t)n * sizeof kept[0]);
    s_order_count = n;
}

bool fm_setting(const char *key, const char *value)
{
    if (!_stricmp(key, "Enabled")) {
        bool on;
        if (fm_word_on(value, &on)) {
            s_enabled = on;
        } else {
            fprintf(stderr, "%s [Mods] Enabled = %s: expected on or off; using %s\n", s_game.log_prefix, value,
                    s_enabled ? "on" : "off");
        }
        return true;
    }
    if (!_stricmp(key, "Order")) {
        fm_set_order(value);
        return true;
    }
    return false;
}

void fm_read_config(const char *path)
{
    size_t size;
    char *text = (char *)fm_read_file(path, &size);
    if (!text) {
        return;
    }
    bool in = false;
    char *next = NULL;
    for (char *line = strtok_s(text, "\n", &next); line; line = strtok_s(NULL, "\n", &next)) {
        char *t = trim(line);
        if (*t == '[') {
            in = !_strnicmp(t, "[Mods]", 6);
            continue;
        }
        char *eq = strchr(t, '=');
        if (!in || !eq || *t == '#' || *t == ';') {
            continue;
        }
        *eq = 0;
        fm_setting(trim(t), trim(eq + 1));
    }
    free(text);
}

void fm_write_settings(FILE *f)
{
    bool enabled = s_saved_enabled < 0 ? s_enabled : s_saved_enabled != 0;
    const OrderItem *order = s_saved_order_count < 0 ? s_order : s_saved_order;
    int count = s_saved_order_count < 0 ? s_order_count : s_saved_order_count;
    fprintf(f, "[Mods]\nEnabled = %s\nOrder = ", enabled ? "on" : "off");
    for (int i = 0; i < count; i++) {
        fprintf(f, "%s%s:%s", i ? ", " : "", order[i].id, order[i].on ? "on" : "off");
    }
    fprintf(f, "\n");
}

bool fm_enabled(void)
{
    return s_enabled;
}

void fm_set_enabled(bool on)
{
    s_saved_enabled = -1;
    s_enabled = on;
}

void fm_override_enabled(bool on)
{
    if (s_saved_enabled < 0) {
        s_saved_enabled = s_enabled;
    }
    s_enabled = on;
}

void fm_override_order(const char *text)
{
    if (s_saved_order_count < 0) {
        s_saved_order_count = s_order_count;
        memcpy(s_saved_order, s_order, (size_t)s_order_count * sizeof s_order[0]);
    }
    fm_set_order(text);
}

bool fm_add_folder(const char *dir, bool create)
{
    if (!dir || !*dir) {
        return false;
    }
    for (int i = 0; i < s_folder_count; i++) {
        if (!_stricmp(s_folders[i], dir)) {
            return true;
        }
    }
    if (s_folder_count >= FM_MAX_FOLDERS + s_player_folder) {
        return false;
    }
    if (create) {
        fm_make_dir(dir);
    }
    snprintf(s_folders[s_folder_count++], sizeof s_folders[0], "%s", dir);
    return true;
}

const char *fm_folder(void)
{
    return s_folder_count ? s_folders[0] : "";
}

void fm_put_folder_first(const char *dir)
{
    int i = 0;
    while (i < s_folder_count && _stricmp(s_folders[i], dir)) {
        i++;
    }
    if (i == FM_MAX_FOLDERS + 1) {
        return;
    }
    s_folder_count += i == s_folder_count;
    memmove(s_folders + 1, s_folders, (size_t)i * sizeof s_folders[0]);
    fm_make_dir(dir);
    snprintf(s_folders[0], sizeof s_folders[0], "%s", dir);
    s_player_folder = true;
}

/* ---- mod.toml: top-level keys with string values (basic, literal and multi-line) ---- */

static bool toml_string(char **cursor, char *out, size_t cap, char *err, size_t errcap)
{
    char *p = *cursor;
    size_t n = 0;
    bool multi = !strncmp(p, "\"\"\"", 3) || !strncmp(p, "'''", 3), literal = *p == '\'';
    char quote = *p;
    p += multi ? 3 : 1;
    if (multi && *p == '\r') {
        p++;
    }
    if (multi && *p == '\n') {
        p++;
    }
    for (;;) {
        if (!*p || (!multi && (*p == '\n' || *p == '\r'))) {
            snprintf(err, errcap, "unterminated string");
            return false;
        }
        if (multi ? (p[0] == quote && p[1] == quote && p[2] == quote) : *p == quote) {
            break;
        }
        char c = *p++;
        if (c == '\\' && !literal) {
            char e = *p++;
            c = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == '"' ? '"' : e == '\\' ? '\\' : 0;
            if (!c) {
                snprintf(err, errcap, "unsupported escape \\%c", e);
                return false;
            }
        }
        if (c == '\r') {
            continue;
        }
        if (n + 1 < cap) {
            out[n++] = c;
        }
    }
    out[n] = 0;
    *cursor = p + (multi ? 3 : 1);
    return true;
}

static bool parse_toml(char *text, FmMod *m, char *game, size_t game_cap, char *target, size_t target_cap, char *err,
                       size_t cap)
{
    const char *const KEYS[7] = { "id", "name", "version", "author", "description", "game", s_game.target_key };
    char *p = (char *)fm_skip_bom(text);
    bool top = true, seen[7] = { false };
    int line = 1;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (*p == '\n') {
            p++;
            line++;
            continue;
        }
        if (*p == '#') {
            while (*p && *p != '\n') {
                p++;
            }
            continue;
        }
        if (*p == '[') {
            top = false;
            while (*p && *p != '\n') {
                p++;
            }
            continue;
        }
        char *key = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-')) {
            p++;
        }
        size_t klen = (size_t)(p - key);
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!klen || *p != '=') {
            snprintf(err, cap, "mod.toml line %d: expected key = value", line);
            return false;
        }
        p++;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        char value[1024], why[64];
        bool quoted = *p == '"' || *p == '\'';
        if (quoted) {
            char *start = p;
            if (!toml_string(&p, value, sizeof value, why, sizeof why)) {
                snprintf(err, cap, "mod.toml line %d: %s", line, why);
                return false;
            }
            for (char *q = start; q < p; q++) {
                line += *q == '\n';
            }
        } else {
            char *v = p;
            while (*p && *p != '\n' && *p != '#') {
                p++;
            }
            snprintf(value, sizeof value, "%.*s", (int)(p - v), v);
            trim(value);
        }
        while (*p && *p != '\n') {
            if (*p == '#') {
                while (*p && *p != '\n') {
                    p++;
                }
                break;
            }
            if (*p != ' ' && *p != '\t' && *p != '\r') {
                snprintf(err, cap, "mod.toml line %d: unexpected text after the value", line);
                return false;
            }
            p++;
        }
        if (!top) {
            continue;
        }
        int known = -1;
        for (int k = 0; k < 7; k++) {
            if (klen == strlen(KEYS[k]) && !strncmp(key, KEYS[k], klen)) {
                known = k;
            }
        }
        if (known >= 0 && seen[known]) {
            snprintf(err, cap, "mod.toml line %d: %s is set twice", line, KEYS[known]);
            return false;
        }
        if (known >= 0 && !quoted) {
            snprintf(err, cap, "mod.toml line %d: %s must be a quoted string (%s = \"...\")", line, KEYS[known],
                     KEYS[known]);
            return false;
        }
        if (known < 0) {
            continue;
        }
        seen[known] = true;
        char *const FIELD[7] = { m->id, m->name, m->version, m->author, m->description, game, target };
        const size_t FIELD_CAP[7] = { sizeof m->id,          sizeof m->name, sizeof m->version, sizeof m->author,
                                      sizeof m->description, game_cap,       target_cap };
        if ((known == 0 || known >= 5) && strlen(value) >= FIELD_CAP[known]) {
            snprintf(err, cap, "mod.toml line %d: %s is longer than %zu bytes", line, KEYS[known],
                     FIELD_CAP[known] - 1);
            return false;
        }
        snprintf(FIELD[known], FIELD_CAP[known], "%s", value);
    }
    return true;
}

/* ---- the catalogue ---- */

void fm_base_name(const char *path, char *out, size_t cap)
{
    const char *b = path;
    for (const char *c = path; *c; c++) {
        if (*c == '\\' || *c == '/') {
            b = c + 1;
        }
    }
    snprintf(out, cap, "%s", b);
}

static void read_mod(FmMod *m)
{
    char err[256] = "";
    FmArchive a;
    fm_base_name(m->path, m->name, sizeof m->name);
    snprintf(m->id, sizeof m->id, "%s", m->name);
    lower(m->id);
    if (!fm_archive_open(&a, m->path, m->folder, err, sizeof err)) {
        snprintf(m->error, sizeof m->error, "Cannot be read: %s", err);
        return;
    }
    int at = fm_archive_find(&a, "mod.toml");
    size_t size;
    char *text = at < 0 ? NULL : (char *)fm_archive_read(&a, at, &size, err, sizeof err);
    char game[128] = "", target[80] = "";
    if (at < 0) {
        snprintf(m->error, sizeof m->error, "Cannot be read: no mod.toml in the archive.");
    } else if (!text) {
        snprintf(m->error, sizeof m->error, "Cannot be read: %s", err);
    } else {
        char name_from_file[128];
        snprintf(name_from_file, sizeof name_from_file, "%s", m->name);
        m->id[0] = m->name[0] = 0;
        const char *const REQUIRED[5] = { "id", "name", "version", "game", s_game.target_key };
        const char *const VALUE[5] = { m->id, m->name, m->version, game, target };
        char missing[128] = "";
        bool parsed = false;
        if (memchr(text, 0, size)) {
            snprintf(err, sizeof err, "mod.toml contains a NUL byte; save it as UTF-8 text.");
        } else {
            parsed = parse_toml(text, m, game, sizeof game, target, sizeof target, err, sizeof err);
        }
        for (int k = 0; k < 5; k++) {
            if (!VALUE[k][0]) {
                size_t used = strlen(missing);
                snprintf(missing + used, sizeof missing - used, "%s%s", used ? ", " : "", REQUIRED[k]);
            }
        }
        uint64_t digits;
        if (!parsed) {
            snprintf(m->error, sizeof m->error, "Cannot be read: %s", err);
        } else if (missing[0]) {
            snprintf(m->error, sizeof m->error, "mod.toml is missing %s.", missing);
        } else if (!fm_valid_id(m->id)) {
            snprintf(m->error, sizeof m->error,
                     "mod.toml: id \"%s\" must be 1-63 characters of a-z, 0-9, '-', '_' or '.', starting with a "
                     "letter or digit.",
                     m->id);
        } else if (strlen(target) != 64 || !fm_parse_hex(target, 16, &digits) ||
                   !fm_parse_hex(target + 16, 16, &digits) || !fm_parse_hex(target + 32, 16, &digits) ||
                   !fm_parse_hex(target + 48, 16, &digits)) {
            snprintf(m->error, sizeof m->error, "mod.toml: %s must be the 64 hex digits of the %s's SHA-256.",
                     s_game.target_key, s_game.target_label);
        } else if (strcmp(game, s_game.name)) {
            snprintf(m->error, sizeof m->error, "Made for \"%s\", not %s.", game, s_game.name);
        } else {
            lower(target);
            if (s_target[0] && strcmp(target, s_target)) {
                snprintf(m->error, sizeof m->error,
                         "Made for a different version of the game: it targets %s SHA-256 %.12s..., this game's is "
                         "%.12s....",
                         s_game.target_label, target, s_target);
            }
        }
        if (!m->id[0]) {
            snprintf(m->id, sizeof m->id, "%s", name_from_file);
            lower(m->id);
        }
        if (!m->name[0]) {
            snprintf(m->name, sizeof m->name, "%s", name_from_file);
        }
    }
    free(text);
    for (int i = 0; i < a.count; i++) {
        const char *n = fm_archive_name(&a, i);
        if (!n) {
            continue;
        }
        m->script |= !_stricmp(n, "scripts/main.lua");
    }
    fm_archive_close(&a);
}

typedef struct {
    const char *dir;
} ScanCtx;

static void scan_entry(void *user, const char *name, bool folder)
{
    const ScanCtx *ctx = user;
    if (s_count >= FM_MAX_MODS) {
        return;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s\\%s", ctx->dir, name);
    if (folder) {
        char toml[1100];
        snprintf(toml, sizeof toml, "%s\\mod.toml", path);
        size_t n;
        uint8_t *probe = fm_read_file(toml, &n);
        if (!probe) {
            return;
        }
        free(probe);
    } else if (!fm_ends_with(name, s_game.extension)) {
        return;
    }
    FmMod *m = &s_mods[s_count++];
    memset(m, 0, sizeof *m);
    snprintf(m->path, sizeof m->path, "%s", path);
    m->folder = folder;
    read_mod(m);
}

static int by_name(const void *a, const void *b)
{
    const FmMod *left = a, *right = b;
    int name = _stricmp(left->name, right->name);
    return name ? name : _stricmp(left->path, right->path);
}

void fm_scan(void)
{
    s_count = 0;
    for (int f = 0; f < s_folder_count; f++) {
        ScanCtx ctx = { s_folders[f] };
        fm_list_dir(s_folders[f], scan_entry, &ctx);
    }
    qsort(s_mods, (size_t)s_count, sizeof s_mods[0], by_name);
    for (int i = 0; i < s_count; i++) {
        for (int k = 0; k < i; k++) {
            if (!s_mods[i].error[0] && !s_mods[k].error[0] && !strcmp(s_mods[i].id, s_mods[k].id)) {
                snprintf(s_mods[i].error, sizeof s_mods[i].error, "Another mod has the same id \"%s\": %s.",
                         s_mods[i].id, s_mods[k].path);
            }
        }
    }
    static FmMod sorted[FM_MAX_MODS];
    bool used[FM_MAX_MODS] = { false };
    int n = 0;
    for (int j = 0; j < s_order_count; j++) {
        for (int i = 0; i < s_count; i++) {
            if (!used[i] && !s_mods[i].error[0] && !strcmp(s_mods[i].id, s_order[j].id)) {
                used[i] = true;
                sorted[n] = s_mods[i];
                sorted[n++].enabled = s_order[j].on;
                break;
            }
        }
    }
    for (int i = 0; i < s_count; i++) {
        if (!used[i]) {
            sorted[n] = s_mods[i];
            sorted[n++].enabled = true;
        }
    }
    memcpy(s_mods, sorted, (size_t)n * sizeof sorted[0]);
    order_from_catalogue();
}

static bool same_mod(const FmMod *a, const FmMod *b)
{
    return !strcmp(a->id, b->id) && !_stricmp(a->path, b->path);
}

int fm_count(void)
{
    return s_count;
}

FmMod *fm_get(int index)
{
    return index >= 0 && index < s_count ? &s_mods[index] : NULL;
}

void fm_set_mod_enabled(int index, bool on)
{
    if (index < 0 || index >= s_count || s_mods[index].error[0]) {
        return;
    }
    s_mods[index].enabled = on;
    s_saved_order_count = -1;
    order_from_catalogue();
}

void fm_move(int from, int to)
{
    if (from < 0 || from >= s_count || to < 0 || to >= s_count || from == to) {
        return;
    }
    FmMod m = s_mods[from];
    if (from < to) {
        memmove(&s_mods[from], &s_mods[from + 1], (size_t)(to - from) * sizeof m);
    } else {
        memmove(&s_mods[to + 1], &s_mods[to], (size_t)(from - to) * sizeof m);
    }
    s_mods[to] = m;
    s_saved_order_count = -1;
    order_from_catalogue();
}

void fm_rescan(void)
{
    fm_scan();
    for (int i = 0; i < s_count; i++) {
        for (int k = 0; k < s_loaded_count; k++) {
            if (same_mod(&s_loaded[k], &s_mods[i])) {
                s_mods[i].loaded = true;
                memcpy(s_mods[i].script_error, s_loaded[k].script_error, sizeof s_mods[i].script_error);
                break;
            }
        }
    }
}

void fm_set_script_error(const char *id, const char *path, const char *why)
{
    for (int i = 0; i < s_loaded_count; i++) {
        FmMod *m = &s_loaded[i];
        if (!strcmp(m->id, id) && !_stricmp(m->path, path)) {
            snprintf(m->script_error, sizeof m->script_error, "%s", why);
            break;
        }
    }
    for (int i = 0; i < s_count; i++) {
        FmMod *m = &s_mods[i];
        if (!strcmp(m->id, id) && !_stricmp(m->path, path)) {
            snprintf(m->script_error, sizeof m->script_error, "%s", why);
            break;
        }
    }
}

void fm_mark_loaded_set(void)
{
    s_loaded_enabled = s_enabled;
    s_loaded_count = 0;
    for (int i = 0; i < s_count; i++) {
        if (s_mods[i].loaded) {
            s_loaded[s_loaded_count++] = s_mods[i];
        }
    }
    s_marked = true;
}

bool fm_restart_needed(void)
{
    if (!s_marked || !s_enabled) {
        return s_marked && s_loaded_enabled;
    }
    if (!s_loaded_enabled) {
        return true;
    }
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        if (s_mods[i].enabled && !s_mods[i].error[0]) {
            if (n == s_loaded_count || !same_mod(&s_loaded[n], &s_mods[i])) {
                return true;
            }
            n++;
        }
    }
    return n != s_loaded_count;
}
