/*
 * ducktales_main.c - the program entry of the window build.
 *
 * NESRecomp's own main (runner/cyc/cyc_host.c) is compiled as
 * ducktales_host_main for this target (CMakeLists.txt), so this runs first:
 *
 *   1. A ROM on the command line (any argument that is an iNES file) is used
 *      as given: developer and test runs behave exactly as before.
 *   2. Otherwise the copy imported on an earlier run (ui/app_rom.h) is
 *      appended, after checking its SHA-256 again, and the game starts
 *      directly.
 *   3. Otherwise the first-run screen asks for the ROM, verifies it,
 *      imports it and starts the game with it.
 *
 * --import-rom FILE verifies and imports without a window (exit 0 or 1), for
 * scripts and tests; DUCKTALES_DATA_DIR overrides the data folder. Everything
 * else goes to NESRecomp's host untouched, and the application UI is told which
 * window flags were given (ui/app_ui.h).
 */
#include "ui/app_rom.h"
#include "ui/app_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifndef DUCKTALES_ROM_SHA256
#error DUCKTALES_ROM_SHA256 is set by CMakeLists.txt
#endif

int ducktales_host_main(int argc, char **argv);

static int is_nes_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char head[4] = { 0 };
    size_t n = fread(head, 1, 4, f);
    fclose(f);
    return n == 4 && !memcmp(head, "NES\x1a", 4);
}

/* Options that make NESRecomp's host run headless (cyc_host.c): no UI then. */
static int headless_option(const char *a)
{
    static const char *const HEADLESS[] = {
        "--headless", "--frames", "--input", "--hash-out", "--trace-frame", "--trace-out", "--screenshot",
        "--shot-every", "--state-frame", "--state-out", "--wav-out", "--mem-frame", "--mem-out", "--ring-out",
        "--frame-log", "--load-state", "--miss-log", "--capture-log", "--acccoin", "--realtime", "--barcode",
        "--zapper-input", "--replay-exact", "--ram-view-list",
    };
    for (size_t i = 0; i < sizeof HEADLESS / sizeof HEADLESS[0]; i++)
        if (!strcmp(a, HEADLESS[i])) return 1;
    return 0;
}

/* The host opens the ROM with fopen (the ANSI code page on Windows); hand it
 * a path that survives that, such as the 8.3 form of a folder under a user
 * name with accents. */
static void host_path(const char *utf8, char *out, size_t cap)
{
    snprintf(out, cap, "%s", utf8);
#ifdef _WIN32
    wchar_t wide[1100], shortp[1100];
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, 1100)) return;
    DWORD n = GetShortPathNameW(wide, shortp, 1100);
    const wchar_t *use = n > 0 && n < 1100 ? shortp : wide;
    BOOL lossy = FALSE;
    char ansi[1100];
    if (WideCharToMultiByte(CP_ACP, 0, use, -1, ansi, sizeof ansi, NULL, &lossy) && !lossy) snprintf(out, cap, "%s", ansi);
#endif
}

int main(int argc, char **argv)
{
    char **args = (char **)calloc((size_t)argc + 2, sizeof *args);
    if (!args) return 2;
    int n = 0;
    AppLaunch launch = { 0 };
    const char *import = NULL;
    int rom_given = 0, headless = 0;
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (i > 0 && !strcmp(a, "--import-rom") && i + 1 < argc) { import = argv[++i]; continue; }
        if (i > 0) {
            if (!strcmp(a, "--config") && i + 1 < argc) launch.config_path = argv[i + 1];
            else if (!strcmp(a, "--hidden")) launch.hidden = true;
            else if (!strcmp(a, "--uncapped")) launch.uncapped = true;
            else if (!strcmp(a, "--window-input") || !strcmp(a, "--virtual-pad")) launch.scripted_input = true;
            else if (headless_option(a)) headless = 1;
            else if (a[0] != '-' && is_nes_file(a)) rom_given = 1;
        }
        args[n++] = argv[i];
    }

    if (import) {
        char stored[1100], actual[65] = "";
        AppRomStatus s = app_rom_import(import, DUCKTALES_ROM_SHA256, stored, sizeof stored, actual);
        if (s == APP_ROM_OK) {
            printf("Imported %s\n  to %s\n", import, stored);
            return 0;
        }
        fprintf(stderr, "%s: %s\n", import, app_rom_status_text(s));
        if (s == APP_ROM_MISMATCH) fprintf(stderr, "  SHA-256 %s\n  expected %s\n", actual, DUCKTALES_ROM_SHA256);
        return 1;
    }

    static char rom[1100], rom_for_host[1100];
    if (!rom_given && !headless) {
        if (app_rom_stored_valid(DUCKTALES_ROM_SHA256, rom, sizeof rom)) {
            /* the imported copy */
        } else if (launch.hidden) {
            fprintf(stderr, "No DuckTales (USA) ROM is set up yet. Start the game without --hidden to choose it, "
                            "pass the ROM's path, or use --import-rom FILE.\n");
            return 2;
        } else {
            int r = app_ui_first_run(DUCKTALES_ROM_SHA256, rom, sizeof rom);
            if (r == 1) return 0;
            if (r != 0) {
                fprintf(stderr, "The first-run screen could not open a window. Pass the ROM's path, or use "
                                "--import-rom FILE.\n");
                return 2;
            }
        }
        host_path(rom, rom_for_host, sizeof rom_for_host);
        args[n++] = rom_for_host;
    }
    args[n] = NULL;
    app_ui_configure(&launch);
    int result = ducktales_host_main(n, args);
    app_ui_finish();
    free(args);
    return result;
}
