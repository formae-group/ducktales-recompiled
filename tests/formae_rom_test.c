/*
 * formae_rom_test.c - unit test of src/ui/formae_rom.c: SHA-256 against the
 * FIPS 180-4 example vectors, and the ROM check / import on synthetic files
 * in a temporary data folder (FORMAE_DATA_DIR). No ROM content is used: the
 * "good" image is a synthetic iNES file whose hash the test computes.
 *
 *   formae_rom_test <scratch dir>
 */
#include "ui/formae_rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0777)
#endif

static int failures;

static void expect(int ok, const char *what)
{
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

static void write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(data, 1, len, f) != len) { perror(path); exit(2); }
    fclose(f);
}

static void hex_of(const uint8_t *data, size_t len, char out[65])
{
    uint8_t d[32];
    formae_sha256(data, len, d);
    formae_sha256_hex(d, out);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: formae_rom_test SCRATCH_DIR\n"); return 2; }
    char hex[65];
    hex_of((const uint8_t *)"", 0, hex);
    expect(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "SHA-256 of the empty string");
    hex_of((const uint8_t *)"abc", 3, hex);
    expect(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256 of abc");
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    hex_of((const uint8_t *)two, strlen(two), hex);
    expect(!strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "SHA-256 of the two-block vector");

    static uint8_t image[16 + 32768];
    memcpy(image, "NES\x1a", 4);
    image[4] = 2;
    for (size_t i = 16; i < sizeof image; i++) image[i] = (uint8_t)(i * 7);
    char good[65];
    hex_of(image, sizeof image, good);

    char dir[1024], data_env[1100], src[1100], bad[1100], txt[1100], stored[1100], actual[65];
    snprintf(dir, sizeof dir, "%s", argv[1]);
    snprintf(data_env, sizeof data_env, "%s/data/nested", dir);
#ifdef _WIN32
    _putenv_s("FORMAE_DATA_DIR", data_env);
#else
    setenv("FORMAE_DATA_DIR", data_env, 1);
#endif
    make_dir(dir);
    snprintf(stored, sizeof stored, "%s/" FORMAE_ROM_FILE_NAME, data_env);
    remove(stored);
    snprintf(src, sizeof src, "%s/good.nes", dir);
    snprintf(bad, sizeof bad, "%s/bad.nes", dir);
    snprintf(txt, sizeof txt, "%s/notes.txt", dir);
    write_file(src, image, sizeof image);
    image[100] ^= 1;
    write_file(bad, image, sizeof image);
    image[100] ^= 1;
    write_file(txt, (const uint8_t *)"not a rom", 9);

    expect(formae_rom_verify(src, good, NULL) == FORMAE_ROM_OK, "the expected image verifies");
    expect(formae_rom_verify(bad, good, actual) == FORMAE_ROM_MISMATCH && strcmp(actual, good),
           "one changed byte is a mismatch, and the actual hash is reported");
    expect(formae_rom_verify(txt, good, NULL) == FORMAE_ROM_NOT_NES, "a text file is not an NES image");
    expect(formae_rom_verify("no/such/file.nes", good, NULL) == FORMAE_ROM_UNREADABLE, "a missing file is unreadable");

    expect(!formae_rom_stored_valid(good, stored, sizeof stored), "nothing is stored before an import");
    expect(formae_rom_import(bad, good, stored, sizeof stored, NULL) == FORMAE_ROM_MISMATCH, "a modified image is not imported");
    expect(!formae_rom_stored_valid(good, stored, sizeof stored), "a refused import stores nothing");
    expect(formae_rom_import(src, good, stored, sizeof stored, NULL) == FORMAE_ROM_OK, "the expected image is imported");
    expect(strstr(stored, "nested") != NULL && strstr(stored, FORMAE_ROM_FILE_NAME) != NULL,
           "the copy is in the data folder (created) under the fixed name");
    char again[1100];
    expect(formae_rom_stored_valid(good, again, sizeof again) && !strcmp(again, stored), "the stored copy is found again");
    expect(formae_rom_verify(stored, good, NULL) == FORMAE_ROM_OK, "the stored copy is identical");
    expect(formae_rom_import(src, good, NULL, 0, NULL) == FORMAE_ROM_OK, "importing again replaces the copy");
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
