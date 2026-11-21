#include "formae_rom.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Paths are UTF-8 (file dialogs and drops give UTF-8); Windows needs the
 * wide-character calls for anything outside the ANSI code page. */
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof *w) : NULL;
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}
static FILE *open_file(const char *path, const char *mode)
{
    wchar_t *p = wide(path), *m = wide(mode);
    FILE *f = p && m ? _wfopen(p, m) : NULL;
    free(p);
    free(m);
    return f;
}
static int make_dir(const char *path)
{
    wchar_t *p = wide(path);
    int r = p ? _wmkdir(p) : -1;
    free(p);
    return r;
}
static int remove_file(const char *path)
{
    wchar_t *p = wide(path);
    int r = p ? _wremove(p) : -1;
    free(p);
    return r;
}
static int rename_file(const char *from, const char *to)
{
    wchar_t *a = wide(from), *b = wide(to);
    int r = a && b ? _wrename(a, b) : -1;
    free(a);
    free(b);
    return r;
}
static const char *env_utf8(const char *name, char *buf, size_t cap)
{
    wchar_t *n = wide(name);
    const wchar_t *v = n ? _wgetenv(n) : NULL;
    free(n);
    if (!v || !*v || !WideCharToMultiByte(CP_UTF8, 0, v, -1, buf, (int)cap, NULL, NULL)) return NULL;
    return buf;
}
#else
#include <sys/stat.h>
#define make_dir(p) mkdir((p), 0755)
#define open_file fopen
#define remove_file remove
#define rename_file rename
static const char *env_utf8(const char *name, char *buf, size_t cap)
{
    const char *v = getenv(name);
    if (!v || !*v) return NULL;
    snprintf(buf, cap, "%s", v);
    return buf;
}
#endif

/* ---- SHA-256 (FIPS 180-4) ---- */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void formae_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    size_t i = 0;
    for (; len - i >= 64; i += 64) sha256_block(h, data + i);
    uint8_t tail[128] = { 0 };
    size_t rest = len - i;
    if (rest) memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    size_t blocks = rest + 9 > 64 ? 2 : 1;
    uint64_t bits = (uint64_t)len * 8;
    for (int k = 0; k < 8; k++) tail[blocks * 64 - 1 - k] = (uint8_t)(bits >> (8 * k));
    for (size_t b = 0; b < blocks; b++) sha256_block(h, tail + 64 * b);
    for (int k = 0; k < 8; k++) {
        out[4 * k] = (uint8_t)(h[k] >> 24);
        out[4 * k + 1] = (uint8_t)(h[k] >> 16);
        out[4 * k + 2] = (uint8_t)(h[k] >> 8);
        out[4 * k + 3] = (uint8_t)h[k];
    }
}

void formae_sha256_hex(const uint8_t digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[64] = 0;
}

/* ---- files ---- */

static uint8_t *read_all(const char *path, size_t *len)
{
    FILE *f = open_file(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = NULL;
    size_t n = 0, cap = 0;
    for (;;) {
        if (n == cap) {
            cap = cap ? cap * 2 : 256 * 1024;
            if (cap > 64u * 1024 * 1024) { free(buf); fclose(f); return NULL; }   /* no NES image is this large */
            uint8_t *grown = (uint8_t *)realloc(buf, cap);
            if (!grown) { free(buf); fclose(f); return NULL; }
            buf = grown;
        }
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (got == 0) break;
    }
    int err = ferror(f);
    fclose(f);
    if (err) { free(buf); return NULL; }
    *len = n;
    return buf;
}

static FormaeRomStatus check(const uint8_t *data, size_t len, const char *expected_hex, char *actual_hex)
{
    if (len < 16 || memcmp(data, "NES\x1a", 4)) return FORMAE_ROM_NOT_NES;
    uint8_t digest[32];
    char hex[65];
    formae_sha256(data, len, digest);
    formae_sha256_hex(digest, hex);
    if (actual_hex) memcpy(actual_hex, hex, 65);
    return strcmp(hex, expected_hex) ? FORMAE_ROM_MISMATCH : FORMAE_ROM_OK;
}

FormaeRomStatus formae_rom_verify(const char *path, const char *expected_hex, char *actual_hex)
{
    size_t len;
    uint8_t *data = path ? read_all(path, &len) : NULL;
    if (!data) return FORMAE_ROM_UNREADABLE;
    FormaeRomStatus s = check(data, len, expected_hex, actual_hex);
    free(data);
    return s;
}

const char *formae_rom_status_text(FormaeRomStatus s)
{
    switch (s) {
    case FORMAE_ROM_OK: return "This is the DuckTales (USA) cartridge image.";
    case FORMAE_ROM_UNREADABLE: return "The file could not be read.";
    case FORMAE_ROM_NOT_NES: return "This file is not an NES ROM image.";
    case FORMAE_ROM_MISMATCH:
        return "This NES ROM is not an unmodified DuckTales (USA). Patched, translated or re-headered "
               "files and other regions or revisions are not supported.";
    case FORMAE_ROM_WRITE_FAILED: return "The ROM was verified but could not be copied to the data folder.";
    }
    return "";
}

static int make_dirs(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        if (p[-1] == ':') continue;   /* a drive root */
        char c = *p;
        *p = 0;
        int bad = make_dir(path) != 0 && errno != EEXIST;
        *p = c;
        if (bad) return 0;
    }
    return make_dir(path) == 0 || errno == EEXIST;
}

bool formae_data_dir(char *out, size_t cap)
{
    char env[1024];
    const char *over = env_utf8("FORMAE_DATA_DIR", env, sizeof env);
    int n;
    if (over && *over) n = snprintf(out, cap, "%s", over);
    else {
#if defined(_WIN32)
        char appdata[1024];
        const char *base = env_utf8("APPDATA", appdata, sizeof appdata);
        if (!base || !*base) return false;
        n = snprintf(out, cap, "%s\\Formae\\DuckTales", base);
#elif defined(__APPLE__)
        const char *home = getenv("HOME");
        if (!home || !*home) return false;
        n = snprintf(out, cap, "%s/Library/Application Support/Formae/DuckTales", home);
#else
        const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
        if (xdg && *xdg) n = snprintf(out, cap, "%s/Formae/DuckTales", xdg);
        else if (home && *home) n = snprintf(out, cap, "%s/.local/share/Formae/DuckTales", home);
        else return false;
#endif
    }
    if (n <= 0 || (size_t)n >= cap) return false;
    while (n > 1 && (out[n - 1] == '/' || out[n - 1] == '\\')) out[--n] = 0;
    return make_dirs(out) != 0;
}

bool formae_rom_stored_path(char *out, size_t cap)
{
    char dir[1024];
    if (!formae_data_dir(dir, sizeof dir)) return false;
#ifdef _WIN32
    int n = snprintf(out, cap, "%s\\%s", dir, FORMAE_ROM_FILE_NAME);
#else
    int n = snprintf(out, cap, "%s/%s", dir, FORMAE_ROM_FILE_NAME);
#endif
    return n > 0 && (size_t)n < cap;
}

FormaeRomStatus formae_rom_import(const char *src, const char *expected_hex, char *stored, size_t cap,
                                  char *actual_hex)
{
    size_t len;
    uint8_t *data = src ? read_all(src, &len) : NULL;
    if (!data) return FORMAE_ROM_UNREADABLE;
    FormaeRomStatus s = check(data, len, expected_hex, actual_hex);
    if (s != FORMAE_ROM_OK) { free(data); return s; }
    char dst[1100], tmp[1120];
    if (!formae_rom_stored_path(dst, sizeof dst)) { free(data); return FORMAE_ROM_WRITE_FAILED; }
    snprintf(tmp, sizeof tmp, "%s.part", dst);
    FILE *f = open_file(tmp, "wb");
    int ok = f && fwrite(data, 1, len, f) == len;
    if (f && fclose(f) != 0) ok = 0;
    free(data);
    if (ok) {
        remove_file(dst);
        ok = rename_file(tmp, dst) == 0;
    }
    if (!ok) { remove_file(tmp); return FORMAE_ROM_WRITE_FAILED; }
    if (stored) snprintf(stored, cap, "%s", dst);
    return FORMAE_ROM_OK;
}

bool formae_rom_stored_valid(const char *expected_hex, char *out, size_t cap)
{
    char path[1100];
    if (!formae_rom_stored_path(path, sizeof path)) return false;
    if (formae_rom_verify(path, expected_hex, NULL) != FORMAE_ROM_OK) return false;
    snprintf(out, cap, "%s", path);
    return true;
}
